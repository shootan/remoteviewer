'use strict';

/**
 * Accounts: every read and write of `store.accounts`, and of what belongs to an account, in one
 * place.
 *
 * Nothing outside this file indexes store.accounts. A future move to a database replaces this
 * file -- the functions below are the whole surface -- and leaves server.js alone. The same goes
 * for what an account owns elsewhere in the store (its hosts, its device families): what is done
 * to them BECAUSE of the account is done here, so that "the account was disabled" or "the
 * account was deleted" has one definition.
 *
 * Like device_credentials.js this decides and changes records in memory; it does not write the
 * store and does not answer requests. The caller persists, then ends sessions, then answers.
 * No clock is read here: `now` is always an argument.
 *
 * The shape of an account, and the three states, are the Account Admin API v1 contract shared
 * with GMux and IdleFirst. Names are not to be changed here alone.
 */

const deviceCredentials = require('./device_credentials');

const STATUSES = Object.freeze(['pending', 'active', 'disabled']);
const ID_PATTERN = /^[a-z0-9._-]{3,32}$/;
const PASSWORD_MIN = 8;
const PASSWORD_MAX = 128;
const MEMO_MAX = 200;

/** The messages a sign-in with the right password is refused with. Contract text, verbatim. */
const REFUSALS = Object.freeze({
  pending: Object.freeze({ error: '승인 대기 중입니다. 관리자 승인 후 사용할 수 있습니다.', code: 'pending' }),
  disabled: Object.freeze({ error: '사용이 정지된 계정입니다.', code: 'disabled' }),
});

function normaliseId(raw) {
  return String(raw || '').trim().toLowerCase();
}

function validId(id) {
  return typeof id === 'string' && ID_PATTERN.test(id);
}

function validPassword(pw) {
  return typeof pw === 'string' && pw.length >= PASSWORD_MIN && pw.length <= PASSWORD_MAX;
}

/** At most 200 characters, with control characters taken out rather than replaced. */
function cleanMemo(raw) {
  return String(raw === undefined || raw === null ? '' : raw)
    .replace(/[\u0000-\u001f\u007f-\u009f]/g, '')
    .slice(0, MEMO_MAX);
}

function has(store, id) {
  return Object.prototype.hasOwnProperty.call(store.accounts, id);
}

/** The account, or null. Never an inherited property of the object that holds them. */
function get(store, id) {
  return has(store, id) ? store.accounts[id] : null;
}

/**
 * The account as it is NOW, if it is still the one `seen` was -- the same account (not deleted
 * and made again under the same id) with the same password. Null otherwise.
 *
 * For whoever read an account, then waited (a password hash takes tens of milliseconds, a request
 * body as long as the client likes), and is about to act on what it read. Anything the admin API
 * did in between -- delete, delete and re-create, a new password -- makes the answer null.
 */
function stillTheSame(store, seen) {
  if (!seen) return null;
  const now = get(store, seen.id);
  if (!now || now.createdAt !== seen.createdAt || now.salt !== seen.salt || now.hash !== seen.hash) {
    return null;
  }
  return now;
}

/** Whether this account exists and may use the service. A missing account is not active. */
function isActive(store, id) {
  const account = get(store, id);
  return !!account && account.status === 'active';
}

/**
 * Brings accounts read from disk to the current shape.
 *
 * A store written before this had no status: every account in it was one somebody could sign
 * in with, so each becomes `active` -- an update must not sign anybody out. A status that is
 * there and not one of the three is not guessed at: it is read as `disabled`, the state that
 * lets nothing through, and said.
 */
function normaliseStore(store, log = () => {}) {
  for (const [id, account] of Object.entries(store.accounts)) {
    if (!account || typeof account !== 'object' || Array.isArray(account)) {
      throw new Error(`invalid directory store schema (account ${id})`);
    }
    if (account.status === undefined) {
      account.status = 'active';
    } else if (!STATUSES.includes(account.status)) {
      log(`[accounts] '${id}' has an unknown status '${String(account.status).slice(0, 16)}'; ` +
          'read as disabled');
      account.status = 'disabled';
    }
    if (typeof account.id !== 'string' || !account.id) account.id = id;
    const createdAt = Number(account.createdAt) || 0;
    account.createdAt = createdAt;
    if (account.updatedAt === undefined) account.updatedAt = createdAt;
    if (account.approvedAt === undefined) account.approvedAt = null;
    if (account.lastLoginAt === undefined) account.lastLoginAt = null;
    account.memo = cleanMemo(account.memo);
  }
}

/** What an account looks like to anybody outside: never the hash, never the salt. */
function publicView(store, account) {
  return {
    id: account.id,
    status: account.status,
    createdAt: account.createdAt,
    updatedAt: account.updatedAt,
    approvedAt: account.approvedAt,
    memo: account.memo,
    lastLoginAt: account.lastLoginAt,
    hostCount: hostsOf(store, account.id).length,
  };
}

function list(store, status) {
  return Object.values(store.accounts)
    .filter((a) => !status || a.status === status)
    .sort((a, b) => a.createdAt - b.createdAt || a.id.localeCompare(b.id));
}

function counts(store) {
  const out = { pending: 0, active: 0, disabled: 0 };
  for (const a of Object.values(store.accounts)) if (out[a.status] !== undefined) ++out[a.status];
  return out;
}

/**
 * Makes an account. `status` is `pending` for everything a person can reach (the admin API, the
 * signup route) and `active` only for what the operator runs by hand on the server.
 */
function create(store, { id, salt, hash, memo, status }, now) {
  if (has(store, id)) throw Object.assign(new Error('taken'), { code: 'taken' });
  if (!STATUSES.includes(status)) throw new Error('unknown status');
  const account = {
    id, salt, hash, status,
    createdAt: now,
    updatedAt: now,
    approvedAt: status === 'active' ? now : null,
    memo: cleanMemo(memo),
    lastLoginAt: null,
  };
  store.accounts[id] = account;
  return account;
}

/** Replaces an account outright -- what --add-account does to an id that already exists. */
function put(store, { id, salt, hash, status }, now) {
  const previous = get(store, id);
  store.accounts[id] = {
    ...(previous || {}),
    id, salt, hash, status,
    createdAt: previous ? previous.createdAt : now,
    updatedAt: now,
    approvedAt: status === 'active' ? (previous && previous.approvedAt) || now : null,
    memo: previous ? cleanMemo(previous.memo) : '',
    lastLoginAt: previous ? previous.lastLoginAt || null : null,
  };
  return store.accounts[id];
}

function setStatus(account, status, now) {
  account.status = status;
  account.updatedAt = now;
  if (status === 'active' && !account.approvedAt) account.approvedAt = now;
}

function setPassword(account, { salt, hash }, now) {
  account.salt = salt;
  account.hash = hash;
  account.updatedAt = now;
}

function touchLogin(account, now) {
  account.lastLoginAt = now;
}

/** Hosts registered under the account. */
function hostsOf(store, id) {
  return Object.values(store.hosts).filter((h) => h.accountId === id);
}

/** Whether a host's account may use the service. A host with no account is not. */
function hostIsActive(store, host) {
  return !!host && isActive(store, host.accountId);
}

/** Device families issued to the account (live or not). */
function devicesOf(store, id) {
  return Object.values(store.devices).filter((d) => d.accountId === id);
}

/** The devices an owner may see. Goes through here so the account filter lives in one file. */
function listDevices(store, id, currentDeviceId, now) {
  return deviceCredentials.listFor(store.devices, id, currentDeviceId, now);
}

/**
 * Ends every device family of the account. Used when the password changes: a family is a
 * standing sign-in, and a new password is the account saying the old sign-ins are over.
 * Returns the ids that were live and are now ended.
 */
function endDevicesOf(store, id, why, now) {
  const ended = [];
  for (const record of devicesOf(store, id)) {
    if (deviceCredentials.revoke(record, why, now)) ended.push(record.deviceId);
  }
  return ended;
}

/**
 * Makes a host token unusable, keeping the host. What a password change does: the PC has to be
 * signed in again, and until then it is in the list as it was, just never online.
 * Returns the token hashes that stopped working.
 */
function dropHostTokensOf(store, id) {
  const dropped = [];
  for (const host of hostsOf(store, id)) {
    if (host.tokenHash) dropped.push(host.tokenHash);
    host.tokenHash = '';
  }
  return dropped;
}

/**
 * Removes the account and everything that belongs to it: its hosts (and with them their
 * tokens) and its device families. Returns what went, so the caller can close what is open.
 */
function remove(store, id) {
  const hostIds = [];
  const tokenHashes = [];
  for (const host of hostsOf(store, id)) {
    hostIds.push(host.hostId);
    if (host.tokenHash) tokenHashes.push(host.tokenHash);
    delete store.hosts[host.hostId];
  }
  const deviceIds = [];
  for (const record of devicesOf(store, id)) {
    deviceIds.push(record.deviceId);
    delete store.devices[record.deviceId];
  }
  delete store.accounts[id];
  return { hostIds, tokenHashes, deviceIds };
}

/** Starts a device family for the account. The account is the one thing this adds. */
function createDevice(store, id, request, now) {
  return deviceCredentials.createFamily(store.devices, id, request, now);
}

/** Keeps the account's families within bounds; returns the ids it removed or ended. */
function pruneDevices(store, id, now) {
  return deviceCredentials.prune(store.devices, id, now);
}

module.exports = {
  STATUSES, ID_PATTERN, PASSWORD_MIN, PASSWORD_MAX, MEMO_MAX, REFUSALS,
  normaliseId, validId, validPassword, cleanMemo,
  get, stillTheSame, isActive, normaliseStore, publicView, list, counts,
  create, put, setStatus, setPassword, touchLogin,
  hostsOf, hostIsActive, devicesOf, listDevices, endDevicesOf, dropHostTokensOf, remove,
  createDevice, pruneDevices,
};
