// The update manifest, on the server side: building one and checking one.
//
// The server is the publisher, so it mostly builds. It also verifies, for one reason: a manifest
// that is published without ever being read back is a manifest nobody has checked, and the first
// time anyone finds out the signature was wrong should not be on a user's machine.
//
// The parsing rules and the field list are the same ones the C++ and Kotlin sides implement, and
// all three read the same vectors from apps/shared/update_manifest/. See that directory's
// README.txt for the contract; see docs/업데이트_기능_설계.md 4.1-4.3 for why it is shaped this way.

'use strict';

const crypto = require('crypto');
const { compareVersions } = require('./version_compare');

const SUPPORTED_SCHEMA = 1;

/** Statuses mirror the C++ ManifestStatus, name for name, so logs from either side read alike. */
const Status = {
  OK: 'Ok',
  SIGNATURE_INVALID: 'SignatureInvalid',
  MALFORMED: 'Malformed',
  WRONG_PLATFORM: 'WrongPlatform',
  UNSUPPORTED_SCHEMA: 'UnsupportedSchema',
};

/** Raw X||Y (64 bytes) to a key object, via JWK -- the only portable way in without DER surgery. */
function publicKeyFromRawXY(raw) {
  if (!Buffer.isBuffer(raw) || raw.length !== 64) return null;
  try {
    return crypto.createPublicKey({
      format: 'jwk',
      key: {
        kty: 'EC',
        crv: 'P-256',
        x: raw.subarray(0, 32).toString('base64url'),
        y: raw.subarray(32).toString('base64url'),
      },
    });
  } catch {
    return null;
  }
}

/**
 * Verifies a detached ECDSA P-256/SHA-256 signature over `document`.
 *
 * Every failure -- bad signature, malformed key, wrong sizes -- returns false. The caller cannot
 * act differently on "forged" than on "could not check", and both mean the same thing.
 */
function verifySignature(document, signatureHex, publicKeyHex) {
  let signature;
  let key;
  try {
    signature = Buffer.from(signatureHex, 'hex');
    key = Buffer.from(publicKeyHex, 'hex');
  } catch {
    return false;
  }
  // Buffer.from silently drops non-hex, so the length check is what actually rejects junk.
  if (signature.length !== 64 || signatureHex.length !== 128) return false;
  if (key.length !== 64 || publicKeyHex.length !== 128) return false;

  const publicKey = publicKeyFromRawXY(key);
  if (!publicKey) return false;
  try {
    // ieee-p1363 is raw r||s, matching what CNG expects on the Windows side. The DER form would
    // have to be unwrapped there for no benefit.
    return crypto.verify('sha256', Buffer.from(document, 'utf8'),
                         { key: publicKey, dsaEncoding: 'ieee-p1363' }, signature);
  } catch {
    return false;
  }
}

/** Splits the document into fields. Returns null with a reason when it does not parse. */
function parseFields(document) {
  const out = { schema: 0, platform: '', version: '', artifact: '', size: 0, sha256: '', versionCode: 0 };
  let sawSchema = false;

  for (const raw of String(document).split('\n')) {
    const line = raw.replace(/\r$/, '').trim();
    if (!line || line.startsWith('#')) continue;

    const eq = line.indexOf('=');
    if (eq < 0) return { error: "line without '='" };
    const key = line.slice(0, eq).trim();
    const value = line.slice(eq + 1).trim();
    if (!key) return { error: 'empty key' };

    const asNumber = (text) => (/^\d{1,20}$/.test(text) ? Number(text) : null);

    switch (key) {
      case 'schema': {
        const n = asNumber(value);
        if (n === null) return { error: 'schema is not a number' };
        out.schema = n;
        sawSchema = true;
        break;
      }
      case 'platform': out.platform = value; break;
      case 'version': out.version = value; break;
      case 'artifact': out.artifact = value; break;
      case 'size': {
        const n = asNumber(value);
        if (n === null) return { error: 'size is not a number' };
        out.size = n;
        break;
      }
      case 'sha256': out.sha256 = value; break;
      case 'versionCode': {
        const n = asNumber(value);
        if (n === null) return { error: 'versionCode is not a number' };
        out.versionCode = n;
        break;
      }
      default:
        // Ignored, not rejected: a newer publisher adding a field must not brick an older reader.
        break;
    }
  }
  if (!sawSchema) return { error: 'missing schema' };
  return { fields: out };
}

/**
 * Checks the signature and, only if it passes, parses the document.
 *
 * The order is the point, and it is observable: a document that is both malformed and badly
 * signed reports SignatureInvalid, never Malformed. `fields` is present only on Ok, so there is
 * no way to read a version out of something unverified.
 */
function loadManifest(document, signatureHex, publicKeyHex, expectedPlatform) {
  if (!verifySignature(document, signatureHex, publicKeyHex)) {
    return { status: Status.SIGNATURE_INVALID, detail: 'signature did not verify' };
  }

  const parsed = parseFields(document);
  if (parsed.error) return { status: Status.MALFORMED, detail: parsed.error };
  const f = parsed.fields;

  if (f.schema !== SUPPORTED_SCHEMA) {
    return { status: Status.UNSUPPORTED_SCHEMA, detail: `schema ${f.schema}` };
  }
  if (!f.platform || !f.version || !f.artifact) {
    return { status: Status.MALFORMED, detail: 'missing platform, version or artifact' };
  }
  if (!f.size) return { status: Status.MALFORMED, detail: 'missing or zero size' };
  if (!/^[0-9a-f]{64}$/.test(f.sha256)) {
    return { status: Status.MALFORMED, detail: 'sha256 is not 64 lowercase hex characters' };
  }
  if (expectedPlatform && f.platform !== expectedPlatform) {
    return { status: Status.WRONG_PLATFORM, detail: f.platform };
  }
  return { status: Status.OK, fields: f };
}

/**
 * Renders fields into the exact bytes that get signed.
 *
 * Field order is fixed here rather than left to object iteration, because the signature covers
 * these bytes and "whatever order the keys happened to be in" is not a specification.
 */
function buildManifest(fields) {
  const lines = [
    `schema=${SUPPORTED_SCHEMA}`,
    `platform=${fields.platform}`,
    `version=${fields.version}`,
    `artifact=${fields.artifact}`,
    `size=${fields.size}`,
    `sha256=${fields.sha256}`,
  ];
  if (fields.versionCode) lines.push(`versionCode=${fields.versionCode}`);
  return lines.join('\n') + '\n';
}

/** True when `manifestVersion` is newer than `installedVersion`, by the shared contract. */
function isNewer(manifestVersion, installedVersion) {
  return compareVersions(manifestVersion, installedVersion) > 0;
}

module.exports = { Status, loadManifest, buildManifest, verifySignature, parseFields, isNewer,
                   SUPPORTED_SCHEMA };
