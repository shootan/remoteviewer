p = r'C:\work\gnlink-wt\maint-r1\automation\gnlink_verify_manifest.js'
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')

def rep(a, c):
    global s
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, c)

rep('''function usableKey(hex) {
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
''', '''function usableKey(hex) {
  if (!/^[0-9a-fA-F]{128}$/.test(hex)) return null;
  const raw = Buffer.from(hex, 'hex');
  try {
    return require('crypto').createPublicKey({
      format: 'jwk',
      key: { kty: 'EC', crv: 'P-256', x: raw.subarray(0, 32).toString('base64url'), y: raw.subarray(32).toString('base64url') },
    });
  } catch {
    return null;
  }
}

// maint r4 (Codex M2-a): the shared verifySignature() also folds an exception from crypto.verify into false, so
// its SignatureInvalid alone cannot say "refused". It is confirmed here by running the same primitive once more
// on the same inputs with nothing folded: false = refused (3); a throw, or true where the shared code said false,
// = the check could not be trusted (4). A signature that is not 128 hex characters is refused as it stands --
// no crypto runs for it, there and here.
function confirmSignatureRefusal(document, signatureHex, publicKey) {
  if (!/^[0-9a-fA-F]{128}$/.test(signatureHex)) return { refused: true };
  try {
    const ok = require('crypto').verify('sha256', Buffer.from(document, 'utf8'),
                                        { key: publicKey, dsaEncoding: 'ieee-p1363' }, Buffer.from(signatureHex, 'hex'));
    return ok ? { refused: false, why: 'crypto.verify accepted what the shared verifier refused' } : { refused: true };
  } catch (e) {
    return { refused: false, why: `crypto.verify threw (${e && e.message ? e.message : e})` };
  }
}
''')
rep('''if (!usableKey(key)) {
  console.error('could not check: the trusted key is not a usable P-256 public key');
  process.exit(4);
}
''', '''const publicKey = usableKey(key);
if (!publicKey) {
  console.error('could not check: the trusted key is not a usable P-256 public key');
  process.exit(4);
}
''')
rep('''if (r.status !== Status.OK) {
  console.error(`manifest REFUSED: ${r.status}`);
  process.exit(3);
}
''', '''if (r.status === Status.SIGNATURE_INVALID) {
  let c;
  try {
    c = confirmSignatureRefusal(doc, sig, publicKey);
  } catch (e) {
    c = { refused: false, why: `${e && e.message ? e.message : e}` };
  }
  if (!c.refused) {
    console.error(`could not check: ${c.why}`);
    process.exit(4);
  }
}
if (r.status !== Status.OK) {
  console.error(`manifest REFUSED: ${r.status}`);
  process.exit(3);
}
''')
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')
