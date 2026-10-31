// The limits of relay_diag.js, with the clock in the test's hands and no socket.
//
// The relay port is reached before any authentication, so whatever the refusal diagnostics
// allocate or print per datagram is a cost a stranger can make us pay. The first version bounded
// lines per (reason, ip) only: ten thousand distinct sources made ten thousand entries and ten
// thousand lines (Codex reproduced it from the exact block). These checks pin the three limits
// that replace it -- entries, lines per key, lines per window -- as properties of the function,
// not of a sweep timer happening to run.
const { createRelayDiag } = require('../relay_diag');

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
  if (!cond) failures++;
}

function harness(opts) {
  let t = 1_000_000;
  const lines = [];
  const diag = createRelayDiag({ now: () => t, log: (l) => lines.push(l), ...opts });
  return { diag, lines, advance: (ms) => { t += ms; }, now: () => t };
}

// ------------------------------------------------------------------ distinct sources
{
  const { diag, lines } = harness({ maxKeys: 256, linesPerWindow: 24, linesPerKey: 3 });
  for (let i = 0; i < 10000; ++i) diag.note('punch-ignored', `10.${(i >> 16) & 255}.${(i >> 8) & 255}.${i & 255}`, `line ${i}`);
  check('ten thousand distinct sources track at most maxKeys entries', diag.size() <= 256, `size=${diag.size()}`);
  check('...and print at most linesPerWindow lines, timer or no timer', lines.length <= 24, `lines=${lines.length}`);
  check('...with zero time elapsed, so no sweep could have helped', lines.length === 24, `lines=${lines.length}`);
}

// ------------------------------------------------------------------ overflow is counted, then said once
{
  const { diag, lines, advance } = harness({ maxKeys: 4, linesPerWindow: 100, linesPerKey: 1 });
  for (let i = 0; i < 4; ++i) diag.note('punch-ignored', `10.0.0.${i}`, `tracked ${i}`);
  for (let i = 0; i < 50; ++i) diag.note('punch-ignored', `10.0.1.${i}`, `untracked ${i}`);
  for (let i = 0; i < 7; ++i) diag.note('hello-unknown-token', `10.0.2.${i}`, `untracked hello ${i}`);
  check('sources past the cap allocate nothing', diag.size() === 4, `size=${diag.size()}`);
  check('...and print nothing of their own', lines.length === 4, `lines=${lines.length}`);
  advance(10 * 1000);
  diag.note('punch-ignored', '10.0.0.0', 'tracked again');
  const overflow = lines.find((l) => l.includes('untracked sources'));
  check('when the window rolls, the untracked refusals are one line, counted per reason',
        !!overflow && overflow.includes('punch-ignored:50') && overflow.includes('hello-unknown-token:7'),
        overflow || 'no overflow line');
  check('...and that line names the cap', !!overflow && overflow.includes('over 4 keys'));
}

// ------------------------------------------------------------------ per-key lines, then a count
{
  const { diag, lines, advance } = harness({ linesPerKey: 3, linesPerWindow: 100 });
  for (let i = 0; i < 20; ++i) diag.note('punch-ignored', '203.0.113.5', `same source ${i}`);
  check('one source gets linesPerKey lines in a window', lines.length === 3, `lines=${lines.length}`);
  advance(10 * 1000);
  diag.note('punch-ignored', '203.0.113.5', 'next window');
  const summary = lines.find((l) => l.includes('punch-ignored:203.0.113.5 suppressed=17'));
  check('...and the next window opens with how many it dropped', !!summary, summary || lines.join(' | '));
  check('...followed by the new line itself', lines[lines.length - 1] === 'next window');
}

// ------------------------------------------------------------------ the global budget covers the summaries too
{
  const { diag, lines, advance } = harness({ linesPerKey: 1, linesPerWindow: 5, maxKeys: 1000 });
  for (let i = 0; i < 40; ++i) diag.note('punch-ignored', `10.1.0.${i}`, `first ${i}`);
  check('forty sources, one line each allowed, five lines printed', lines.length === 5, `lines=${lines.length}`);
  for (let i = 0; i < 40; ++i) diag.note('punch-ignored', `10.1.0.${i}`, `second ${i}`);  // each key now suppressed 1
  advance(10 * 1000);
  // Each of the 40 keys would now like to print a per-key summary; the budget is 5 per window.
  for (let i = 0; i < 40; ++i) diag.note('punch-ignored', `10.1.0.${i}`, `third ${i}`);
  const inSecondWindow = lines.length - 5;
  check('per-key summaries draw from the same budget as lines', inSecondWindow <= 5, `printed=${inSecondWindow}`);
  advance(10 * 1000);
  diag.note('punch-ignored', '10.1.0.0', 'fourth');
  const dropped = lines.find((l) => l.startsWith('[relay] diag suppressed='));
  check('what the budget dropped is said once, as a number', !!dropped, dropped || 'no global summary');
}

// ------------------------------------------------------------------ sweep frees quiet keys, keeps live ones
{
  const { diag, advance } = harness({ maxKeys: 8, linesPerWindow: 100 });
  for (let i = 0; i < 8; ++i) diag.note('punch-ignored', `10.2.0.${i}`, `x`);
  advance(21 * 1000);
  diag.note('punch-ignored', '10.2.0.0', 'still here');   // refreshes this one key's window
  diag.sweep();
  check('quiet keys are swept after two windows; a live one stays', diag.size() === 1, `size=${diag.size()}`);
  diag.note('punch-ignored', '10.2.0.9', 'new source after the sweep');
  check('...so the cap is spent on live sources, not dead ones', diag.size() === 2, `size=${diag.size()}`);
}

// ------------------------------------------------------------------ limits are exposed, for the socket test to read
{
  const { diag } = harness({});
  check('defaults: 10 s window, 3 per key, 256 keys, 24 lines per window',
        diag.limits.windowMs === 10000 && diag.limits.linesPerKey === 3 &&
        diag.limits.maxKeys === 256 && diag.limits.linesPerWindow === 24, JSON.stringify(diag.limits));
}

console.log(failures === 0 ? '\nrelay_diag_test: PASS' : `\nrelay_diag_test: ${failures} FAILED`);
process.exit(failures === 0 ? 0 : 1);
