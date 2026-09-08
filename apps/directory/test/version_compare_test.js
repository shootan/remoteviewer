// Checks the JavaScript side of the version-comparison contract against the shared vectors.
//
// The same file is read by the C++ test (remote60_version_compare_test) and the Android client's
// Kotlin test. Three implementations, one set of expected answers -- if they drift apart, one of
// the three suites fails rather than the disagreement shipping quietly.
//
// Needs no server, so it runs first in run.js and costs nothing.
'use strict';

const fs = require('fs');
const path = require('path');
const { compareVersions } = require('../version_compare');

const vectorsPath = process.argv[2] ||
  path.join(__dirname, '..', '..', 'shared', 'version_compare_vectors.txt');

let failures = 0;
let checks = 0;

function check(name, cond, detail) {
  checks++;
  if (!cond) failures++;
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
}

/** Normalises to -1/0/1 so a sign convention change cannot pass unnoticed. */
function signOf(v) {
  return v < 0 ? -1 : (v > 0 ? 1 : 0);
}

let text;
try {
  text = fs.readFileSync(vectorsPath, 'utf8');
} catch (err) {
  console.log(`FAIL  could not read vectors file: ${vectorsPath}  ${err.message}`);
  process.exit(2);
}

let vectors = 0;
text.split('\n').forEach((raw, index) => {
  const line = raw.replace(/\r$/, '');
  if (!line || line.startsWith('#')) return;

  const parts = line.split('|');
  if (parts.length !== 3) {
    check(`vector line ${index + 1}`, false, `malformed: ${line}`);
    return;
  }
  const [left, right, expectText] = parts;
  const expect = Number(expectText);
  vectors++;

  const label = `"${left}" vs "${right}"`;
  const got = signOf(compareVersions(left, right));
  check(label, got === expect, `expected ${expect} got ${got}`);

  // Reversing the arguments must reverse the sign. Not listed in the vectors file because it has
  // to hold for every one of them; stating it once is stronger than listing pairs.
  const reversed = signOf(compareVersions(right, left));
  check(`${label} (antisymmetric)`, reversed === -expect, `expected ${-expect} got ${reversed}`);
});

// A vectors file that silently became empty would otherwise report a clean pass.
check('vectors file was not empty', vectors > 0, `${vectors} vectors`);

// Non-string input has to compare as older rather than throw: this reads a manifest field that
// arrives over the wire. Not in the vectors file because it is a JS-only concern.
check('undefined compares older than a version', signOf(compareVersions(undefined, '0.0.1')) === -1);
check('null compares equal to empty', signOf(compareVersions(null, '')) === 0);

console.log(`\n${failures === 0 ? 'RESULT: ALL PASS' : 'RESULT: FAILED'}` +
            `  (${checks} checks, ${failures} failed, ${vectors} vectors from ${vectorsPath})`);
process.exit(failures === 0 ? 0 : 1);
