const c = require('crypto');
const fs = require('fs');
const original = c.verify;
let n = 0;
c.verify = function (...a) {
  ++n;
  fs.appendFileSync(process.env.REVIEW_HIT, String(n) + '\n');
  if (process.env.REVIEW_MODE === 'throw') throw new Error('review provider unavailable');
  if (process.env.REVIEW_MODE === 'false-first' && n === 1) return false;
  if (process.env.REVIEW_MODE === 'throw-first' && n === 1) throw new Error('review first call failed');
  return original.apply(this, a);
};
