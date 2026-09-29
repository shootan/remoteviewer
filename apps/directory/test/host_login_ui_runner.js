// Signs in through GNLink Host's own window, against a private instance of the production
// directory, and checks where the requests went.
//
//   node apps/directory/test/host_login_ui_runner.js [path-to-GNLinkHostUiTest.exe]
//
// The window is the product's, in the asInvoker test build (apps/native_poc/src/
// host_app_ui_test.cpp says what that build replaces). It is worked from outside by
// automation/gnlink_host_login_uia.ps1 through UI Automation. The installed GNLinkHost, its
// host.json and its streaming child are not touched: everything here lives under
// .claude/test-tmp and the process is one this runner started.
//
// Three things are shown, in this order:
//   1. host.json names ANOTHER server and holds a token. The window comes up signed out, that
//      server hears nothing, and the file is left exactly as it was.
//   2. An id and a password -- nothing else -- sign in, and the streaming child is handed the
//      program's server.
//   3. Started again, the window comes up signed in without anything being typed.
//   4. host.json names a LISTED FORMER NAME of the server: the window comes up signed in, the
//      streaming child is handed the program's server, the former name hears nothing, and the
//      window itself does not rewrite the file. (Rewriting it is the streaming host's job, after
//      the server has accepted the token -- directory_migration_test shows that half.)
'use strict';
const fs = require('fs'), path = require('path'), net = require('net'), dgram = require('dgram');
const http = require('http'), crypto = require('crypto');
const { spawn, spawnSync } = require('child_process');
const assert = require('assert');

const root = path.resolve(__dirname, '../../..');
const scratchRoot = path.join(root, '.claude', 'test-tmp');
fs.mkdirSync(scratchRoot, { recursive: true });
const scratch = fs.mkdtempSync(path.join(scratchRoot, 'host-login-'));
assert(!path.relative(root, scratch).startsWith('..'));
fs.writeFileSync(path.join(scratch, '.fixture'), 'owned test profile');
const profile = path.join(scratch, 'profile'); fs.mkdirSync(profile);
const appData = path.join(scratch, 'appdata'); fs.mkdirSync(appData);
const cachePath = path.join(appData, 'remote60', 'host.json');
const launches = path.join(scratch, 'stream-launches.txt');
const serverPath = path.resolve(__dirname, '../server.js');
const uia = path.join(root, 'automation', 'gnlink_host_login_uia.ps1');
const shots = {
  signIn: path.join(root, '.claude', 'host-login-ui.signin.png'),
  signedIn: path.join(root, '.claude', 'host-login-ui.signedin.png'),
  restarted: path.join(root, '.claude', 'host-login-ui.restarted.png'),
  formerName: path.join(root, '.claude', 'host-login-ui.former-name.png'),
};
const ACCOUNT = 'host-user', PASSWORD = 'fixture-password';

let passed = 0;
const pass = (label) => { ++passed; console.log('PASS ' + label); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const sha = (file) => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
async function port(tcp) {
  const socket = tcp ? net.createServer() : dgram.createSocket('udp4');
  await new Promise((r) => (tcp ? socket.listen(0, '127.0.0.1', r) : socket.bind(0, '127.0.0.1', r)));
  const value = socket.address().port;
  await new Promise((r) => socket.close(r));
  return value;
}
const launchLines = () =>
  (fs.existsSync(launches) ? fs.readFileSync(launches, 'utf8').split('\n').filter(Boolean) : []);
const directoryOf = (line) => {
  const args = line.split('\t');
  return args[args.indexOf('--directory-url') + 1];
};

let server, host, decoy, former;
async function stopHost() {
  // A child ended by kill() has no exit code, only a signal; asking for one alone would wait
  // here for an exit that has already happened.
  if (!host || host.exitCode !== null || host.signalCode !== null) return;
  const done = new Promise((r) => host.once('exit', r));
  host.kill();  // the process this runner started; its stand-in child goes with its job
  await done;
}
function drive(args) {
  const run = spawnSync('powershell.exe',
    ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', uia, '-ProcessId', String(host.pid), ...args],
    { encoding: 'utf8' });
  process.stdout.write(run.stdout || '');
  process.stderr.write(run.stderr || '');
  passed += ((run.stdout || '').match(/^PASS /gm) || []).length;
  if (run.status === 3) {
    // Another program's window was over the button: the case could not be looked at. Not a
    // pass and not a product failure -- the whole run is INVALID, and says whose window it was.
    throw Object.assign(new Error("another program's window covered the test window"), { invalid: true });
  }
  return run.status;
}

(async () => {
  const httpPort = await port(true), udpPort = await port(false);
  const decoyHits = [];
  decoy = http.createServer((req, res) => {
    decoyHits.push(req.method + ' ' + req.url);
    res.writeHead(200, { 'content-type': 'application/json' }); res.end('{"ok":true}');
  });
  await new Promise((r) => decoy.listen(0, '127.0.0.1', r));
  const decoyUrl = `http://127.0.0.1:${decoy.address().port}`;
  // The counter is only evidence if it counts. One request of our own, seen, then forgotten.
  await fetch(decoyUrl + '/self-check');
  assert.deepEqual(decoyHits, ['GET /self-check'], 'the decoy counts what reaches it');
  decoyHits.length = 0;
  // A second address nothing may reach: the one the test build is told is a former name of
  // the server. Separate from the decoy, which is an address nobody listed.
  const formerHits = [];
  former = http.createServer((req, res) => {
    formerHits.push(req.method + ' ' + req.url);
    res.writeHead(200, { 'content-type': 'application/json' }); res.end('{"ok":true}');
  });
  await new Promise((r) => former.listen(0, '127.0.0.1', r));
  const formerUrl = `http://127.0.0.1:${former.address().port}`;
  await fetch(formerUrl + '/self-check');
  assert.deepEqual(formerHits, ['GET /self-check'], 'the former-name listener counts what reaches it');
  formerHits.length = 0;

  const env = { ...process.env, TEMP: profile, TMP: profile, LOCALAPPDATA: appData,
    GNLINK_HOST_TEST_ROOT: scratch,
    REMOTE60_DIR_DATA: path.join(scratch, 'store.json'), REMOTE60_DIR_PORT: String(httpPort),
    REMOTE60_DIR_UDP_PORT: String(udpPort), REMOTE60_RELAY_ENABLED: '0',
    REMOTE60_DIR_TLS_KEY: '', REMOTE60_DIR_TLS_CERT: '', REMOTE60_UPDATE_MANIFEST_URL: '',
    REMOTE60_LOG_DIR: path.join(scratch, 'logs') };
  assert.equal(spawnSync(process.execPath, [serverPath, '--add-account', ACCOUNT, PASSWORD],
                         { env, stdio: 'ignore' }).status, 0);
  server = spawn(process.execPath, [serverPath], { env, stdio: 'ignore' });
  const url = `http://127.0.0.1:${httpPort}`;
  let ready = false;
  for (let i = 0; i < 100 && !ready; ++i) {
    try { ready = (await fetch(url + '/healthz')).ok; } catch {}
    if (!ready) await sleep(50);
  }
  assert(ready, 'the fixture directory is up');
  env.GNLINK_HOST_TEST_DIRECTORY = url;
  env.GNLINK_HOST_TEST_FORMER_NAME = formerUrl;

  const executable = process.argv[2] ||
    path.join(root, 'build-local/apps/native_poc/Release/GNLinkHostUiTest.exe');

  // ---- 1. what an installed host has on disk: another address, and a token issued there.
  fs.mkdirSync(path.dirname(cachePath), { recursive: true });
  fs.writeFileSync(cachePath, JSON.stringify({
    directoryUrl: decoyUrl, accountId: 'stored-user', machineId: 'stored-machine',
    hostName: 'STORED-PC', hostId: 'stored-host', hostToken: 'stored-token-not-a-real-one' }, null, 2));
  const storedHash = sha(cachePath);

  host = spawn(executable, [], { env, stdio: 'ignore' });
  assert.equal(drive(['-Expect', 'signin', '-Account', ACCOUNT, '-Password', PASSWORD,
                      '-Shot', shots.signIn, '-ShotAfter', shots.signedIn]), 0,
               'signing in through the window');
  // The sign-in form was up (the script photographed it) with that file in place; by now the
  // sign-in has replaced it. What the file said while it was still the stored one is checked
  // in the second start below, where nothing signs in.

  // ---- 2. where the sign-in went, and what it left behind.
  const saved = JSON.parse(fs.readFileSync(cachePath, 'utf8'));
  assert.equal(saved.directoryUrl, url); assert.equal(saved.accountId, ACCOUNT);
  assert(saved.hostToken && saved.hostToken !== 'stored-token-not-a-real-one');
  pass('[fixed-server] host.json keeps its layout: directoryUrl is the program\'s server, with a token issued by it');
  const login = await (await fetch(url + '/api/login', { method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ id: ACCOUNT, pw: PASSWORD }) })).json();
  const listed = await (await fetch(url + '/api/hosts',
    { headers: { authorization: 'Bearer ' + login.sessionToken } })).json();
  assert.equal(listed.hosts.length, 1); assert.equal(listed.hosts[0].hostId, saved.hostId);
  pass('[fixed-server] the fixture directory lists this PC under the account that signed in');
  let waited = 0;
  while (launchLines().length < 1 && waited < 10000) { await sleep(200); waited += 200; }
  assert(launchLines().length >= 1, 'the streaming child was started');
  assert(launchLines().every((l) => directoryOf(l) === url), launchLines().join(' / '));
  pass('[fixed-server] the streaming child was handed the program\'s server as --directory-url');
  await stopHost();

  // ---- 3. started again: signed in, nothing typed.
  const launchesBefore = launchLines().length;
  host = spawn(executable, [], { env, stdio: 'ignore' });
  assert.equal(drive(['-Expect', 'signedin', '-Shot', shots.restarted]), 0, 'restart without typing');
  waited = 0;
  while (launchLines().length <= launchesBefore && waited < 10000) { await sleep(200); waited += 200; }
  assert(launchLines().length > launchesBefore && launchLines().every((l) => directoryOf(l) === url));
  pass('[fixed-server] after a restart the streaming child is started again, to the same server');
  await stopHost();

  // ---- 4. the same sign-in, as an install from before this build has it: under a former name.
  const underFormerName = { ...saved, directoryUrl: formerUrl };
  fs.writeFileSync(cachePath, JSON.stringify(underFormerName, null, 2));
  const formerHash = sha(cachePath);
  const launchesBeforeFormer = launchLines().length;
  host = spawn(executable, [], { env, stdio: 'ignore' });
  assert.equal(drive(['-Expect', 'signedin', '-Shot', shots.formerName]), 0,
               'a sign-in stored under a former name comes up signed in');
  waited = 0;
  while (launchLines().length <= launchesBeforeFormer && waited < 10000) { await sleep(200); waited += 200; }
  assert(launchLines().length > launchesBeforeFormer, 'the streaming child was started');
  assert(launchLines().slice(launchesBeforeFormer).every((l) => directoryOf(l) === url));
  pass('[migrate] stored under a former name: signed in without typing, and the child is handed the server of this build');
  await sleep(3000);
  assert.equal(sha(cachePath), formerHash);
  pass('[migrate] the window does not rewrite host.json before the server has accepted the token');
  await stopHost();
  assert.equal(formerHits.length, 0, formerHits.join(', '));
  pass('[migrate] the former name received no request');

  // ---- 1 again, on its own: the stored file names another server and nothing signs in.
  fs.writeFileSync(cachePath, JSON.stringify({
    directoryUrl: decoyUrl, accountId: 'stored-user', machineId: 'stored-machine',
    hostName: 'STORED-PC', hostId: 'stored-host', hostToken: 'stored-token-not-a-real-one' }, null, 2));
  const launchesAtSeed = launchLines().length;
  host = spawn(executable, [], { env, stdio: 'ignore' });
  assert.equal(drive(['-Expect', 'signin', '-Shot', path.join(scratch, 'stored-other-server.png')]), 0,
               'a stored token from another server leaves the window signed out');
  await sleep(3000);  // long enough for an uploader or a child to have made its first request
  assert.equal(sha(cachePath), storedHash);
  pass('[fixed-server] a host.json naming another server is left byte-for-byte as it was found');
  assert.equal(launchLines().length, launchesAtSeed);
  pass('[fixed-server] ...and no streaming child is started on its token');
  await stopHost();

  console.log('      (requests the decoy received: ' + decoyHits.length +
              (decoyHits.length ? ' -- ' + decoyHits.join(', ') : '') + ')');
  assert.equal(decoyHits.length, 0);
  assert.equal(formerHits.length, 0, formerHits.join(', '));
  pass('[fixed-server] the address in host.json received no request, with its token or without');
  console.log('PASS screenshots ' + Object.values(shots).join(' , '));
  console.log(`host_login_ui_runner: ALL PASS (${passed} checks; product window, asInvoker test build, fixture directory)`);
})().catch((error) => {
  if (error && error.invalid) {
    console.log(`host_login_ui_runner: INVALID (${error.message}; its identity is printed above)`);
    process.exitCode = 3;
    return;
  }
  console.error('FAIL', error.message); console.log('host_login_ui_runner: FAIL'); process.exitCode = 1;
})
  .finally(async () => {
    await stopHost();
    if (server && server.exitCode === null && server.signalCode === null) { const done = new Promise((r) => server.once('exit', r)); server.kill(); await done; }
    for (const listener of [decoy, former]) {
      if (listener) { listener.closeAllConnections?.(); await new Promise((r) => listener.close(r)); }
    }
    console.log('Private fixture retained at ' + scratch);
  });
