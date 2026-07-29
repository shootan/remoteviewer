// End-to-end exercise of the directory service: account login, host registration and
// heartbeat, host listing, and the connect handshake that exchanges punch addresses.
const http = require('http');
const dgram = require('dgram');

const HTTP = Number(process.env.T_PORT || 18080);
const UDP = Number(process.env.T_UDP || 18081);
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

// Ask the server what public address it sees for this exact socket.
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

(async () => {
  // Account creation: without it the only working accounts are ones made by hand on the
  // server, and choosing your own id comes back as "not correct", which reads like a typo.
  let r = await api('POST', '/api/signup',
    { id: 'newcomer', pw: 'a-good-password', signupKey: 'test-signup-key' });
  check('signup creates an account', r.status === 200, `status=${r.status}`);

  r = await api('POST', '/api/login', { id: 'newcomer', pw: 'a-good-password' });
  check('the new account can log in', r.status === 200 && !!r.body.sessionToken, `status=${r.status}`);

  r = await api('POST', '/api/signup', { id: 'intruder', pw: 'a-good-password', signupKey: 'wrong' });
  check('signup refuses a wrong key', r.status === 403, `status=${r.status}`);

  // The key must not become a way to take over an id that already exists.
  r = await api('POST', '/api/signup',
    { id: 'newcomer', pw: 'different-password', signupKey: 'test-signup-key' });
  check('signup refuses an id already taken', r.status === 409, `status=${r.status}`);
  r = await api('POST', '/api/login', { id: 'newcomer', pw: 'a-good-password' });
  check('the original password still works', r.status === 200, `status=${r.status}`);

  r = await api('POST', '/api/signup', { id: 'shorty', pw: 'short', signupKey: 'test-signup-key' });
  check('signup refuses a weak password', r.status === 400, `status=${r.status}`);

  r = await api('POST', '/api/login', { id: 'tester', pw: 'wrong-password' });
  check('wrong password rejected', r.status === 401, `status=${r.status}`);

  r = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
  check('login succeeds', r.status === 200 && !!r.body.sessionToken, `status=${r.status}`);
  const session = r.body.sessionToken;

  r = await api('GET', '/api/hosts', null, session);
  check('host list empty before registration', r.status === 200 && r.body.hosts.length === 0);

  r = await api('GET', '/api/hosts', null, 'bogus-token');
  check('bad session rejected', r.status === 401, `status=${r.status}`);

  r = await api('POST', '/api/host/register',
    { id: 'tester', pw: 'test-pass-1234', hostName: 'Office PC', machineId: 'machine-aaa' });
  check('host registers', r.status === 200 && !!r.body.hostToken, `status=${r.status}`);
  let hostToken = r.body.hostToken;
  const hostId = r.body.hostId;

  // Same machine registering twice must not create a second entry, and the fresh token
  // replaces the old one: re-registering is how a host recovers, so the previous token has
  // to stop working.
  r = await api('POST', '/api/host/register',
    { id: 'tester', pw: 'test-pass-1234', hostName: 'Office PC', machineId: 'machine-aaa' });
  check('re-register keeps one host', r.body.hostId === hostId, `hostId=${r.body.hostId}`);
  check('re-register issues a new token', r.body.hostToken !== hostToken);

  const staleHeartbeat = await api('POST', '/api/host/heartbeat', { hostToken });
  check('previous token stops working', staleHeartbeat.status === 401,
    `status=${staleHeartbeat.status}`);
  hostToken = r.body.hostToken;

  const hostObs = await observe('host-observe-1');
  check('udp observation returns the socket port',
    hostObs.seen.port === hostObs.localPort,
    `seen=${hostObs.seen.ip}:${hostObs.seen.port} local=${hostObs.localPort}`);

  r = await api('POST', '/api/host/heartbeat', { hostToken, observeToken: 'host-observe-1' });
  check('heartbeat accepted', r.status === 200 && r.body.ok === true, `status=${r.status}`);
  check('heartbeat reports observed port', r.body.observedPort === hostObs.localPort,
    `observed=${r.body.observedPort}`);

  r = await api('GET', '/api/hosts', null, session);
  check('host now listed and online',
    r.body.hosts.length === 1 && r.body.hosts[0].online === true,
    JSON.stringify(r.body.hosts));

  const cliObs = await observe('client-observe-1');
  r = await api('POST', '/api/connect', { hostId, observeToken: 'client-observe-1' }, session);
  check('connect returns host address',
    r.status === 200 && r.body.hostPublicUdpPort === hostObs.localPort && !!r.body.punchToken,
    `port=${r.body.hostPublicUdpPort} token=${(r.body.punchToken || '').slice(0, 8)}`);

  r = await api('POST', '/api/host/heartbeat', { hostToken, observeToken: 'host-observe-1' });
  check('host receives the pending punch',
    r.body.pendingPunch.length === 1 && r.body.pendingPunch[0].port === cliObs.localPort,
    JSON.stringify(r.body.pendingPunch));

  r = await api('POST', '/api/host/heartbeat', { hostToken, observeToken: 'host-observe-1' });
  check('punch is delivered once only', r.body.pendingPunch.length === 0);

  r = await api('POST', '/api/connect', { hostId: 'does-not-exist', observeToken: 'client-observe-1' }, session);
  check('unknown host rejected', r.status === 404, `status=${r.status}`);


  console.log(failures === 0 ? '\nRESULT: ALL PASS' : `\nRESULT: ${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error('test error:', e.message); process.exit(1); });
