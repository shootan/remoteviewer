// Verifies a release manifest before it is allowed anywhere near the server.
//
// Uses the directory server's own implementation, so the check here and the check the server
// performs on the way out are the same code. The signer is .NET CNG on Windows; a signature that
// only its own producer accepts is the failure this catches, and it catches it while the release
// is still a directory on someone's disk.
//
// The public key is read out of apps/native_poc/src/update_manifest.cpp rather than written
// here. It is the key compiled into the product, and a second copy of it is a second thing to get
// wrong -- the last time a key existed in two places, one of them was an empty string and every
// update silently failed to verify.
//
//   node automation/gnlink_verify_manifest.js <manifest> <sig> [platform]
//
// Exit 0 = verified. Anything else = do not publish.
// maint r3 (Codex M2): the non-zero codes are told apart, because "the document was checked and refused" and
// "the check could not be done" are different statements about what clients get:
//   3 = REFUSED: the verifier ran and the document does not verify (the only code that says so)
//   2 = usage
//   4 = the check could not be done (a file or the key could not be read, the verifier misbehaved, anything thrown)
// node itself exits 1 on an uncaught error and the shell gives 127 when node cannot run: neither is 3.
'use strict';

const fs = require('fs');
const path = require('path');

const REPO = path.join(__dirname, '..');
const { loadManifest, Status } = require(path.join(REPO, 'apps/directory/update_manifest'));

const [, , manifestPath, sigPath, platformArg] = process.argv;
if (!manifestPath || !sigPath) {
  console.error('usage: gnlink_verify_manifest.js <manifest> <sig> [platform]');
  process.exit(2);
}
const platform = platformArg || 'windows';

function trustedKeyHex() {
  const override = String(process.env.GNLINK_PUBLIC_KEY_HEX || '').trim();
  if (override) return override;
  const src = fs.readFileSync(path.join(REPO, 'apps/native_poc/src/update_manifest.cpp'), 'utf8');
  // The one 128-character hex literal in that file is the key. Anchored on the return so a hash
  // that happens to be 128 characters somewhere else cannot be picked up instead.
  const m = src.match(/return\s+"([0-9a-f]{128})"\s*;/);
  if (!m) throw new Error('could not find the trusted key in update_manifest.cpp');
  return m[1];
}

// The key is checked here, before it is used: the shared loadManifest() turns a key it cannot use into the same
// "signature did not verify" as a forged document (it is the server's code, left as it is), so an unusable key
// would read as a refusal. A usable key that is simply not the signer's still is one -- that is what refusal means.
function usableKey(hex) {
  if (!/^[0-9a-fA-F]{128}$/.test(hex)) return false;
  const raw = Buffer.from(hex, 'hex');
  try {
    require('crypto').createPublicKey({
      format: 'jwk',
      key: { kty: 'EC', crv: 'P-256', x: raw.subarray(0, 32).toString('base64url'), y: raw.subarray(32).toString('base64url') },
    });
    return true;
  } catch {
    return false;
  }
}

let doc, sig, key;
try {
  doc = fs.readFileSync(manifestPath, 'utf8');
  sig = fs.readFileSync(sigPath, 'utf8').trim();
  key = trustedKeyHex();
} catch (e) {
  console.error(`could not check: ${e && e.message ? e.message : e}`);
  process.exit(4);
}
if (!usableKey(key)) {
  console.error('could not check: the trusted key is not a usable P-256 public key');
  process.exit(4);
}

let r;
try {
  r = loadManifest(doc, sig, key, platform);
} catch (e) {
  console.error(`could not check: the verifier threw (${e && e.message ? e.message : e})`);
  process.exit(4);
}
if (r.status !== Status.OK) {
  console.error(`manifest REFUSED: ${r.status}`);
  process.exit(3);
}

// Acceptance alone is also true of a verifier that never looks at the signature. The cheap way to
// tell the difference is to hand it something that must fail. If it does not, nothing was checked: 4, not 3.
const flipped = doc.slice(0, 20) + (doc[20] === 'a' ? 'b' : 'a') + doc.slice(21);
if (loadManifest(flipped, sig, key, platform).status === Status.OK) {
  console.error('could not check: the verifier accepted a tampered document -- it is not actually checking');
  process.exit(4);
}

console.log(`manifest ok: version=${r.fields.version} release=${r.fields.releaseId} ` +
            `artifacts=${r.fields.artifacts.length} platform=${platform}`);
process.exit(0);
