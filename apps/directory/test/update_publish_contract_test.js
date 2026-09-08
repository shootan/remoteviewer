// What the server publishes against what the three clients read.
//
// Every runtime already has its own manifest suite, and all three read the same fixed vectors --
// so all three agree with the vectors. What none of them checks is whether the document the SERVER
// actually emits has the same shape as those vectors. It could drift: a field renamed here, a
// field added there, and every suite would stay green while nothing in the field could install
// anything.
//
// So this compares the two mechanically rather than by reading them side by side. The key set the
// builder emits must equal the key set the shared vector carries, and the server's own loader must
// accept its own output. A table in a document would go stale; this fails.
//
// Design: docs/업데이트_배선_계획.md W8.
'use strict';

const fs = require('fs');
const path = require('path');
const { buildManifest, loadManifest, parseFields, SUPPORTED_SCHEMA } =
    require('../update_manifest.js');

let failures = 0;
let checks = 0;

function check(name, cond, detail) {
  checks++;
  if (!cond) failures++;
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
}

/** The key names a document uses, in the order they appear, with duplicates collapsed. */
function keysOf(document) {
  const keys = [];
  for (const raw of document.split('\n')) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const key = line.slice(0, eq).trim();
    if (!keys.includes(key)) keys.push(key);
  }
  return keys;
}

const vectorPath =
    path.join(__dirname, '..', '..', 'shared', 'update_manifest', 'test_manifest.txt');
const vector = fs.readFileSync(vectorPath, 'utf8');

// ---------------------------------------------------------------- the published shape

const published = buildManifest({
  releaseId: 'r-0.2.105-test',
  platform: 'windows',
  arch: 'x64',
  version: '0.2.105',
  artifacts: [
    { name: 'GNLinkHost.exe', size: 24, sha256: 'a'.repeat(64), url: 'https://u.example/h' },
    { name: 'ui\\shell.html', size: 18, sha256: 'b'.repeat(64), url: 'https://u.example/s' },
    { name: 'GNLinkSetup.exe', size: 48, sha256: 'c'.repeat(64), url: 'https://u.example/x' },
  ],
});

const vectorKeys = keysOf(vector).sort();
const publishedKeys = keysOf(published).sort();

check('the server emits exactly the keys the shared vector carries',
      JSON.stringify(vectorKeys) === JSON.stringify(publishedKeys),
      `vector=[${vectorKeys}] published=[${publishedKeys}]`);

// Named individually as well, so a failure above says which one rather than only that they differ.
for (const key of ['schema', 'releaseId', 'platform', 'arch', 'version', 'artifact']) {
  check(`published documents carry ${key}`, publishedKeys.includes(key), publishedKeys.join(','));
}

check('the schema published is the one the clients support',
      published.includes(`schema=${SUPPORTED_SCHEMA}`), `schema=${SUPPORTED_SCHEMA}`);

// ---------------------------------------------------------------- what the clients need from it

const parsed = parseFields(published);
check('the server can parse its own output', parsed.fields !== null,
      parsed.error || '');

if (parsed.fields) {
  const f = parsed.fields;
  // Each of these is read by name on the other side. C++ reads releaseId to key its staging
  // directory, Kotlin reads versionCode to decide whether Android will accept the package, and
  // every runtime reads name/size/sha256/url per artifact. A field that stopped being emitted
  // would break exactly one of them, silently.
  check('releaseId survives the round trip', f.releaseId === 'r-0.2.105-test', f.releaseId);
  check('platform survives', f.platform === 'windows', f.platform);
  check('arch survives', f.arch === 'x64', f.arch);
  check('version survives', f.version === '0.2.105', f.version);
  check('all three artifacts survive, in the order given',
        f.artifacts.length === 3 && f.artifacts[0].name === 'GNLinkHost.exe' &&
            f.artifacts[2].name === 'GNLinkSetup.exe',
        f.artifacts.map((a) => a.name).join(','));
  check('each artifact keeps its four parts',
        f.artifacts.every((a) => a.name && a.size > 0 && a.sha256.length === 64 && a.url),
        JSON.stringify(f.artifacts[0]));
  // The separator inside a name has to survive: the destination is a relative path, and a
  // runtime that received `uishell.html` would write to the wrong place.
  check('a name with a separator is not mangled', f.artifacts[1].name === 'ui\\shell.html',
        f.artifacts[1].name);
}

// ---------------------------------------------------------------- android's extra field

const androidDoc = buildManifest({
  releaseId: 'r-0.2.13',
  platform: 'android',
  arch: 'arm64',
  version: '0.2.13',
  versionCode: 12,
  artifacts: [
    { name: 'GNLink-0.2.13.apk', size: 4096, sha256: 'd'.repeat(64), url: 'https://u.example/a' },
  ],
});
const androidParsed = parseFields(androidDoc);
check('an android release carries versionCode',
      androidParsed.fields && androidParsed.fields.versionCode === 12,
      androidParsed.fields ? String(androidParsed.fields.versionCode) : androidParsed.error);
// Emitted after the artifact lines by the builder. The format is key=value with no ordering rule,
// and all three parsers read line by line -- but it is worth pinning, because a parser that
// stopped at the first artifact line would lose it and Android would refuse to install anything.
check('versionCode is readable even though it follows the artifact lines',
      androidDoc.indexOf('versionCode=') > androidDoc.indexOf('artifact='),
      'versionCode comes after artifact');

// Windows releases carry no versionCode, and that must not become a zero the other side reads as
// a real value.
check('a windows release has no versionCode line', !published.includes('versionCode='),
      published.replace(/\n/g, ' | '));

// ---------------------------------------------------------------- the loader agrees

const rejected = loadManifest(published, '0'.repeat(128), '0'.repeat(128), 'windows');
check('an unsigned document is refused by the server loader too',
      rejected.status !== 'Ok', String(rejected.status));

console.log(failures === 0 ? `\nRESULT: ALL PASS  (${checks} checks, 0 failed)`
                           : `\nRESULT: FAILED  (${checks} checks, ${failures} failed)`);
process.exit(failures === 0 ? 0 : 1);
