import io

# ---------------------------------------------------------------- JS
p = 'apps/directory/update_manifest.js'
d = io.open(p, encoding='utf-8').read()

d = d.replace('const SUPPORTED_SCHEMA = 1;',
              '''// Schema 1 named one artifact and was never published. Schema 2 carries a release identity,
// an architecture and a list of files -- which is what a real update needs, and which lets the
// signature cover the whole set rather than one name at a time.
const SUPPORTED_SCHEMA = 2;''', 1)

old = """function parseFields(document) {
  const out = { schema: 0, platform: '', version: '', artifact: '', size: 0, sha256: '', versionCode: 0 };
  let sawSchema = false;"""
new = """function parseFields(document) {
  const out = {
    schema: 0, platform: '', arch: '', version: '', releaseId: '', versionCode: 0,
    artifacts: [],
  };
  let sawSchema = false;"""
assert old in d, 'js parseFields anchor'
d = d.replace(old, new, 1)

old = """      case 'platform': out.platform = value; break;
      case 'version': out.version = value; break;
      case 'artifact': out.artifact = value; break;
      case 'size': {
        const n = asNumber(value);
        if (n === null) return { error: 'size is not a number' };
        out.size = n;
        break;
      }
      case 'sha256': out.sha256 = value; break;"""
new = """      case 'platform': out.platform = value; break;
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
      }"""
assert old in d, 'js field cases anchor'
d = d.replace(old, new, 1)

old = """  if (!f.platform || !f.version || !f.artifact) {
    return { status: Status.MALFORMED, detail: 'missing platform, version or artifact' };
  }
  if (!f.size) return { status: Status.MALFORMED, detail: 'missing or zero size' };
  if (!/^[0-9a-f]{64}$/.test(f.sha256)) {
    return { status: Status.MALFORMED, detail: 'sha256 is not 64 lowercase hex characters' };
  }
  if (expectedPlatform && f.platform !== expectedPlatform) {
    return { status: Status.WRONG_PLATFORM, detail: f.platform };
  }
  return { status: Status.OK, fields: f };"""
new = """  if (!f.platform || !f.version) {
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
    if (!/^https:\\/\\//i.test(a.url) || /^https:\\/\\/[^/]*@/i.test(a.url)) {
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
  return { status: Status.OK, fields: f };"""
assert old in d, 'js validation anchor'
d = d.replace(old, new, 1)

d = d.replace("function loadManifest(document, signatureHex, publicKeyHex, expectedPlatform) {",
              "function loadManifest(document, signatureHex, publicKeyHex, expectedPlatform, expectedArch) {", 1)

old = """function buildManifest(fields) {
  const lines = [
    `schema=${SUPPORTED_SCHEMA}`,
    `platform=${fields.platform}`,
    `version=${fields.version}`,
    `artifact=${fields.artifact}`,
    `size=${fields.size}`,
    `sha256=${fields.sha256}`,
  ];
  if (fields.versionCode) lines.push(`versionCode=${fields.versionCode}`);
  return lines.join('\\n') + '\\n';
}"""
new = """function buildManifest(fields) {
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
  return lines.join('\\n') + '\\n';
}"""
assert old in d, 'js build anchor'
d = d.replace(old, new, 1)

d = d.replace("const SUPPORTED_SCHEMA = 2;",
              """const SUPPORTED_SCHEMA = 2;

/** Limits on an artifact list, so a signed-but-absurd manifest cannot exhaust a client. */
const LIMITS = { maxArtifacts: 64, maxArtifactBytes: 512 * 1024 * 1024, maxTotalBytes: 2048 * 1024 * 1024 };""", 1)

d = d.replace("module.exports = { Status, loadManifest, buildManifest, verifySignature, parseFields, isNewer,\n                   SUPPORTED_SCHEMA };",
              "module.exports = { Status, loadManifest, buildManifest, verifySignature, parseFields, isNewer,\n                   SUPPORTED_SCHEMA, LIMITS };", 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('JS moved to schema 2')
