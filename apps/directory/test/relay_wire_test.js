// The viewer on the server's own LAN, going to a host that is NOT -- the direction same_lan_test
// does not cover, and the one that failed in the field on 2026-10-11 (Home PC -> company PC).
//
// From that viewer the server sees two addresses: the WIRE address its OBSERVE arrived from (the
// router's hairpin address, 192.168.0.1 in the field) and the ADVERTISED one observedAddressFor
// substitutes for it (our public IP). handleConnect used to key the relay capability on the
// advertised one; the relay listener can only look up the wire one. The viewer punched, found
// nothing, and the relay stayed silent -- `candidates=4 ... punch answered=0`.
//
// This reproduces the address-substitution combination, not a NAT: the client binds to this
// machine's LAN interface and talks to the server on that interface, so its wire address is on
// the server's LAN and the advertised one is REMOTE60_PUBLIC_IP (a documentation address). The
// host is on loopback, i.e. an ordinary remote host whose public and wire addresses agree. The
// relay allowlist names only the advertised address, which is what the policy is meant to judge.
//
// Starts and stops its own server, because it asserts on the server's log and the servers run.js
// spawns discard stdout. Modes (argv[2]):
//   fixed       -- the shipped server; the whole chain must work, and the refusals must refuse
//   punch-only  -- a copy of server.js with the Hello check put back on the advertised address:
//                  the punch is answered, the Hello is refused. Pins that fixing punch alone is
//                  not a fix.
//   hello-only  -- a copy with the eligibility map put back on the advertised address: the punch
//                  goes unanswered, so the relay candidate is never selected. Pins the other half.
// A mutant that does not apply (the text it rewrites is gone) fails the run rather than passing
// as a vacuous "fixed".
const http = require('http');
const dgram = require('dgram');
const os = require('os');
const fs = require('fs');
const path = require('path');
const { spawn, spawnSync } = require('child_process');

const MODE = process.argv[2] || 'fixed';
const HTTP = 18390;
const UDP = 18391;
const RELAY_PORT = 18473;
const GRACE_MS = 400;
const PUBLIC_IP = '203.0.113.9';

const MAGIC = 0x31435052;
const HELLO_BYTES = 49;
const KIND_HELLO = 300;
const KIND_HELLO_ACK = 301;
const KIND_PUNCH = 303;
const FEATURE_FEC = 0x2;
const FEATURE_DIRECTORY_AUTH = 0x4;

// RELAY_WIRE_TEST_SERVER points the run at another server.js -- how the pre-fix server was shown
// to fail this file (its requires must resolve from wherever it sits). Unset, the shipped one.
const realServerPath = process.env.RELAY_WIRE_TEST_SERVER || path.join(__dirname, '..', 'server.js');
const dataPath = path.join(os.tmpdir(), `remote60-relay-wire-${process.pid}.json`);
let failures = 0;
let serverLog = '';

function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function api(method, urlPath, body, token) {
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : null;
    const req = http.request(
      { host: '127.0.0.1', port: HTTP, path: urlPath, method,
        headers: Object.assign(
          payload ? { 'content-type': 'application/json', 'content-length': Buffer.byteLength(payload) } : {},
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

function recorder() {
  const sock = dgram.createSocket('udp4');
  const got = [];
  sock.on('message', (msg, rinfo) => got.push({ msg, rinfo }));
  sock.on('error', () => { /* a refused send is a result, not a crash */ });
  return { sock, got };
}

function observe(sock, token, serverIp) {
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
    sock.send(Buffer.from('OBSERVE ' + token), UDP, serverIp);
  });
}

const bindOn = (sock, ip) => new Promise((r) => sock.bind(0, ip, r));

const isKind = (kind) => (p) => p.msg.length === HELLO_BYTES && p.msg.readUInt32LE(0) === MAGIC &&
                               p.msg.readUInt16LE(4) === kind;
const isPunch = isKind(KIND_PUNCH);
const isHello = isKind(KIND_HELLO);
const isAck = isKind(KIND_HELLO_ACK);

/** A LAN address the server will store and that observedAddressFor will treat as its own LAN. */
function publishableLanAddress() {
  for (const addrs of Object.values(os.networkInterfaces())) {
    for (const a of addrs || []) {
      if (a.family !== 'IPv4' || a.internal || !a.address) continue;
      if (a.address.startsWith('169.254.') || a.address.startsWith('127.')) continue;
      return a.address;
    }
  }
  return null;
}

/**
 * A copy of server.js with one of the fix's two halves undone, written where requiring it still
 * finds its modules. The text each mutant rewrites is the fixed line itself, so if the fix is
 * refactored the mutant stops applying and says so, instead of silently testing the real server.
 */
function writeMutant(kind) {
  const src = fs.readFileSync(realServerPath, 'utf8');
  const edits = {
    'punch-only': ['if (auth.clientWireIp !== rinfo.address) {',
                   'if (auth.clientAdvertisedIp !== rinfo.address) {'],
    'hello-only': ['relayEligibleByWireIp.set(clientWireIp, {',
                   'relayEligibleByWireIp.set(clientIp, {'],
  }[kind];
  if (!edits) throw new Error(`unknown mode ${kind}`);
  if (!src.includes(edits[0])) throw new Error(`mutant ${kind} does not apply: text not found`);
  const dir = path.dirname(realServerPath).split(path.sep).join('/');
  const mutated = src.split(edits[0]).join(edits[1])
    .split("require('./").join(`require('${dir}/`);
  const out = path.join(os.tmpdir(), `remote60-server-mutant-${kind}-${process.pid}.js`);
  fs.writeFileSync(out, mutated);
  return out;
}

function logHas(re) { return re.test(serverLog); }
function logCount(re) { return (serverLog.match(re) || []).length; }

(async () => {
  const lan = publishableLanAddress();
  if (!lan) {
    console.log('SKIP  no publishable non-loopback IPv4 interface on this machine');
    process.exit(0);
  }

  const env = {
    ...process.env,
    REMOTE60_DIR_DATA: dataPath,
    REMOTE60_DIR_PORT: String(HTTP),
    REMOTE60_DIR_UDP_PORT: String(UDP),
    REMOTE60_DIR_SIGNUP_KEY: 'test-signup-key',
    REMOTE60_PUBLIC_IP: PUBLIC_IP,
    REMOTE60_RELAY_ENABLED: '1',
    REMOTE60_RELAY_IP: '127.0.0.1',
    REMOTE60_RELAY_PORT: String(RELAY_PORT),
    REMOTE60_RELAY_GRACE_MS: String(GRACE_MS),
    // The ADVERTISED address, and only it. The viewer's wire address is deliberately absent: the
    // policy judges what the world sees, and that is what the field allowlist holds.
    REMOTE60_RELAY_ALLOW_IPS: PUBLIC_IP,
    REMOTE60_RELAY_ALLOW_ACCOUNTS: 'tester',
    REMOTE60_LOG_DISABLED: '1',
  };
  fs.rmSync(dataPath, { force: true });
  spawnSync(process.execPath, [realServerPath, '--add-account', 'tester', 'test-pass-1234'],
            { env, stdio: 'ignore' });
  spawnSync(process.execPath, [realServerPath, '--add-account', 'outsider', 'outsider-pass-1234'],
            { env, stdio: 'ignore' });

  const serverPath = MODE === 'fixed' ? realServerPath : writeMutant(MODE);
  const server = spawn(process.execPath, [serverPath], { env, stdio: ['ignore', 'pipe', 'pipe'] });
  server.stdout.on('data', (c) => (serverLog += c));
  server.stderr.on('data', (c) => (serverLog += c));
  await sleep(1500);
  console.log(`mode=${MODE} lan=${lan} advertised=${PUBLIC_IP}`);

  const sockets = [];
  const rec = async (ip) => { const r = recorder(); await bindOn(r.sock, ip); sockets.push(r.sock); return r; };
  const relayTo = lan;  // the relay listener binds every interface; sending here keeps the wire on the LAN

  try {
    let r = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
    const session = r.body.sessionToken;
    check('login', r.status === 200 && !!session, `status=${r.status}`);

    // ------------------------------------------------------------ the host: remote, loopback
    r = await api('POST', '/api/host/register',
                  { id: 'tester', pw: 'test-pass-1234', hostName: 'company-pc', machineId: 'm-company' });
    const hostToken = r.body.hostToken;
    const hostId = r.body.hostId;
    check('host registers', r.status === 200 && !!hostToken, `status=${r.status}`);
    const host = await rec('127.0.0.1');
    const hostObs = await observe(host.sock, 'host-observe', '127.0.0.1');
    host.got.length = 0;
    check('the host\'s advertised and wire addresses agree (public==wire regression kept)',
          hostObs.ip === '127.0.0.1' && hostObs.port === host.sock.address().port,
          `reported=${hostObs.ip}:${hostObs.port}`);
    r = await api('POST', '/api/host/heartbeat', { hostToken, hostName: 'company-pc',
                                                   observeToken: 'host-observe',
                                                   localUdpPort: host.sock.address().port, localIps: [] });
    check('host heartbeat accepted', r.status === 200, `status=${r.status}`);

    // ------------------------------------------------------------ the viewer: on our LAN
    const viewer = await rec(lan);
    const viewerObs = await observe(viewer.sock, 'viewer-observe', lan);
    viewer.got.length = 0;
    check('the viewer\'s wire address differs from its advertised one (the field shape)',
          viewerObs.ip === PUBLIC_IP && viewerObs.port === viewer.sock.address().port,
          `advertised=${viewerObs.ip}:${viewerObs.port} wire=${lan}`);

    if (MODE === 'fixed') {
      // -------------------------------------------------- refusals first: policy, before any
      // eligibility exists under the LAN wire address (eligibility is per IP, as a NAT demands)
      r = await api('POST', '/api/login', { id: 'outsider', pw: 'outsider-pass-1234' });
      const outsiderSession = r.body.sessionToken;
      r = await api('POST', '/api/host/register',
                    { id: 'outsider', pw: 'outsider-pass-1234', hostName: 'other-pc', machineId: 'm-other' });
      const otherHostId = r.body.hostId;
      const otherHost = await rec('127.0.0.1');
      await observe(otherHost.sock, 'other-host-observe', '127.0.0.1');
      otherHost.got.length = 0;
      await api('POST', '/api/host/heartbeat', { hostToken: r.body.hostToken, hostName: 'other-pc',
                                                 observeToken: 'other-host-observe',
                                                 localUdpPort: otherHost.sock.address().port, localIps: [] });
      r = await api('POST', '/api/connect', { hostId: otherHostId, observeToken: 'viewer-observe' },
                    outsiderSession);
      check('an account outside the allowlist is offered no relay, whatever its address',
            r.status === 200 && !(r.body.candidates || []).some((c) => c.kind === 'relay'),
            `status=${r.status} kinds=${(r.body.candidates || []).map((c) => c.kind).join(',')}`);
      viewer.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, relayTo);
      await sleep(GRACE_MS + 500);
      check('...and its punch from the LAN wire address is not answered', !viewer.got.some(isPunch),
            `viewerReceived=${viewer.got.length}`);
      check('...which the log says, naming the source and the reason',
            logHas(new RegExp(`\\[relay\\] punch ignored src=${lan.replace(/\./g, '\\.')}:\\d+ reason=not-eligible`)));
      viewer.got.length = 0;

      // An allowed account whose advertised address is outside the allowlist: loopback is not
      // substituted, so this client advertises 127.0.0.1, which the policy does not list.
      const lte = await rec('127.0.0.1');
      await observe(lte.sock, 'lte-observe', '127.0.0.1');
      lte.got.length = 0;
      r = await api('POST', '/api/connect', { hostId, observeToken: 'lte-observe' }, session);
      check('an allowed account from an address outside the allowlist is offered no relay',
            r.status === 200 && !(r.body.candidates || []).some((c) => c.kind === 'relay'),
            `status=${r.status} kinds=${(r.body.candidates || []).map((c) => c.kind).join(',')}`);
      lte.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, '127.0.0.1');
      await sleep(GRACE_MS + 500);
      check('...and its punch is not answered', !lte.got.some(isPunch), `received=${lte.got.length}`);
    }

    // ------------------------------------------------------------ the chain that failed
    await sleep(1100);  // past the per-host wake interval, which the connects above consumed
    host.got.length = 0;
    r = await api('POST', '/api/connect', { hostId, observeToken: 'viewer-observe' }, session);
    const token1 = r.body.punchToken;
    const connectId1 = r.body.connectId;
    const kinds = (r.body.candidates || []).map((c) => c.kind);
    check('connect succeeds for the LAN viewer and offers the relay (policy judged the advertised address)',
          r.status === 200 && !!token1 && kinds.includes('relay'),
          `status=${r.status} kinds=${kinds.join(',')}`);
    check('the eligible line names both addresses',
          logHas(new RegExp(`connect=${connectId1} eligible client=${PUBLIC_IP.replace(/\./g, '\\.')}:\\d+ ` +
                            `wire=${lan.replace(/\./g, '\\.')} `)));
    await sleep(300);
    check('the host is woken (pendingPunch still carries the advertised address)', host.got.some(isPunch),
          `hostReceived=${host.got.length}`);

    viewer.got.length = 0;
    viewer.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, relayTo);
    await sleep(GRACE_MS + 500);
    const punchReplies = viewer.got.filter(isPunch);

    if (MODE === 'hello-only') {
      check('hello-only mutant: the punch from the LAN wire address goes unanswered, so the relay ' +
            'candidate is never selected', punchReplies.length === 0,
            `replies=${punchReplies.length}`);
      check('hello-only mutant: the log says the punch found no eligibility',
            logHas(/punch ignored src=.* reason=not-eligible/));
    } else {
      check('the relay answers the punch sent from the LAN wire address',
            punchReplies.length > 0, `replies=${punchReplies.length}`);
      check('...the reply reaches the socket the punch came from (raw tuple, not the advertised one)',
            punchReplies.length > 0 && punchReplies[0].rinfo.port === RELAY_PORT,
            punchReplies.length ? `from=${punchReplies[0].rinfo.address}:${punchReplies[0].rinfo.port}` : 'none');
    }

    host.got.length = 0;
    viewer.got.length = 0;
    viewer.sock.send(buildHello(KIND_HELLO, token1, FEATURE_FEC), RELAY_PORT, relayTo);
    await sleep(300);
    const forwarded = host.got.find(isHello);

    if (MODE === 'punch-only') {
      check('punch-only mutant: the Hello from the LAN wire address is refused', !forwarded,
            `hostReceived=${host.got.length}`);
      check('punch-only mutant: the log names the mismatch',
            logHas(/hello rejected src=.* reason=wire-mismatch/));
      return;
    }
    if (MODE === 'hello-only') return;

    check('the token Hello from the LAN wire address reaches the host', !!forwarded,
          `hostReceived=${host.got.length}`);
    check('...from the observe socket', !!forwarded && forwarded.rinfo.port === UDP,
          forwarded ? `src=${forwarded.rinfo.address}:${forwarded.rinfo.port}` : 'missing');
    check('the session is bound to the raw wire tuple, not the advertised address',
          logHas(new RegExp(`connect=${connectId1} bound client=${lan.replace(/\./g, '\\.')}:` +
                            `${viewer.sock.address().port} `)) &&
          !logHas(new RegExp(`bound client=${PUBLIC_IP.replace(/\./g, '\\.')}:`)));

    let gotAck = null;
    if (forwarded) {
      host.sock.send(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH),
                     forwarded.rinfo.port, forwarded.rinfo.address);
      await sleep(300);
      gotAck = viewer.got.find(isAck);
    }
    check('the host\'s HelloAck comes back to the viewer', !!gotAck, `viewerReceived=${viewer.got.length}`);
    check('...from the relay, as the client expects', !!gotAck && gotAck.rinfo.port === RELAY_PORT,
          gotAck ? `from=${gotAck.rinfo.address}:${gotAck.rinfo.port}` : 'none');
    check('the session is active in the log', logHas(new RegExp(`connect=${connectId1} active;`)));

    host.got.length = 0;
    viewer.got.length = 0;
    const c2h = Buffer.from(`viewer->host ${connectId1} ${Date.now()}`);
    const h2c = Buffer.from(`host->viewer ${connectId1} ${Date.now()}`);
    viewer.sock.send(c2h, RELAY_PORT, relayTo);
    await sleep(250);
    check('viewer media reaches the host unchanged', host.got.some((p) => p.msg.equals(c2h)),
          `hostReceived=${host.got.length}`);
    host.sock.send(h2c, UDP, '127.0.0.1');
    await sleep(250);
    check('host media reaches the viewer unchanged', viewer.got.some((p) => p.msg.equals(h2c)),
          `viewerReceived=${viewer.got.length}`);

    // ------------------------------------------------------------ token refusals
    host.got.length = 0;
    const strangerLan = await rec(lan);
    strangerLan.sock.send(buildHello(KIND_HELLO, '', FEATURE_FEC), RELAY_PORT, relayTo);
    strangerLan.sock.send(buildHello(KIND_HELLO, 'f'.repeat(32), FEATURE_FEC), RELAY_PORT, relayTo);
    strangerLan.sock.send(Buffer.from('not a packet anyone knows'), RELAY_PORT, relayTo);
    await sleep(300);
    check('a Hello with no token, a wrong token, or junk from the LAN opens nothing and reaches the host 0 times',
          host.got.length === 0, `hostReceived=${host.got.length}`);
    check('...the wrong token is logged as unknown, without printing it',
          logHas(/hello rejected src=.* reason=unknown-or-expired-token/) && !serverLog.includes('f'.repeat(32)));

    // The valid token from a different wire address: loopback is not the LAN, so this is the
    // capability presented from somewhere it was not minted for.
    const thief = await rec('127.0.0.1');
    thief.sock.send(buildHello(KIND_HELLO, token1, FEATURE_FEC), RELAY_PORT, '127.0.0.1');
    await sleep(300);
    check('a valid token presented from a different wire address is refused (host reached 0 times)',
          host.got.length === 0, `hostReceived=${host.got.length}`);
    check('...logged as wire-mismatch with the expected wire and the advertised address, not the token',
          logHas(new RegExp(`connect=${connectId1} hello rejected src=127\\.0\\.0\\.1:\\d+ reason=wire-mismatch ` +
                            `expectedWire=${lan.replace(/\./g, '\\.')} advertised=${PUBLIC_IP.replace(/\./g, '\\.')}`)) &&
          !serverLog.includes(token1));
    viewer.got.length = 0;
    const stillMine = Buffer.from('host media after the refusals');
    host.sock.send(stillMine, UDP, '127.0.0.1');
    await sleep(250);
    check('the refusals did not move the lease: host media still reaches the original viewer',
          viewer.got.some((p) => p.msg.equals(stillMine)), `viewerReceived=${viewer.got.length}`);

    // Same wire address, different port: the NAT contract. A new connect from a second socket on
    // the LAN takes the host; the old token is then superseded and cannot take it back.
    await sleep(1100);  // past the per-host wake interval
    const viewer2 = await rec(lan);
    await observe(viewer2.sock, 'viewer2-observe', lan);
    viewer2.got.length = 0;
    r = await api('POST', '/api/connect', { hostId, observeToken: 'viewer2-observe' }, session);
    const token2 = r.body.punchToken;
    const connectId2 = r.body.connectId;
    check('a second connect from the same wire address (new port) is accepted', r.status === 200 && !!token2);
    host.got.length = 0;
    viewer2.sock.send(buildHello(KIND_HELLO, token2, FEATURE_FEC), RELAY_PORT, relayTo);
    await sleep(300);
    const fwd2 = host.got.find(isHello);
    check('a valid Hello from the same wire address on another port is accepted (NAT contract kept)',
          !!fwd2, `hostReceived=${host.got.length}`);
    check('...bound to its own raw tuple',
          logHas(new RegExp(`connect=${connectId2} bound client=${lan.replace(/\./g, '\\.')}:${viewer2.sock.address().port} `)));
    if (fwd2) {
      host.sock.send(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH),
                     fwd2.rinfo.port, fwd2.rinfo.address);
      await sleep(250);
    }
    host.got.length = 0;
    viewer.sock.send(buildHello(KIND_HELLO, token1, FEATURE_FEC), RELAY_PORT, relayTo);
    await sleep(300);
    check('the superseded token cannot reclaim the host (host reached 0 times)', host.got.length === 0,
          `hostReceived=${host.got.length}`);
    // Minting token2 deleted token1's record outright, so from the listener it is an unknown
    // token -- logged under the viewer's own tuple, which is what ties the line to the attempt.
    check('...and is logged as a refused token from the viewer\'s tuple',
          logHas(new RegExp(`hello rejected src=${lan.replace(/\./g, '\\.')}:${viewer.sock.address().port} ` +
                            `reason=(unknown-or-expired-token|superseded-token)`)));
    const dropped = Buffer.from('media from the superseded session');
    viewer.sock.send(dropped, RELAY_PORT, relayTo);
    await sleep(250);
    check('media from the superseded session is dropped', !host.got.some((p) => p.msg.equals(dropped)));

    // ------------------------------------------------------------ two LAN clients, one advertised address
    // Before the fix both were eligible under PUBLIC_IP and the second connect overwrote the
    // first's record. Now each session is indexed by its raw tuple and traffic must not cross.
    r = await api('POST', '/api/host/register',
                  { id: 'tester', pw: 'test-pass-1234', hostName: 'second-pc', machineId: 'm-second' });
    const host2Id = r.body.hostId;
    const host2 = await rec('127.0.0.1');
    await observe(host2.sock, 'host2-observe', '127.0.0.1');
    host2.got.length = 0;
    await api('POST', '/api/host/heartbeat', { hostToken: r.body.hostToken, hostName: 'second-pc',
                                               observeToken: 'host2-observe',
                                               localUdpPort: host2.sock.address().port, localIps: [] });
    const viewerA = await rec(lan);
    await observe(viewerA.sock, 'viewerA-observe', lan);
    const viewerB = await rec(lan);
    await observe(viewerB.sock, 'viewerB-observe', lan);
    r = await api('POST', '/api/connect', { hostId, observeToken: 'viewerA-observe' }, session);
    const tokenA = r.body.punchToken;
    const connectIdA = r.body.connectId;
    r = await api('POST', '/api/connect', { hostId: host2Id, observeToken: 'viewerB-observe' }, session);
    const tokenB = r.body.punchToken;
    const connectIdB = r.body.connectId;
    check('two LAN clients (one advertised address) each get a relay capability for their own host',
          !!tokenA && !!tokenB && (r.body.candidates || []).some((c) => c.kind === 'relay'));
    viewerA.got.length = 0;
    viewerB.got.length = 0;
    viewerA.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, relayTo);
    viewerB.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, relayTo);
    await sleep(GRACE_MS + 500);
    check('both LAN clients get their punches answered',
          viewerA.got.some(isPunch) && viewerB.got.some(isPunch),
          `A=${viewerA.got.filter(isPunch).length} B=${viewerB.got.filter(isPunch).length}`);
    host.got.length = 0;
    host2.got.length = 0;
    viewerA.sock.send(buildHello(KIND_HELLO, tokenA, FEATURE_FEC), RELAY_PORT, relayTo);
    viewerB.sock.send(buildHello(KIND_HELLO, tokenB, FEATURE_FEC), RELAY_PORT, relayTo);
    await sleep(300);
    const fwdA = host.got.find(isHello);
    const fwdB = host2.got.find(isHello);
    check('each client\'s Hello reaches ITS host and only that one',
          !!fwdA && !!fwdB && host.got.filter(isHello).length === 1 && host2.got.filter(isHello).length === 1,
          `host1=${host.got.length} host2=${host2.got.length}`);
    check('...each bound to its own raw tuple',
          logHas(new RegExp(`connect=${connectIdA} bound client=${lan.replace(/\./g, '\\.')}:${viewerA.sock.address().port} `)) &&
          logHas(new RegExp(`connect=${connectIdB} bound client=${lan.replace(/\./g, '\\.')}:${viewerB.sock.address().port} `)));
    if (fwdA) host.sock.send(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH), fwdA.rinfo.port, fwdA.rinfo.address);
    if (fwdB) host2.sock.send(buildHello(KIND_HELLO_ACK, '', FEATURE_FEC | FEATURE_DIRECTORY_AUTH), fwdB.rinfo.port, fwdB.rinfo.address);
    await sleep(250);
    host.got.length = 0;
    host2.got.length = 0;
    viewerA.got.length = 0;
    viewerB.got.length = 0;
    const mA = Buffer.from('from LAN client A');
    const mB = Buffer.from('from LAN client B');
    viewerA.sock.send(mA, RELAY_PORT, relayTo);
    viewerB.sock.send(mB, RELAY_PORT, relayTo);
    await sleep(250);
    check('client media does not cross between the two sessions',
          host.got.some((p) => p.msg.equals(mA)) && !host.got.some((p) => p.msg.equals(mB)) &&
          host2.got.some((p) => p.msg.equals(mB)) && !host2.got.some((p) => p.msg.equals(mA)),
          `host1=${host.got.length} host2=${host2.got.length}`);
    const rA = Buffer.from('to LAN client A');
    const rB = Buffer.from('to LAN client B');
    host.sock.send(rA, UDP, '127.0.0.1');
    host2.sock.send(rB, UDP, '127.0.0.1');
    await sleep(250);
    check('host media does not cross between the two sessions',
          viewerA.got.some((p) => p.msg.equals(rA)) && !viewerA.got.some((p) => p.msg.equals(rB)) &&
          viewerB.got.some((p) => p.msg.equals(rB)) && !viewerB.got.some((p) => p.msg.equals(rA)),
          `A=${viewerA.got.length} B=${viewerB.got.length}`);

    // ------------------------------------------------------------ the diagnostics are bounded
    const flood = await rec('127.0.0.1');
    const before = logCount(/punch ignored src=127\.0\.0\.1:/g);
    for (let i = 0; i < 20; ++i) flood.sock.send(buildHello(KIND_PUNCH, '', 0), RELAY_PORT, '127.0.0.1');
    await sleep(300);
    const after = logCount(/punch ignored src=127\.0\.0\.1:/g);
    check('twenty ineligible punches from one address produce at most three log lines',
          after - before >= 1 && after - before <= 3, `lines=${after - before}`);
    check('no session was opened by any unauthenticated datagram',
          logCount(/ bound client=/g) === 4, `bound=${logCount(/ bound client=/g)}`);
    check('no capability token appears in the log',
          ![token1, token2, tokenA, tokenB].some((t) => t && serverLog.includes(t)));
  } catch (e) {
    console.error('relay_wire_test error:', e.stack || e.message);
    failures++;
  } finally {
    for (const s of sockets) { try { s.close(); } catch { /* closed */ } }
    await new Promise((resolve) => { server.on('exit', resolve); server.kill(); });
    fs.rmSync(dataPath, { force: true });
    if (serverPath !== realServerPath) fs.rmSync(serverPath, { force: true });
    if (process.env.RELAY_WIRE_TEST_SHOW_LOG === '1') {
      console.log('--- server log ---');
      console.log(serverLog.split('\n').filter((l) => l.includes('[relay]') || l.includes('[observe]')).join('\n'));
    }
    console.log(failures === 0 ? `\nrelay_wire_test(${MODE}): PASS` : `\nrelay_wire_test(${MODE}): ${failures} FAILED`);
    process.exit(failures === 0 ? 0 : 1);
  }
})();
