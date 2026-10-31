'use strict';

/**
 * Device credentials: what lets a signed-in client get a new session without a password.
 *
 * A session lives in memory for twelve hours and is forgotten by a restart. A client that wants
 * to come back signed in needs something that outlives both, and that something must not be the
 * password -- a password cannot be revoked for one device, and it opens every device on the
 * account.
 *
 * So a sign-in that asks for it is given a FAMILY: a device id, a credential, and a revoke token.
 *
 *   credential    exchanges for a session, and for the next credential. Every use rotates it.
 *   revoke token  ends the family, and does nothing else. Fixed for the life of the family,
 *                 because its whole purpose is to work when the client is unsure which
 *                 credential the server currently holds.
 *
 * Only hashes are kept. A copy of the store opens nothing.
 *
 * This file decides; it does not persist and it does not answer requests. Everything here
 * changes a record in memory and says what happened; the caller writes the store and only then
 * tells the client. No function here takes a clock or randomness from anywhere but its
 * arguments, which is what lets the rules be tested without waiting sixty seconds for one.
 */

const crypto = require('crypto');

const DAY_MS = 24 * 60 * 60 * 1000;

const LIMITS = Object.freeze({
  // Sliding: every accepted use pushes it out again. An unused device is forgotten after this.
  DEVICE_TTL_MS: 90 * DAY_MS,
  // How long, and how many times, the credential that was just rotated away is still accepted.
  // It exists for one case: the server rotated and the answer never arrived.
  GRACE_MS: 60 * 1000,
  GRACE_USES: 1,
  // Hashes of credentials this family was really issued, newest last. What tells a superseded
  // credential coming back apart from a value somebody made up.
  ISSUED_KEPT: 32,
  // Families per account. The oldest by last use are dropped past this.
  PER_ACCOUNT: 32,
  // A revoked or expired family is kept this long so that a late request still meets a record
  // that says "revoked" rather than nothing at all.
  TOMBSTONE_MS: 30 * DAY_MS,
});

const KINDS = Object.freeze(['windows-client', 'android']);

function randomToken(bytes = 32) {
  return crypto.randomBytes(bytes).toString('hex');
}

function hashToken(token) {
  return crypto.createHash('sha256').update(String(token)).digest('hex');
}

function sameHash(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length !== b.length || !a.length) {
    return false;
  }
  return crypto.timingSafeEqual(Buffer.from(a, 'hex'), Buffer.from(b, 'hex'));
}

/** What a sign-in may say about the device it is on. Anything else is refused, not repaired. */
function parseDeviceRequest(device) {
  if (device === undefined || device === null) return { wanted: false };
  if (typeof device !== 'object' || Array.isArray(device)) return { wanted: true, error: 'device must be an object' };
  const kind = String(device.kind || '');
  if (!KINDS.includes(kind)) return { wanted: true, error: 'unknown device kind' };
  const label = String(device.label || '').replace(/[\u0000-\u001f\u007f]/g, ' ').trim().slice(0, 64);
  return { wanted: true, kind, label: label || kind };
}

function isLive(record, now) {
  return !!record && !record.revokedAt && record.expiresAt > now;
}

/**
 * Starts a family for an account. Always a new one: a sign-in the user made on purpose is not
 * a continuation of whatever this device held before, and a revoke token left over from an
 * earlier family must not be able to end this one.
 */
function createFamily(devices, accountId, request, now, random = randomToken) {
  const deviceId = random(16);
  const credential = random(32);
  const revokeToken = random(32);
  const credHash = hashToken(credential);
  devices[deviceId] = {
    deviceId,
    accountId,
    kind: request.kind,
    label: request.label,
    credHash,
    prevCredHash: '',
    prevValidUntil: 0,
    prevUsesLeft: 0,
    issued: [credHash],
    revokeHash: hashToken(revokeToken),
    createdAt: now,
    lastUsedAt: now,
    expiresAt: now + LIMITS.DEVICE_TTL_MS,
    revokedAt: 0,
    revokedWhy: '',
  };
  return { deviceId, credential, revokeToken, expiresAt: devices[deviceId].expiresAt };
}

/**
 * What a presented credential is to this family.
 *
 *   current   the one the server holds
 *   grace     the one just rotated away, inside its window and with a use left
 *   reused    one this family WAS issued and has since superseded -- or the rotated one, late or
 *             used up. Somebody holds a credential that should no longer exist.
 *   unknown   never issued here. A guess, a typo, another family's.
 *   dead      the family is revoked or expired; what was presented does not matter
 */
function classify(record, credential, now) {
  if (!record) return 'unknown';
  if (!isLive(record, now)) return 'dead';
  const presented = hashToken(credential);
  if (sameHash(presented, record.credHash)) return 'current';
  if (sameHash(presented, record.prevCredHash)) {
    return record.prevUsesLeft > 0 && now <= record.prevValidUntil ? 'grace' : 'reused';
  }
  for (const issued of record.issued || []) {
    if (sameHash(presented, issued)) return 'reused';
  }
  return 'unknown';
}

/**
 * Replaces the family's credential. Called for `current` and for `grace`.
 *
 * After `current`, the credential that was presented becomes the one in grace: if this answer
 * is lost, the client still holds it and may present it once more, for a minute.
 *
 * After `grace`, nothing is in grace. The credential the lost answer carried was never
 * received; it is superseded like any other, and if it turns up later it is a reuse.
 */
function rotate(record, how, now, random = randomToken) {
  const credential = random(32);
  const credHash = hashToken(credential);
  if (how === 'current') {
    record.prevCredHash = record.credHash;
    record.prevValidUntil = now + LIMITS.GRACE_MS;
    record.prevUsesLeft = LIMITS.GRACE_USES;
  } else {
    record.prevUsesLeft = 0;
    record.prevValidUntil = 0;
  }
  record.credHash = credHash;
  record.issued = [...(record.issued || []), credHash].slice(-LIMITS.ISSUED_KEPT);
  record.lastUsedAt = now;
  record.expiresAt = now + LIMITS.DEVICE_TTL_MS;
  return { credential, expiresAt: record.expiresAt };
}

/** Ends a family. Idempotent: ending it twice keeps the first time and the first reason. */
function revoke(record, why, now) {
  if (!record || record.revokedAt) return false;
  record.revokedAt = now;
  record.revokedWhy = String(why || '').slice(0, 64);
  record.prevUsesLeft = 0;
  return true;
}

function revokeTokenMatches(record, revokeToken) {
  return !!record && sameHash(hashToken(revokeToken), record.revokeHash);
}

/** The devices of one account, as an owner may see them. No hash leaves through here. */
function listFor(devices, accountId, currentDeviceId, now) {
  return Object.values(devices)
    .filter((d) => d.accountId === accountId)
    .map((d) => ({
      deviceId: d.deviceId,
      kind: d.kind,
      label: d.label,
      createdAt: d.createdAt,
      lastUsedAt: d.lastUsedAt,
      expiresAt: d.expiresAt,
      state: d.revokedAt ? 'revoked' : (d.expiresAt > now ? 'active' : 'expired'),
      current: d.deviceId === currentDeviceId,
    }))
    .sort((a, b) => b.lastUsedAt - a.lastUsedAt);
}

/**
 * Forgets what no longer needs remembering, and keeps an account's families within bounds.
 * Returns the ids it removed or revoked, so the caller can end their sessions.
 */
function prune(devices, accountId, now) {
  const touched = [];
  for (const d of Object.values(devices)) {
    const endedAt = d.revokedAt || (d.expiresAt <= now ? d.expiresAt : 0);
    if (endedAt && now - endedAt > LIMITS.TOMBSTONE_MS) {
      delete devices[d.deviceId];
      touched.push(d.deviceId);
    }
  }
  const live = Object.values(devices)
    .filter((d) => d.accountId === accountId && isLive(d, now))
    .sort((a, b) => a.lastUsedAt - b.lastUsedAt);
  while (live.length > LIMITS.PER_ACCOUNT) {
    const oldest = live.shift();
    revoke(oldest, 'too many devices on the account', now);
    touched.push(oldest.deviceId);
  }
  return touched;
}

/** A store read from disk either has a usable `devices` or is given an empty one. */
function normaliseStore(store) {
  if (store.devices === undefined) {
    store.devices = {};
    return;
  }
  if (!store.devices || typeof store.devices !== 'object' || Array.isArray(store.devices)) {
    throw new Error('invalid directory store schema (devices)');
  }
}

module.exports = {
  LIMITS, KINDS, randomToken, hashToken,
  parseDeviceRequest, isLive, createFamily, classify, rotate, revoke, revokeTokenMatches,
  listFor, prune, normaliseStore,
};
