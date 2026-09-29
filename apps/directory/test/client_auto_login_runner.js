// Staying signed in, in the client's own window, against the real directory.
//
//   node apps/directory/test/client_auto_login_runner.js [path-to-client_auto_login_ui_test.exe]
//
// Each step below is ONE START of the client (client_auto_login_ui_test, the product's shell in
// its test build). Between the steps this runner restarts the directory, stops it, holds one of
// its answers, and reads the store it wrote. Everything lives under .claude/test-tmp; the
// installed client, its client.txt and its sign-in are not touched.
//
// What is judged: which screen the window reaches without anything being typed, what is on
// disk afterwards, and what the directory recorded about the device. And, at the end, that
// nothing the directory handed out is written in any log.
//
// Not shown by this: GNLinkClient.exe itself, TLS, the real server, twelve hours passing (a
// restart of the directory is what makes a session stop being one here), a physical keyboard.
'use strict';
const fs = require('fs'), path = require('path'), net = require('net'), dgram = require('dgram');
const { spawn, spawnSync } = require('child_process');
const assert = require('assert');

const root = path.resolve(__dirname, '../../..');
const scratchRoot = path.join(root, '.claude', 'test-tmp');
fs.mkdirSync(scratchRoot, { recursive: true });
const scratch = fs.mkdtempSync(path.join(scratchRoot, 'client-auto-login-'));
assert(!path.relative(root, scratch).startsWith('..'));
const profile = path.join(scratch, 'profile'); fs.mkdirSync(profile);
const appData = path.join(scratch, 'appdata'); fs.mkdirSync(appData);
fs.writeFileSync(path.join(scratch, '.fixture'), 'owned test profile');
const shellData = path.join(scratch, 'shelldata');
const serverPath = path.resolve(__dirname, '../server.js');
const data = path.join(scratch, 'store.json');
const serverLog = path.join(scratch, 'server.out.log');
// Kept OUT of what is searched at the end: this is where the test writes down what the
// directory handed out, so that it knows what to search for.
const captured = path.join(scratchRoot, path.basename(scratch) + '.handed-out.txt');
const holdLogin = path.join(scratch, 'hold-login');
const preload = path.join(scratch, 'observe.cjs');
fs.writeFileSync(preload, `
const http=require('http'),fs=require('fs');const create=http.createServer;
const SECRET=/"(sessionToken|deviceCredential|revokeToken)":"([^"]+)"/g;
http.createServer=function(listener){return create.call(http,(req,res)=>{
  const end=res.end;
  res.end=function(body,...rest){
    try{const text=body?String(body):'';let m;while((m=SECRET.exec(text)))fs.appendFileSync(process.env.TEST_HANDED_OUT,m[2]+'\\n');}catch{}
    return end.call(this,body,...rest);};
  if(req.url==='/api/login'&&fs.existsSync(process.env.TEST_HOLD_LOGIN)&&
     !fs.existsSync(process.env.TEST_HOLD_LOGIN+'.seen')){
    // The FIRST sign-in to arrive while the flag is up is held until the runner removes the
    // flag; the ones after it go straight through. The held request is answered by the
    // product's own handler, unchanged, only later.
    fs.writeFileSync(process.env.TEST_HOLD_LOGIN+'.seen','1');
    const wait=()=>fs.existsSync(process.env.TEST_HOLD_LOGIN)?setTimeout(wait,50):listener(req,res);
    wait();
  } else listener(req,res);
});};`);

const ACCOUNT = 'tester', PASSWORD = 'fixture-password-4417';
const shot = (name) => path.join(root, '.claude', `client-auto-login.${name}.png`);

let passed = 0, failed = 0;
const pass = (label, detail) => { ++passed; console.log('PASS ' + label + (detail ? '  ' + detail : '')); };
const fail = (label, detail) => { ++failed; console.log('FAIL ' + label + (detail ? '  ' + detail : '')); };
const check = (label, ok, detail) => (ok ? pass : fail)(label, detail);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function freePort(tcp) {
  const socket = tcp ? net.createServer() : dgram.createSocket('udp4');
  await new Promise((r) => (tcp ? socket.listen(0, '127.0.0.1', r) : socket.bind(0, '127.0.0.1', r)));
  const port = socket.address().port;
  await new Promise((r) => socket.close(r));
  return port;
}

let server, port, env, url;
const clients = new Set();
async function startDirectory() {
  const out = fs.openSync(serverLog, 'a');
  server = spawn(process.execPath, ['--require', preload, serverPath], { env, stdio: ['ignore', out, out] });
  fs.closeSync(out);
  let ready = false;
  for (let i = 0; i < 100 && !ready; ++i) {
    try { ready = (await fetch(url + '/healthz')).ok; } catch { /* not yet */ }
    if (!ready) await sleep(50);
  }
  assert(ready, 'the directory starts');
}
async function stopDirectory() {
  if (server && server.exitCode === null && server.signalCode === null) {
    const exited = new Promise((r) => server.once('exit', r));
    server.kill();
    await exited;
  }
}
function startClient(executable, mode, name, argument) {
  const child = spawn(executable, [url, mode, shot(name), argument || ''], { env, stdio: ['ignore', 'pipe', 'pipe'] });
  clients.add(child);
  let output = '';
  child.stdout.on('data', (c) => { output += c; });
  child.stderr.on('data', (c) => { output += c; });
  const done = new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('exit', (code) => {
      clients.delete(child);
      process.stdout.write(output.replace(/^/gm, '    '));
      passed += (output.match(/^PASS /gm) || []).length;
      failed += (output.match(/^FAIL /gm) || []).length;
      resolve(code);
    });
  });
  return { child, done };
}
const runClient = async (executable, mode, name, argument) =>
  (await startClient(executable, mode, name, argument).done);

const devices = () => Object.values((JSON.parse(fs.readFileSync(data, 'utf8')).devices) || {});
const live = () => devices().filter((d) => !d.revokedAt);
const flag = (name) => path.join(scratch, name);
async function waitForFile(file, ms) {
  for (let waited = 0; waited < ms; waited += 100) {
    if (fs.existsSync(file)) return true;
    await sleep(100);
  }
  return fs.existsSync(file);
}
function clearFlags() {
  for (const name of ['client.ready', 'directory.restarted']) fs.rmSync(flag(name), { force: true });
}
function walk(dir) {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walk(full));
    else out.push(full);
  }
  return out;
}

(async () => {
  port = await freePort(true);
  const udp = await freePort(false);
  url = `http://127.0.0.1:${port}`;
  env = { ...process.env, TEMP: profile, TMP: profile, LOCALAPPDATA: appData,
    GNLINK_RECOVERY_TEST_ROOT: scratch, TEST_HANDED_OUT: captured, TEST_HOLD_LOGIN: holdLogin,
    REMOTE60_DIR_DATA: data, REMOTE60_DIR_PORT: String(port), REMOTE60_DIR_UDP_PORT: String(udp),
    REMOTE60_RELAY_ENABLED: '0', REMOTE60_DIR_TLS_KEY: '', REMOTE60_DIR_TLS_CERT: '',
    REMOTE60_UPDATE_MANIFEST_URL: '', REMOTE60_LOG_DIR: path.join(scratch, 'logs') };
  assert.equal(spawnSync(process.execPath, [serverPath, '--add-account', ACCOUNT, PASSWORD],
                         { env, stdio: 'ignore' }).status, 0);
  await startDirectory();
  const executable = process.argv[2] ||
    path.join(root, 'build-local/apps/native_poc/Release/remote60_client_auto_login_ui_test.exe');

  // ------------------------------------------------------------ 1. the first start
  console.log('\n== 1. first start: nothing stored, the sign-in form; id + password signs in');
  check('the client signs in', await runClient(executable, 'sign-in', '1-signed-in', ACCOUNT) === 0);
  check('the directory has one device for the account, a windows client',
        live().length === 1 && live()[0].accountId === ACCOUNT && live()[0].kind === 'windows-client',
        JSON.stringify(live().map((d) => d.kind)));
  const firstDevice = live()[0].deviceId;
  const credFile = path.join(shellData, 'login.cred');
  const handedOut = () => fs.readFileSync(captured, 'utf8').split('\n').filter((s) => s.length >= 16);
  check('what is on disk is not readable as what the directory handed out',
        fs.existsSync(credFile) &&
        !handedOut().some((s) => fs.readFileSync(credFile, 'latin1').includes(s)));
  check('the password is not in it either',
        !fs.readFileSync(credFile, 'latin1').includes(PASSWORD));

  // ------------------------------------------------------------ 2. opened again
  console.log('\n== 2. closed and opened again: the PC list, nothing typed');
  check('the client comes back signed in', await runClient(executable, 'expect-list', '2-came-back', ACCOUNT) === 0);
  check('it is the same device, still alive', live().length === 1 && live()[0].deviceId === firstDevice);

  // ------------------------------------------------------------ 3. the directory restarted
  console.log('\n== 3. the directory was restarted in between: every session is gone');
  await stopDirectory();
  await startDirectory();
  check('the client comes back signed in', await runClient(executable, 'expect-list', '3-after-restart', ACCOUNT) === 0);
  check('the same device, still alive', live().length === 1 && live()[0].deviceId === firstDevice);

  // ------------------------------------------------------------ 4. restarted under an open client
  console.log('\n== 4. the directory is restarted while the client is open');
  clearFlags();
  {
    const open = startClient(executable, 'stay-open', '4-stayed-open', ACCOUNT);
    assert(await waitForFile(flag('client.ready'), 60000), 'the client reached the list');
    await stopDirectory();
    await startDirectory();
    fs.writeFileSync(flag('directory.restarted'), '1');
    check('the client replaces its session and keeps the list', (await open.done) === 0);
  }
  check('the same device, still alive', live().length === 1 && live()[0].deviceId === firstDevice);

  // ------------------------------------------------------------ 5. two windows at once
  console.log('\n== 5. two windows started together, one stored sign-in between them');
  {
    const a = startClient(executable, 'expect-list', '5-window-a', ACCOUNT);
    const b = startClient(executable, 'expect-list', '5-window-b', ACCOUNT);
    const codes = await Promise.all([a.done, b.done]);
    check('both come back signed in', codes[0] === 0 && codes[1] === 0, codes.join(','));
  }
  check('THE DEVICE WAS NOT ENDED BY THE TWO OF THEM',
        live().length === 1 && live()[0].deviceId === firstDevice,
        'live devices: ' + live().length);
  check('...and the next start still comes back signed in',
        await runClient(executable, 'expect-list', '5-after-both', ACCOUNT) === 0);

  // ------------------------------------------------------------ 6. the directory is down
  console.log('\n== 6. the directory is down when the client starts');
  clearFlags();
  await stopDirectory();
  {
    const open = startClient(executable, 'retry', '6-retried', ACCOUNT);
    assert(await waitForFile(flag('client.ready'), 120000), 'the client offered a retry');
    await startDirectory();
    fs.writeFileSync(flag('directory.restarted'), '1');
    check('the form stayed usable, a retry was offered, and it signed in', (await open.done) === 0);
  }
  check('the device is still alive', live().length === 1 && live()[0].deviceId === firstDevice);

  // ------------------------------------------------------------ 7. ended from somewhere else
  console.log('\n== 7. the device is ended from another of the account\'s sessions');
  {
    const login = await (await fetch(url + '/api/login', { method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ id: ACCOUNT, pw: PASSWORD }) })).json();
    const ended = await fetch(url + '/api/devices/revoke', { method: 'POST',
      headers: { 'content-type': 'application/json', authorization: 'Bearer ' + login.sessionToken },
      body: JSON.stringify({ deviceId: firstDevice }) });
    assert.equal(ended.status, 200);
  }
  check('the client is sent to the sign-in form and asked to sign in again',
        await runClient(executable, 'expect-form', '7-ended-elsewhere', 'sign-in-again') === 0);
  check('the refused credential is no longer on disk', !fs.existsSync(credFile));
  check('the start after that is simply the sign-in form',
        await runClient(executable, 'expect-form', '7-form-again', '') === 0);

  // ------------------------------------------------------------ 8. signing out
  console.log('\n== 8. signing in again, then signing out');
  check('the client signs in', await runClient(executable, 'sign-in', '8-signed-in', ACCOUNT) === 0);
  check('it is a NEW device: a sign-in made on purpose starts a family of its own',
        live().length === 1 && live()[0].deviceId !== firstDevice);
  const secondDevice = live()[0].deviceId;
  check('the client signs out', await runClient(executable, 'sign-out', '8-signed-out', ACCOUNT) === 0);
  const afterSignOut = devices().find((d) => d.deviceId === secondDevice);
  check('THE DIRECTORY HAS THE DEVICE AS ENDED, BY A SIGN-OUT',
        !!afterSignOut && afterSignOut.revokedAt > 0 && afterSignOut.revokedWhy === 'signed out',
        afterSignOut && afterSignOut.revokedWhy);
  check('nothing is left on disk: no credential, no sign-out owed',
        !fs.existsSync(credFile) && !fs.existsSync(credFile + '.revoke'));
  check('THE NEXT START IS THE SIGN-IN FORM',
        await runClient(executable, 'expect-form', '8-form-after-sign-out', '') === 0);

  // ------------------------------------------------------------ 9. signing out while offline
  console.log('\n== 9. signing out with the directory down');
  check('the client signs in', await runClient(executable, 'sign-in', '9-signed-in', ACCOUNT) === 0);
  const thirdDevice = live()[0].deviceId;
  clearFlags();
  {
    // The window is open and signed in; the directory goes away; sign out is pressed.
    const open = startClient(executable, 'sign-out-offline', '9-signed-out-offline', ACCOUNT);
    assert(await waitForFile(flag('client.ready'), 60000), 'the client reached the list');
    await stopDirectory();
    fs.writeFileSync(flag('directory.restarted'), '1');   // "the directory has changed": it is down
    check('the client signs out although the directory cannot be told', (await open.done) === 0);
  }
  check('the credential is gone and the sign-out is written down as owed',
        !fs.existsSync(credFile) && fs.existsSync(credFile + '.revoke'));
  check('what is written down is not readable as what the directory handed out',
        !handedOut().some((s) => fs.readFileSync(credFile + '.revoke', 'latin1').includes(s)));
  check('(the directory, which was down, still has the device as alive)',
        live().some((d) => d.deviceId === thirdDevice));
  await startDirectory();
  check('THE NEXT START IS THE SIGN-IN FORM, not the list',
        await runClient(executable, 'expect-form', '9-form-after', '') === 0);
  check('AND THAT START TOLD THE DIRECTORY: the device is ended there now',
        devices().find((d) => d.deviceId === thirdDevice).revokedAt > 0);
  check('nothing is owed any more', !fs.existsSync(credFile + '.revoke'));

  // ------------------------------------------------------------ 10. an answer that came late
  console.log('\n== 10. a sign-in is in flight in one window while another signs in and out');
  // Nothing is stored at this point, so both windows come up on the form. The first presses
  // sign in and the directory holds its answer; the second signs in, and signs out; then the
  // first one's answer is let go. It is an answer to a question from before the sign-out.
  check('(nothing is stored, no device is alive)', !fs.existsSync(credFile) && live().length === 0);
  const devicesBefore = devices().length;
  clearFlags();
  fs.rmSync(holdLogin + '.seen', { force: true });
  fs.writeFileSync(holdLogin, '1');
  {
    const slow = startClient(executable, 'slow-sign-in', '10-late-answer', ACCOUNT);
    assert(await waitForFile(flag('client.ready'), 60000), 'the slow window pressed sign in');
    assert(await waitForFile(holdLogin + '.seen', 20000), 'the directory is holding its answer');
    check('the other window signs in and signs out meanwhile',
          await runClient(executable, 'sign-in-then-out', '10-other-window', ACCOUNT) === 0);
    fs.rmSync(holdLogin, { force: true });   // the held answer goes out now
    check('the slow window was not signed in by it', (await slow.done) === 0);
  }
  check('nothing is stored', !fs.existsSync(credFile) && !fs.existsSync(credFile + '.revoke'));
  check('the directory issued two devices meanwhile: one to each window',
        devices().length === devicesBefore + 2, String(devices().length - devicesBefore));
  check('BOTH ARE ENDED, the one the late answer carried included',
        live().length === 0, 'live devices: ' + live().length + ' of ' + devices().length);
  check('the next start is the sign-in form',
        await runClient(executable, 'expect-form', '10-form-after', '') === 0);

  // ------------------------------------------------------------ 11. what was written down
  console.log('\n== 11. nothing the directory handed out is written in any log');
  await stopDirectory();
  const secrets = [...new Set(handedOut())];
  const files = walk(scratch).filter((f) =>
    !f.endsWith('observe.cjs') && !f.startsWith(profile) /* the browser's own profile */);
  const leaks = [];
  for (const file of files) {
    const text = fs.readFileSync(file, 'latin1');
    const wide = Buffer.from(text, 'latin1').toString('utf16le');
    for (const secret of [...secrets, PASSWORD]) {
      if (text.includes(secret) || wide.includes(secret)) {
        leaks.push(path.relative(scratch, file) + ' contains a ' + secret.length + '-character secret');
      }
    }
  }
  const clientLog = path.join(shellData, 'client.log');
  const clientText = fs.existsSync(clientLog) ? fs.readFileSync(clientLog, 'utf8') : '';
  check('the search had something to search for and somewhere to search',
        secrets.length >= 20 && /sign-in store: device [0-9a-f]{8}/.test(clientText) &&
        /\[device\] [0-9a-f]{8} refreshed/.test(fs.readFileSync(serverLog, 'utf8')),
        secrets.length + ' secrets, ' + files.length + ' files');
  check('NO SESSION, CREDENTIAL, REVOKE TOKEN OR PASSWORD IS IN THE CLIENT LOG, THE SERVER LOG, ' +
        'THE UPLOADED LOGS OR THE STORE', leaks.length === 0, leaks.slice(0, 3).join(' / '));
  check('a device is never written out in full',
        !devices().some((d) => clientText.includes(d.deviceId)));

  console.log('PASS screenshots ' + shot('*'));
})().catch((error) => { fail('the run completed', error.stack || error.message); })
  .finally(async () => {
    for (const child of clients) {
      if (child.exitCode === null && child.signalCode === null) {
        const exited = new Promise((r) => child.once('exit', r));
        child.kill();
        await exited;
      }
    }
    await stopDirectory();
    fs.rmSync(captured, { force: true });
    console.log(`Private fixture retained at ${scratch}`);
    console.log(failed === 0
      ? `client_auto_login_runner: ALL PASS (${passed} checks; product shell in its test build, real directory)`
      : `client_auto_login_runner: FAIL (${failed} of ${passed + failed} failed)`);
    process.exitCode = failed === 0 ? 0 : 1;
  });
