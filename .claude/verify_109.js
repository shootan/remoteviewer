// Verifies the real 0.2.109 manifest with the directory server's own implementation.
//
// Independent of the signer: the signature was produced by .NET CNG in PowerShell and is checked
// here by Node's crypto against the public key that is compiled into the product. Two different
// implementations agreeing on the same bytes is the evidence; one implementation agreeing with
// itself is not.
//
// Also flips one byte, because acceptance on its own is equally true of a verifier that never
// looks at the signature.
const fs = require('fs');
const path = require('path');
const { loadManifest, Status } = require('../apps/directory/update_manifest');

const REL = path.join(__dirname, 'rel', '0.2.109');
// The same 128 hex characters as trusted_public_key_hex() in directory_client.cpp -- raw X||Y,
// not SPKI. The SPKI form decodes fine and then fails a length check inside the verifier, so
// getting it wrong looks exactly like a release nobody published.
const PUB = '8709ea70daac6464af4ed0fff1ed7489ec9e9a4a908d48babe5b62753242d9a872a4556df0c9ffa2e98dc70e6c553a624a8a235e8c4303488743f37ba240193e';

const doc = fs.readFileSync(path.join(REL, 'windows.manifest'), 'utf8');
const sig = fs.readFileSync(path.join(REL, 'windows.sig'), 'utf8').trim();

let bad = 0;
function check(what, ok, detail) {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${what}${detail ? '  ' + detail : ''}`);
  if (!ok) bad++;
}

const r = loadManifest(doc, sig, PUB, 'windows', 'x64');
check('node accepts the signed 0.2.109 manifest', r.status === Status.OK, r.status);
// loadManifest returns { status, fields } -- not { manifest }. Written as an unconditional check
// rather than guarded by `if (r.fields)`, because a guard that is false skips its assertions
// silently, which is how a verification passes by not running.
check('...and reads the version it claims', !!r.fields && r.fields.version === '0.2.109',
      r.fields && r.fields.version);
check('...and the release identity', !!r.fields && r.fields.releaseId === 'r-0.2.109',
      r.fields && r.fields.releaseId);
check('...with all nine artifacts', !!r.fields && r.fields.artifacts.length === 9,
      r.fields && String(r.fields.artifacts.length));

const flipped = doc.slice(0, 40) + (doc[40] === 'a' ? 'b' : 'a') + doc.slice(41);
const t = loadManifest(flipped, sig, PUB, 'windows', 'x64');
check('...and refuses the same document with one byte changed', t.status !== Status.OK, t.status);

const wrongKey = PUB.slice(0, 126) + (PUB.slice(126) === 'ff' ? '00' : 'ff');
const w = loadManifest(doc, sig, wrongKey, 'windows', 'x64');
check('...and refuses it under a different key', w.status !== Status.OK, w.status);

process.exit(bad ? 1 : 0);
