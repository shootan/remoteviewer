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

// Schema 1 named one artifact and was never published. Schema 2 carries a release identity,
// an architecture and a list of files -- which is what a real update needs, and which lets the
// signature cover the whole set rather than one name at a time.
const SUPPORTED_SCHEMA = 2;

/** Limits on an artifact list, so a signed-but-absurd manifest cannot exhaust a client. */
const LIMITS = { maxArtifacts: 64, maxArtifactBytes: 512 * 1024 * 1024, maxTotalBytes: 2048 * 1024 * 1024 };

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
  const out = {
    schema: 0, platform: '', arch: '', version: '', releaseId: '', versionCode: 0,
    artifacts: [],
  };
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
      case 'arch': out.arch = value; break;
      case 'releaseId': out.releaseId = value; break;
      case 'version': out.version = value; break;
      case 'artifact': {
        // name|size|sha256|url. Pipe-separated because the document is signed as bytes, and a
        // format with one obvious reading has nothing to disagree about between signer and reader.
        const parts = value.split('|');
        if (parts.length !== 4) return { error: 'artifact line needs name|size|sha256|url' };
        const size = asNumber(parts[1].trim());
        if (size === null) return { error: 'artifact size is not a number' };
        out.artifacts.push({
          name: parts[0].trim(), size, sha256: parts[2].trim(), url: parts[3].trim(),
        });
        break;
      }
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
function loadManifest(document, signatureHex, publicKeyHex, expectedPlatform, expectedArch) {
  if (!verifySignature(document, signatureHex, publicKeyHex)) {
    return { status: Status.SIGNATURE_INVALID, detail: 'signature did not verify' };
  }

  const parsed = parseFields(document);
  if (parsed.error) return { status: Status.MALFORMED, detail: parsed.error };
  const f = parsed.fields;

  if (f.schema !== SUPPORTED_SCHEMA) {
    return { status: Status.UNSUPPORTED_SCHEMA, detail: `schema ${f.schema}` };
  }
  if (!f.platform || !f.version) {
    return { status: Status.MALFORMED, detail: 'missing platform or version' };
  }
  if (!f.releaseId) return { status: Status.MALFORMED, detail: 'missing releaseId' };
  if (!f.arch) return { status: Status.MALFORMED, detail: 'missing arch' };
  if (!f.artifacts.length) return { status: Status.MALFORMED, detail: 'no artifacts listed' };

  let total = 0;
  for (let i = 0; i < f.artifacts.length; i++) {
    const a = f.artifacts[i];
    if (!a.size || a.size > LIMITS.maxArtifactBytes) {
      return { status: Status.MALFORMED, detail: `artifact ${i} size out of range` };
    }
    total += a.size;
    if (total > LIMITS.maxTotalBytes) {
      return { status: Status.MALFORMED, detail: 'artifacts exceed the total size limit' };
    }
    if (!/^[0-9a-f]{64}$/.test(a.sha256)) {
      return { status: Status.MALFORMED, detail: `artifact ${i} sha256 is not 64 lowercase hex` };
    }
    // The transport boundary, enforced where the manifest is read rather than only where it is
    // fetched -- so a caller that downloads some other way cannot sidestep it.
    if (!/^https:\/\//i.test(a.url) || /^https:\/\/[^/]*@/i.test(a.url)) {
      return { status: Status.MALFORMED, detail: `artifact ${i} url must be https and carry no credentials` };
    }
  }
  if (f.artifacts.length > LIMITS.maxArtifacts) {
    return { status: Status.MALFORMED, detail: 'too many artifacts' };
  }

  if (expectedPlatform && f.platform !== expectedPlatform) {
    return { status: Status.WRONG_PLATFORM, detail: f.platform };
  }
  if (expectedArch && f.arch !== expectedArch) {
    return { status: Status.WRONG_PLATFORM, detail: `arch ${f.arch}` };
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
    `releaseId=${fields.releaseId}`,
    `platform=${fields.platform}`,
    `arch=${fields.arch}`,
    `version=${fields.version}`,
  ];
  // The artifact list is emitted in the order given, because the updater replaces files in that
  // order and a rollback restores from the backups it made along the way.
  for (const a of fields.artifacts || []) {
    lines.push(`artifact=${a.name}|${a.size}|${a.sha256}|${a.url}`);
  }
  if (fields.versionCode) lines.push(`versionCode=${fields.versionCode}`);
  return lines.join('\n') + '\n';
}

/** True when `manifestVersion` is newer than `installedVersion`, by the shared contract. */
function isNewer(manifestVersion, installedVersion) {
  return compareVersions(manifestVersion, installedVersion) > 0;
}

module.exports = { Status, loadManifest, buildManifest, verifySignature, parseFields, isNewer,
                   SUPPORTED_SCHEMA, LIMITS };
