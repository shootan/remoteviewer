// Real HTTP routes in an isolated server. Only the filesystem failure is injected; no product
// success handler is replaced. Every scratch path and cleanup remains inside this worktree.
const fs = require('fs'), path = require('path'), os = require('os');
const { spawn, spawnSync } = require('child_process');
const net = require('net'), dgram = require('dgram'), assert = require('assert');
const root = path.resolve(__dirname, '../../..');
const scratchRoot = path.join(root, '.claude', 'test-tmp');
fs.mkdirSync(scratchRoot, { recursive: true });
const scratch = fs.mkdtempSync(path.join(scratchRoot, 'server-recovery-'));
assert(!path.relative(root, scratch).startsWith('..'));
const serverPath = path.resolve(__dirname, '../server.js');
const data = path.join(scratch, 'store.json'), flag = path.join(scratch, 'fail-write');
const preload = path.join(scratch, 'fault.cjs');
fs.writeFileSync(preload, `const fs=require('fs'); const write=fs.writeFileSync;
fs.writeFileSync=function(p,...args){if(String(p)===process.env.REMOTE60_DIR_DATA+'.tmp'&&fs.existsSync(process.env.TEST_FAULT_FLAG))
throw Object.assign(new Error('injected disk full'),{code:'ENOSPC'});return write.call(this,p,...args)};`);
let server;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function freeTcpPort() {
  const socket = net.createServer();
  await new Promise(resolve => socket.listen(0, '127.0.0.1', resolve));
  const port = socket.address().port;
  await new Promise(resolve => socket.close(resolve)); return port;
}
async function freeUdpPort() {
  const socket = dgram.createSocket('udp4');
  await new Promise(resolve => socket.bind(0, '127.0.0.1', resolve));
  const port = socket.address().port; socket.close(); return port;
}
async function stop() {
  if (server && server.exitCode === null) {
    const exited = new Promise(resolve => server.once('exit', resolve));
    server.kill(); await exited;
  }
}
(async () => {
  const port = await freeTcpPort(), udp = await freeUdpPort();
  const env = { ...process.env, REMOTE60_DIR_DATA: data, REMOTE60_DIR_PORT: String(port),
    REMOTE60_DIR_UDP_PORT: String(udp), REMOTE60_RELAY_ENABLED: '0', REMOTE60_DIR_SIGNUP_KEY: 'fixture',
    REMOTE60_LOG_DIR: path.join(scratch, 'logs'), TEST_FAULT_FLAG: flag };
  assert.equal(spawnSync(process.execPath, [serverPath, '--add-account', 'tester', 'fixture-password'], { env, stdio: 'ignore' }).status, 0);
  server = spawn(process.execPath, ['--require', preload, serverPath], { env, stdio: 'ignore' });
  const api = async (route, body) => {
    const response = await fetch(`http://127.0.0.1:${port}${route}`, { method: body ? 'POST' : 'GET',
      headers: body ? { 'content-type': 'application/json' } : {}, body: body ? JSON.stringify(body) : undefined,
      signal: AbortSignal.timeout(5000) });
    return { status: response.status, body: await response.json() };
  };
  let ready = false;
  for (let i = 0; i < 100 && !ready; ++i) { try { ready = (await api('/healthz')).status === 200; } catch {} if (!ready) await sleep(50); }
  assert(ready, 'isolated server starts');
  const registration = { id: 'tester', pw: 'fixture-password', machineId: 'fixture-pc', hostName: 'Fixture' };
  const before = await api('/api/host/register', registration);
  assert.equal(before.status, 200);
  const saved = fs.readFileSync(data);
  fs.writeFileSync(flag, '1');
  const failed = await api('/api/host/register', registration);
  assert.equal(failed.status, 503);
  assert(fs.readFileSync(data).equals(saved), 'failed registration preserves durable bytes');
  assert.equal((await api('/api/host/heartbeat', { hostToken: before.body.hostToken })).status, 409,
               'old host token still valid after failed rotation');
  console.log('PASS disk failure returns503, preserves disk and previously issued token');
  const signup = await api('/api/signup', { id: 'newuser', pw: 'fixture-password', signupKey: 'fixture' });
  assert.equal(signup.status, 503);
  fs.unlinkSync(flag);
  assert.equal((await api('/api/signup', { id: 'newuser', pw: 'fixture-password', signupKey: 'fixture' })).status, 200);
  console.log('PASS failed signup is rolled back and retry succeeds after storage recovery');
  for (let i = 0; i < 4; ++i) assert.equal((await api('/api/login', { id: 'tester', pw: 'wrong' })).status, 401);
  assert.equal((await api('/api/host/register', registration)).status, 429);
  console.log('PASS login backoff also applies to host registration');
  assert.equal((await api('/healthz')).status, 200);
  console.log('PASS server health remains responsive during authentication backoff');
  await stop();
  fs.writeFileSync(data, '{corrupt');
  const corrupted = fs.readFileSync(data);
  const boot = spawnSync(process.execPath, [serverPath], { env, stdio: 'ignore', timeout: 5000 });
  assert.notEqual(boot.status, 0); assert(fs.readFileSync(data).equals(corrupted));
  console.log('PASS corrupt store refuses startup and preserves original bytes');
  console.log('recovery_fault_test: ALL PASS');
})().catch(error => { console.error('FAIL', error.message); process.exitCode = 1; })
  .finally(async () => { await stop(); fs.rmSync(scratch, { recursive: true, force: true }); });
