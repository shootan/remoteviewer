// A host must stay signed in across a server restart.
//
// Host tokens once lived only in memory, so every deploy or reboot quietly invalidated them.
// Each PC would then be told its token was unknown, and since the host app deliberately does
// not keep the password, someone had to walk to the machine and sign in again. This runs in two
// phases around a restart performed by the runner; the register phase hands the runner
// "hostToken:sessionToken". Client sessions are in memory by contract (the server's auth
// lifetime is a separate scope): the verify phase pins that a restart DOES forget them and that
// an unknown session is refused, which is what the client shell's uploader must cope with.

const http = require('http');
const dgram = require('dgram');

const HTTP = Number(process.env.T_PORT || 18080);
const UDP = Number(process.env.T_UDP || 18081);
const phase = process.argv[2];

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

// A heartbeat needs an observation now: the server publishes the address the UDP packet came
// from and has no second-best answer, so this sends one from a socket of its own first.
function observe(token) {
  return new Promise((resolve, reject) => {
    const sock = dgram.createSocket('udp4');
    const timer = setTimeout(() => { sock.close(); reject(new Error('observe timeout')); }, 3000);
    sock.on('message', () => { clearTimeout(timer); sock.close(); resolve(); });
    sock.on('error', (e) => { clearTimeout(timer); reject(e); });
    sock.send(Buffer.from('OBSERVE ' + token), UDP, '127.0.0.1');
  });
}

(async () => {
  if (phase === 'register') {
    const r = await api('POST', '/api/host/register',
      { id: 'tester', pw: 'test-pass-1234', hostName: 'Restart PC', machineId: 'machine-restart' });
    if (r.status !== 200 || !r.body.hostToken) {
      console.log(`FAIL  host registers before restart  status=${r.status}`);
      process.exit(1);
    }
    const login = await api('POST', '/api/login', { id: 'tester', pw: 'test-pass-1234' });
    if (login.status !== 200 || !login.body.sessionToken) {
      console.log(`FAIL  client logs in before restart  status=${login.status}`);
      process.exit(1);
    }
    // Handed to the runner, which restarts the server and passes it back.
    process.stdout.write(r.body.hostToken + ':' + login.body.sessionToken);
    process.exit(0);
  }

  if (phase === 'verify') {
    const handed = (process.argv[3] || '').split(':');
    const token = handed[0] || '';
    const sessionToken = handed[1] || '';
    let failures = 0;

    // Two halves, because they fail differently: a forgotten token is 401 and comes from the
    // token check, while a missing observation is 409 and comes from after it. Asserting only the
    // 200 would let a 409 read as "the token did not survive", which is the opposite of true.
    let r = await api('POST', '/api/host/heartbeat', { hostToken: token });
    const recognised = r.status === 409 && r.body.error === 'observation_required';
    console.log(`${recognised ? 'PASS' : 'FAIL'}  the restored token is recognised (and still needs an observation)  status=${r.status} error=${r.body.error}`);
    if (!recognised) failures++;

    await observe('restart-observe-1');
    r = await api('POST', '/api/host/heartbeat', { hostToken: token, observeToken: 'restart-observe-1' });
    const survived = r.status === 200 && r.body.ok === true;
    console.log(`${survived ? 'PASS' : 'FAIL'}  host token survives a server restart  status=${r.status}`);
    if (!survived) failures++;

    // The stored form must not be the token itself, or a leaked store file is a set of keys.
    const fs = require('fs');
    const raw = fs.readFileSync(process.env.REMOTE60_DIR_DATA, 'utf8');
    const plaintextAbsent = !raw.includes(token) && !raw.includes(sessionToken);
    console.log(`${plaintextAbsent ? 'PASS' : 'FAIL'}  store keeps only the token hashes`);
    if (!plaintextAbsent) failures++;

    r = await api('GET', '/api/hosts', null, sessionToken);
    const sessionForgotten = r.status === 401;
    console.log(`${sessionForgotten ? 'PASS' : 'FAIL'}  a client session does not survive a server restart (in-memory by contract)  status=${r.status}`);
    if (!sessionForgotten) failures++;

    r = await api('GET', '/api/hosts', null, 'not-a-real-session');
    const sessionRejected = r.status === 401;
    console.log(`${sessionRejected ? 'PASS' : 'FAIL'}  unknown session still rejected  status=${r.status}`);
    if (!sessionRejected) failures++;

    r = await api('POST', '/api/host/heartbeat', { hostToken: 'not-a-real-token' });
    const rejected = r.status === 401;
    console.log(`${rejected ? 'PASS' : 'FAIL'}  unknown token still rejected  status=${r.status}`);
    if (!rejected) failures++;

    // Re-registering rotates the token, so the previous one must stop working.
    const again = await api('POST', '/api/host/register',
      { id: 'tester', pw: 'test-pass-1234', hostName: 'Restart PC', machineId: 'machine-restart' });
    r = await api('POST', '/api/host/heartbeat', { hostToken: token });
    const rotated = again.status === 200 && again.body.hostToken !== token && r.status === 401;
    console.log(`${rotated ? 'PASS' : 'FAIL'}  re-registration retires the old token  status=${r.status}`);
    if (!rotated) failures++;

    process.exit(failures === 0 ? 0 : 1);
  }

  console.error('usage: restart_test.js register|verify <token>');
  process.exit(2);
})().catch((e) => { console.error('restart test error:', e.message); process.exit(1); });
