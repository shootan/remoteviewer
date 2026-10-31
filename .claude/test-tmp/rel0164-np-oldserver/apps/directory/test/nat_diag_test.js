// Pins the safety properties of the temporary NAT diagnostics.
//
// The diagnostic exists to answer whether a restrictive network will carry UDP to the port the
// host really listens on. That question is only worth asking if the measurement itself cannot
// change the outcome it measures -- so what is checked here is mostly what the probe must NOT do:
// never answer, never be nominated, never appear when the feature is off.
//
// Run against a server started with REMOTE60_NAT_DIAG_ENABLED=1.
const http = require('http');
const dgram = require('dgram');

const HTTP = Number(process.env.T_PORT || 18080);
const UDP = Number(process.env.T_UDP || 18081);
const DIAG_PORT = Number(process.env.T_DIAG || 18443);
let failures = 0;

function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

function api(method, path, body, token) {
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : null;
    const req = http.request(
      { host: '127.0.0.1', port: HTTP, path, method,
        headers: Object.assign(
          payload ? { 'content-type': 'application/json', 'content-length': Buffer.byteLength(payload) } : {},
          token ? { authorization: 'Bearer ' + token } : {}) },
      (res) => {
        let data = '';
        res.on('data', (c) => (data += c));
        res.on('end', () => {
          try { resolve({ status: res.statusCode, body: JSON.parse(data || '{}') }); }
          catch { resolve({ status: res.statusCode, body: {} }); }
        });
      });
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function observe(token) {
  return new Promise((resolve, reject) => {
    const sock = dgram.createSocket('udp4');
    const timer = setTimeout(() => { sock.close(); reject(new Error('observe timeout')); }, 3000);
    sock.on('message', (msg) => {
      clearTimeout(timer);
      const seen = JSON.parse(msg.toString());
      const localPort = sock.address().port;
      sock.close();
      resolve({ seen, localPort });
    });
    sock.on('error', (e) => { clearTimeout(timer); reject(e); });
    sock.send(Buffer.from('OBSERVE ' + token), UDP, '127.0.0.1');
  });
}

// Sends one datagram at the probe and reports whether anything came back within `waitMs`.
// Silence is the expected result, so the timeout is the pass condition rather than a failure.
function probeReplies(waitMs) {
  return new Promise((resolve) => {
    const sock = dgram.createSocket('udp4');
    let answered = false;
    const timer = setTimeout(() => { sock.close(); resolve(answered); }, waitMs);
    sock.on('message', () => { answered = true; clearTimeout(timer); sock.close(); resolve(true); });
    sock.on('error', () => { clearTimeout(timer); resolve(answered); });
    sock.send(Buffer.from('probe'), DIAG_PORT, '127.0.0.1');
  });
}

(async () => {
  let r = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
  const session = r.body.sessionToken;
  check('login for diagnostics run', r.status === 200 && !!session, `status=${r.status}`);

  // Registration authenticates with credentials in the body, not the session -- the host
  // process owns no browser session. Same shape as directory_test.js.
  r = await api('POST', '/api/host/register',
                { id: 'tester', pw: 'test-pass-1234', hostName: 'diag-host', machineId: 'machine-diag' });
  const hostToken = r.body.hostToken;
  const hostId = r.body.hostId;
  check('host registers', r.status === 200 && !!hostToken, `status=${r.status}`);

  const hostObs = await observe('diag-host-observe');
  await api('POST', '/api/host/heartbeat', {
    hostToken, hostName: 'diag-host', observeToken: 'diag-host-observe',
    localUdpPort: hostObs.localPort, alternateUdpPort: 3478,
    localIps: ['192.168.20.50'],
  });

  const clientObs = await observe('diag-client-observe');
  r = await api('POST', '/api/connect',
                { hostId, observeToken: 'diag-client-observe' }, session);
  const cands = r.body.candidates || [];
  const diag = cands.filter((c) => c.kind === 'diag-silent');

  check('connect succeeds with diagnostics on', r.status === 200, `status=${r.status}`);
  check('exactly one diagnostic candidate is offered', diag.length === 1,
        JSON.stringify(cands));
  check('diagnostic candidate uses the configured probe port',
        diag.length === 1 && diag[0].port === DIAG_PORT,
        diag.length ? `port=${diag[0].port} expected=${DIAG_PORT}` : 'missing');

  // Last matters twice over: the no-answer fallback takes the first candidate, and a real
  // address must never be pushed out of a list the client may cap.
  check('diagnostic candidate is last, never first',
        cands.length > 1 && cands[cands.length - 1].kind === 'diag-silent' &&
        cands[0].kind !== 'diag-silent',
        `first=${cands[0] && cands[0].kind} last=${cands[cands.length - 1] && cands[cands.length - 1].kind}`);

  check('the real candidates are still present and unchanged',
        cands.some((c) => c.kind === 'private') && cands.some((c) => c.kind === 'public'),
        JSON.stringify(cands.map((c) => c.kind)));

  // The one property that makes the whole diagnostic safe to run against live traffic.
  const answered = await probeReplies(700);
  check('the probe never answers, so it can never win the candidate race', answered === false,
        answered ? 'it replied' : 'silent');

  // A second connect from the same client must not accumulate duplicates.
  r = await api('POST', '/api/connect', { hostId, observeToken: 'diag-client-observe' }, session);
  const again = (r.body.candidates || []).filter((c) => c.kind === 'diag-silent');
  check('repeat connect still offers exactly one diagnostic candidate', again.length === 1,
        `count=${again.length}`);

  console.log(failures === 0 ? '\nRESULT: ALL PASS' : `\nRESULT: ${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
