// keepalive-r2 r5/r6 (Codex N4): where the directory suite may write and delete. The suite used to put its files in
// os.tmpdir() and remove them there -- outside the repository. Now the caller names a test root (REMOTE60_TEST_TMP),
// which must be ONE folder directly under the trusted anchor D:/remote/remote/.claude/test-tmp (r6: a path merely
// shaped like ".claude/test-tmp/<name>" elsewhere is refused). Nothing is written or removed unless its real path is
// inside that root; every test that writes takes its folder from here (scratchDir), never from the raw variable.
const fs = require('fs');
const path = require('path');

// The trusted anchor: the main repository's own scratch folder. Not configurable -- a variable could point it anywhere.
const ANCHOR = path.resolve('D:/remote/remote/.claude/test-tmp');

let cached = null;

const same = (a, b) => path.relative(a, b) === '' && a.toLowerCase() === b.toLowerCase();

function isLink(p) {
  try { return fs.lstatSync(p).isSymbolicLink(); } catch { return false; }
}

/** The anchor's real path -- only if it exists, is no link, and every ancestor resolves to itself (no junction on the way). */
function anchorReal() {
  if (!fs.existsSync(ANCHOR) || isLink(ANCHOR)) throw new Error(`the test anchor ${ANCHOR} is missing or a link`);
  const real = fs.realpathSync.native(ANCHOR);
  if (!same(path.resolve(real), ANCHOR)) throw new Error(`the test anchor resolves elsewhere (${real})`);
  return real;
}

/** The real path of the test root (created once, inside the anchor). Checked before anything is made, and again after. */
function root() {
  if (cached) return cached;
  const given = process.env.REMOTE60_TEST_TMP;
  if (!given) throw new Error('REMOTE60_TEST_TMP is not set: the suite writes and deletes only inside a named test root');
  const anchor = anchorReal();
  const want = path.resolve(given);
  const name = path.basename(want);
  // before: exactly one plain folder name directly under the anchor
  if (!same(path.dirname(want), ANCHOR) || !name || name === '.' || name === '..' || /[\\/]/.test(name)) {
    throw new Error(`REMOTE60_TEST_TMP must be a folder directly under ${ANCHOR} (${given})`);
  }
  const target = path.join(anchor, name);
  if (fs.existsSync(target) || isLink(target)) {
    if (isLink(target)) throw new Error(`the test root is a link: ${target}`);
  } else {
    fs.mkdirSync(target);  // one level, inside the anchor only
  }
  // after: its real path is that folder of the anchor
  const real = fs.realpathSync.native(target);
  if (!same(path.dirname(real), anchor) || isLink(target)) throw new Error(`the test root resolves outside the anchor (${real})`);
  cached = real;
  return cached;
}

function inside(real) {
  const rel = path.relative(root(), real);
  return rel !== '' && !rel.startsWith('..') && !path.isAbsolute(rel);
}

/** The real path p would have: its own if it exists, else its parent's real path plus its name. */
function realOf(p) {
  const abs = path.resolve(p);
  if (fs.existsSync(abs)) return fs.realpathSync.native(abs);
  return path.join(fs.realpathSync.native(path.dirname(abs)), path.basename(abs));
}

/** A path for the suite's own file or folder, inside the root. */
function file(name) {
  const p = path.join(root(), name);
  if (!inside(realOf(p))) throw new Error(`test path outside the test root: ${p}`);
  return p;
}

/** r6 (N4 b): a test's own fresh scratch folder (mkdtemp) inside the root -- the one entry point for every child test. */
function scratchDir(prefix) {
  const dir = fs.mkdtempSync(path.join(root(), prefix));
  if (isLink(dir) || !inside(realOf(dir))) throw new Error(`scratch folder outside the test root: ${dir}`);
  return dir;
}

/** fs.rmSync, only after the real path (the link's target, if it is one) is checked to be inside the root. */
function remove(p, opts = {}) {
  const abs = path.resolve(p);
  if (isLink(abs) || !inside(realOf(abs))) throw new Error(`refusing to remove outside the test root: ${abs}`);
  fs.rmSync(abs, { force: true, ...opts });
}

module.exports = { ANCHOR, root, file, scratchDir, remove };
