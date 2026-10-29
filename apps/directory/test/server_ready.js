// rel-0164 (verifier, 53fced4 directory suite x3): a test's server is ready when it ANSWERS, not after a fixed sleep.
// On this PC spawning node alone took ~1.07 s and server.js said "http on" after 0.97-2.48 s (CPU 9%), while tests slept
// a fixed 300-1500 ms and then connected: ECONNREFUSED in a different test each run (device_credential, relay 18080,
// wake_resend 18094). Product unchanged; this replaces the sleeps.
//
// waitForServer(child, port, label[, { timeoutMs }]): polls GET /healthz on 127.0.0.1:<port> (each request bounded at
// 1 s) until it answers 200, for at most timeoutMs (default 15 s). Returns the milliseconds it took. If the server
// exits first, or never answers within the bound, it says so -- "SERVER DID NOT START <label>: <why>" -- ends that
// server, and exits the process with code 3: a setup failure, never counted as a test result.
const http = require('http');

const DEFAULT_TIMEOUT_MS = 15000;

function probe(port) {
  return new Promise((resolve) => {
    const req = http.get({ host: '127.0.0.1', port, path: '/healthz', timeout: 1000 }, (res) => {
      res.resume();
      resolve(res.statusCode);
    });
    req.on('timeout', () => {
      req.destroy();
      resolve('timeout');
    });
    req.on('error', (e) => resolve(e.code || e.message));
  });
}

async function waitForServer(child, port, label, opts = {}) {
  const timeoutMs = opts.timeoutMs || DEFAULT_TIMEOUT_MS;
  const t0 = Date.now();
  let exited = '';
  const onExit = (code, signal) => { exited = `the server exited first (code=${code} signal=${signal})`; };
  if (child) child.once('exit', onExit);
  let last = 'no answer yet';
  while (Date.now() - t0 < timeoutMs && !exited) {
    const r = await probe(Number(port));
    if (r === 200) {
      if (child) child.removeListener('exit', onExit);
      return Date.now() - t0;
    }
    last = String(r);
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  if (child) child.removeListener('exit', onExit);
  const why = exited || `no /healthz 200 on port ${port} within ${timeoutMs} ms (last: ${last})`;
  console.log(`SERVER DID NOT START  ${label}: ${why} -- a setup failure, not a test result`);
  try { if (child && child.exitCode === null) child.kill(); } catch { /* already gone */ }
  process.exit(3);
}

// waitForOutput(getText, needle, label[, { timeoutMs }]): for a test that judges a server's startup output -- waits until
// the text holds needle (bounded the same way); returns the milliseconds, or exits 3 the same way.
async function waitForOutput(getText, needle, label, opts = {}) {
  const timeoutMs = opts.timeoutMs || DEFAULT_TIMEOUT_MS;
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    if (getText().includes(needle)) return Date.now() - t0;
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  console.log(`SERVER DID NOT START  ${label}: "${needle}" not said within ${timeoutMs} ms -- a setup failure, not a test result`);
  process.exit(3);
}

module.exports = { waitForServer, waitForOutput, DEFAULT_TIMEOUT_MS };
