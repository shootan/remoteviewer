// The wake keeps going until the host takes the capability -- and stops, and stays bounded.
//
// The burst of three is over in 300ms. A host whose heartbeat has just gone out will not come
// back for twenty-five seconds, and if it missed all three there is nothing else: the capability
// sits in pendingPunch until it expires and the user's first click fails. That is the shape of
// the company PC1 -> PC2 complaint.
//
// So this counts DATAGRAMS, at the host, rather than reading the server's intentions. The things
// that matter are all counts or absences: at most thirteen per connect, none after the capability
// is handed over, one timer per host no matter how many connects, and nothing at all caused by
// the host's own punches -- which the host now sends, since C1, and which must not feed back
// into this.
//
// Its own server, its own ports, its own data file, so it cannot disturb anything beside it.

const { spawn, spawnSync } = require('child_process');
const dgram = require('dgram');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const HTTP = 18094;
const UDP = 18095;

const MAGIC = 0x31435052;
const HELLO_BYTES = 49;
const KIND_PUNCH = 303;

// Matching server.js. Named here rather than imported: if a constant changes, this file should
// fail and be read, not silently follow.
const WAKE_BURST = 3;
const WAKE_RESEND_MAX = 10;
const WAKE_RESEND_INTERVAL_MS = 1000;

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const serverPath = path.join(__dirname, '..', 'server.js');
const dataPath = path.join(os.tmpdir(), `remote60-wake-resend-${process.pid}.json`);
const env = {
  ...process.env,
  REMOTE60_DIR_DATA: dataPath,
  REMOTE60_DIR_PORT: String(HTTP),
  REMOTE60_DIR_UDP_PORT: String(UDP),
  REMOTE60_DIR_SIGNUP_KEY: 'test-signup-key',
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

/** Counts only the punch datagrams; the observe reply is JSON on the same socket. */
function recorder() {
  const sock = dgram.createSocket('udp4');
  const punches = [];
  sock.on('message', (msg) => {
    if (msg.length === HELLO_BYTES && msg.readUInt32LE(0) === MAGIC &&
        msg.readUInt16LE(4) === KIND_PUNCH) {
      punches.push(Date.now());
    }
  });
  return { sock, punches };
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
                  { id: 'tester', pw: 'test-pass-1234', hostName: 'wake-host',
                    machineId: 'machine-wake' });
    const hostToken = r.body.hostToken;
    const hostId = r.body.hostId;
    check('host registers', r.status === 200 && !!hostToken, `status=${r.status}`);

    const host = recorder();
    await bindLocal(host.sock);
    await observe(host.sock, 'wake-host-observe');

    const heartbeat = () => api('POST', '/api/host/heartbeat', {
      hostToken, hostName: 'wake-host', observeToken: 'wake-host-observe',
      localUdpPort: host.sock.address().port, localIps: ['127.0.0.1'],
    });
    await heartbeat();

    const client = dgram.createSocket('udp4');
    await bindLocal(client);
    await observe(client, 'wake-client-observe');
    const connect = () =>
      api('POST', '/api/connect', { hostId, observeToken: 'wake-client-observe' }, session);

    // =============================================== a host that never collects, counted at the wire
    host.punches.length = 0;
    const started = Date.now();
    r = await connect();
    check('connect succeeds', r.status === 200 && !!r.body.punchToken, `status=${r.status}`);

    // Long enough for the whole allowance and a little past it, so a resend that does not stop
    // shows up as a count over the bound rather than as a test that ended early.
    await sleep(WAKE_RESEND_INTERVAL_MS * (WAKE_RESEND_MAX + 2) + 600);

    const total = host.punches.length;
    check('a host that never collects still gets more than the burst', total > WAKE_BURST,
          `${total} datagrams in ${Date.now() - started}ms`);
    check('...and never more than the burst plus the resend allowance',
          total <= WAKE_BURST + WAKE_RESEND_MAX,
          `${total} of at most ${WAKE_BURST + WAKE_RESEND_MAX}`);

    // The spacing is the other half of "bounded": thirteen datagrams in 300ms would satisfy the
    // count and be exactly the flood this is meant not to be.
    const afterBurst = host.punches.filter((t) => t - host.punches[0] > 500);
    let tooClose = 0;
    for (let i = 1; i < afterBurst.length; i++) {
      if (afterBurst[i] - afterBurst[i - 1] < WAKE_RESEND_INTERVAL_MS - 250) tooClose++;
    }
    check('...spread one a second, not bunched', tooClose === 0,
          `${tooClose} gaps under ${WAKE_RESEND_INTERVAL_MS - 250}ms of ${afterBurst.length}`);

    check('the resend says it stopped, and why', lines('resend stopped').length >= 1,
          (lines('resend stopped')[0] || '').trim());
    check('...on its budget, since nothing collected the capability',
          (lines('resend stopped')[0] || '').includes('reason=budget'),
          (lines('resend stopped')[0] || '').trim());

    // =============================================== the capability handed over stops it at once
    log = '';
    host.punches.length = 0;
    r = await connect();
    check('a second connect succeeds', r.status === 200, `status=${r.status}`);
    // Let the burst land, then collect. The heartbeat is what hands the capability over.
    await sleep(1400);
    const beforeCollect = host.punches.length;
    const hb = await heartbeat();
    // At least this connect's. The first phase deliberately never heartbeats, so its capability
    // is still queued and gets collected here too -- asserting exactly one was wrong about the
    // fixture, not about the server.
    check('the heartbeat carries the capability',
          hb.status === 200 && (hb.body.pendingPunch || []).length >= 1,
          `${(hb.body.pendingPunch || []).length} pending`);
    await sleep(WAKE_RESEND_INTERVAL_MS * 3);
    const afterCollect = host.punches.length - beforeCollect;
    check('nothing more is sent once the capability has been handed over', afterCollect === 0,
          `${afterCollect} datagrams in ${WAKE_RESEND_INTERVAL_MS * 3}ms after collecting`);
    check('...and the log says that is why it stopped',
          lines('resend stopped').some((l) => l.includes('reason=collected')),
          (lines('resend stopped')[0] || '').trim());

    // The word itself. "collected" has been read as "the host received and applied it", which it
    // is not -- the host acknowledges nothing, and the line now says so.
    check('the collected line says what collected means',
          lines('[capability] collected').some((l) => l.includes('meaning=handed-to-response')),
          (lines('[capability] collected')[0] || '').trim());

    // =============================================== several connects, one timer
    log = '';
    host.punches.length = 0;
    await connect();
    await sleep(120);
    await connect();
    await sleep(120);
    await connect();
    await sleep(WAKE_RESEND_INTERVAL_MS * (WAKE_RESEND_MAX + 2) + 600);

    const armed = lines('resend armed').length;
    check('three connects to one host arm one resend each, replacing rather than adding',
          armed === 3, `${armed} armed lines`);
    // Three timers would be three datagrams a second. The bound is per HOST, so it is still the
    // single allowance -- plus each connect's own burst, which is a separate thing.
    const many = host.punches.length;
    check('...and the datagrams stay on one host allowance, not three',
          many <= WAKE_BURST * 3 + WAKE_RESEND_MAX + 1,
          `${many} of at most ${WAKE_BURST * 3 + WAKE_RESEND_MAX + 1}`);
    const stopped = lines('resend stopped').length;
    check('...with one stop, not three', stopped === 1, `${stopped} stop lines`);

    // =============================================== the host's own punch changes nothing (C1)
    //
    // Since C1 the host answers punches. Those answers go to whoever punched it -- never to this
    // server, which does not listen for them -- so they cannot extend, restart or re-arm any of
    // the above. Pinned here because "the two features do not interact" is the kind of claim
    // that is true until somebody wires a listener.
    log = '';
    host.punches.length = 0;
    const punch = Buffer.alloc(HELLO_BYTES);
    punch.writeUInt32LE(MAGIC, 0);
    punch.writeUInt16LE(KIND_PUNCH, 4);
    punch.writeUInt16LE(HELLO_BYTES, 6);
    punch.writeUInt32LE(2, 8);
    for (let i = 0; i < 12; i++) {
      host.sock.send(punch, UDP, '127.0.0.1');
      await sleep(80);
    }
    await sleep(600);
    check('a host punching the server arms no resend', lines('resend armed').length === 0,
          `${lines('resend armed').length} armed lines`);
    check('...and produces no wake at all', lines('[wake]').length === 0,
          `${lines('[wake]').length} wake lines`);
    check('...and no datagram comes back to it', host.punches.length === 0,
          `${host.punches.length} datagrams`);

    // =============================================== the map is reclaimed, so the next one works
    log = '';
    host.punches.length = 0;
    r = await connect();
    check('a connect after all that still arms', r.status === 200 &&
          lines('resend armed').length === 1, `${lines('resend armed').length} armed lines`);
    check('...with one timer, not a pile left behind',
          (lines('resend armed')[0] || '').includes('timers=1'),
          (lines('resend armed')[0] || '').trim());
    await sleep(1400);
    await heartbeat();
    await sleep(300);

    host.sock.close();
    client.close();
  } catch (err) {
    check('the test ran to the end', false, err && err.message);
  }

  cleanup();
  console.log(failures === 0 ? '\nwake_resend_test: PASS' : `\nwake_resend_test: FAILED (${failures})`);
  process.exit(failures === 0 ? 0 : 1);
})();
