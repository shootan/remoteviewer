p = r'C:\work\gnlink-wt\maint-r1\automation\gnlink_verify_manifest.js'
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')
i = s.index("const doc = fs.readFileSync(manifestPath, 'utf8');")
new_tail = """// The key is checked here, before it is used: the shared loadManifest() turns a key it cannot use into the same
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
"""
s = s[:i] + new_tail
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')
