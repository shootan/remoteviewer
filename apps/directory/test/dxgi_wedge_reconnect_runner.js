// dxgi-wedge-fix r1 (Codex B7): a DXGI wedge in the streaming child, end to end, as processes.
//
//   node apps/directory/test/dxgi_wedge_reconnect_runner.js <Release dir of a test build>
//
// What runs, and what it is:
//   * the directory: a private instance of the PRODUCTION server (apps/directory/server.js), loopback;
//   * the host: GNLinkHostUiTest.exe (the product's window and supervisor, asInvoker test build) signed in
//     through its own window (automation/gnlink_host_login_uia.ps1), supervising the streaming TEST build
//     GNLinkStreamMigrationTest.exe staged in the fixture -- the product's capture/encode/watchdog sources,
//     with test seams for the loopback directory and the wedge below. Its own UDP port, input injection off;
//   * the client: client_reconnect_restore_e2e_test.exe -- the product shell (client_shell_main.cpp, entry
//     renamed) -- which starts the REAL GNLinkViewer.exe next to it.
// The wedge: this runner creates GNLINK_STREAM_TEST_DXGI_WEDGE_FILE on the client's word; the streaming
// child's DXGI worker then stops inside its frame handler and the product watchdog ends it (exit 44).
// Everything lives under .claude/test-tmp. The installed GNLinkHost/GNLinkStream are not touched.
// Tags: display-capture, gpu, network(loopback). One viewer and one test host capture THIS desktop.
'use strict';
const fs = require('fs'), path = require('path'), net = require('net'), dgram = require('dgram');
const { spawn, spawnSync } = require('child_process');
const assert = require('assert');

const root = path.resolve(__dirname, '../../..');
const bin = path.resolve(process.argv[2] || path.join(root, 'build-local/apps/native_poc/Release'));
const scratchRoot = path.join(root, '.claude', 'test-tmp');
fs.mkdirSync(scratchRoot, { recursive: true });
const scratch = fs.mkdtempSync(path.join(scratchRoot, 'dxgi-wedge-'));
assert(!path.relative(root, scratch).startsWith('..'));
fs.writeFileSync(path.join(scratch, '.fixture'), 'owned test profile');
const profile = path.join(scratch, 'profile'); fs.mkdirSync(profile);
const hostAppData = path.join(scratch, 'host-appdata'); fs.mkdirSync(hostAppData);
const clientAppData = path.join(scratch, 'client-appdata'); fs.mkdirSync(clientAppData);
const shots = path.join(scratch, 'shots'); fs.mkdirSync(shots);
const stream = path.join(scratch, 'stream', 'GNLinkStreamMigrationTest.exe');
fs.mkdirSync(path.dirname(stream));
fs.copyFileSync(path.join(bin, 'GNLinkStreamMigrationTest.exe'), stream);
const wedgeFile = path.join(scratch, 'wedge.flag');
const serverPath = path.resolve(__dirname, '../server.js');
const uia = path.join(root, 'automation', 'gnlink_host_login_uia.ps1');
// The host lists itself under this PC's name (GetComputerNameW); the client looks for it by that name.
const ACCOUNT = 'chain-user', PASSWORD = 'fixture-password', HOSTNAME = require('os').hostname();

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function port(tcp) {
  const socket = tcp ? net.createServer() : dgram.createSocket('udp4');
  await new Promise((r) => (tcp ? socket.listen(0, '127.0.0.1', r) : socket.bind(0, '127.0.0.1', r)));
  const value = socket.address().port;
  await new Promise((r) => socket.close(r));
  return value;
}
const read = (p) => (fs.existsSync(p) ? fs.readFileSync(p, 'utf8') : '');

let server, host, client;
async function stop(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  const done = new Promise((r) => child.once('exit', r));
  child.kill();
  await done;
}

(async () => {
  // Loopback only, here and in the test host (--bind-address 127.0.0.1): nothing in this run listens on a
  // LAN address, so Windows Firewall has nothing to ask the user about.
  const httpPort = await port(true), udpPort = await port(false), streamPort = await port(false);
  const env = { ...process.env, TEMP: profile, TMP: profile,
    REMOTE60_DIR_DATA: path.join(scratch, 'store.json'), REMOTE60_DIR_PORT: String(httpPort),
    REMOTE60_DIR_UDP_PORT: String(udpPort), REMOTE60_RELAY_ENABLED: '0',
    REMOTE60_DIR_TLS_KEY: '', REMOTE60_DIR_TLS_CERT: '', REMOTE60_UPDATE_MANIFEST_URL: '',
    REMOTE60_LOG_DIR: path.join(scratch, 'logs') };
  assert.equal(spawnSync(process.execPath, [serverPath, '--add-account', ACCOUNT, PASSWORD], { env, stdio: 'ignore' }).status, 0);
  server = spawn(process.execPath, [serverPath], { env, stdio: 'ignore' });
  const url = `http://127.0.0.1:${httpPort}`;
  let ready = false;
  for (let i = 0; i < 100 && !ready; ++i) { try { ready = (await fetch(url + '/healthz')).ok; } catch {} if (!ready) await sleep(50); }
  assert(ready, 'the fixture directory is up');
  console.log('PASS the fixture directory is up ' + url);

  // ---- the host: sign in through its window; the supervisor starts the real streaming test build.
  const hostEnv = { ...env, LOCALAPPDATA: hostAppData, GNLINK_HOST_TEST_ROOT: scratch,
    GNLINK_HOST_TEST_DIRECTORY: url, GNLINK_HOST_TEST_STREAM_EXE: stream,
    GNLINK_HOST_TEST_STREAM_PORT: String(streamPort),
    GNLINK_STREAM_TEST_DIRECTORY: url,
    GNLINK_STREAM_TEST_CACHE: path.join(hostAppData, 'remote60', 'host.json'),
    GNLINK_STREAM_TEST_DXGI_WEDGE_FILE: wedgeFile };
  host = spawn(path.join(bin, 'GNLinkHostUiTest.exe'), [], { env: hostEnv, stdio: 'ignore' });
  const signin = spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', uia,
    '-ProcessId', String(host.pid), '-Expect', 'signin', '-Account', ACCOUNT, '-Password', PASSWORD,
    '-Shot', path.join(shots, '0-host-signin.png'), '-ShotAfter', path.join(shots, '0-host-signedin.png')],
    { encoding: 'utf8' });
  process.stdout.write(signin.stdout || '');
  if (signin.status === 3) throw Object.assign(new Error("another program's window covered the host window"), { invalid: true });
  assert.equal(signin.status, 0, 'the host signs in through its window');
  const hostLog = path.join(hostAppData, 'GNLink', 'host_app.log');
  let online = false;
  for (let i = 0; i < 300 && !online; ++i) { online = /directory online public=/.test(read(hostLog)); if (!online) await sleep(200); }
  assert(online, 'the streaming child came up and went online: ' + read(hostLog).slice(-1500));
  console.log('PASS the supervised streaming child is online at the fixture directory');

  // ---- the client: the product shell and the real viewer.
  const clientEnv = { ...env, LOCALAPPDATA: clientAppData, GNLINK_RECOVERY_TEST_ROOT: scratch,
    GNLINK_CHAIN_DIRECTORY: url, GNLINK_CHAIN_ACCOUNT: ACCOUNT, GNLINK_CHAIN_PASSWORD: PASSWORD,
    GNLINK_CHAIN_HOSTNAME: HOSTNAME, GNLINK_CHAIN_WEDGE_FILE: wedgeFile, GNLINK_CHAIN_SHOTS: shots };
  client = spawn(path.join(bin, 'remote60_client_reconnect_restore_e2e_test.exe'), [], { env: clientEnv, stdio: 'inherit' });
  const code = await new Promise((resolve, reject) => { client.once('error', reject); client.once('exit', resolve); });

  // ---- what the supervisor saw.
  const h = read(hostLog);
  const hasLine = (re) => re.test(h);
  console.log(hasLine(/dxgi-worker-wedge phase=frame_handler/) ? 'PASS the product watchdog named the wedge (phase=frame_handler)' : 'FAIL no dxgi-worker-wedge line in host_app.log');
  console.log(hasLine(/self-terminated \(dxgi-worker watchdog\) code=0x0000002C \(44\)/) ? 'PASS the supervisor observed exit 44' : 'FAIL the supervisor did not observe exit 44');
  console.log(hasLine(/repeated DXGI wedge: using WGC recovery backend/) && hasLine(/effective=wgc dxgiQuarantined=1 reason=wedge-this-run/)
    ? 'PASS the relaunched child runs WGC (quarantine for this run)' : 'FAIL the relaunched child is not on WGC');
  console.log(hasLine(/capture-quarantine\] (recorded|not recorded)/) ? 'PASS the supervisor tried to record the wedge: ' + (h.match(/capture-quarantine\] (recorded|not recorded)[^\n]*/) || [''])[0] : 'FAIL no record attempt logged');
  const fails = ['dxgi-worker-wedge phase=frame_handler', 'code=0x0000002C (44)', 'using WGC recovery backend'].filter((s) => !h.includes(s)).length;
  console.log('      host log: ' + hostLog);
  console.log('      client log dir: ' + path.join(clientAppData, 'GNLink'));
  console.log('      screenshots: ' + shots);
  if (code !== 0 || fails) { console.log('dxgi_wedge_reconnect_runner: FAIL'); process.exitCode = 1; return; }
  console.log('dxgi_wedge_reconnect_runner: ALL PASS');
})().catch((error) => {
  if (error && error.invalid) { console.log('dxgi_wedge_reconnect_runner: INVALID (' + error.message + ')'); process.exitCode = 3; return; }
  console.error('FAIL', error.message); console.log('dxgi_wedge_reconnect_runner: FAIL'); process.exitCode = 1;
}).finally(async () => {
  await stop(client);
  await stop(host);  // its streaming child goes with its job
  await stop(server);
  console.log('      (fixture kept for inspection: ' + scratch + ')');
});
