"""AVD r6 (Codex codex_avd_r5_scripts_src.md N1): no secret typed with `adb shell input text` may reach an error text,
a traceback, or a step line -- also when adb REPEATS it in its own stderr. A stand-in for subprocess.run (no adb, no
device): it answers like a failing adb that echoes its input. The real argv is still handed to the call.

    python -I ka_avd_redact_test.py [path to ka_avd.py]     (default: the one beside this file)

The sentinel is a dummy value, never a real password."""
import importlib.util
import os
import subprocess
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'oa_adb.py')
spec = importlib.util.spec_from_file_location('ka_avd_under_test', path)
ka = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ka)

SENTINEL = 'dummy-SENTINEL-7Qx'
checks = fails = 0
seen_argv = []


def check(what, ok, detail=''):
    global checks, fails
    checks += 1
    fails += 0 if ok else 1
    print('%s  %s%s' % ('PASS' if ok else 'FAIL', what, ('  ' + detail) if detail else ''))


class FakeRun:
    def __init__(self, rc, out, err):
        self.rc, self.out, self.err = rc, out, err

    def __call__(self, argv, capture_output=True, **kw):
        seen_argv.append(list(argv))
        return subprocess.CompletedProcess(argv, self.rc, self.out.encode(), self.err.encode())


real_run = ka.subprocess.run
# 1. a failing `input text` whose stderr repeats the value (Codex's reproduction)
ka.subprocess.run = FakeRun(1, '', 'cmd: Failure calling service input: text %s rejected (device busy)' % SENTINEL)
try:
    ka.adb('shell', 'input', 'text', SENTINEL)
    tb = 'no exception'
except RuntimeError:
    tb = traceback.format_exc()
check('the call itself still carries the real value (argv unchanged)', seen_argv and seen_argv[-1][-1] == SENTINEL)
check('the error / traceback text holds NO trace of the value', SENTINEL not in tb, tb.strip().splitlines()[-1][:200])
check('...and keeps the non-secret diagnosis (what failed, the exit code, adb\'s words around it)',
      'device busy' in tb and 'Failure calling service input' in tb and ('exit 1' in tb or '(exit' in tb or 'rejected' in tb),
      tb.strip().splitlines()[-1][:200])
# 2. the full-result call (r6 run): rc and both texts, cleaned
if hasattr(ka, 'run'):
    ka.subprocess.run = FakeRun(1, 'echo %s' % SENTINEL, 'Error: %s' % SENTINEL)
    rc, out, err = ka.run('shell', 'input', 'text', SENTINEL)
    check('run(): exit code kept, stdout and stderr cleaned of the value', rc == 1 and SENTINEL not in out and
          SENTINEL not in err and '<redacted>' in err, '%r %r' % (out, err))
    ka.subprocess.run = FakeRun(0, '', '')
    rc, out, err = ka.run('shell', 'pm', 'clear', 'com.remote60.androiddirect')
    check('run(): an empty answer is told apart by its exit code (pm clear rc=0, nothing said)', rc == 0 and out == '' and err == '')
    ka.subprocess.run = FakeRun(1, '', 'Error: java.lang.SecurityException: not allowed')
    rc, out, err = ka.run('shell', 'pm', 'clear', 'com.remote60.androiddirect')
    check('run(): a failing pm clear keeps its exit code and its (non-secret) error whole', rc == 1 and 'SecurityException' in err)
else:
    check('run(): the full-result call exists (exit code + cleaned stdout / stderr)', False, 'no run() in this ka_avd.py')
# 3. a call without a secret is not touched
ka.subprocess.run = FakeRun(1, '', 'error: device offline')
try:
    ka.adb('shell', 'input', 'tap', '10', '20')
    msg = ''
except RuntimeError as e:
    msg = str(e)
check('a call without a secret: its error whole', 'device offline' in msg and "'tap'" in msg, msg[:160])
ka.subprocess.run = real_run

print('\n%d checks, %d failed\nRESULT: %s' % (checks, fails, 'PASS' if fails == 0 else 'FAIL'))
sys.exit(0 if fails == 0 else 1)
