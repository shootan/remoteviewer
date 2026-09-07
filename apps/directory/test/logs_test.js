// Pins POST /api/logs -- the endpoint that lets a host, a client or a phone hand its log lines to
// the directory instead of having someone copy files off three machines.
//
// What matters here is not that a line can be stored; it is that the endpoint cannot be used by
// someone who has no account, cannot be used to fill the disk, and cannot be talked into writing
// outside its own directory. Those are the three ways a log sink on a home NAS goes wrong.
//
// Run against a server started with REMOTE60_LOG_DIR and a small REMOTE60_LOG_RATE_KB_PER_MIN.
const http = require('http');
const fs = require('fs');
const path = require('path');

const HTTP = Number(process.env.T_PORT || 18080);
const LOG_DIR = process.env.REMOTE60_LOG_DIR || '';
let failures = 0;

function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

function request(method, urlPath, { body, headers = {} } = {}) {
  return new Promise((resolve, reject) => {
    const payload = body === undefined ? null
      : (typeof body === 'string' ? Buffer.from(body) : Buffer.from(JSON.stringify(body)));
    const req = http.request(
      {
        host: '127.0.0.1', port: HTTP, path: urlPath, method,
        headers: Object.assign(
          payload ? { 'content-length': payload.length } : {},
          typeof body === 'object' && body !== null ? { 'content-type': 'application/json' } : {},
          headers),
      },
      (res) => {
        let out = '';
        res.on('data', (c) => (out += c));
        res.on('end', () => {
          let parsed = null;
          try { parsed = JSON.parse(out); } catch { /* some errors are not json */ }
          resolve({ status: res.statusCode, body: parsed, raw: out });
        });
      });
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

(async () => {
  const login = await request('POST', '/api/login', { body: { id: 'tester', pw: 'test-pass-1234' } });
  const token = login.body && login.body.sessionToken;
  if (!token) {
    console.log('FAIL  could not log in to get a session token', login.status, login.raw);
    process.exit(1);
  }

  // 1. no credentials at all
  const anon = await request('POST', '/api/logs', {
    body: 'anonymous line\n',
    headers: { 'x-log-device': 'dev1', 'x-log-stream': 'viewer' },
  });
  check('an upload without a token is refused', anon.status === 401, `status=${anon.status}`);

  // 2. the happy path
  const first = await request('POST', '/api/logs', {
    body: 'hello from the viewer\nsecond line\n',
    headers: { authorization: `Bearer ${token}`, 'x-log-device': 'dev1', 'x-log-stream': 'viewer' },
  });
  check('a session token stores the batch', first.status === 200 && first.body && first.body.ok,
        `status=${first.status}`);

  const stored = path.join(LOG_DIR, 'tester', 'dev1', 'viewer.log');
  const text = fs.existsSync(stored) ? fs.readFileSync(stored, 'utf8') : '';
  check('the lines land under account/device/stream', text.includes('hello from the viewer'),
        stored);

  // 3. appends rather than replaces, because a session spans many batches
  await request('POST', '/api/logs', {
    body: 'third line\n',
    headers: { authorization: `Bearer ${token}`, 'x-log-device': 'dev1', 'x-log-stream': 'viewer' },
  });
  const appended = fs.readFileSync(stored, 'utf8');
  check('a second batch appends',
        appended.includes('hello from the viewer') && appended.includes('third line'));

  // 4. a device name cannot climb out of the log directory
  await request('POST', '/api/logs', {
    body: 'escape attempt\n',
    headers: { authorization: `Bearer ${token}`, 'x-log-device': '../../etc', 'x-log-stream': 'sh' },
  });
  const escaped = path.join(path.dirname(path.dirname(LOG_DIR)), 'etc');
  check('a traversing device name is flattened, not followed', !fs.existsSync(escaped), escaped);

  // 5. the budget bites; the rate limit is set small for this run
  let limited = 0;
  const chunk = 'x'.repeat(2048) + '\n';
  for (let i = 0; i < 12; i++) {
    const r = await request('POST', '/api/logs', {
      body: chunk,
      headers: { authorization: `Bearer ${token}`, 'x-log-device': 'flooder', 'x-log-stream': 'viewer' },
    });
    if (r.status === 429) limited++;
  }
  check('a device that floods is rate limited rather than served', limited > 0,
        `429s=${limited}/12`);

  // 6. the limit is per device, so one noisy machine does not silence another
  const other = await request('POST', '/api/logs', {
    body: 'still fine\n',
    headers: { authorization: `Bearer ${token}`, 'x-log-device': 'dev2', 'x-log-stream': 'host' },
  });
  check('another device is unaffected by that budget', other.status === 200, `status=${other.status}`);

  // 7. a host token authenticates too: the host app has no session, only its registration
  const hostReg = { id: 'tester', pw: 'test-pass-1234', hostName: 'Log PC', machineId: 'machine-logs' };
  const reg = await request('POST', '/api/host/register', { body: hostReg });
  const hostToken = reg.body && reg.body.hostToken;
  check('a host registers for an upload token', reg.status === 200 && !!hostToken, `status=${reg.status}`);
  const viaHost = await request('POST', '/api/logs', {
    body: 'host line\n',
    headers: { 'x-host-token': hostToken, 'x-log-device': 'pc1', 'x-log-stream': 'host' },
  });
  check('a host token stores the batch', viaHost.status === 200, `status=${viaHost.status}`);
  const hostFile = path.join(LOG_DIR, 'tester', 'pc1', 'host.log');
  check('host lines land under the host\'s account',
        fs.existsSync(hostFile) && fs.readFileSync(hostFile, 'utf8').includes('host line'), hostFile);

  // 8. re-registering retires the previous token for uploads as well -- the uploader has to be
  //    re-pointed at the new one (P10, 2026-09-07: four minutes of the old token = 401 for 2 hours)
  const reg2 = await request('POST', '/api/host/register', { body: hostReg });
  const rotated = reg2.body && reg2.body.hostToken;
  check('re-registration issues a different token', reg2.status === 200 && rotated && rotated !== hostToken);
  const oldAfter = await request('POST', '/api/logs', {
    body: 'stale\n',
    headers: { 'x-host-token': hostToken, 'x-log-device': 'pc1', 'x-log-stream': 'host' },
  });
  check('the retired host token is refused for uploads', oldAfter.status === 401, `status=${oldAfter.status}`);
  const newAfter = await request('POST', '/api/logs', {
    body: 'fresh\n',
    headers: { 'x-host-token': rotated, 'x-log-device': 'pc1', 'x-log-stream': 'host' },
  });
  check('the rotated host token is accepted', newAfter.status === 200, `status=${newAfter.status}`);

  // 9. a session the server does not know (restarted before persistence, expired, made up)
  const stale = await request('POST', '/api/logs', {
    body: 'x\n',
    headers: { authorization: 'Bearer not-a-session', 'x-log-device': 'dev1', 'x-log-stream': 'viewer' },
  });
  check('an unknown session token is refused', stale.status === 401, `status=${stale.status}`);

  console.log(failures === 0 ? '\nlogs_test: PASS' : `\nlogs_test: ${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => {
  console.error('logs_test error:', e.message);
  process.exit(1);
});
