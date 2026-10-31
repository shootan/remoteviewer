"""old-APK lane r3 (Codex codex_old_apk_lane_src_r2.md N1): oa_avd.py itself, against a FAKE adb (stubs/fake_adb.cmd:
no device, no adb server, no emulator). The app may be started ONLY after its install and its data clear both said
Success: a clear that fails (exit 1 with "Success", "Failed", nothing, no answer in time) or an install that fails
ends the arm INVALID (exit 2) with NO `am start` and NO `input tap` -- and the clear is not retried. The control (both
Success) does reach `am start`. Each case runs in its own new folder under <worktree>/.claude/oa_runs (nothing
outside it is written).
  python -I oa_pmclear_test.py"""
import os
import subprocess
import sys
import time

E = os.path.dirname(os.path.abspath(__file__))
WT = os.path.normpath(os.path.join(E, '..', '..'))
RUNS = os.path.join(WT, '.claude', 'oa_runs')
checks = fails = 0


def check(what, ok, detail=''):
    global checks, fails
    checks += 1
    fails += 0 if ok else 1
    print(f'{"PASS" if ok else "FAIL"}  {what}' + (f'  {detail}' if detail else ''), flush=True)


def run_case(tag, arm, mode, wait_for_start=False, extra_env=None):
    run_id = f'{tag}-{time.strftime("%Y%m%d%H%M%S")}-{os.getpid()}'
    root = os.path.join(RUNS, run_id)
    os.makedirs(os.path.join(root, 'a'))  # new: refused if it exists
    with open(os.path.join(root, 'fixture_account.txt'), 'w', encoding='ascii') as f:
        f.write('oatest notarealpassword\n')
    cp = 18460 if arm == 'P' else 0
    with open(os.path.join(root, 'fixture_ready.txt'), 'w', encoding='ascii') as f:
        f.write(f'ready run={run_id} hosts=a standby=none controlPort={cp}\n')
    host_log = os.path.join(root, 'a', 'host.log')
    with open(host_log, 'w', encoding='ascii') as f:
        f.write('[native-video-host] waiting client bindPort=50123\n[native-video-host] control waiting port=18460\n')
    with open(os.path.join(root, 'a', 'directoryhost_ready.txt'), 'w', encoding='ascii') as f:
        f.write(f'ready run={run_id}-a harness_pid=1 host_pid=2 host_log={host_log}\n')
    apk = os.path.join(root, 'fake.apk')
    with open(apk, 'wb') as f:
        f.write(b'PK fake apk for ' + run_id.encode())
    calls = os.path.join(root, 'adb_calls.txt')
    open(calls, 'w').close()
    env = {k: v for k, v in os.environ.items() if k not in ('KE_ADB_SHA256',)}
    env.update({'OA_TEST_FAKE_ADB': os.path.join(E, 'stubs', 'fake_adb.cmd'), 'OA_FAKE_ADB_LOG': calls,
                'OA_FAKE_ADB_MODE': mode, 'OA_FAKE_ADB_APK': apk, 'OA_PM_CLEAR_TIMEOUT_SEC': '2',
                'KA_SERIAL': 'emulator-5554'})
    env.update(extra_env or {})
    out = os.path.join(root, 'avd')
    p = subprocess.Popen([sys.executable, '-I', os.path.join(E, 'oa_avd.py'), root, arm, apk, out], env=env,
                         stdout=open(os.path.join(root, 'avd_stdout.txt'), 'wb'), stderr=subprocess.STDOUT)
    started = False
    end = time.time() + 60
    while time.time() < end and p.poll() is None:
        if wait_for_start and 'shell am start' in open(calls, encoding='utf-8').read():
            started = True
            break
        time.sleep(0.2)
    if p.poll() is None:
        p.kill()  # the control case: it reached the start -- nothing after it is under test here
        p.wait(15)
    log = open(calls, encoding='utf-8').read()
    steps = open(os.path.join(out, 'steps.txt'), encoding='utf-8').read() if os.path.exists(os.path.join(out, 'steps.txt')) else ''
    return {'rc': p.returncode, 'calls': log, 'steps': steps, 'started': started or 'shell am start' in log,
            'taps': log.count('shell input tap'), 'clears': log.count('shell pm clear')}


def main():
    os.makedirs(RUNS, exist_ok=True)
    print('oa_pmclear_test (old-APK lane r3 N1): oa_avd.py against a fake adb')
    for arm in ('P', 'S'):
        for mode in ('rc1', 'failed', 'empty', 'timeout'):
            r = run_case(f'pmc-{arm.lower()}-{mode}', arm, mode)
            check(f'arm {arm}, pm clear {mode}: INVALID (exit 2), no app start, no tap, the clear not retried',
                  r['rc'] == 2 and not r['started'] and r['taps'] == 0 and r['clears'] == 1 and 'INVALID' in r['steps'],
                  f'rc={r["rc"]} am-start={r["started"]} taps={r["taps"]} clears={r["clears"]}')
    r = run_case('pmc-p-installbad', 'P', 'installbad')
    check('arm P, install failed: INVALID (exit 2), no clear, no app start',
          r['rc'] == 2 and not r['started'] and r['clears'] == 0 and 'INVALID' in r['steps'],
          f'rc={r["rc"]} am-start={r["started"]} clears={r["clears"]}')
    for arm in ('P', 'S'):
        r = run_case(f'pmc-{arm.lower()}-ok', arm, 'ok', wait_for_start=True)
        check(f'control, arm {arm}: install and clear both Success -> the app IS started (the gate is not a dead end)',
              r['started'] and r['clears'] == 1, f'am-start={r["started"]} clears={r["clears"]}')
    # a fake client together with an adb pin refuses at once: a fake can never stand in for the pinned SDK client
    r = run_case('pmc-pin', 'P', 'ok', extra_env={'KE_ADB_SHA256': '7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c3496606'})
    check('a fake adb with an adb pin set: refused before any adb call', r['rc'] not in (0, None) and r['calls'] == '' and not r['started'],
          f'rc={r["rc"]} calls={len(r["calls"])}')
    print(f'\n{checks} checks, {fails} failed\nRESULT: {"PASS" if fails == 0 else "FAIL"}')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
