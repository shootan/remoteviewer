// keepalive-r2 r5 (Codex N4): where the directory suite may write and delete. The suite used to put its files in
// os.tmpdir() and remove them there -- outside the repository. Now the caller names a test root (REMOTE60_TEST_TMP,
// e.g. D:/remote/remote/.claude/test-tmp/<task>); nothing is written or removed unless its real path is inside it.
const fs = require('fs');
const path = require('path');

let cached = null;

/** The real path of the test root. No root, or one that is not a .claude/test-tmp/<name> folder: the suite stops. */
function root() {
  if (cached) return cached;
  const given = process.env.REMOTE60_TEST_TMP;
  if (!given) throw new Error('REMOTE60_TEST_TMP is not set: the suite writes and deletes only inside a named test root');
  const shaped = (p) => {
    const parts = p.split(path.sep).map((s) => s.toLowerCase());
    const at = parts.lastIndexOf('test-tmp');
    return at >= 1 && parts[at - 1] === '.claude' && at === parts.length - 2;
  };
  // The shape is checked before anything is made, and again on the real path once it exists.
  if (!shaped(path.resolve(given))) throw new Error(`REMOTE60_TEST_TMP must be a folder directly under .claude/test-tmp (${given})`);
  fs.mkdirSync(given, { recursive: true });
  const real = fs.realpathSync.native(given);
  if (!shaped(real)) throw new Error(`REMOTE60_TEST_TMP must be a folder directly under .claude/test-tmp (real path ${real})`);
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

/** fs.rmSync, only after the real path (the link's target, if it is one) is checked to be inside the root. */
function remove(p, opts = {}) {
  const abs = path.resolve(p);
  let linked = false;
  try { linked = fs.lstatSync(abs).isSymbolicLink(); } catch { /* not there */ }
  if (linked || !inside(realOf(abs))) throw new Error(`refusing to remove outside the test root: ${abs}`);
  fs.rmSync(abs, { force: true, ...opts });
}

module.exports = { root, file, remove };
