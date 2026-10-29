"""A stand-in for mobile_abr_session_e2e_test --scenario directoryhost in the orchestration test (no host, no window,
no network): it behaves per KE_STUB_PLAN ("a=ok,b=exit2,...") for the host its --out names.
  ok       a readiness naming its --run-id and its own pid, then waits for <out>\\stop, exits 0
  exit2    exits 2 at once, no readiness (a staging / launch / guard failure)
  failed   a readiness file that says "failed", then waits for stop, exits 2
  silent   no readiness at all, waits for stop, exits 0 (the readiness timeout)
  foreign  a readiness naming ANOTHER run, then waits for stop, exits 0
  late     a valid readiness, then 3 s later exits 2 (a late INVALID / FAIL)
  nostop   a valid readiness, then never ends by itself (its stop is ignored: it must be ended by its handle)
  readyexit a valid readiness, then exits 2 at once (dead before the fixture takes it as ready)
  readydie  a valid readiness, then exits 2 after 1.5 s (dead while a later host is being prepared)
  slowready a valid readiness only after 4 s, then as ok"""
import os
import sys
import time

args = sys.argv[1:]
def arg(name):
    return args[args.index(name) + 1] if name in args else ''

out = arg('--out')
run = arg('--run-id')
k = os.path.basename(os.path.normpath(out))
plan = dict(p.split('=') for p in os.environ.get('KE_STUB_PLAN', '').split(',') if '=' in p)
mode = plan.get(k, 'ok')
log = os.path.join(out, 'host.log')


def ready(text):
    tmp = os.path.join(out, 'directoryhost_ready.tmp')
    with open(tmp, 'w', encoding='ascii') as f:
        f.write(text + '\n')
    os.replace(tmp, os.path.join(out, 'directoryhost_ready.txt'))


def wait_stop():
    while not os.path.exists(os.path.join(out, 'stop')):
        time.sleep(0.2)


print(f'stub harness {k} mode={mode} run={run} pid={os.getpid()}', flush=True)
open(log, 'w').close()
with open(os.path.join(out, 'harness_args.txt'), 'w', encoding='ascii') as f:  # oa: what the fixture passed
    f.write(' '.join(args) + '\n')
good = f'ready run={run} harness_pid={os.getpid()} host_pid=1 host_log={log}'
if mode == 'exit2':
    sys.exit(2)
if mode == 'failed':
    ready('failed')
    wait_stop()
    sys.exit(2)
if mode == 'silent':
    wait_stop()
    sys.exit(0)
if mode == 'foreign':
    ready(f'ready run=another-run harness_pid={os.getpid()} host_pid=1 host_log={log}')
    wait_stop()
    sys.exit(0)
if mode == 'slowready':
    time.sleep(4)
ready(good)
if mode == 'readyexit':
    sys.exit(2)
if mode == 'readydie':
    time.sleep(1.5)
    sys.exit(2)
if mode == 'late':
    time.sleep(3)
    sys.exit(2)
if mode == 'nostop':
    while True:
        time.sleep(1)
wait_stop()
sys.exit(0)
