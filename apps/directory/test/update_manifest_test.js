// The server's manifest handling, checked against the same vectors the C++ suite verifies.
//
// The interesting assertion is not that JavaScript can verify a signature -- it is that this
// implementation accepts exactly the signature the Windows side accepts, over exactly the same
// bytes, and rejects the same tampering. Two independent implementations agreeing on one fixed
// artifact is worth more than either one agreeing with itself.
//
// Needs no server or network.
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const m = require('../update_manifest');

const vectorsDir = process.argv[2] || path.join(__dirname, '..', '..', 'shared', 'update_manifest');

let failures = 0;
let checks = 0;

function check(name, cond, detail) {
  checks++;
  if (!cond) failures++;
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
}

let document;
let signatureHex;
let publicKeyHex;
try {
  // Binary read, then utf8: the signature covers exact bytes, so nothing may translate line
  // endings on the way in.
  document = fs.readFileSync(path.join(vectorsDir, 'test_manifest.txt')).toString('utf8');
  signatureHex = fs.readFileSync(path.join(vectorsDir, 'test_manifest.sig'), 'utf8').trim();
  publicKeyHex = fs.readFileSync(path.join(vectorsDir, 'test_public_key.txt'), 'utf8').trim();
} catch (err) {
  console.log(`FAIL  could not read vectors from ${vectorsDir}  ${err.message}`);
  process.exit(2);
}

check('signature is 128 hex characters', signatureHex.length === 128, String(signatureHex.length));
check('public key is 128 hex characters', publicKeyHex.length === 128, String(publicKeyHex.length));

// ---------------------------------------------------------------- signature

check('the genuine signature verifies', m.verifySignature(document, signatureHex, publicKeyHex));

check('a tampered document is rejected',
      !m.verifySignature(document.replace('0.2.105', '0.2.106'), signatureHex, publicKeyHex));

{
  const flipped = Buffer.from(signatureHex, 'hex');
  flipped[0] ^= 0x01;
  check('a tampered signature is rejected',
        !m.verifySignature(document, flipped.toString('hex'), publicKeyHex));
}
{
  const otherKey = Buffer.from(publicKeyHex, 'hex');
  otherKey[0] ^= 0x01;
  check('the wrong key is rejected',
        !m.verifySignature(document, signatureHex, otherKey.toString('hex')));
}
check('a short signature is rejected', !m.verifySignature(document, 'ab'.repeat(31), publicKeyHex));
check('a short key is rejected', !m.verifySignature(document, signatureHex, 'ab'.repeat(31)));
check('non-hex is rejected', !m.verifySignature(document, 'z'.repeat(128), publicKeyHex));
check('empty is rejected', !m.verifySignature(document, '', ''));

// A signature made with a DIFFERENT key over the same document must not pass. This is the case
// that catches a verifier that checks shape but not provenance.
{
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const sig = crypto.sign('sha256', Buffer.from(document, 'utf8'),
                          { key: privateKey, dsaEncoding: 'ieee-p1363' });
  const jwk = publicKey.export({ format: 'jwk' });
  const impostorKey = Buffer.concat([Buffer.from(jwk.x, 'base64url'),
                                     Buffer.from(jwk.y, 'base64url')]).toString('hex');
  check('a signature from another key does not pass under our key',
        !m.verifySignature(document, sig.toString('hex'), publicKeyHex));
  check('...though it does verify under its own key (so the test is meaningful)',
        m.verifySignature(document, sig.toString('hex'), impostorKey));
}

// ---------------------------------------------------------------- verify before parse

{
  const r = m.loadManifest(document, signatureHex, publicKeyHex, 'windows');
  check('good signature -> Ok', r.status === m.Status.OK, `${r.status} ${r.detail || ''}`);
  check('fields present only on Ok', !!r.fields);
  if (r.fields) {
    check('version', r.fields.version === '0.2.105', r.fields.version);
    check('artifact', r.fields.artifact === 'GNLinkSetup-0.2.105.exe', r.fields.artifact);
    check('size', r.fields.size === 3475968, String(r.fields.size));
    check('sha256 length', r.fields.sha256.length === 64);
    check('newer than 0.2.104', m.isNewer(r.fields.version, '0.2.104'));
    check('not newer than itself', !m.isNewer(r.fields.version, '0.2.105'));
    check('newer than 0.2.99 (numeric, not lexicographic)', m.isNewer(r.fields.version, '0.2.99'));
  }
}

{
  // Both malformed AND badly signed. Must be SignatureInvalid -- a parse-first implementation
  // would say Malformed, so this is what pins the ordering.
  const r = m.loadManifest('not a manifest\nno equals here\n', 'ab'.repeat(64), publicKeyHex, 'windows');
  check('malformed AND badly signed -> SignatureInvalid', r.status === m.Status.SIGNATURE_INVALID,
        r.status);
  check('nothing parsed from it', !r.fields);
}

// ---------------------------------------------------------------- build, sign, read back

{
  // Round trip through this module's own builder, which is what the server will publish with.
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const jwk = publicKey.export({ format: 'jwk' });
  const keyHex = Buffer.concat([Buffer.from(jwk.x, 'base64url'),
                                Buffer.from(jwk.y, 'base64url')]).toString('hex');

  const built = m.buildManifest({
    platform: 'windows',
    version: '0.3.0',
    artifact: 'GNLinkSetup-0.3.0.exe',
    size: 1234567,
    sha256: 'a'.repeat(64),
  });
  const sig = crypto.sign('sha256', Buffer.from(built, 'utf8'),
                          { key: privateKey, dsaEncoding: 'ieee-p1363' }).toString('hex');

  const r = m.loadManifest(built, sig, keyHex, 'windows');
  check('a manifest this module built reads back as Ok', r.status === m.Status.OK,
        `${r.status} ${r.detail || ''}`);
  check('round trip keeps the version', r.fields && r.fields.version === '0.3.0');

  // One byte changed anywhere in the built document must break it.
  const edited = built.replace('1234567', '1234568');
  check('editing the size after signing breaks it',
        m.loadManifest(edited, sig, keyHex, 'windows').status === m.Status.SIGNATURE_INVALID);

  // Field order is fixed by the builder, not by object iteration -- the signature covers bytes.
  check('the builder emits schema first', built.startsWith('schema=1\n'), built.split('\n')[0]);
}

// ---------------------------------------------------------------- field rules

{
  // Signature checking is bypassed for these by using a verifier-passing shortcut: parseFields is
  // exported so the field rules can be tested without minting a signature for each case.
  const cases = [
    ['missing schema', 'platform=windows\n', 'missing schema'],
    ["line without '='", 'schema=1\nnonsense\n', "line without '='"],
    ['non-numeric schema', 'schema=x\n', 'schema is not a number'],
    ['non-numeric size', 'schema=1\nsize=big\n', 'size is not a number'],
  ];
  for (const [name, doc, expected] of cases) {
    const r = m.parseFields(doc);
    check(`parse: ${name}`, r.error === expected, `${r.error}`);
  }
  const ok = m.parseFields('# comment\n\nschema=1\nplatform=windows\nfutureField=whatever\n');
  check('parse: unknown key is ignored, not rejected', !ok.error && ok.fields.schema === 1,
        ok.error || '');
}

{
  // Platform and schema gates, with a real signature so the check is reached.
  const r = m.loadManifest(document, signatureHex, publicKeyHex, 'android');
  check('platform mismatch -> WrongPlatform', r.status === m.Status.WRONG_PLATFORM, r.status);
  check('platform mismatch exposes no fields', !r.fields);
}

console.log(`\n${failures === 0 ? 'RESULT: ALL PASS' : 'RESULT: FAILED'}` +
            `  (${checks} checks, ${failures} failed, vectors from ${vectorsDir})`);
process.exit(failures === 0 ? 0 : 1);
