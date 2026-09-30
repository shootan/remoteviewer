// The rules of a device credential, with the clock in the test's hands.
//
// Nothing here opens a socket or writes a file. device_credentials.js takes its time and its
// randomness as arguments, so "sixty seconds later" is a number and not a wait -- which is the
// only way the grace window and the ninety-day lifetime get tested at all.
//
// What the server does around these rules (persist first, then answer; end sessions; refuse the
// same way every time) is device_credential_test.js, against a real server.
'use strict';
const assert = require('assert');
const dc = require('../device_credentials');

let passed = 0, failed = 0;
function check(label, fn) {
  try { fn(); ++passed; console.log('PASS  ' + label); }
  catch (error) { ++failed; console.log('FAIL  ' + label + '  ' + error.message); }
}

const T0 = 1_800_000_000_000;
const MINUTE = 60 * 1000, DAY = 24 * 60 * MINUTE;
const request = { kind: 'windows-client', label: 'Fixture PC' };

function family(devices = {}, account = 'tester', now = T0) {
  const made = dc.createFamily(devices, account, request, now);
  return { devices, made, record: devices[made.deviceId] };
}

// ------------------------------------------------------------------ what is kept
check('a family keeps hashes and none of the three secrets', () => {
  const { made, record } = family();
  const stored = JSON.stringify(record);
  assert(!stored.includes(made.credential));
  assert(!stored.includes(made.revokeToken));
  assert.equal(record.credHash, dc.hashToken(made.credential));
  assert.equal(record.revokeHash, dc.hashToken(made.revokeToken));
  assert.notEqual(made.credential, made.revokeToken);
});

check('every sign-in starts a family of its own', () => {
  const devices = {};
  const a = family(devices).made, b = family(devices).made;
  assert.notEqual(a.deviceId, b.deviceId);
  assert.notEqual(a.credential, b.credential);
  assert.notEqual(a.revokeToken, b.revokeToken);
  assert.equal(Object.keys(devices).length, 2);
});

// ------------------------------------------------------------------ rotation and grace
check('the current credential is current, and rotating replaces it', () => {
  const { made, record } = family();
  assert.equal(dc.classify(record, made.credential, T0 + 1), 'current');
  const next = dc.rotate(record, 'current', T0 + 1);
  assert.notEqual(next.credential, made.credential);
  assert.equal(dc.classify(record, next.credential, T0 + 2), 'current');
});

check('the rotated-away credential is accepted once more, for a minute', () => {
  const { made, record } = family();
  dc.rotate(record, 'current', T0);
  assert.equal(dc.classify(record, made.credential, T0 + 59 * 1000), 'grace');
  assert.equal(dc.classify(record, made.credential, T0 + dc.LIMITS.GRACE_MS), 'grace');
});

check('...and a second later it is a reuse', () => {
  const { made, record } = family();
  dc.rotate(record, 'current', T0);
  assert.equal(dc.classify(record, made.credential, T0 + dc.LIMITS.GRACE_MS + 1), 'reused');
});

check('a grace use is spent by being used', () => {
  const { made, record } = family();
  const lost = dc.rotate(record, 'current', T0);           // the answer that never arrived
  assert.equal(dc.classify(record, made.credential, T0 + 1000), 'grace');
  const second = dc.rotate(record, 'grace', T0 + 1000);
  assert.equal(dc.classify(record, second.credential, T0 + 2000), 'current');
  // Two answers lost in a row: the client still holds the first credential, and it is done.
  assert.equal(dc.classify(record, made.credential, T0 + 2000), 'reused');
  // The credential the lost answer carried was never received. If it turns up, it is a reuse.
  assert.equal(dc.classify(record, lost.credential, T0 + 2000), 'reused');
});

check('the grace window and its use count are in the record, not in memory', () => {
  const { made, record } = family();
  dc.rotate(record, 'current', T0);
  const restarted = JSON.parse(JSON.stringify(record));     // what a restart reads back
  assert.equal(restarted.prevUsesLeft, 1);
  assert.equal(restarted.prevValidUntil, T0 + dc.LIMITS.GRACE_MS);
  assert.equal(dc.classify(restarted, made.credential, T0 + 1000), 'grace');
  dc.rotate(restarted, 'grace', T0 + 1000);
  const again = JSON.parse(JSON.stringify(restarted));
  assert.equal(dc.classify(again, made.credential, T0 + 2000), 'reused');
});

// ------------------------------------------------------------------ reuse against guessing
check('a credential the family was issued long ago is a reuse', () => {
  const { made, record } = family();
  let last = made.credential;
  for (let i = 0; i < 5; ++i) last = dc.rotate(record, 'current', T0 + i * DAY).credential;
  assert.equal(dc.classify(record, made.credential, T0 + 6 * DAY), 'reused');
  assert.equal(dc.classify(record, last, T0 + 6 * DAY), 'current');
});

check('A VALUE NOBODY ISSUED IS UNKNOWN, NOT A REUSE', () => {
  const { record } = family();
  assert.equal(dc.classify(record, 'not-a-credential', T0), 'unknown');
  assert.equal(dc.classify(record, dc.randomToken(32), T0), 'unknown');
  assert.equal(dc.classify(record, '', T0), 'unknown');
});

check('another family\'s credential is unknown here', () => {
  const devices = {};
  const a = family(devices), b = family(devices);
  assert.equal(dc.classify(a.record, b.made.credential, T0), 'unknown');
});

check('the revoke token is not a credential', () => {
  const { made, record } = family();
  assert.equal(dc.classify(record, made.revokeToken, T0), 'unknown');
  assert(!dc.revokeTokenMatches(record, made.credential));
  assert(dc.revokeTokenMatches(record, made.revokeToken));
});

check('the list of issued hashes is bounded', () => {
  const { record } = family();
  for (let i = 0; i < 100; ++i) dc.rotate(record, 'current', T0 + i);
  assert(record.issued.length <= dc.LIMITS.ISSUED_KEPT);
});

check('no device at all is unknown', () => {
  assert.equal(dc.classify(null, 'anything', T0), 'unknown');
  assert.equal(dc.classify(undefined, 'anything', T0), 'unknown');
});

// ------------------------------------------------------------------ lifetime
check('ninety days, sliding: a use pushes the end out', () => {
  const { made, record } = family();
  assert.equal(record.expiresAt, T0 + 90 * DAY);
  assert.equal(dc.classify(record, made.credential, T0 + 90 * DAY - 1), 'current');
  assert.equal(dc.classify(record, made.credential, T0 + 90 * DAY), 'dead');
  const { record: used, made: usedMade } = family();
  const next = dc.rotate(used, 'current', T0 + 80 * DAY);
  assert.equal(used.expiresAt, T0 + 170 * DAY);
  assert.equal(dc.classify(used, next.credential, T0 + 100 * DAY), 'current');
  assert(usedMade.credential);
});

// ------------------------------------------------------------------ revoking
check('a revoked family is dead whatever is presented', () => {
  const { made, record } = family();
  assert(dc.revoke(record, 'signed out', T0 + 5));
  assert.equal(dc.classify(record, made.credential, T0 + 6), 'dead');
  assert.equal(dc.classify(record, 'not-a-credential', T0 + 6), 'dead');
  assert(!dc.isLive(record, T0 + 6));
});

check('revoking twice keeps the first time and the first reason', () => {
  const { record } = family();
  assert(dc.revoke(record, 'signed out', T0 + 5));
  assert(!dc.revoke(record, 'revoked by the account', T0 + 99));
  assert.equal(record.revokedAt, T0 + 5);
  assert.equal(record.revokedWhy, 'signed out');
});

check('the revoke token survives every rotation', () => {
  const { made, record } = family();
  for (let i = 0; i < 10; ++i) dc.rotate(record, 'current', T0 + i);
  assert(dc.revokeTokenMatches(record, made.revokeToken));
});

check('an old family\'s revoke token does nothing to a new one', () => {
  const devices = {};
  const before = family(devices), after = family(devices);
  assert(!dc.revokeTokenMatches(after.record, before.made.revokeToken));
  dc.revoke(before.record, 'signed out', T0);
  assert(dc.isLive(after.record, T0));
});

// ------------------------------------------------------------------ what a sign-in may ask for
check('no device in the request means none is wanted', () => {
  assert.deepEqual(dc.parseDeviceRequest(undefined), { wanted: false });
  assert.deepEqual(dc.parseDeviceRequest(null), { wanted: false });
});

check('a device request is refused rather than repaired', () => {
  assert(dc.parseDeviceRequest('windows-client').error);
  assert(dc.parseDeviceRequest([]).error);
  assert(dc.parseDeviceRequest({ kind: 'host' }).error);
  assert(dc.parseDeviceRequest({}).error);
});

check('a label is bounded and carries no control characters', () => {
  const parsed = dc.parseDeviceRequest({ kind: 'android', label: 'a\nb\u0000' + 'x'.repeat(200) });
  assert.equal(parsed.kind, 'android');
  assert(parsed.label.length <= 64);
  assert(!/[\u0000-\u001f]/.test(parsed.label));
  assert.equal(dc.parseDeviceRequest({ kind: 'android' }).label, 'android');
});

// ------------------------------------------------------------------ listing and bounds
check('a list shows an account its own devices, and no hash', () => {
  const devices = {};
  const mine = family(devices, 'tester'), theirs = family(devices, 'someone-else');
  const list = dc.listFor(devices, 'tester', mine.made.deviceId, T0);
  assert.equal(list.length, 1);
  assert.equal(list[0].deviceId, mine.made.deviceId);
  assert.equal(list[0].current, true);
  assert.equal(list[0].state, 'active');
  const text = JSON.stringify(list);
  assert(!text.includes(mine.record.credHash) && !text.includes(mine.record.revokeHash));
  assert(!text.includes(theirs.made.deviceId));
});

check('an account\'s families are bounded, and the least recently used goes first', () => {
  const devices = {};
  const first = family(devices, 'tester', T0);
  for (let i = 1; i <= dc.LIMITS.PER_ACCOUNT; ++i) family(devices, 'tester', T0 + i);
  const touched = dc.prune(devices, 'tester', T0 + 1000);
  assert.deepEqual(touched, [first.made.deviceId]);
  assert(!dc.isLive(first.record, T0 + 1000));
  const live = Object.values(devices).filter((d) => dc.isLive(d, T0 + 1000));
  assert.equal(live.length, dc.LIMITS.PER_ACCOUNT);
});

check('...without touching another account\'s', () => {
  const devices = {};
  const other = family(devices, 'someone-else', T0);
  for (let i = 1; i <= dc.LIMITS.PER_ACCOUNT + 3; ++i) family(devices, 'tester', T0 + i);
  dc.prune(devices, 'tester', T0 + 1000);
  assert(dc.isLive(other.record, T0 + 1000));
});

check('an ended family is remembered for a while, then forgotten', () => {
  const devices = {};
  const { made, record } = family(devices);
  dc.revoke(record, 'signed out', T0);
  dc.prune(devices, 'tester', T0 + dc.LIMITS.TOMBSTONE_MS);
  assert(devices[made.deviceId], 'still there: a late request meets "revoked", not nothing');
  dc.prune(devices, 'tester', T0 + dc.LIMITS.TOMBSTONE_MS + 1);
  assert(!devices[made.deviceId]);
});

// ------------------------------------------------------------------ the store
check('a store from before device credentials is given an empty set', () => {
  const store = { accounts: {}, hosts: {} };
  dc.normaliseStore(store);
  assert.deepEqual(store.devices, {});
});

check('a store whose devices are not an object is refused', () => {
  for (const bad of [[], 'x', 7, null]) {
    assert.throws(() => dc.normaliseStore({ accounts: {}, hosts: {}, devices: bad }));
  }
});

console.log(failed === 0
  ? `device_credentials_unit_test: ALL PASS (${passed} checks)`
  : `device_credentials_unit_test: FAIL (${failed} of ${passed + failed} failed)`);
process.exitCode = failed === 0 ? 0 : 1;
