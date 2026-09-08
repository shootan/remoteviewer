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
    check('releaseId', r.fields.releaseId === 'r-0.2.105-test', r.fields.releaseId);
    check('arch', r.fields.arch === 'x64', r.fields.arch);
    // Three artifacts of different sizes and contents, so nothing passes by treating them as
    // interchangeable -- and GNLinkSetup.exe is one, because the installer travels in its package.
    check('three artifacts', r.fields.artifacts.length === 3, String(r.fields.artifacts.length));
    check('the Setup is a member',
          r.fields.artifacts.some((a) => a.name === 'GNLinkSetup.exe'));
    check('sizes differ',
          new Set(r.fields.artifacts.map((a) => a.size)).size === 3);
    check('every URL is https',
          r.fields.artifacts.every((a) => a.url.startsWith('https://')));
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
    releaseId: 'r-0.3.0',
    platform: 'windows',
    arch: 'x64',
    version: '0.3.0',
    artifacts: [
      { name: 'GNLinkHost.exe', size: 1234567, sha256: 'a'.repeat(64),
        url: 'https://u.example/h.exe' },
      { name: 'GNLinkSetup.exe', size: 7654321, sha256: 'b'.repeat(64),
        url: 'https://u.example/s.exe' },
    ],
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
  check('the builder emits schema first', built.startsWith('schema=2\n'), built.split('\n')[0]);
  // Two artifacts, in the order given -- the updater replaces files in that order and a rollback
  // restores from the backups it made along the way, so the order is part of the contract.
  check('the builder emits both artifacts in order',
        built.indexOf('artifact=GNLinkHost.exe|') < built.indexOf('artifact=GNLinkSetup.exe|') &&
            built.indexOf('artifact=GNLinkHost.exe|') > 0);
}

// ---------------------------------------------------------------- field rules

{
  // Signature checking is bypassed for these by using a verifier-passing shortcut: parseFields is
  // exported so the field rules can be tested without minting a signature for each case.
  const cases = [
    ['missing schema', 'platform=windows\n', 'missing schema'],
    ["line without '='", 'schema=2\nnonsense\n', "line without '='"],
    ['non-numeric schema', 'schema=x\n', 'schema is not a number'],
    ['a short artifact line', 'schema=2\nartifact=a|1|onlythree\n',
     'artifact line needs name|size|sha256|url'],
    ['non-numeric artifact size', 'schema=2\nartifact=a|big|x|y\n',
     'artifact size is not a number'],
  ];
  for (const [name, doc, expected] of cases) {
    const r = m.parseFields(doc);
    check(`parse: ${name}`, r.error === expected, `${r.error}`);
  }
  const ok = m.parseFields('# comment\n\nschema=2\nplatform=windows\nfutureField=whatever\n');
  check('parse: unknown key is ignored, not rejected', !ok.error && ok.fields.schema === 2,
        ok.error || '');
}

{
  // Platform and schema gates, with a real signature so the check is reached.
  const r = m.loadManifest(document, signatureHex, publicKeyHex, 'android');
  check('platform mismatch -> WrongPlatform', r.status === m.Status.WRONG_PLATFORM, r.status);
  check('platform mismatch exposes no fields', !r.fields);
}

// ---------------------------------------------------------------- artifact list rules
{
  const crypto2 = require('crypto');
  const { publicKey, privateKey } = crypto2.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const jwk2 = publicKey.export({ format: 'jwk' });
  const keyHex = Buffer.concat([Buffer.from(jwk2.x, 'base64url'),
                                Buffer.from(jwk2.y, 'base64url')]).toString('hex');
  const sign = (doc) => crypto2.sign('sha256', Buffer.from(doc, 'utf8'),
                                     { key: privateKey, dsaEncoding: 'ieee-p1363' }).toString('hex');
  const head = 'schema=2\nreleaseId=r\nplatform=windows\narch=x64\nversion=1.0.0\n';
  const HASH = '0'.repeat(64);

  // Each case is properly SIGNED, so the only thing rejecting it is the rule under test.
  const rules = [
    ['a well formed manifest', `artifact=a.exe|1|${HASH}|https://u.example/a\n`, m.Status.OK],
    ['no artifacts', '', m.Status.MALFORMED],
    ['zero size', `artifact=a.exe|0|${HASH}|https://u.example/a\n`, m.Status.MALFORMED],
    ['uppercase sha256', `artifact=a.exe|1|${'A'.repeat(64)}|https://u.example/a\n`, m.Status.MALFORMED],
    ['http url', `artifact=a.exe|1|${HASH}|http://u.example/a\n`, m.Status.MALFORMED],
    ['url with credentials', `artifact=a.exe|1|${HASH}|https://evil@u.example/a\n`, m.Status.MALFORMED],
    ['over the per-file limit', `artifact=a.exe|999999999999|${HASH}|https://u.example/a\n`, m.Status.MALFORMED],
  ];
  for (const [name, body, expect] of rules) {
    const doc = head + body;
    const r = m.loadManifest(doc, sign(doc), keyHex, 'windows', 'x64');
    check(`artifact rule: ${name}`, r.status === expect, `${r.status} ${r.detail || ''}`);
    if (expect !== m.Status.OK) check(`artifact rule: ${name} -> no fields`, !r.fields);
  }

  // Arch mismatch is not an error, the same way a platform mismatch is not.
  {
    const doc = head + `artifact=a.exe|1|${HASH}|https://u.example/a\n`;
    const r = m.loadManifest(doc, sign(doc), keyHex, 'windows', 'arm64');
    check('a different arch -> WrongPlatform', r.status === m.Status.WRONG_PLATFORM, r.status);
  }
  // Schema 1 is no longer accepted.
  {
    const doc = 'schema=1\nreleaseId=r\nplatform=windows\narch=x64\nversion=1\n' +
                `artifact=a.exe|1|${HASH}|https://u.example/a\n`;
    const r = m.loadManifest(doc, sign(doc), keyHex, 'windows', 'x64');
    check('schema 1 -> UnsupportedSchema', r.status === m.Status.UNSUPPORTED_SCHEMA, r.status);
  }
}

console.log(`\n${failures === 0 ? 'RESULT: ALL PASS' : 'RESULT: FAILED'}` +
            `  (${checks} checks, ${failures} failed, vectors from ${vectorsDir})`);
process.exit(failures === 0 ? 0 : 1);
