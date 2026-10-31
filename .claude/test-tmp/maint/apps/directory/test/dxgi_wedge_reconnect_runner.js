// dxgi-wedge-fix r1 (Codex B7): a DXGI wedge in the streaming child, end to end, as processes.
//
//   node apps/directory/test/dxgi_wedge_reconnect_runner.js <Release dir of a test build> [scenario]
//
// scenario (r3, Codex E2/E3; see client_reconnect_restore_e2e_test.cpp): desktop (default) | monitor (the test
// host lists a second screen: GNLINK_STREAM_TEST_MONITORS_FILE=extra) | presentfail | presentok (a topmost
// fixture window B, and the viewer is GNLinkViewerR3Test -- presents failed while a file exists).
// GNLINK_R3_VIEWER=<exe name> runs another test viewer instead (the restore mutants: they must FAIL).
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
const scenario = process.argv[3] || 'desktop';
const B_TITLE = '!GNLINK-CHAIN-B';  // the host lists windows by title: '!' sorts first
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

let server, host, client, fixtureWindow, sisService;
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
  if (scenario === 'monitor' || scenario === 'nomonitors') {
    const monitorsFile = path.join(scratch, 'monitors.txt');
    fs.writeFileSync(monitorsFile, scenario === 'monitor' ? 'extra' : 'nomonitors');
    hostEnv.GNLINK_STREAM_TEST_MONITORS_FILE = monitorsFile;
  }
  if (scenario === 'lockscreen') {
    // integ-0162 r1b (LK + DW): the host's own session stands in as locked (test seam, as in the LK e2e
    // --lock-screen), the service's stand-in (the product service code, lifecycle test build) answers the
    // lock-cred probe on its unlock pipe, and the host also lists a second screen -- so the SHIPPED viewer
    // sees LockCredV1 and the screen list on one connection, then again after DW's reconnect.
    const tag = process.pid + '-' + Date.now();
    const suffix = 'integ-lock-' + tag, unlockEvent = 'GNLinkSisTestUnlock-' + tag;
    const sisEnv = Object.fromEntries(Object.entries(process.env).filter(([k]) => !/^(REMOTE60_SIS_TEST_|GNLINK_SIS_TEST_)/i.test(k)));
    Object.assign(sisEnv, { REMOTE60_SIS_TEST_SUFFIX: suffix, REMOTE60_SIS_TEST_DIAG: path.join(scratch, 'sis_diag.log'),
      REMOTE60_SIS_TEST_STOP_EVENT: 'GNLinkSisTestStop-' + tag, REMOTE60_SIS_TEST_FAKE_LOCK: '1',
      REMOTE60_SIS_TEST_UNLOCK_EVENT: unlockEvent, GNLINK_SIS_TEST_CRED_TARGET_TITLE: 'GNLINK-INTEG-NO-SUCH-TARGET' });
    const pipe = '\\\\.\\pipe\\GNLinkSisTest-' + suffix + '-unlock';
    if (process.env.GNLINK_R1B_NO_SERVICE === '1') {
      // Negative control: no service stand-in, so the host never hears it can type at the lock screen and
      // never advertises LockCredV1 -- the "Windows 로그인" checks must FAIL.
      console.log('      (negative control: no service stand-in)');
    } else {
      sisService = spawn(path.join(bin, 'GNLinkInputServiceLifecycleTest.exe'), ['--lifecycle-test'],
        { env: sisEnv, cwd: scratch, stdio: 'ignore', windowsHide: true });
      let up = false;
      for (let i = 0; i < 300 && !up; ++i) { up = fs.existsSync(pipe); if (!up) await sleep(100); }  // existsSync is false while the one instance is busy: keep looking
      assert(up, 'the service stand-in serves its unlock pipe ' + pipe);
      console.log('PASS the service stand-in serves its unlock pipe');
    }
    hostEnv.GNLINK_STREAM_TEST_FAKE_LOCK_UNTIL_EVENT = unlockEvent;
    hostEnv.GNLINK_STREAM_TEST_UNLOCK_PIPE = pipe;
    const monitorsFile = path.join(scratch, 'monitors.txt');
    fs.writeFileSync(monitorsFile, 'extra');
    hostEnv.GNLINK_STREAM_TEST_MONITORS_FILE = monitorsFile;
  }
  if (scenario === 'presentfail' || scenario === 'presentok') {
    // A window of our own, topmost and never activated, for the picker's first window card. The client
    // checks it is listed first before it clicks anything.
    const ps = "Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing;" +
      "Add-Type -ReferencedAssemblies System.Windows.Forms -TypeDefinition 'public class F : System.Windows.Forms.Form {" +
      " protected override bool ShowWithoutActivation { get { return true; } }" +
      " protected override System.Windows.Forms.CreateParams CreateParams { get { var c = base.CreateParams; c.ExStyle |= 0x08000008; return c; } } }';" +
      "$f = New-Object F; $f.Text = '" + B_TITLE + "'; $f.StartPosition = 'Manual'; $f.Location = New-Object System.Drawing.Point(80, 80);" +
      "$f.ClientSize = New-Object System.Drawing.Size(640, 400); $f.BackColor = [System.Drawing.Color]::DarkOrange; $f.TopMost = $true;" +
      "$t = New-Object System.Windows.Forms.Timer; $t.Interval = 600000; $t.Add_Tick({ $f.Close() }); $t.Start();" +
      "[System.Windows.Forms.Application]::Run($f)";
    fixtureWindow = spawn('powershell.exe', ['-NoProfile', '-Command', ps], { stdio: 'ignore' });
    await sleep(3000);
  }
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
    GNLINK_CHAIN_HOSTNAME: HOSTNAME, GNLINK_CHAIN_WEDGE_FILE: wedgeFile, GNLINK_CHAIN_SHOTS: shots,
    GNLINK_CHAIN_SCENARIO: scenario };
  if (scenario === 'monitor' && process.env.GNLINK_R3_VIEWER) {
    // A restore mutant (it must FAIL by restoring the desktop); the shipped viewer otherwise.
    clientEnv.GNLINK_CHAIN_VIEWER_EXE = path.join(bin, process.env.GNLINK_R3_VIEWER);
  }
  if (scenario === 'presentfail' || scenario === 'presentok') {
    clientEnv.GNLINK_CHAIN_VIEWER_EXE = path.join(bin, process.env.GNLINK_R3_VIEWER || 'GNLinkViewerR3Test.exe');
    clientEnv.GNLINK_VIEWER_TEST_PRESENT_FAIL_FILE = path.join(scratch, 'present-fail.flag');
    clientEnv.GNLINK_CHAIN_B_TITLE = B_TITLE;
  }
  client = spawn(path.join(bin, 'remote60_client_reconnect_restore_e2e_test.exe'), [], { env: clientEnv, stdio: 'inherit' });
  const code = await new Promise((resolve, reject) => { client.once('error', reject); client.once('exit', resolve); });

  // ---- what the supervisor saw.
  const h = read(hostLog);
  const hasLine = (re) => re.test(h);
  const wedged = !(scenario === 'presentfail' || scenario === 'presentok' || scenario === 'nomonitors');
  if (!wedged) console.log('      (no wedge in this scenario; wedge lines not expected)');
  if (wedged) console.log(hasLine(/dxgi-worker-wedge phase=frame_handler/) ? 'PASS the product watchdog named the wedge (phase=frame_handler)' : 'FAIL no dxgi-worker-wedge line in host_app.log');
  if (wedged) console.log(hasLine(/self-terminated \(dxgi-worker watchdog\) code=0x0000002C \(44\)/) ? 'PASS the supervisor observed exit 44' : 'FAIL the supervisor did not observe exit 44');
  if (wedged) console.log(hasLine(/repeated DXGI wedge: using WGC recovery backend/) && hasLine(/effective=wgc dxgiQuarantined=1 reason=wedge-this-run/)
    ? 'PASS the relaunched child runs WGC (quarantine for this run)' : 'FAIL the relaunched child is not on WGC');
  if (wedged) console.log(hasLine(/capture-quarantine\] (recorded|not recorded)/) ? 'PASS the supervisor tried to record the wedge: ' + (h.match(/capture-quarantine\] (recorded|not recorded)[^\n]*/) || [''])[0] : 'FAIL no record attempt logged');
  // presentfail ends the session through the viewer's own present failure (43), not a wedge.
  const fails = !wedged ? 0 :
    ['dxgi-worker-wedge phase=frame_handler', 'code=0x0000002C (44)', 'using WGC recovery backend'].filter((s) => !h.includes(s)).length;
  if (scenario === 'lockscreen') {
    // The streaming child's own log lines (wherever under the fixture they went): it advertised LockCredV1.
    const all = [];
    const walk = (d) => { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name);
      if (e.isDirectory()) walk(p); else if (/\.log(\.\d+)?$/.test(e.name)) { try { all.push(read(p)); } catch {} } } };  // a live log may be locked
    walk(scratch);
    const text = all.join('\n');
    const adv = (text.match(/\[lock-cred\] advertised=1/g) || []).length;
    console.log(adv >= 2 ? 'PASS the host advertised LockCredV1 to both connections (' + adv + ' times: before and after the wedge)'
                         : 'FAIL the host advertised LockCredV1 ' + adv + ' time(s), expected one per streaming child (2)');
    if (adv < 2) process.exitCode = 1;
  }
  console.log('      host log: ' + hostLog);
  console.log('      client log dir: ' + path.join(clientAppData, 'GNLink'));
  console.log('      screenshots: ' + shots);
  if (code === 3) { console.log('dxgi_wedge_reconnect_runner: INVALID (see the client lines above)'); process.exitCode = 3; return; }
  if (code !== 0 || fails) { console.log('dxgi_wedge_reconnect_runner: FAIL'); process.exitCode = 1; return; }
  console.log('dxgi_wedge_reconnect_runner: ALL PASS');
})().catch((error) => {
  if (error && error.invalid) { console.log('dxgi_wedge_reconnect_runner: INVALID (' + error.message + ')'); process.exitCode = 3; return; }
  console.error('FAIL', error.message); console.log('dxgi_wedge_reconnect_runner: FAIL'); process.exitCode = 1;
}).finally(async () => {
  await stop(client);
  await stop(fixtureWindow);
  await stop(sisService);
  await stop(host);  // its streaming child goes with its job
  await stop(server);
  console.log('      (fixture kept for inspection: ' + scratch + ')');
});
