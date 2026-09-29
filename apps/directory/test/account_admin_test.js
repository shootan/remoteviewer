// Account states and the Account Admin API v1, against the real server.
//
//   node apps/directory/test/account_admin_test.js
//
// apps/directory/server.js is started as a process on ports the OS picks, with its store, logs
// and output inside .claude/test-tmp. Several servers are started in turn: with no admin
// settings, with only one of the two, and with both. One thing is injected: a store write that
// fails, through a preload that replaces nothing else.
//
// Not shown by this: TLS, the real server, a real main server, a live relay session being cut
// (the relay is off here; closeHostsNow is exercised but has nothing to close), or any client.
'use strict';
const fs = require('fs'), path = require('path'), net = require('net'), dgram = require('dgram'),
      os = require('os');
const { spawn, spawnSync } = require('child_process');
const assert = require('assert');

const root = path.resolve(__dirname, '../../..');
const scratchRoot = path.join(root, '.claude', 'test-tmp');
fs.mkdirSync(scratchRoot, { recursive: true });
const scratch = fs.mkdtempSync(path.join(scratchRoot, 'account-admin-'));
assert(!path.relative(root, scratch).startsWith('..'));
const serverPath = path.resolve(__dirname, '../server.js');
const data = path.join(scratch, 'store.json');
const flag = path.join(scratch, 'fail-write');
const serverLog = path.join(scratch, 'server.out.log');
const preload = path.join(scratch, 'fault.cjs');
fs.writeFileSync(preload, `const fs=require('fs'); const write=fs.writeFileSync;
fs.writeFileSync=function(p,...args){if(String(p)===process.env.REMOTE60_DIR_DATA+'.tmp'&&fs.existsSync(process.env.TEST_FAULT_FLAG))
throw Object.assign(new Error('injected disk full'),{code:'ENOSPC'});return write.call(this,p,...args)};`);

const ADMIN_KEY = 'fixture-admin-key-5b1e0c7d93a2';
const PW = { old: 'fixture-old-pass-3391', a: 'fixture-pass-a-7720', b: 'fixture-pass-b-1185',
             changed: 'fixture-changed-6604' };
const PENDING_TEXT = '승인 대기 중입니다. 관리자 승인 후 사용할 수 있습니다.';
const DISABLED_TEXT = '사용이 정지된 계정입니다.';

let passed = 0, failed = 0;
function check(label, ok, detail) {
  if (ok) ++passed; else ++failed;
  console.log((ok ? 'PASS  ' : 'FAIL  ') + label + (detail ? '  ' + detail : ''));
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function freePort(tcp) {
  const socket = tcp ? net.createServer() : dgram.createSocket('udp4');
  await new Promise((r) => (tcp ? socket.listen(0, '127.0.0.1', r) : socket.bind(0, '127.0.0.1', r)));
  const port = socket.address().port;
  await new Promise((r) => socket.close(r));
  return port;
}
/** Whether anything accepts a TCP connection there. */
function listening(host, port) {
  return new Promise((resolve) => {
    const s = net.connect({ host, port });
    const done = (v) => { s.destroy(); resolve(v); };
    s.once('connect', () => done(true));
    s.once('error', () => done(false));
    s.setTimeout(1500, () => done(false));
  });
}

let server, port, udp, adminPort, baseEnv;
async function call(base, route, { body, bearer, method, headers, raw } = {}) {
  const response = await fetch(base + route, {
    method: method || (body !== undefined || raw !== undefined ? 'POST' : 'GET'),
    headers: { ...(body !== undefined || raw !== undefined ? { 'content-type': 'application/json' } : {}),
               ...(bearer ? { authorization: 'Bearer ' + bearer } : {}), ...(headers || {}) },
    body: raw !== undefined ? raw : (body !== undefined ? JSON.stringify(body) : undefined),
    signal: AbortSignal.timeout(8000),
  });
  const text = await response.text();
  let json = {};
  try { json = JSON.parse(text || '{}'); } catch { /* left empty */ }
  return { status: response.status, body: json, text };
}
const api = (route, body, bearer, extra = {}) =>
  call(`http://127.0.0.1:${port}`, route, { body, bearer, ...extra });
const admin = (route, extra = {}) =>
  call(`http://127.0.0.1:${adminPort}`, route, { bearer: ADMIN_KEY, ...extra });

async function start(extraEnv) {
  const out = fs.openSync(serverLog, 'a');
  server = spawn(process.execPath, ['--require', preload, serverPath],
                 { env: { ...baseEnv, ...extraEnv }, stdio: ['ignore', out, out] });
  fs.closeSync(out);
  let ready = false;
  for (let i = 0; i < 100 && !ready; ++i) {
    try { ready = (await api('/healthz')).status === 200; } catch { /* not yet */ }
    if (!ready) await sleep(50);
  }
  assert(ready, 'the server starts');
  await sleep(150);   // the admin listener binds in the same tick; give it a moment either way
}
async function stop() {
  if (server && server.exitCode === null && server.signalCode === null) {
    const exited = new Promise((r) => server.once('exit', r));
    server.kill();
    await exited;
  }
}
const storeJson = () => JSON.parse(fs.readFileSync(data, 'utf8'));
const withAdmin = () => ({ REMOTE60_DIR_ADMIN_PORT: String(adminPort), REMOTE60_DIR_ADMIN_KEY: ADMIN_KEY });

async function registerHost(id, pw, machineId) {
  return api('/api/host/register', { id, pw, hostName: 'Fixture PC', machineId });
}
const hostWorks = async (hostToken) =>
  (await api('/api/logs', undefined, undefined, { raw: 'line\n', headers: { 'x-host-token': hostToken } }));
const sessionWorks = async (session) => (await api('/api/hosts', undefined, session)).status === 200;
const DEVICE = { kind: 'windows-client', label: 'Fixture PC' };

(async () => {
  port = await freePort(true);
  udp = await freePort(false);
  adminPort = await freePort(true);
  baseEnv = { ...process.env, REMOTE60_DIR_DATA: data, REMOTE60_DIR_PORT: String(port),
    REMOTE60_DIR_UDP_PORT: String(udp), REMOTE60_RELAY_ENABLED: '0', REMOTE60_DIR_SIGNUP_KEY: 'fixture-signup',
    REMOTE60_DIR_TLS_KEY: '', REMOTE60_DIR_TLS_CERT: '', REMOTE60_DIR_ADMIN_PORT: '',
    REMOTE60_DIR_ADMIN_KEY: '', REMOTE60_DIR_ADMIN_HOST: '', REMOTE60_DIR_ADMIN_MAX_PENDING: '',
    REMOTE60_LOG_DIR: path.join(scratch, 'logs'), REMOTE60_UPDATE_DIR: path.join(scratch, 'updates'),
    REMOTE60_UPDATE_PUBLIC_KEY: '', TEST_FAULT_FLAG: flag };

  // ---------------------------------------------------------------- 1. a store from before
  console.log('\n== a store written before account states: every account is active, nothing breaks');
  assert.equal(spawnSync(process.execPath, [serverPath, '--add-account', 'veteran', PW.old],
                         { env: baseEnv, stdio: 'ignore' }).status, 0);
  const cli = storeJson();
  check('--add-account makes an active account', cli.accounts.veteran.status === 'active');
  // Strip it back to the shape the old server wrote.
  const legacy = { accounts: { veteran: { id: 'veteran', salt: cli.accounts.veteran.salt,
                                          hash: cli.accounts.veteran.hash, createdAt: 1700000000000 } },
                   hosts: {} };
  fs.writeFileSync(data, JSON.stringify(legacy, null, 2));
  await start({});
  let r = await api('/api/login', { id: 'veteran', pw: PW.old });
  check('an account with no status signs in', r.status === 200 && !!r.body.sessionToken, String(r.status));
  const veteranHost = await registerHost('veteran', PW.old, 'machine-veteran');
  check('...and registers a PC', veteranHost.status === 200 && !!veteranHost.body.hostToken);
  await stop();
  // A host token from before, too: the store is rewritten with the token hash only, no status.
  const withHost = storeJson();
  for (const a of Object.values(withHost.accounts)) {
    delete a.status; delete a.updatedAt; delete a.approvedAt; delete a.memo; delete a.lastLoginAt;
  }
  fs.writeFileSync(data, JSON.stringify(withHost, null, 2));
  await start({});
  r = await hostWorks(veteranHost.body.hostToken);
  check('a host token from before still works after the update', r.status === 200, String(r.status));

  // ---------------------------------------------------------------- 2. no listener unless both are set
  console.log('\n== the admin listener');
  check('no port and no key: nothing listens', !(await listening('127.0.0.1', adminPort)));
  r = await api('/admin/v1/health', undefined, ADMIN_KEY);
  check('the service port has no /admin path', r.status === 404, String(r.status));
  r = await api('/admin/v1/accounts', { id: 'sneaky', pw: 'fixture-pass-x' }, ADMIN_KEY);
  check('...for writes either', r.status === 404 && !storeJson().accounts.sneaky, String(r.status));
  await stop();
  await start({ REMOTE60_DIR_ADMIN_PORT: String(adminPort) });
  check('a port without a key: nothing listens', !(await listening('127.0.0.1', adminPort)));
  await stop();
  await start({ REMOTE60_DIR_ADMIN_KEY: ADMIN_KEY });
  check('a key without a port: nothing listens', !(await listening('127.0.0.1', adminPort)));
  await stop();
  await start({ ...withAdmin(), REMOTE60_DIR_ADMIN_PORT: String(port) });
  check('the service port as the admin port: refused, and the service port has no /admin',
        (await api('/admin/v1/health', undefined, ADMIN_KEY)).status === 404);
  await stop();

  await start({ ...withAdmin(), REMOTE60_DIR_ADMIN_MAX_PENDING: '4' });
  check('with both: it listens', await listening('127.0.0.1', adminPort));
  const lan = Object.values(os.networkInterfaces()).flat()
    .find((a) => a && a.family === 'IPv4' && !a.internal);
  if (lan) {
    check(`...on 127.0.0.1 only: not on ${lan.address}`, !(await listening(lan.address, adminPort)));
  } else {
    console.log('SKIP  the bind address: this machine has no non-loopback IPv4 to try');
  }
  r = await admin('/admin/v1/health', { bearer: '' });
  check('no key: 401 unauthorized', r.status === 401 && r.body.code === 'unauthorized', r.text);
  r = await admin('/admin/v1/health', { bearer: ADMIN_KEY + 'x' });
  check('a wrong key: 401 unauthorized', r.status === 401 && r.body.code === 'unauthorized', r.text);
  r = await admin('/admin/v1/accounts/veteran/disable', { bearer: 'nope', method: 'POST' });
  check('...and a wrong key changes nothing', r.status === 401 &&
        storeJson().accounts.veteran.status !== 'disabled');
  r = await admin('/admin/v1/nope', { bearer: 'nope' });
  check('...nor does it tell which paths exist', r.status === 401);
  r = await admin('/admin/v1/health');
  check('health', r.status === 200 && r.body.ok === true && r.body.service === 'gnlink' &&
        typeof r.body.version === 'string' && r.body.counts &&
        r.body.counts.active === 1 && r.body.counts.pending === 0 && r.body.counts.disabled === 0, r.text);
  r = await admin('/admin/v1/accounts');
  const vet = (r.body.accounts || []).find((a) => a.id === 'veteran');
  check('the old account is listed as active, with the contract fields',
        !!vet && vet.status === 'active' && vet.createdAt === 1700000000000 &&
        'updatedAt' in vet && vet.approvedAt === null && vet.memo === '' && 'lastLoginAt' in vet &&
        vet.hostCount === 1, JSON.stringify(vet));

  // ---------------------------------------------------------------- 3. create
  console.log('\n== creating an account: always pending');
  r = await admin('/admin/v1/accounts', { body: { id: 'alice', pw: PW.a, memo: 'from\u0007 the\nmain server' } });
  check('201 with the account, pending', r.status === 201 && r.body.ok === true &&
        r.body.account.id === 'alice' && r.body.account.status === 'pending', r.text);
  check('the memo lost its control characters', r.body.account && r.body.account.memo === 'from the' + 'main server',
        JSON.stringify(r.body.account && r.body.account.memo));
  check('no hash, no salt in the answer', !/salt|hash/i.test(r.text));
  r = await admin('/admin/v1/accounts', { body: { id: 'alice-long', pw: PW.a, memo: 'x'.repeat(500) } });
  check('a memo is cut to 200', r.status === 201 && r.body.account.memo.length === 200);
  r = await admin('/admin/v1/accounts', { body: { id: 'alice', pw: PW.b } });
  check('the same id again: 409 taken', r.status === 409 && r.body.code === 'taken', r.text);
  for (const id of ['AB', 'Upper', 'a'.repeat(33), 'sp ace', '', 'x/y']) {
    r = await admin('/admin/v1/accounts', { body: { id, pw: PW.a } });
    check(`id ${JSON.stringify(id).slice(0, 12)}: 400 id`, r.status === 400 && r.body.code === 'id', r.text);
  }
  for (const pw of ['short', 'x'.repeat(129), 12345678, undefined]) {
    r = await admin('/admin/v1/accounts', { body: { id: 'pwcheck', pw } });
    check(`pw of ${pw === undefined ? 'none' : String(pw).length}: 400 pw`, r.status === 400 && r.body.code === 'pw', r.text);
  }
  r = await admin('/admin/v1/accounts', { body: { id: 'edge-pw', pw: 'x'.repeat(128) } });
  check('a pw of exactly 128 is accepted', r.status === 201, r.text);
  r = await admin('/admin/v1/accounts', { raw: '{not json' });
  check('a body that is not JSON: 400 bad_request', r.status === 400 && r.body.code === 'bad_request', r.text);
  r = await admin('/admin/v1/accounts', { raw: JSON.stringify({ id: 'big', pw: PW.a, memo: 'x'.repeat(17000) }) });
  check('a body over 16KB: 400 bad_request', r.status === 400 && r.body.code === 'bad_request' &&
        !storeJson().accounts.big, r.text);
  r = await admin('/admin/v1/accounts', { body: { id: 'one-too-many', pw: PW.a } });
  check('a fourth pending account under a limit of 4: accepted', r.status === 201, r.text);
  r = await admin('/admin/v1/accounts', { body: { id: 'five-too-many', pw: PW.a } });
  check('past the pending limit: 429 too_many_pending', r.status === 429 && r.body.code === 'too_many_pending', r.text);
  r = await admin('/admin/v1/accounts?status=pending');
  check('?status=pending lists only those', r.status === 200 &&
        r.body.accounts.every((a) => a.status === 'pending') && r.body.accounts.length === 4, r.text.slice(0, 200));
  r = await admin('/admin/v1/accounts?status=gone');
  check('an unknown status filter: 400', r.status === 400);
  r = await admin('/admin/v1/accounts/nobody/approve', { method: 'POST' });
  check('an account that does not exist: 404 not_found', r.status === 404 && r.body.code === 'not_found', r.text);

  // ---------------------------------------------------------------- 4. pending
  console.log('\n== a pending account: the right password gets the sentence, nothing else');
  r = await api('/api/login', { id: 'alice', pw: PW.a });
  check('login: 403 pending with the contract sentence', r.status === 403 && r.body.code === 'pending' &&
        r.body.error === PENDING_TEXT && !r.body.sessionToken, r.text);
  r = await api('/api/login', { id: 'alice', pw: PW.a, device: DEVICE });
  check('...and no device family is started', r.status === 403 &&
        !Object.values(storeJson().devices || {}).some((d) => d.accountId === 'alice'));
  r = await registerHost('alice', PW.a, 'machine-alice');
  check('host registration: 403 pending', r.status === 403 && r.body.code === 'pending' && !r.body.hostToken, r.text);
  check('...and no host was stored', !Object.values(storeJson().hosts).some((h) => h.accountId === 'alice'));
  const wrongPending = await api('/api/login', { id: 'alice', pw: 'not-her-password' });
  const noSuch = await api('/api/login', { id: 'nobody-here', pw: 'not-her-password' });
  check('a wrong password: 401, the same answer as an account that does not exist',
        wrongPending.status === 401 && wrongPending.text === noSuch.text, `${wrongPending.text} vs ${noSuch.text}`);
  // 403 is not a failed attempt. Six of them, then wrong passwords: the first three wrong ones
  // are answered at once, as for an account with no history.
  for (let i = 0; i < 6; ++i) await api('/api/login', { id: 'alice', pw: PW.a });
  const afterRight = [];
  for (let i = 0; i < 3; ++i) afterRight.push((await api('/api/login', { id: 'alice', pw: 'wrong-' + i })).status);
  check('403 is not counted as a failed attempt', afterRight.every((s) => s === 401), afterRight.join(','));
  await api('/api/login', { id: 'alice', pw: 'wrong-3' });
  r = await api('/api/login', { id: 'alice', pw: 'wrong-4' });
  check('...while wrong passwords still are (the fifth is held back)', r.status === 429, String(r.status));
  await sleep(1100);

  // ---------------------------------------------------------------- 5. approve
  console.log('\n== approve');
  const beforeApprove = Date.now();
  r = await admin('/admin/v1/accounts/alice/approve', { method: 'POST' });
  check('pending -> active, approvedAt set', r.status === 200 && r.body.account.status === 'active' &&
        r.body.account.approvedAt >= beforeApprove, r.text);
  r = await admin('/admin/v1/accounts/alice/approve', { method: 'POST' });
  check('approving again: 200, the same account', r.status === 200 && r.body.account.status === 'active');
  r = await admin('/admin/v1/accounts/alice/enable', { method: 'POST' });
  check('enabling an active account: 409 state', r.status === 409 && r.body.code === 'state', r.text);
  r = await api('/api/login', { id: 'alice', pw: PW.a, device: DEVICE });
  check('the approved account signs in', r.status === 200 && !!r.body.sessionToken, r.text.slice(0, 80));
  const aliceSession = r.body.sessionToken;
  const aliceDevice = { deviceId: r.body.deviceId, deviceCredential: r.body.deviceCredential };
  r = await admin('/admin/v1/accounts?status=active');
  const aliceRow = r.body.accounts.find((a) => a.id === 'alice');
  check('lastLoginAt is recorded', aliceRow && aliceRow.lastLoginAt >= beforeApprove, JSON.stringify(aliceRow));
  const aliceHost = await registerHost('alice', PW.a, 'machine-alice');
  check('...and registers a PC', aliceHost.status === 200 && !!aliceHost.body.hostToken);
  check('both work', await sessionWorks(aliceSession) && (await hostWorks(aliceHost.body.hostToken)).status === 200);
  r = await api('/api/update/manifest?platform=windows', undefined, undefined,
                { headers: { 'x-host-token': aliceHost.body.hostToken } });
  check('...the update manifest too (404: nothing published here, past the token check)', r.status === 404, r.text);

  // ---------------------------------------------------------------- 6. disable
  console.log('\n== disable: everything stops at once, nothing is thrown away');
  const devicesBefore = JSON.stringify(storeJson().devices[aliceDevice.deviceId]);
  r = await admin('/admin/v1/accounts/alice/disable', { method: 'POST' });
  check('-> disabled', r.status === 200 && r.body.account.status === 'disabled', r.text);
  check('the session it had: 401', !(await sessionWorks(aliceSession)));
  r = await hostWorks(aliceHost.body.hostToken);
  check('its host token: 401', r.status === 401 && r.body.code === 'account_inactive', r.text);
  r = await api('/api/host/heartbeat', { hostToken: aliceHost.body.hostToken, observeToken: 'x' });
  check('...on heartbeat too (401, so the host shows it must sign in again)', r.status === 401, r.text);
  r = await api('/api/update/manifest?platform=windows', undefined, undefined,
                { headers: { 'x-host-token': aliceHost.body.hostToken } });
  check('...and on the update manifest', r.status === 401, `${r.status} ${r.text}`);
  r = await api('/api/session/refresh', aliceDevice);
  check('its device credential: 401, not rotated', r.status === 401 && r.body.code === 'account_inactive' &&
        JSON.stringify(storeJson().devices[aliceDevice.deviceId]) === devicesBefore, r.text);
  r = await api('/api/login', { id: 'alice', pw: PW.a });
  check('the right password: 403 disabled with the contract sentence',
        r.status === 403 && r.body.code === 'disabled' && r.body.error === DISABLED_TEXT, r.text);
  r = await registerHost('alice', PW.a, 'machine-alice-2');
  check('registering a PC: 403 disabled', r.status === 403 && r.body.code === 'disabled');
  r = await api('/api/login', { id: 'alice', pw: 'wrong-again' });
  check('a wrong password: still 401', r.status === 401);
  check('the host and the device family are still in the store',
        !!Object.values(storeJson().hosts).find((h) => h.accountId === 'alice' && h.tokenHash) &&
        !storeJson().devices[aliceDevice.deviceId].revokedAt);
  r = await api('/api/login', { id: 'veteran', pw: PW.old });
  const vetSession = r.body.sessionToken;
  r = await api('/api/hosts', undefined, vetSession);
  check('another account does not see the disabled one\'s PC', r.status === 200 &&
        !(r.body.hosts || []).some((h) => h.hostId === aliceHost.body.hostId));
  r = await api('/api/connect', { hostId: aliceHost.body.hostId, observeToken: 'x' }, vetSession);
  check('...nor can it be a connect target', r.status === 404, String(r.status));
  r = await admin('/admin/v1/accounts/alice/approve', { method: 'POST' });
  check('approving a disabled account: 409 state', r.status === 409 && r.body.code === 'state');

  // ---------------------------------------------------------------- 7. enable
  console.log('\n== enable: back without registering again');
  r = await admin('/admin/v1/accounts/alice/enable', { method: 'POST' });
  check('-> active', r.status === 200 && r.body.account.status === 'active');
  r = await hostWorks(aliceHost.body.hostToken);
  check('the host token works again, not re-registered', r.status === 200, r.text);
  r = await api('/api/session/refresh', aliceDevice);
  check('the device credential works again', r.status === 200 && !!r.body.sessionToken, r.text.slice(0, 80));
  aliceDevice.deviceCredential = r.body.deviceCredential;
  check('the session the disable ended stays ended', !(await sessionWorks(aliceSession)));

  // ---------------------------------------------------------------- 8. password
  console.log('\n== password: every standing sign-in ends');
  r = await api('/api/login', { id: 'alice', pw: PW.a });
  const beforePwSession = r.body.sessionToken;
  r = await admin('/admin/v1/accounts/alice/password', { body: { pw: 'short' } });
  check('a password against the rule: 400 pw, nothing changed', r.status === 400 && r.body.code === 'pw' &&
        (await api('/api/login', { id: 'alice', pw: PW.a })).status === 200);
  r = await admin('/admin/v1/accounts/alice/password', { raw: 'nope' });
  check('a body that is not JSON: 400 bad_request', r.status === 400 && r.body.code === 'bad_request');
  r = await admin('/admin/v1/accounts/alice/password', { body: { pw: PW.changed } });
  check('changed', r.status === 200 && r.body.account.status === 'active' && !/salt|hash/i.test(r.text), r.text);
  check('the old password: 401', (await api('/api/login', { id: 'alice', pw: PW.a })).status === 401);
  check('the new one: 200', (await api('/api/login', { id: 'alice', pw: PW.changed })).status === 200);
  check('the session from before: 401', !(await sessionWorks(beforePwSession)));
  r = await hostWorks(aliceHost.body.hostToken);
  check('the host token from before: 401', r.status === 401 && r.body.code === 'unknown_host_token', r.text);
  r = await api('/api/session/refresh', aliceDevice);
  check('the device credential from before: 401, and the family is ended',
        r.status === 401 && !!storeJson().devices[aliceDevice.deviceId].revokedAt, r.text);
  r = await registerHost('alice', PW.changed, 'machine-alice');
  check('the PC signs in again with the new password, as the same host',
        r.status === 200 && r.body.hostId === aliceHost.body.hostId);
  const aliceHost2 = r.body;

  // ---------------------------------------------------------------- 9. delete
  console.log('\n== delete: the account and what belongs to it');
  r = await api('/api/login', { id: 'alice', pw: PW.changed, device: DEVICE });
  const lastDevice = { deviceId: r.body.deviceId, deviceCredential: r.body.deviceCredential };
  const lastSession = r.body.sessionToken;
  r = await admin('/admin/v1/accounts/alice', { method: 'DELETE' });
  check('200', r.status === 200 && r.body.ok === true, r.text);
  const after = storeJson();
  check('no account, no host, no device family left for it in the store',
        !after.accounts.alice && !Object.values(after.hosts).some((h) => h.accountId === 'alice') &&
        !Object.values(after.devices).some((d) => d.accountId === 'alice'));
  check('its session: 401', !(await sessionWorks(lastSession)));
  check('its host token: 401', (await hostWorks(aliceHost2.hostToken)).status === 401);
  check('its device credential: 401', (await api('/api/session/refresh', lastDevice)).status === 401);
  check('its password: 401, like any id that does not exist',
        (await api('/api/login', { id: 'alice', pw: PW.changed })).status === 401);
  r = await admin('/admin/v1/accounts/alice', { method: 'DELETE' });
  check('deleting it again: 404 not_found', r.status === 404 && r.body.code === 'not_found');
  check('the other account is untouched', await sessionWorks(vetSession) &&
        (await hostWorks(veteranHost.body.hostToken)).status === 200);

  // ---------------------------------------------------------------- 10. a write that fails
  console.log('\n== a store write that fails changes nothing');
  fs.writeFileSync(flag, '1');
  r = await admin('/admin/v1/accounts/veteran/disable', { method: 'POST' });
  check('disable: 503', r.status === 503, r.text);
  check('...and the account is still usable', await sessionWorks(vetSession) &&
        (await admin('/admin/v1/accounts?status=active')).body.accounts.some((a) => a.id === 'veteran'));
  r = await admin('/admin/v1/accounts', { body: { id: 'not-kept', pw: PW.a } });
  check('create: 503, not made', r.status === 503 &&
        !(await admin('/admin/v1/accounts')).body.accounts.some((a) => a.id === 'not-kept'), r.text);
  fs.unlinkSync(flag);

  // ---------------------------------------------------------------- 11. signup and restart
  console.log('\n== signup makes a pending account; states survive a restart');
  r = await api('/api/signup', { id: 'selfmade', pw: PW.b, signupKey: 'fixture-signup' });
  check('signup: 200', r.status === 200, r.text);
  r = await api('/api/login', { id: 'selfmade', pw: PW.b });
  check('...and the account waits', r.status === 403 && r.body.code === 'pending');
  await admin('/admin/v1/accounts/veteran/disable', { method: 'POST' });
  await stop();
  await start(withAdmin());
  r = await admin('/admin/v1/accounts');
  const states = Object.fromEntries(r.body.accounts.map((a) => [a.id, a.status]));
  check('after a restart: disabled is disabled, pending is pending',
        states.veteran === 'disabled' && states.selfmade === 'pending', JSON.stringify(states));
  r = await admin('/admin/v1/accounts/veteran/enable', { method: 'POST' });
  check('...and enable brings the host back with its old token',
        r.status === 200 && (await hostWorks(veteranHost.body.hostToken)).status === 200);

  // ---------------------------------------------------------------- 12. what was written down
  console.log('\n== the log says what was done, and nothing secret');
  await stop();
  const log = fs.readFileSync(serverLog, 'utf8');
  for (const action of ['create', 'approve', 'disable', 'enable', 'password', 'delete']) {
    check(`the ${action} is in the log`, new RegExp(`\\[admin\\] \\S+ action=${action} `).test(log));
  }
  const leaked = [ADMIN_KEY, ...Object.values(PW)].filter((s) => log.includes(s));
  check('no password and not the key', leaked.length === 0, leaked.map((s) => s.slice(0, 6)).join(','));
  check('the store holds no password', !Object.values(PW).some((s) => fs.readFileSync(data, 'utf8').includes(s)));

  console.log(`\naccount_admin_test: ${failed ? 'FAILED' : 'ALL PASS'} (${passed} passed, ${failed} failed)`);
  process.exitCode = failed ? 1 : 0;
})().catch(async (error) => {
  console.error('FAIL  ' + (error && error.stack || error));
  process.exitCode = 1;
}).finally(async () => {
  await stop();
});
