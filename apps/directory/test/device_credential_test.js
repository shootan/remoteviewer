// Device credentials, against the real server.
//
//   node apps/directory/test/device_credential_test.js
//
// The server is apps/directory/server.js, started as a process on ports the OS picks, with a
// store, a log directory and its own output all inside .claude/test-tmp. It is stopped and
// started again where a case is about what survives that. One thing is injected: a write to
// the store that fails, through a preload that replaces nothing else.
//
// The rules with a clock in them (the sixty-second grace, the ninety days) are
// device_credentials_unit_test.js. Here the clock is the real one, so what is checked is order
// and effect: what is on disk before the client is told, what stops working when a family
// ends, and what a request that should be refused leaves behind.
//
// Not shown by this: TLS, a proxy in front, the real server, or any client.
'use strict';
const fs = require('fs'), path = require('path'), net = require('net'), dgram = require('dgram');
const { spawn, spawnSync } = require('child_process');
const assert = require('assert');

const root = path.resolve(__dirname, '../../..');
const scratchRoot = path.join(root, '.claude', 'test-tmp');
fs.mkdirSync(scratchRoot, { recursive: true });
const scratch = fs.mkdtempSync(path.join(scratchRoot, 'device-credential-'));
assert(!path.relative(root, scratch).startsWith('..'));
const serverPath = path.resolve(__dirname, '../server.js');
const data = path.join(scratch, 'store.json');
const flag = path.join(scratch, 'fail-write');
const serverLog = path.join(scratch, 'server.out.log');
const preload = path.join(scratch, 'fault.cjs');
fs.writeFileSync(preload, `const fs=require('fs'); const write=fs.writeFileSync;
fs.writeFileSync=function(p,...args){if(String(p)===process.env.REMOTE60_DIR_DATA+'.tmp'&&fs.existsSync(process.env.TEST_FAULT_FLAG))
throw Object.assign(new Error('injected disk full'),{code:'ENOSPC'});return write.call(this,p,...args)};`);

const PASSWORD = 'fixture-password-4417';
const OTHER_PASSWORD = 'fixture-password-9026';

let passed = 0, failed = 0;
function pass(label, detail) { ++passed; console.log('PASS  ' + label + (detail ? '  ' + detail : '')); }
function fail(label, detail) { ++failed; console.log('FAIL  ' + label + (detail ? '  ' + detail : '')); }
function check(label, ok, detail) { (ok ? pass : fail)(label, detail); }

// Every secret this run is handed, so that the last case can look for each of them everywhere.
const secrets = new Set([PASSWORD, OTHER_PASSWORD]);
const remember = (...values) => values.forEach((v) => { if (v) secrets.add(String(v)); });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function freePort(tcp) {
  const socket = tcp ? net.createServer() : dgram.createSocket('udp4');
  await new Promise((r) => (tcp ? socket.listen(0, '127.0.0.1', r) : socket.bind(0, '127.0.0.1', r)));
  const port = socket.address().port;
  await new Promise((r) => socket.close(r));
  return port;
}

let server, port, env;
async function api(route, body, bearer, method) {
  const response = await fetch(`http://127.0.0.1:${port}${route}`, {
    method: method || (body ? 'POST' : 'GET'),
    headers: { ...(body ? { 'content-type': 'application/json' } : {}),
               ...(bearer ? { authorization: 'Bearer ' + bearer } : {}) },
    body: body ? JSON.stringify(body) : undefined,
    signal: AbortSignal.timeout(8000),
  });
  const text = await response.text();
  let json = {};
  try { json = JSON.parse(text || '{}'); } catch { /* left empty */ }
  return { status: response.status, body: json, text };
}
async function start(serverFile = serverPath, withFault = true) {
  const out = fs.openSync(serverLog, 'a');
  const args = withFault ? ['--require', preload, serverFile] : [serverFile];
  server = spawn(process.execPath, args, { env, stdio: ['ignore', out, out] });
  fs.closeSync(out);
  let ready = false;
  for (let i = 0; i < 100 && !ready; ++i) {
    try { ready = (await api('/healthz')).status === 200; } catch { /* not yet */ }
    if (!ready) await sleep(50);
  }
  assert(ready, 'the server starts');
}
async function stop() {
  if (server && server.exitCode === null && server.signalCode === null) {
    const exited = new Promise((r) => server.once('exit', r));
    server.kill();
    await exited;
  }
}
const restart = async () => { await stop(); await start(); };

const DEVICE = { kind: 'windows-client', label: 'Fixture PC' };
async function signIn(id = 'tester', pw = PASSWORD, device = DEVICE) {
  const r = await api('/api/login', { id, pw, device });
  assert.equal(r.status, 200, 'sign-in with a device: ' + r.text);
  remember(r.body.sessionToken, r.body.deviceCredential, r.body.revokeToken);
  return r.body;
}
async function refresh(deviceId, deviceCredential) {
  const r = await api('/api/session/refresh', { deviceId, deviceCredential });
  if (r.status === 200) remember(r.body.sessionToken, r.body.deviceCredential);
  return r;
}
const works = async (session) => (await api('/api/hosts', null, session)).status === 200;
const storeText = () => fs.readFileSync(data, 'utf8');
const storeDevices = () => JSON.parse(storeText()).devices || {};

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
  env = { ...process.env, REMOTE60_DIR_DATA: data, REMOTE60_DIR_PORT: String(port),
    REMOTE60_DIR_UDP_PORT: String(udp), REMOTE60_RELAY_ENABLED: '0',
    REMOTE60_DIR_TLS_KEY: '', REMOTE60_DIR_TLS_CERT: '', REMOTE60_UPDATE_MANIFEST_URL: '',
    REMOTE60_LOG_DIR: path.join(scratch, 'logs'), TEST_FAULT_FLAG: flag };
  for (const [id, pw] of [['tester', PASSWORD], ['other', OTHER_PASSWORD]]) {
    assert.equal(spawnSync(process.execPath, [serverPath, '--add-account', id, pw],
                           { env, stdio: 'ignore' }).status, 0);
  }
  await start();

  // ---------------------------------------------------------------- 1. a client from before
  console.log('\n== a sign-in that asks for no device is answered as it always was');
  const plain = await api('/api/login', { id: 'tester', pw: PASSWORD });
  remember(plain.body.sessionToken);
  check('it is accepted', plain.status === 200);
  check('the answer has a session, its expiry, where to observe -- and nothing else',
        JSON.stringify(Object.keys(plain.body)) === '["sessionToken","expiresAt","observe"]',
        JSON.stringify(Object.keys(plain.body)));
  check('no family was started for it', Object.keys(storeDevices()).length === 0);
  check('its session works', await works(plain.body.sessionToken));

  // ---------------------------------------------------------------- 2. a sign-in with a device
  console.log('\n== a sign-in that asks for a device starts a family');
  const a = await signIn();
  check('it is given a device id, a credential, a revoke token and an expiry',
        !!a.deviceId && !!a.deviceCredential && !!a.revokeToken && a.deviceExpiresAt > Date.now());
  check('the family is on disk by the time the client hears of it',
        !!storeDevices()[a.deviceId] && storeDevices()[a.deviceId].accountId === 'tester');
  check('...as hashes: the store holds none of what the client was handed',
        ![a.deviceCredential, a.revokeToken, a.sessionToken].some((s) => storeText().includes(s)));
  const bad = await api('/api/login', { id: 'tester', pw: PASSWORD, device: { kind: 'host' } });
  check('a device of a kind the server does not know is refused, not repaired',
        bad.status === 400 && !bad.body.sessionToken, String(bad.status));
  const wrongPw = await api('/api/login', { id: 'tester', pw: 'not-the-password', device: DEVICE });
  check('a wrong password starts no family',
        wrongPw.status === 401 && Object.keys(storeDevices()).length === 1);

  // ---------------------------------------------------------------- 3. refresh
  console.log('\n== the credential exchanges for a session, and for the next credential');
  const r1 = await refresh(a.deviceId, a.deviceCredential);
  check('a refresh is accepted', r1.status === 200, r1.text.slice(0, 80));
  check('...with a new session and a new credential',
        r1.body.sessionToken && r1.body.sessionToken !== a.sessionToken &&
        r1.body.deviceCredential && r1.body.deviceCredential !== a.deviceCredential);
  check('...and no new revoke token: that one is fixed for the family', !('revokeToken' in r1.body));
  check('the new session works, and so does the one from the sign-in',
        await works(r1.body.sessionToken) && await works(a.sessionToken));
  check('the store still holds no secret',
        ![r1.body.deviceCredential, r1.body.sessionToken].some((s) => storeText().includes(s)));

  // ---------------------------------------------------------------- 4. across a restart
  console.log('\n== after a restart the sessions are gone and the credential is not');
  await restart();
  check('the old sessions are refused',
        !(await works(r1.body.sessionToken)) && !(await works(a.sessionToken)));
  const r2 = await refresh(a.deviceId, r1.body.deviceCredential);
  check('THE CREDENTIAL GETS A NEW SESSION WITHOUT A PASSWORD', r2.status === 200, String(r2.status));
  check('...which works', await works(r2.body.sessionToken));

  // ---------------------------------------------------------------- 5. an answer that was lost
  console.log('\n== an answer that never arrived: the credential before it is good once more');
  // r2 rotated r1's credential away. A client that never received r2 still holds r1's.
  await restart();  // the grace window and its count are in the store, not in memory
  const g1 = await refresh(a.deviceId, r1.body.deviceCredential);
  check('the previous credential is accepted, after a restart too', g1.status === 200,
        String(g1.status));
  check('...and r2\'s credential, which was never received, is no longer the current one',
        g1.body.deviceCredential !== r2.body.deviceCredential);
  const sessionBeforeReuse = g1.body.sessionToken;
  check('the family is still alive', await works(sessionBeforeReuse));

  console.log('\n== two answers lost in a row: the same credential a third time ends the family');
  const g2 = await refresh(a.deviceId, r1.body.deviceCredential);
  check('it is refused', g2.status === 401, String(g2.status));
  check('...with the one body every refusal has', g2.body.error === 'invalid device credential');
  const afterReuse = await refresh(a.deviceId, g1.body.deviceCredential);
  check('THE FAMILY IS ENDED: its current credential is refused as well',
        afterReuse.status === 401, String(afterReuse.status));
  check('...AND SO IS THE SESSION IT HAD ISSUED', !(await works(sessionBeforeReuse)));
  check('the store says revoked', storeDevices()[a.deviceId].revokedAt > 0);
  await restart();
  check('...and still says so after a restart',
        (await refresh(a.deviceId, g1.body.deviceCredential)).status === 401);

  // ---------------------------------------------------------------- 6. guessing
  console.log('\n== a value nobody issued is refused and ends nothing');
  const b = await signIn();
  const guess = await refresh(b.deviceId, 'f'.repeat(64));
  check('a made-up credential is refused', guess.status === 401);
  check('...with the same body as any other refusal', guess.body.error === 'invalid device credential');
  const unknownDevice = await refresh('0'.repeat(32), 'f'.repeat(64));
  check('...and so is a device that does not exist, identically',
        unknownDevice.status === 401 && unknownDevice.text === guess.text);
  check('THE FAMILY IS NOT ENDED BY IT', storeDevices()[b.deviceId].revokedAt === 0);
  let limited = 0;
  for (let i = 0; i < 8; ++i) {
    if ((await refresh(b.deviceId, 'e'.repeat(64))).status === 429) ++limited;
  }
  check('guessing is slowed down', limited > 0, 'answers that were 429: ' + limited);
  const b1 = await refresh(b.deviceId, b.deviceCredential);
  check('THE OWNER IS NOT KEPT OUT BY SOMEBODY ELSE\'S GUESSES', b1.status === 200, String(b1.status));
  check('...and the family is still not ended', storeDevices()[b.deviceId].revokedAt === 0);

  // ---------------------------------------------------------------- 7. the wrong kind of secret
  console.log('\n== a host token and a device credential are not each other');
  const host = await api('/api/host/register',
    { id: 'tester', pw: PASSWORD, hostName: 'Fixture Host', machineId: 'fixture-machine' });
  remember(host.body.hostToken);
  check('(a host registers)', host.status === 200);
  check('a host token is not a device credential',
        (await refresh(b.deviceId, host.body.hostToken)).status !== 200);
  check('a device credential is not a host token',
        (await api('/api/host/heartbeat',
                   { hostToken: b1.body.deviceCredential, observeToken: 'x' })).status === 401);
  check('a device credential is not a session', !(await works(b1.body.deviceCredential)));
  check('a revoke token is not a device credential',
        [401, 429].includes((await refresh(b.deviceId, b.revokeToken)).status));
  check('a revoke token is not a session', !(await works(b.revokeToken)));
  check('none of that ended the family', storeDevices()[b.deviceId].revokedAt === 0);

  // ---------------------------------------------------------------- 8. signing out
  console.log('\n== signing out ends the family and every session it issued');
  const c = await signIn();
  const c1 = await refresh(c.deviceId, c.deviceCredential);
  check('(two sessions from one family)', await works(c.sessionToken) && await works(c1.body.sessionToken));
  const out = await api('/api/session/logout', { deviceId: c.deviceId }, c1.body.sessionToken);
  check('it is accepted', out.status === 200);
  check('BOTH SESSIONS ARE REFUSED AFTERWARDS',
        !(await works(c.sessionToken)) && !(await works(c1.body.sessionToken)));
  check('the credential is refused', (await refresh(c.deviceId, c1.body.deviceCredential)).status === 401);
  check('the store says revoked, and why', storeDevices()[c.deviceId].revokedWhy === 'signed out');
  check('signing out again, with the revoke token this time, is still a success',
        (await api('/api/session/logout', { deviceId: c.deviceId, revokeToken: c.revokeToken })).status === 200);

  console.log('\n== a client that signed out while offline finishes later, with the revoke token');
  const d = await signIn();
  const d1 = await refresh(d.deviceId, d.deviceCredential);
  const wrongToken = await api('/api/session/logout', { deviceId: d.deviceId, revokeToken: 'a'.repeat(64) });
  check('a wrong revoke token is refused', wrongToken.status === 401);
  check('...and ends nothing', storeDevices()[d.deviceId].revokedAt === 0 && await works(d1.body.sessionToken));
  const stale = await api('/api/session/logout', { deviceId: d.deviceId, revokeToken: c.revokeToken });
  check('AN OLDER FAMILY\'S REVOKE TOKEN DOES NOT END THIS ONE',
        stale.status !== 200 && storeDevices()[d.deviceId].revokedAt === 0);
  // d1 rotated the credential; the revoke token the client was given at sign-in still works.
  const later = await api('/api/session/logout', { deviceId: d.deviceId, revokeToken: d.revokeToken });
  check('the right one ends it, with no session and after a rotation', later.status === 200);
  check('...sessions and credential alike',
        !(await works(d.sessionToken)) && !(await works(d1.body.sessionToken)) &&
        (await refresh(d.deviceId, d1.body.deviceCredential)).status === 401);
  const plainAgain = await api('/api/login', { id: 'tester', pw: PASSWORD });
  remember(plainAgain.body.sessionToken);
  const plainOut = await api('/api/session/logout', {}, plainAgain.body.sessionToken);
  check('a plain sign-in, which has no family, signs out too: its session ends',
        plainOut.status === 200 && !(await works(plainAgain.body.sessionToken)),
        String(plainOut.status));
  check('signing out with nothing at all is refused',
        (await api('/api/session/logout', {})).status === 401);

  // ---------------------------------------------------------------- 9. one device of several
  console.log('\n== ending one device leaves the others signed in');
  const e1 = await signIn('tester', PASSWORD, { kind: 'windows-client', label: 'Desk' });
  const e2 = await signIn('tester', PASSWORD, { kind: 'android', label: 'Phone' });
  const listed = await api('/api/devices', null, e2.sessionToken);
  const mine = (listed.body.devices || []);
  check('the account sees its devices',
        listed.status === 200 && mine.some((x) => x.deviceId === e1.deviceId) &&
        mine.some((x) => x.deviceId === e2.deviceId && x.current));
  check('...with no secret and no hash in the list',
        ![...secrets].some((s) => listed.text.includes(s)) && !/Hash/.test(listed.text));
  const revoked = await api('/api/devices/revoke', { deviceId: e1.deviceId }, e2.sessionToken);
  check('one of them is ended from another', revoked.status === 200);
  check('THAT ONE IS SIGNED OUT',
        !(await works(e1.sessionToken)) &&
        (await refresh(e1.deviceId, e1.deviceCredential)).status === 401);
  const e2r = await refresh(e2.deviceId, e2.deviceCredential);
  check('THE OTHER IS NOT', await works(e2.sessionToken) && e2r.status === 200);

  // ---------------------------------------------------------------- 10. somebody else's device
  console.log('\n== another account cannot see or end it, knowing its id');
  const o = await signIn('other', OTHER_PASSWORD, { kind: 'android', label: 'Theirs' });
  const theirList = await api('/api/devices', null, o.sessionToken);
  check('their list has their device and none of ours',
        theirList.body.devices.length === 1 && theirList.body.devices[0].deviceId === o.deviceId &&
        !theirList.text.includes(e2.deviceId));
  const theirRevoke = await api('/api/devices/revoke', { deviceId: e2.deviceId }, o.sessionToken);
  const noSuch = await api('/api/devices/revoke', { deviceId: '0'.repeat(32) }, o.sessionToken);
  check('ending ours is refused', theirRevoke.status === 404);
  check('...exactly as a device that does not exist is', theirRevoke.text === noSuch.text);
  const theirLogout = await api('/api/session/logout', { deviceId: e2.deviceId }, o.sessionToken);
  check('signing ours out with their session is refused', theirLogout.status === 401);
  check('OURS IS STILL SIGNED IN',
        storeDevices()[e2.deviceId].revokedAt === 0 && await works(e2r.body.sessionToken));
  check('no list without a session', (await api('/api/devices')).status === 401);

  // ---------------------------------------------------------------- 11. the store cannot be written
  console.log('\n== a store that cannot be written: nothing is half done');
  const f = await signIn();
  const f1 = await refresh(f.deviceId, f.deviceCredential);
  const before = fs.readFileSync(data);
  fs.writeFileSync(flag, '1');
  const failedSignIn = await api('/api/login', { id: 'tester', pw: PASSWORD, device: DEVICE });
  check('a sign-in with a device answers 503 and hands out nothing',
        failedSignIn.status === 503 && !failedSignIn.body.deviceCredential &&
        !failedSignIn.body.sessionToken, String(failedSignIn.status));
  const failedRefresh = await api('/api/session/refresh',
    { deviceId: f.deviceId, deviceCredential: f1.body.deviceCredential });
  check('a refresh answers 503 and hands out nothing',
        failedRefresh.status === 503 && !failedRefresh.body.deviceCredential, String(failedRefresh.status));
  const failedLogout = await api('/api/session/logout', { deviceId: f.deviceId }, f1.body.sessionToken);
  const failedRevoke = await api('/api/devices/revoke', { deviceId: f.deviceId }, f1.body.sessionToken);
  check('a sign-out and a revoke answer 503', failedLogout.status === 503 && failedRevoke.status === 503,
        failedLogout.status + ' ' + failedRevoke.status);
  check('THE STORE IS BYTE FOR BYTE WHAT IT WAS', fs.readFileSync(data).equals(before));
  check('THE SESSION THAT WAS TO BE ENDED STILL WORKS', await works(f1.body.sessionToken));
  check('a plain sign-in, which writes nothing, still works',
        (await api('/api/login', { id: 'tester', pw: PASSWORD })).status === 200);
  fs.unlinkSync(flag);
  const devicesAfterFault = (await api('/api/devices', null, f1.body.sessionToken)).body.devices;
  check('no family was left behind in memory by the failed sign-in',
        devicesAfterFault.length === Object.values(storeDevices())
          .filter((x) => x.accountId === 'tester').length);
  // If the failed refresh had rotated in memory, this credential would now be the previous one:
  // the first use below would spend its grace and the second would be a reuse.
  const f2 = await refresh(f.deviceId, f1.body.deviceCredential);
  const f3 = await refresh(f.deviceId, f1.body.deviceCredential);
  check('THE CREDENTIAL WAS NOT ROTATED BY THE FAILED REFRESH',
        f2.status === 200 && f3.status === 200, f2.status + ' then ' + f3.status);

  // ---------------------------------------------------------------- 12. a server from before
  console.log('\n== a server from before device credentials, on a store that has them');
  const oldDir = path.join(scratch, 'old-server');
  fs.mkdirSync(oldDir);
  const OLD = '6e5cd70';
  let haveOld = true;
  for (const name of ['server.js', 'update_manifest.js', 'version_compare.js', 'wake_target.js', 'package.json']) {
    const shown = spawnSync('git', ['-C', root, 'show', `${OLD}:apps/directory/${name}`], { encoding: 'buffer' });
    if (shown.status !== 0) { haveOld = false; break; }
    fs.writeFileSync(path.join(oldDir, name), shown.stdout);
  }
  if (!haveOld) {
    fail('the server at ' + OLD + ' could be read from git', 'not run: the rollback case needs it');
  } else {
    const g = await signIn();
    const devicesBefore = JSON.stringify(storeDevices());
    await stop();
    await start(path.join(oldDir, 'server.js'), false);
    check('it starts on that store', (await api('/healthz')).status === 200);
    const oldLogin = await api('/api/login', { id: 'tester', pw: PASSWORD, device: DEVICE });
    remember(oldLogin.body.sessionToken);
    check('it ignores the device in a sign-in and answers the old way',
          oldLogin.status === 200 &&
          JSON.stringify(Object.keys(oldLogin.body)) === '["sessionToken","expiresAt","observe"]',
          JSON.stringify(Object.keys(oldLogin.body)));
    check('...the same shape this server gives a sign-in with no device',
          JSON.stringify(Object.keys(oldLogin.body)) === JSON.stringify(Object.keys(plain.body)));
    check('it has no refresh route: 404', (await refresh(g.deviceId, g.deviceCredential)).status === 404);
    // Make it write the store: a host registration is saved at once.
    const oldHost = await api('/api/host/register',
      { id: 'tester', pw: PASSWORD, hostName: 'Rollback Host', machineId: 'rollback-machine' });
    remember(oldHost.body.hostToken);
    check('(it wrote the store)', oldHost.status === 200 && storeText().includes('rollback-machine'));
    check('THE DEVICES ARE STILL IN THE STORE IT WROTE, UNCHANGED',
          JSON.stringify(storeDevices()) === devicesBefore);
    await stop();
    await start();
    const back = await refresh(g.deviceId, g.deviceCredential);
    check('back on this server, the credential still gets a session',
          back.status === 200 && await works(back.body.sessionToken), String(back.status));
  }

  // ---------------------------------------------------------------- 13. what was written down
  console.log('\n== nothing that was handed out is written anywhere');
  await stop();
  const files = walk(scratch).filter((f) => !f.endsWith('fault.cjs'));
  const leaks = [];
  for (const file of files) {
    const text = fs.readFileSync(file, 'latin1');
    for (const secret of secrets) {
      if (secret.length >= 16 && text.includes(secret)) {
        leaks.push(path.relative(scratch, file) + ' contains a ' + secret.length + '-character secret');
      }
    }
  }
  const logText = fs.readFileSync(serverLog, 'utf8');
  check('the search had something to search: the server logged device events',
        /\[device\] [0-9a-f]{8} started/.test(logText) && /\[device\] [0-9a-f]{8} refreshed/.test(logText) &&
        /ended: signed out/.test(logText), files.length + ' files, ' + secrets.size + ' secrets');
  check('NO CREDENTIAL, REVOKE TOKEN, SESSION, HOST TOKEN OR PASSWORD IS IN ANY FILE',
        leaks.length === 0, leaks.slice(0, 3).join(' / '));
  check('a device is named in the log by eight characters of its id, never the whole of it',
        !Object.keys(storeDevices()).some((id) => logText.includes(id)));
  check('the log does not claim to know a theft from a lost answer',
        !/stolen|theft|attack/i.test(logText));
})().catch((error) => { fail('the run completed', error.stack || error.message); })
  .finally(async () => {
    await stop();
    console.log(`\nPrivate fixture retained at ${scratch}`);
    console.log(failed === 0
      ? `device_credential_test: ALL PASS (${passed} checks)`
      : `device_credential_test: FAIL (${failed} of ${passed + failed} failed)`);
    process.exitCode = failed === 0 ? 0 : 1;
  });
