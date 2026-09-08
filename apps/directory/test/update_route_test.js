// The update-manifest endpoint, against a running throwaway server.
//
// What is worth checking here is mostly refusal: unauthenticated callers, platforms that are not
// one of the two, and the case where nothing has been published. The happy path matters too, but
// it is the shortest part -- the endpoint hands back bytes it read from disk.
//
// Run by run.js against a server started with REMOTE60_UPDATE_DIR pointing at a scratch directory.
'use strict';

const http = require('http');

const PORT = Number(process.env.T_PORT || 18080);
let failures = 0;
let checks = 0;

function check(name, cond, detail) {
  checks++;
  if (!cond) failures++;
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
}

function request(path, headers = {}) {
  return new Promise((resolve) => {
    const req = http.request({ host: '127.0.0.1', port: PORT, path, method: 'GET', headers },
      (res) => {
        let body = '';
        res.on('data', (c) => (body += c));
        res.on('end', () => {
          let json = null;
          try { json = JSON.parse(body); } catch { /* not json */ }
          resolve({ status: res.statusCode, body, json });
        });
      });
    req.on('error', () => resolve({ status: 0, body: '', json: null }));
    req.end();
  });
}

async function login() {
  const body = JSON.stringify({ id: 'tester', pw: 'test-pass-1234' });
  return new Promise((resolve) => {
    const req = http.request({
      host: '127.0.0.1', port: PORT, path: '/api/login', method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) },
    }, (res) => {
      let out = '';
      res.on('data', (c) => (out += c));
      res.on('end', () => {
        try { resolve(JSON.parse(out).sessionToken || ''); } catch { resolve(''); }
      });
    });
    req.on('error', () => resolve(''));
    req.write(body);
    req.end();
  });
}

(async () => {
  // This test does not start a server; run.js owns that, because the endpoint only exists when
  // REMOTE60_UPDATE_DIR points somewhere and publishing a manifest is part of the setup. Run
  // directly and every case fails with HTTP 0, which reads like fifteen separate bugs. So the
  // reachability check comes first and stops here.
  {
    const probe = await request('/healthz');
    if (probe.status === 0) {
      console.log(`FAIL  no server on 127.0.0.1:${PORT}`);
      console.log('      This test needs the server run.js starts with REMOTE60_UPDATE_DIR set.');
      console.log('      Run the suite instead:  node test/run.js');
      process.exit(2);
    }
    check('server is reachable', probe.status === 200, `HTTP ${probe.status}`);
  }

  // Anonymous first: the endpoint must not be an open list of what every machine should be running.
  {
    const r = await request('/api/update/manifest?platform=windows');
    check('anonymous is refused', r.status === 401, `HTTP ${r.status}`);
    // Gated on having actually received a response. Without that, "the body has no manifest in
    // it" is satisfied by there being no body at all -- the assertion would pass hardest exactly
    // when the server was unreachable, which is the opposite of useful.
    check('and nothing leaks in the body', r.status !== 0 && !r.body.includes('schema='),
          `HTTP ${r.status} ${r.body.slice(0, 60)}`);
  }

  const token = await login();
  check('logged in for the remaining cases', !!token);
  const auth = { Authorization: `Bearer ${token}` };

  {
    const r = await request('/api/update/manifest?platform=windows', auth);
    check('a published manifest is returned', r.status === 200, `HTTP ${r.status} ${r.body.slice(0, 80)}`);
    check('it carries the document', !!(r.json && r.json.manifest && r.json.manifest.includes('schema=1')));
    check('and its signature', !!(r.json && typeof r.json.signature === 'string' && r.json.signature.length === 128),
          r.json ? String(r.json.signature || '').length : 'none');
    // The document must come back byte-identical or its signature stops matching.
    check('the document ends with a newline, as signed',
          !!(r.json && r.json.manifest.endsWith('\n')));
  }

  {
    const r = await request('/api/update/manifest?platform=android', auth);
    check('a platform with nothing published is 404, not an error',
          r.status === 404, `HTTP ${r.status}`);
  }

  // The platform value becomes part of a filename, so it is whitelisted rather than sanitised.
  for (const bad of ['', 'linux', '../../etc/passwd', 'windows/../android', 'WINDOWS']) {
    const r = await request(`/api/update/manifest?platform=${encodeURIComponent(bad)}`, auth);
    check(`platform "${bad}" is rejected`, r.status === 400, `HTTP ${r.status}`);
  }

  {
    const r = await request('/api/update/manifest', auth);
    check('a missing platform is rejected', r.status === 400, `HTTP ${r.status}`);
  }

  {
    // Route matching is exact, so a near-miss must not fall through to something else.
    const r = await request('/api/update/manifests?platform=windows', auth);
    check('a near-miss path is 404', r.status === 404, `HTTP ${r.status}`);
  }

  {
    const r = await request('/api/update/manifest?platform=windows', { Authorization: 'Bearer nope' });
    check('a bad session token is refused', r.status === 401, `HTTP ${r.status}`);
  }

  console.log(`\n${failures === 0 ? 'RESULT: ALL PASS' : 'RESULT: FAILED'}  (${checks} checks, ${failures} failed)`);
  process.exit(failures === 0 ? 0 : 1);
})();
