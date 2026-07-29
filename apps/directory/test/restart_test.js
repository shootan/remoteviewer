// A host must stay signed in across a server restart.
//
// Host tokens once lived only in memory, so every deploy or reboot quietly invalidated them.
// Each PC would then be told its token was unknown, and since the host app deliberately does
// not keep the password, someone had to walk to the machine and sign in again. This runs in two
// phases around a restart performed by the runner.

const http = require('http');

const HTTP = Number(process.env.T_PORT || 18080);
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

(async () => {
  if (phase === 'register') {
    const r = await api('POST', '/api/host/register',
      { id: 'tester', pw: 'test-pass-1234', hostName: 'Restart PC', machineId: 'machine-restart' });
    if (r.status !== 200 || !r.body.hostToken) {
      console.log(`FAIL  host registers before restart  status=${r.status}`);
      process.exit(1);
    }
    // Handed to the runner, which restarts the server and passes it back.
    process.stdout.write(r.body.hostToken);
    process.exit(0);
  }

  if (phase === 'verify') {
    const token = process.argv[3] || '';
    let failures = 0;

    let r = await api('POST', '/api/host/heartbeat', { hostToken: token });
    const survived = r.status === 200 && r.body.ok === true;
    console.log(`${survived ? 'PASS' : 'FAIL'}  host token survives a server restart  status=${r.status}`);
    if (!survived) failures++;

    // The stored form must not be the token itself, or a leaked store file is a set of keys.
    const fs = require('fs');
    const raw = fs.readFileSync(process.env.REMOTE60_DIR_DATA, 'utf8');
    const plaintextAbsent = !raw.includes(token);
    console.log(`${plaintextAbsent ? 'PASS' : 'FAIL'}  store keeps only the token hash`);
    if (!plaintextAbsent) failures++;

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
