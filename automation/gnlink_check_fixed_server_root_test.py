"""The release gate's self-test root (test-injection-finding, Codex codex_avd_guard_src_r2.md T): its fixtures only in a
NEW run folder of its own inside REMOTE60_TEST_TMP (a plain folder directly under the repository's test anchor), nothing
under %TEMP%, every refusal BEFORE anything is created, the cleanup inside its run folder only.

    python -I automation/gnlink_check_fixed_server_root_test.py

Everything this test creates is inside the anchor D:\\remote\\remote\\.claude\\test-tmp (one root of its own, a file it
makes for the "regular file" case), and it removes only those."""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('gate', os.path.join(HERE, 'gnlink_check_fixed_server.py'))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

checks = fails = 0


def check(what, ok, detail=''):
    global checks, fails
    checks += 1
    fails += 0 if ok else 1
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', what, ('  ' + detail) if detail else ''))


anchor = gate.TEST_ANCHOR
listing = lambda: sorted(os.listdir(anchor))
before = listing()
pid = os.getpid()

src = open(os.path.join(HERE, 'gnlink_check_fixed_server.py'), encoding='utf-8').read()
import ast
tree = ast.parse(src)
imports = [a.name for n in ast.walk(tree) if isinstance(n, (ast.Import, ast.ImportFrom)) for a in n.names] +           [n.module for n in ast.walk(tree) if isinstance(n, ast.ImportFrom) and n.module]
check('the gate no longer imports tempfile (nothing of it can reach %TEMP%)', 'tempfile' not in imports, str(sorted(set(imports))))

for label, env in [
    ('unset', {}),
    ('outside the anchor', {'REMOTE60_TEST_TMP': r'D:\remote\remote\.claude\gate-root-outside-%d' % pid}),
    ('the anchor itself', {'REMOTE60_TEST_TMP': anchor}),
    ('two levels below the anchor', {'REMOTE60_TEST_TMP': os.path.join(anchor, 'gate-a-%d' % pid, 'b')}),
    ('a name with a space', {'REMOTE60_TEST_TMP': os.path.join(anchor, 'gate root %d' % pid)}),
    ('a %TEMP% path', {'REMOTE60_TEST_TMP': os.path.join(os.environ.get('TEMP', r'C:\Temp'), 'gate-%d' % pid)}),
]:
    run, why = gate.self_test_run_dir(env)
    check('refused (%s): nothing created' % label, run is None and listing() == before and
          not os.path.exists(r'D:\remote\remote\.claude\gate-root-outside-%d' % pid), why)

# a regular file where the root should be
fpath = os.path.join(anchor, 'gate-root-file-%d' % pid)
with open(fpath, 'w') as f:
    f.write('x')
run, why = gate.self_test_run_dir({'REMOTE60_TEST_TMP': fpath})
check('refused (the root is a regular file): nothing created inside it', run is None and os.path.isfile(fpath), why)
os.remove(fpath)  # this test's own file

# a link on the path (the check itself, without making one): the function is asked about a link
saved = gate._is_link
gate._is_link = lambda p: os.path.normcase(p) == os.path.normcase(os.path.join(anchor, 'gate-root-link-%d' % pid))
run, why = gate.self_test_run_dir({'REMOTE60_TEST_TMP': os.path.join(anchor, 'gate-root-link-%d' % pid)})
gate._is_link = saved
check('refused (a link on the path): nothing created', run is None and 'link' in why and listing() == before, why)

# valid: a new run folder inside a root of this test's own, then the cleanup of only that
root = os.path.join(anchor, 'gate-root-test-%d' % pid)
run, why = gate.self_test_run_dir({'REMOTE60_TEST_TMP': root})
check('valid root: a NEW run folder inside it', run is not None and os.path.dirname(run) == root and os.path.isdir(run), why)
if run:
    os.makedirs(os.path.join(run, 'good'))
    with open(os.path.join(run, 'good', 'GNLinkStream.exe'), 'wb') as f:
        f.write(b'MZ')
    outside = os.path.join(root, 'beside-the-run.txt')  # beside the run folder: never the cleanup's
    with open(outside, 'w') as f:
        f.write('kept')
    files, dirs, left = gate.cleanup_run_dir(run)
    check('cleanup: the run folder and what is in it removed, nothing beside it',
          files == 1 and dirs == 2 and not left and not os.path.exists(run) and os.path.isfile(outside),
          'files=%d dirs=%d left=%s' % (files, dirs, left))
    os.remove(outside)
    # a link inside the run folder is not followed or removed: asked about it, the cleanup leaves it and says so
    run2, _ = gate.self_test_run_dir({'REMOTE60_TEST_TMP': root})
    target = os.path.join(run2, 'pretend-link')
    with open(target, 'w') as f:
        f.write('x')
    gate._is_link = lambda p: os.path.normcase(p) == os.path.normcase(target)
    files, dirs, left = gate.cleanup_run_dir(run2)
    gate._is_link = saved
    check('cleanup: a link inside is left and said (never followed)', target in left and os.path.exists(target),
          'left=%s' % left)
    os.remove(target)
    os.rmdir(run2)

# T1 (Codex codex_avd_guard_src_r3.md): a REAL directory junction inside the run folder, pointing at a sibling folder of
# this test's own (beside the run, holding a sentinel). The cleanup must not enter it: with removals RECORDED (nothing
# deleted), no path below the junction may be chosen; then the real cleanup keeps the sentinel and the junction, and
# says the run is not clean. The pre-fix cleanup (8a84198) is run the same recorded way, as the contrast.
import subprocess


class RecordingOs:
    """os, except that remove / rmdir only record (the cleanup under test deletes nothing)."""
    def __init__(self):
        self.removed = []

    def __getattr__(self, name):
        return getattr(os, name)

    def remove(self, p):
        self.removed.append(os.path.abspath(p))

    def rmdir(self, p):
        self.removed.append(os.path.abspath(p))


def recorded_cleanup(fn_globals, fn, run):
    rec = RecordingOs()
    saved_os = fn_globals['os']
    fn_globals['os'] = rec
    try:
        fn(run)
    finally:
        fn_globals['os'] = saved_os
    return rec.removed


jroot = os.path.join(anchor, 'gate-junction-test-%d' % pid)
os.mkdir(jroot)
sibling = os.path.join(jroot, 'sibling')
os.mkdir(sibling)
sentinel = os.path.join(sibling, 'sentinel.txt')
with open(sentinel, 'w') as f:
    f.write('must stay')
jrun, why = gate.self_test_run_dir({'REMOTE60_TEST_TMP': jroot})
link = os.path.join(jrun, 'link')
mk = subprocess.run(['cmd', '/c', 'mklink', '/J', link, sibling], capture_output=True, text=True)
with open(os.path.join(jrun, 'own.txt'), 'w') as f:
    f.write('x')
made = mk.returncode == 0 and os.path.isdir(link) and gate._is_link(link)
check('T1 setup: a real directory junction in the run folder, to a sibling of this test (with a sentinel)', made,
      (mk.stdout + mk.stderr).strip()[:120])
below = os.path.normcase(os.path.abspath(link)) + os.sep
if made:
    # the pre-fix cleanup, from git (8a84198), recorded
    old_src = subprocess.run(['git', '-C', os.path.dirname(HERE), 'show', '8a84198:automation/gnlink_check_fixed_server.py'],
                             capture_output=True, text=True, encoding='utf-8').stdout
    old_ns = {'__name__': 'gate_8a84198'}
    exec(compile(old_src, 'gate_8a84198', 'exec'), old_ns)
    old_removed = recorded_cleanup(old_ns, old_ns['cleanup_run_dir'], jrun)
    old_below = [p for p in old_removed if os.path.normcase(p).startswith(below)]
    check('T1 contrast: the pre-fix cleanup (8a84198) chooses a path BELOW the junction (the sibling\'s sentinel)',
          len(old_below) > 0, 'chosen below the junction: %s' % old_below)
    new_removed = recorded_cleanup(gate.__dict__, gate.cleanup_run_dir, jrun)
    new_below = [p for p in new_removed if os.path.normcase(p).startswith(below)]
    check('T1 fixed: nothing below the junction is chosen (recorded, nothing deleted); its own file is',
          not new_below and os.path.join(jrun, 'own.txt') in new_removed and
          os.path.normcase(os.path.abspath(link)) not in [os.path.normcase(p) for p in new_removed],
          'chosen: %s' % new_removed)
    files, dirs, left = gate.cleanup_run_dir(jrun)  # the real one
    check('T1 fixed, for real: the sentinel and the junction stay, the junction is said, the run is not called clean',
          os.path.isfile(sentinel) and gate._is_link(link) and link in left and jrun in left and files == 1,
          'files=%d dirs=%d left=%s' % (files, dirs, left))
# teardown: the junction removed AS A LINK (rmdir of the junction itself, never a recursive delete), then this test's own
if os.path.isdir(link) and gate._is_link(link):
    os.rmdir(link)
check('T1 teardown: the junction gone, the sentinel untouched', not os.path.exists(link) and os.path.isfile(sentinel))
if os.path.isdir(jrun):
    os.rmdir(jrun)
os.remove(sentinel)
os.rmdir(sibling)
os.rmdir(jroot)

# the real self-test in that root: the gate's own checks, then its run folder gone
os.environ['REMOTE60_TEST_TMP'] = root
saved_out = sys.stdout
from io import StringIO
buf = StringIO()
sys.stdout = buf
report = gate.self_test()
sys.stdout = saved_out
out = buf.getvalue()
left_runs = [n for n in os.listdir(root)] if os.path.isdir(root) else []
check('the self-test in the repository root: all its checks pass, its run folder removed, nothing left',
      report.failed == 0 and report.passed >= 50 and 'self-test run folder: ' + root in out and not left_runs,
      'passed=%d failed=%d left=%s' % (report.passed, report.failed, left_runs))
del os.environ['REMOTE60_TEST_TMP']
buf = StringIO()
sys.stdout = buf
report = gate.self_test()
sys.stdout = saved_out
check('the self-test without a root: refused (a FAIL), nothing created', report.failed == 1 and report.passed == 0 and
      'nothing created' in buf.getvalue() and listing() == sorted(set(before) | {os.path.basename(root)}),
      buf.getvalue().strip()[:160])
os.rmdir(root)  # this test's own (now empty) root
check('this test left the anchor as it found it', listing() == before)

print('\n%d checks, %d failed\nRESULT: %s' % (checks, fails, 'PASS' if fails == 0 else 'FAIL'))
sys.exit(0 if fails == 0 else 1)
