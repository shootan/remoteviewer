// The connect diagnostics actually reach the log, stay bounded, and never carry a secret.
//
// This one starts its own server rather than using the shared one, because the shared one is
// spawned with stdio 'ignore' and the whole subject here is what the server prints. Its own ports,
// its own data file, so it cannot disturb anything running beside it.
//
// What it pins is the gap that made 2026-09-21 unresolvable: an off-LAN host's observe and
// heartbeat left no trace, a wake said nothing about whether its datagrams left, the moment a host
// collected a waiting capability was invisible, and a relay session that reached a silent host
// looked exactly like one that was never reached at all.

const { spawn, spawnSync } = require('child_process');
const dgram = require('dgram');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const HTTP = 18090;
const UDP = 18091;
const RELAY_PORT = 18453;
const GRACE_MS = 400;

const MAGIC = 0x31435052;
const HELLO_BYTES = 49;
const KIND_HELLO = 300;
const KIND_HELLO_ACK = 301;
const KIND_PUNCH = 303;
const FEATURE_FEC = 0x2;
const FEATURE_DIRECTORY_AUTH = 0x4;

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const serverPath = path.join(__dirname, '..', 'server.js');
const dataPath = path.join(os.tmpdir(), `remote60-connect-diag-${process.pid}.json`);
const env = {
  ...process.env,
  REMOTE60_DIR_DATA: dataPath,
  REMOTE60_DIR_PORT: String(HTTP),
  REMOTE60_DIR_UDP_PORT: String(UDP),
  REMOTE60_DIR_SIGNUP_KEY: 'test-signup-key',
  REMOTE60_RELAY_ENABLED: '1',
  REMOTE60_RELAY_IP: '127.0.0.1',
  REMOTE60_RELAY_PORT: String(RELAY_PORT),
  REMOTE60_RELAY_GRACE_MS: String(GRACE_MS),
  REMOTE60_RELAY_ALLOW_IPS: '127.0.0.1',
  REMOTE60_RELAY_ALLOW_ACCOUNTS: 'tester',
};

function api(method, p, body, token) {
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : null;
    const req = http.request(
      { host: '127.0.0.1', port: HTTP, path: p, method,
        headers: Object.assign(
          payload ? { 'content-type': 'application/json',
                      'content-length': Buffer.byteLength(payload) } : {},
          token ? { authorization: 'Bearer ' + token } : {}) },
      (res) => {
        let out = '';
        res.on('data', (c) => (out += c));
        res.on('end', () => {
          let parsed = {};
          try { parsed = JSON.parse(out); } catch { /* keep {} */ }
          resolve({ status: res.statusCode, body: parsed });
        });
      });
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function recorder() {
  const sock = dgram.createSocket('udp4');
  const got = [];
  sock.on('message', (msg, rinfo) => got.push({ msg, rinfo }));
  return { sock, got };
}

const bindLocal = (sock) => new Promise((r) => sock.bind(0, '127.0.0.1', r));

function observe(sock, token) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('observe timeout')), 3000);
    const onMessage = (msg) => {
      let seen;
      try { seen = JSON.parse(msg.toString()); } catch { return; }
      clearTimeout(timer);
      sock.off('message', onMessage);
      resolve(seen);
    };
    sock.on('message', onMessage);
    sock.send(Buffer.from('OBSERVE ' + token), UDP, '127.0.0.1');
  });
}

function buildHello(kind, token, features) {
  const buf = Buffer.alloc(HELLO_BYTES);
  buf.writeUInt32LE(MAGIC, 0);
  buf.writeUInt16LE(kind, 4);
  buf.writeUInt16LE(HELLO_BYTES, 6);
  buf.writeUInt32LE(2, 8);
  buf.writeUInt32LE(features || 0, 12);
  if (token) buf.write(token, 16, 'latin1');
  return buf;
}

(async () => {
  fs.rmSync(dataPath, { force: true });
  spawnSync(process.execPath, [serverPath, '--add-account', 'tester', 'test-pass-1234'],
            { env, stdio: 'ignore' });

  let log = '';
  const server = spawn(process.execPath, [serverPath], { env, stdio: ['ignore', 'pipe', 'pipe'] });
  server.stdout.on('data', (c) => (log += c));
  server.stderr.on('data', (c) => (log += c));
  const lines = (tag) => log.split('\n').filter((l) => l.includes(tag));
  const cleanup = () => {
    try { server.kill(); } catch { /* already gone */ }
    fs.rmSync(dataPath, { force: true });
  };

  try {
    await sleep(900);

    let r = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
    const session = r.body.sessionToken;
    check('login', r.status === 200 && !!session, `status=${r.status}`);

    r = await api('POST', '/api/host/register',
                  { id: 'tester', pw: 'test-pass-1234', hostName: 'diag-host',
                    machineId: 'machine-diag' });
    const hostToken = r.body.hostToken;
    const hostId = r.body.hostId;
    check('host registers', r.status === 200 && !!hostToken, `status=${r.status}`);

    // ------------------------------------------------------- A1: observe and heartbeat arrival
    const host = recorder();
    await bindLocal(host.sock);
    await observe(host.sock, 'diag-host-observe');
    host.got.length = 0;
    await sleep(120);
    check('an observe arrival is logged for any peer, not only one on our lan',
          lines('[observe-rx]').length === 1, `${lines('[observe-rx]').length} lines`);
    check('...without the token itself', !log.includes('diag-host-observe'),
          'the token is a lookup key a stranger could reuse');

    const hb1 = await api('POST', '/api/host/heartbeat', {
      hostToken, hostName: 'diag-host', observeToken: 'diag-host-observe',
      localUdpPort: host.sock.address().port, localIps: ['127.0.0.2'],
    });
    check('heartbeat accepted', hb1.status === 200, `status=${hb1.status}`);
    await sleep(120);
    const hbLines = lines('[host-hb]');
    check('a heartbeat arrival is logged', hbLines.length === 1, `${hbLines.length} lines`);
    check('...saying where our datagrams will go',
          hbLines[0] && /aim=127\.0\.0\.1:\d+ via=(wire|lan)/.test(hbLines[0]), hbLines[0] || '');

    // Bounded: a second heartbeat from the same tuple, well inside the interval, adds nothing.
    await api('POST', '/api/host/heartbeat', {
      hostToken, hostName: 'diag-host', observeToken: 'diag-host-observe',
      localUdpPort: host.sock.address().port, localIps: ['127.0.0.2'],
    });
    await sleep(150);
    check('a repeat heartbeat inside the interval is not logged again',
          lines('[host-hb]').length === 1, `${lines('[host-hb]').length} lines`);

    // ...but a moved tuple is never thinned away, because that is the line worth having.
    const host2 = recorder();
    await bindLocal(host2.sock);
    await observe(host2.sock, 'diag-host-observe-2');
    await api('POST', '/api/host/heartbeat', {
      hostToken, hostName: 'diag-host', observeToken: 'diag-host-observe-2',
      localUdpPort: host2.sock.address().port, localIps: ['127.0.0.2'],
    });
    await sleep(150);
    const movedLines = lines('[host-hb]');
    check('a moved tuple is logged even inside the interval', movedLines.length === 2,
          `${movedLines.length} lines`);
    check('...and says where it moved from',
          movedLines[1] && movedLines[1].includes('moved-from='), movedLines[1] || '');
    host2.sock.close();

    // ------------------------------------------------------- A2/A3: wake and capability
    const client = recorder();
    await bindLocal(client.sock);
    await observe(client.sock, 'diag-client-observe');
    client.got.length = 0;
    host.got.length = 0;

    r = await api('POST', '/api/connect', { hostId, observeToken: 'diag-client-observe' }, session);
    const punchToken = r.body.punchToken;
    check('connect succeeds', r.status === 200 && !!punchToken, `status=${r.status}`);
    const connectLine = lines('[capability]').find((l) => l.includes('minted'));
    check('the capability mint is logged against a connect', !!connectLine, connectLine || '');
    const connectId = connectLine ? (/connect=([0-9a-f]+)/.exec(connectLine) || [])[1] : '';
    check('...with an id the other lines can be joined on', !!connectId, connectId || '');

    await sleep(700);  // past the 0/100/300ms wake burst
    const txLines = lines('] sent ').filter((l) => l.includes('[wake]'));
    check('every wake datagram reports leaving the socket', txLines.length === 3,
          `${txLines.length} of 3`);
    check('...each tagged with the connect that caused it',
          txLines.every((l) => l.includes(`connect=${connectId}`)), txLines[0] || '');
    check('...and with which of the three it was',
          ['tx[0]', 'tx[100]', 'tx[300]'].every((t) => txLines.some((l) => l.includes(t))),
          txLines.join(' | '));

    // A3: the host collects it, and the wait is on the line.
    await api('POST', '/api/host/heartbeat', {
      hostToken, hostName: 'diag-host', observeToken: 'diag-host-observe',
      localUdpPort: host.sock.address().port, localIps: ['127.0.0.2'],
    });
    await sleep(150);
    const collected = lines('[capability]').find((l) => l.includes('collected'));
    check('the moment the host collects the capability is logged', !!collected, collected || '');
    check('...naming the connect it belonged to',
          !!collected && collected.includes(connectId), collected || '');
    check('...and how long it waited',
          !!collected && /waited=[0-9a-f]+:\d+ms/.test(collected), collected || '');

    // Bounded: a second connect inside the wake interval says why it sent nothing.
    r = await api('POST', '/api/connect', { hostId, observeToken: 'diag-client-observe' }, session);
    await sleep(200);
    const skipped = lines('[wake]').find((l) => l.includes('skipped reason=rate'));
    check('a wake suppressed by the rate limit says so', !!skipped, skipped || '');

    // ------------------------------------------------------- A4: the relay legs
    await sleep(1100);  // past the wake interval, so this connect gets its own burst
    r = await api('POST', '/api/connect', { hostId, observeToken: 'diag-client-observe' }, session);
    const relayToken = r.body.punchToken;
    check('a connect for the relay leg succeeds', r.status === 200 && !!relayToken,
          `status=${r.status}`);

    client.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, '127.0.0.1');
    await sleep(GRACE_MS + 400);
    host.got.length = 0;
    client.got.length = 0;
    client.sock.send(buildHello(KIND_HELLO, relayToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
    await sleep(400);

    const firstC2H = lines('first c2h');
    check('the first hop towards the host is logged', firstC2H.length === 1,
          `${firstC2H.length} lines`);
    check('...saying it was the handshake and not media',
          firstC2H[0] && firstC2H[0].includes(`kind=${KIND_HELLO}`), firstC2H[0] || '');

    const relayed = host.got.find((p) => p.msg.length === HELLO_BYTES &&
                                         p.msg.readUInt16LE(4) === KIND_HELLO);
    check('the relayed hello reaches the host fixture', !!relayed,
          `hostReceived=${host.got.length}`);
    if (relayed) {
      host.sock.send(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH),
                     relayed.rinfo.port, relayed.rinfo.address);
      await sleep(300);
      const firstH2C = lines('first h2c');
      check('the first answer from the host is logged', firstH2C.length === 1,
            `${firstH2C.length} lines`);
      check('...with the gap that separates "never reached" from "reached and refused"',
            firstH2C[0] && /afterFirstC2HMs=\d+/.test(firstH2C[0]), firstH2C[0] || '');
    }

    // The silence case: a session that reaches a host which never answers. This is the shape the
    // field log showed -- `closed reason=no HelloAck h2c=0/0B` -- said while it is still happening.
    await sleep(1100);
    r = await api('POST', '/api/connect', { hostId, observeToken: 'diag-client-observe' }, session);
    const silentToken = r.body.punchToken;
    client.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, '127.0.0.1');
    await sleep(GRACE_MS + 400);
    host.got.length = 0;
    client.sock.send(buildHello(KIND_HELLO, silentToken, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
    // The fixture deliberately does not answer. The sweep runs every 5s and reports after 3s.
    await sleep(9000);
    const silent = lines('host silent h2c=0');
    check('a bound session whose host never answers says so while it is open',
          silent.length === 1, `${silent.length} lines`);
    check('...naming where it was aimed',
          silent[0] && /host=127\.0\.0\.1:\d+/.test(silent[0]), silent[0] || '');

    // ------------------------------------------------------- secrets
    check('no capability appears anywhere in the log',
          !log.includes(punchToken) && !log.includes(relayToken) && !log.includes(silentToken),
          'a capability in a log is a capability anyone reading logs can replay');
    check('no host token appears either', !log.includes(hostToken));
    check('no session token appears either', !log.includes(session));

    host.sock.close();
    client.sock.close();
  } catch (e) {
    check('the test ran to completion', false, e.message);
  }

  cleanup();
  console.log(failures === 0 ? '\nconnect_diag_test: PASS' : `\nconnect_diag_test: ${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
})();
