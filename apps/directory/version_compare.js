// Version comparison -- the JavaScript side of a contract three runtimes have to agree on.
//
// The server publishes a manifest version; the Windows updater (C++) and the Android client
// (Kotlin) each decide from it whether they are behind. Nothing can be shared across those three
// but the answers, so the answers are pinned in apps/shared/version_compare_vectors.txt and all
// three test suites read that same file. The contract and the reasoning for each rule are written
// at the top of it. See docs/업데이트_기능_설계.md 2(d).

'use strict';

// The ceiling a single component saturates at, so the three runtimes cannot disagree by
// overflowing differently. JS numbers would not overflow here at all, which is exactly why the
// clamp has to be explicit -- otherwise a huge component would compare larger in JS and wrap or
// saturate elsewhere.
const COMPONENT_MAX = 2147483647;

function isDigit(ch) {
  return ch >= '0' && ch <= '9';
}

/**
 * Negative when a < b, 0 when equal, positive when a > b.
 *
 * Non-string input is treated as the empty string rather than throwing: this runs on a manifest
 * field that arrives over the wire, and "missing version" has to compare as older, not crash.
 */
function compareVersions(a, b) {
  const left = typeof a === 'string' ? a : '';
  const right = typeof b === 'string' ? b : '';

  let i = 0;
  let j = 0;
  while (i < left.length || j < right.length) {
    // A run of digits is one component. No digits here -- end of string, a separator straight
    // away, or a non-numeric character -- reads as zero, which is what makes "0.1" and "0.1.0"
    // compare equal and ".1" equal "0.1".
    let lv = 0;
    while (i < left.length && isDigit(left[i])) {
      lv = Math.min(lv * 10 + (left.charCodeAt(i) - 48), COMPONENT_MAX);
      i++;
    }
    let rv = 0;
    while (j < right.length && isDigit(right[j])) {
      rv = Math.min(rv * 10 + (right.charCodeAt(j) - 48), COMPONENT_MAX);
      j++;
    }
    if (lv !== rv) return lv < rv ? -1 : 1;

    if (i < left.length && left[i] === '.') i++;
    if (j < right.length && right[j] === '.') j++;

    // Anything that is not a digit or a separator ends the comparison: a suffix like "-beta" or
    // "+build3" can never decide the outcome, in either direction.
    if ((i < left.length && !isDigit(left[i])) || (j < right.length && !isDigit(right[j]))) {
      break;
    }
  }
  return 0;
}

module.exports = { compareVersions, COMPONENT_MAX };
