'use strict';
// Local source-level probe only: no listeners, network traffic, or persistent product writes.
const fs = require('fs');
const vm = require('vm');
const crypto = require('crypto');
const EventEmitter = require('events');
let kdfCalls = 0;
const wrappedCrypto = Object.create(crypto);
wrappedCrypto.scryptSync = (...args) => { kdfCalls++; return crypto.scryptSync(...args); };
const source = fs.readFileSync('apps/directory/server.js', 'utf8').split('// ---------------------------------------------------------------- boot')[0];
const output = [];
const context = {
  require: (name) => name === 'crypto' ? wrappedCrypto : name === 'os' ? { networkInterfaces: () => ({ test: [{ family: 'IPv4', internal: false, address: '192.0.2.1', netmask: '255.255.255.0' }] }) } : require(name),
  process: { env: { REMOTE60_LOG_RATE_KB_PER_MIN: '8', REMOTE60_PUBLIC_IP: '203.0.113.9', REMOTE60_RELAY_ENABLED: '1', REMOTE60_RELAY_IP: '203.0.113.9', REMOTE60_RELAY_ALLOW_ACCOUNTS: '*', REMOTE60_RELAY_ALLOW_IPS: '*' }, argv: [] }, __dirname: 'D:/remote/remote/apps/directory',
  console: { log: (...x) => output.push(x.join(' ')), error: (...x) => output.push(x.join(' ')) },
  Buffer, Date, setInterval: () => ({ unref() {} }),
  setTimeout: () => ({ unref() {} }), clearTimeout() {},
  EventEmitter, setImmediate, getKdfCalls: () => kdfCalls,
};
vm.createContext(context);
const test = `
(async () => {
  async function invoke(fn, body, headers = {}) {
    const req = new EventEmitter(); req.headers = headers; req.socket = { remoteAddress: '127.0.0.1' };
    const response = new Promise(resolve => {
      const res = { status: 0, writeHead(status) { this.status = status; }, end(text) { resolve({ status: this.status, body: JSON.parse(text) }); } };
      fn(req, res);
    });
    setImmediate(() => { req.emit('data', Buffer.from(JSON.stringify(body))); req.emit('end'); });
    return response;
  }
  const account = hashPassword('correct-password');
  store.accounts['audit-user'] = { id: 'audit-user', ...account };
  const statuses = [];
  for (let i = 0; i < 5; i++) statuses.push((await invoke(handleLogin, { id: 'audit-user', pw: 'wrong-password' })).status);
  const before = getKdfCalls();
  const register = await invoke(handleHostRegister, { id: 'audit-user', pw: 'wrong-password', machineId: 'isolated-probe' });
  console.log(JSON.stringify({ case: 'registration_bypasses_login_backoff', loginStatuses: statuses, registerStatus: register.status, registrationKdfCalls: getKdfCalls() - before }));
  const ids = ['alice', '.alice', '..alice'];
  console.log(JSON.stringify({ case: 'valid_account_log_collision', items: ids.map(id => ({ id, signupValid: /^[a-z0-9._-]{3,32}$/.test(id), directory: logSegment(id, 'unknown-account') })) }));
  console.log(JSON.stringify({ case: 'first_batch_exceeds_configured_rate', cap: LOG_RATE_BYTES_PER_MIN, firstOversizeAccepted: logBudgetAllows('isolated', LOG_RATE_BYTES_PER_MIN + 1) }));
  const now = Date.now(); observed.set('expired', { ip: '127.0.0.1', port: 9999, at: now - OBSERVE_TTL_MS - 10000 });
  store.hosts['h'] = { hostId: 'h', accountId: 'audit-user', publicIp: '127.0.0.1', publicUdpPort: 9998, lastSeen: now };
  hostTokens.set(hashToken('isolated-token'), 'h');
  const hb = await invoke(handleHostHeartbeat, { hostToken: 'isolated-token', observeToken: 'expired' });
  console.log(JSON.stringify({ case: 'expired_observation_consumed_before_sweep', status: hb.status, observedPort: hb.body.observedPort }));
  const wire = { address: '192.0.2.55', port: 34567 };
  const corrected = observedAddressFor(wire);
  observed.set('lan-client', { ...corrected, at: now });
  sessions.set('fixture-session', { accountId: 'audit-user', expiresAt: now + 60000 });
  const conn = await invoke(handleConnect, { hostId: 'h', observeToken: 'lan-client' }, { authorization: 'Bearer fixture-session' });
  const auth = relayAuthByToken.get(conn.body.punchToken);
  const bound = relayBindSession(wire, { token: conn.body.punchToken });
  console.log(JSON.stringify({ case: 'same_lan_client_relay_binding', connectStatus: conn.status, relayCandidate: conn.body.candidates.some(c => c.kind === 'relay'), authClientIp: auth.clientIp, wireIp: wire.address, eligibleByWire: relayEligibleByIp.has(wire.address), sessionBound: !!bound }));
  return 'finished';
})()
`;
Promise.resolve(vm.runInContext(source + test, context)).then(() => {
  for (const line of output) console.log(line);
}).catch(e => { console.error(e.stack); process.exitCode = 1; });
