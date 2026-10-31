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

const doc = fs.readFileSync(manifestPath, 'utf8');
const sig = fs.readFileSync(sigPath, 'utf8').trim();
const key = trustedKeyHex();

const r = loadManifest(doc, sig, key, platform);
if (r.status !== Status.OK) {
  console.error(`manifest REFUSED: ${r.status}`);
  process.exit(1);
}

// Acceptance alone is also true of a verifier that never looks at the signature. The cheap way to
// tell the difference is to hand it something that must fail.
const flipped = doc.slice(0, 20) + (doc[20] === 'a' ? 'b' : 'a') + doc.slice(21);
if (loadManifest(flipped, sig, key, platform).status === Status.OK) {
  console.error('the verifier accepted a tampered document -- it is not actually checking');
  process.exit(1);
}

console.log(`manifest ok: version=${r.fields.version} release=${r.fields.releaseId} ` +
            `artifacts=${r.fields.artifacts.length} platform=${platform}`);
process.exit(0);
