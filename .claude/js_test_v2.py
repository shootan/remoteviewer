import io

H = '0' * 64
p = 'apps/directory/test/update_manifest_test.js'
d = io.open(p, encoding='utf-8').read()

# The happy-path assertions move to the artifact list.
old = """  if (r.fields) {
    check('version', r.fields.version === '0.2.105', r.fields.version);
    check('artifact', r.fields.artifact === 'GNLinkSetup-0.2.105.exe', r.fields.artifact);
    check('size', r.fields.size === 3475968, String(r.fields.size));
    check('sha256 length', r.fields.sha256.length === 64);"""
new = """  if (r.fields) {
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
          r.fields.artifacts.every((a) => a.url.startsWith('https://')));"""
assert old in d, 'js happy path'
d = d.replace(old, new, 1)

# The build round trip.
old = """  const built = m.buildManifest({
    platform: 'windows',
    version: '0.3.0',
    artifact: 'GNLinkSetup-0.3.0.exe',
    size: 1234567,
    sha256: 'a'.repeat(64),
  });"""
new = """  const built = m.buildManifest({
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
  });"""
assert old in d, 'js build round trip'
d = d.replace(old, new, 1)

d = d.replace("const edited = built.replace('1234567', '1234568');",
              "const edited = built.replace('1234567', '1234568');", 1)

# field-rule cases
old = """  const cases = [
    ['missing schema', 'platform=windows\\n', 'missing schema'],
    ["line without '='", 'schema=1\\nnonsense\\n', "line without '='"],
    ['non-numeric schema', 'schema=x\\n', 'schema is not a number'],
    ['non-numeric size', 'schema=1\\nsize=big\\n', 'size is not a number'],
  ];"""
new = """  const cases = [
    ['missing schema', 'platform=windows\\n', 'missing schema'],
    ["line without '='", 'schema=2\\nnonsense\\n', "line without '='"],
    ['non-numeric schema', 'schema=x\\n', 'schema is not a number'],
    ['a short artifact line', 'schema=2\\nartifact=a|1|onlythree\\n',
     'artifact line needs name|size|sha256|url'],
    ['non-numeric artifact size', 'schema=2\\nartifact=a|big|x|y\\n',
     'artifact size is not a number'],
  ];"""
assert old in d, 'js field cases'
d = d.replace(old, new, 1)

old = """  const ok = m.parseFields('# comment\\n\\nschema=1\\nplatform=windows\\nfutureField=whatever\\n');
  check('parse: unknown key is ignored, not rejected', !ok.error && ok.fields.schema === 1,
        ok.error || '');"""
new = """  const ok = m.parseFields('# comment\\n\\nschema=2\\nplatform=windows\\nfutureField=whatever\\n');
  check('parse: unknown key is ignored, not rejected', !ok.error && ok.fields.schema === 2,
        ok.error || '');"""
assert old in d, 'js unknown key'
d = d.replace(old, new, 1)

# new: artifact-list validation, mirroring the C++ suite
old = """console.log(`\\n${failures === 0 ? 'RESULT: ALL PASS' : 'RESULT: FAILED'}`"""
new = """// ---------------------------------------------------------------- artifact list rules
{
  const crypto2 = require('crypto');
  const { publicKey, privateKey } = crypto2.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const jwk2 = publicKey.export({ format: 'jwk' });
  const keyHex = Buffer.concat([Buffer.from(jwk2.x, 'base64url'),
                                Buffer.from(jwk2.y, 'base64url')]).toString('hex');
  const sign = (doc) => crypto2.sign('sha256', Buffer.from(doc, 'utf8'),
                                     { key: privateKey, dsaEncoding: 'ieee-p1363' }).toString('hex');
  const head = 'schema=2\\nreleaseId=r\\nplatform=windows\\narch=x64\\nversion=1.0.0\\n';
  const HASH = '0'.repeat(64);

  // Each case is properly SIGNED, so the only thing rejecting it is the rule under test.
  const rules = [
    ['a well formed manifest', `artifact=a.exe|1|${HASH}|https://u.example/a\\n`, m.Status.OK],
    ['no artifacts', '', m.Status.MALFORMED],
    ['zero size', `artifact=a.exe|0|${HASH}|https://u.example/a\\n`, m.Status.MALFORMED],
    ['uppercase sha256', `artifact=a.exe|1|${'A'.repeat(64)}|https://u.example/a\\n`, m.Status.MALFORMED],
    ['http url', `artifact=a.exe|1|${HASH}|http://u.example/a\\n`, m.Status.MALFORMED],
    ['url with credentials', `artifact=a.exe|1|${HASH}|https://evil@u.example/a\\n`, m.Status.MALFORMED],
    ['over the per-file limit', `artifact=a.exe|999999999999|${HASH}|https://u.example/a\\n`, m.Status.MALFORMED],
  ];
  for (const [name, body, expect] of rules) {
    const doc = head + body;
    const r = m.loadManifest(doc, sign(doc), keyHex, 'windows', 'x64');
    check(`artifact rule: ${name}`, r.status === expect, `${r.status} ${r.detail || ''}`);
    if (expect !== m.Status.OK) check(`artifact rule: ${name} -> no fields`, !r.fields);
  }

  // Arch mismatch is not an error, the same way a platform mismatch is not.
  {
    const doc = head + `artifact=a.exe|1|${HASH}|https://u.example/a\\n`;
    const r = m.loadManifest(doc, sign(doc), keyHex, 'windows', 'arm64');
    check('a different arch -> WrongPlatform', r.status === m.Status.WRONG_PLATFORM, r.status);
  }
  // Schema 1 is no longer accepted.
  {
    const doc = 'schema=1\\nreleaseId=r\\nplatform=windows\\narch=x64\\nversion=1\\n' +
                `artifact=a.exe|1|${HASH}|https://u.example/a\\n`;
    const r = m.loadManifest(doc, sign(doc), keyHex, 'windows', 'x64');
    check('schema 1 -> UnsupportedSchema', r.status === m.Status.UNSUPPORTED_SCHEMA, r.status);
  }
}

console.log(`\\n${failures === 0 ? 'RESULT: ALL PASS' : 'RESULT: FAILED'}`"""
assert old in d, 'js tail'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('JS test moved to schema 2')
