"""AVD r11 (Codex codex_apk_inbox_plan_src_r1.md, r10 1/2/4): ke_avd.py's sample() and qemu_identify() -- the REAL
functions, loaded from ke_avd.py with a stand-in ka_avd module (its adb a fake: stubs/fake_adb_sample.cmd) and the
PowerShell probe replaced per case. No device, no emulator touched. Checks: the numbers parsed from known answers
(PSS, loadavg, %CPU found by its header -- toybox's "S[%CPU]" -- not a fixed column), a slow call bounded and recorded as
a miss ('?', never 0), an unknown top format, and qemu identity: only the one owner of emulator-5554's console port,
named qemu-system*, pid + start time; anything else unknown; a changed start time stops the CPU reading.
  python -I ke_sample_test.py"""
import os
import re
import sys
import time
import types

E = os.path.dirname(os.path.abspath(__file__))
checks = fails = 0


def check(what, ok, detail=''):
    global checks, fails
    checks += 1
    fails += 0 if ok else 1
    print(f'{"PASS" if ok else "FAIL"}  {what}' + (f'  {detail}' if detail else ''), flush=True)


# r12: this run's own new folder under runs\ (no tempfile, no %TEMP%; kept)
tmp = os.path.join(E, 'runs', f'ke_sample_test-{time.strftime("%Y%m%d%H%M%S")}-{os.getpid()}')
os.makedirs(tmp)  # refused if it exists
open(os.path.join(tmp, 'fixture_account.txt'), 'w').write('ketest notarealpassword\n')
stub = types.ModuleType('ka_avd')
stub.ADB = os.path.join(E, 'stubs', 'fake_adb_sample.cmd')
stub.SERIAL = 'emulator-5554'
stub.PKG = 'com.remote60.androiddirect'
sys.modules['ka_avd'] = stub
sys.argv = ['ke_avd.py', tmp, 'off.apk', 'on.apk', os.path.join(tmp, 'avd')]
ns = {'__name__': 'ke_avd_under_test', '__file__': os.path.join(E, 'ke_avd.py')}
exec(compile(open(os.path.join(E, 'ke_avd.py'), encoding='utf-8').read(), 'ke_avd.py', 'exec'), ns)
steps_path = os.path.join(tmp, 'avd', 'steps.txt')


def last_sample():
    lines = [l for l in open(steps_path, encoding='utf-8').read().splitlines() if ' SAMPLE ' in l]
    return lines[-1] if lines else ''


def field(line, k):
    m = re.search(r' ' + k + r'=("[^"]*"|\S+)', line)
    return m.group(1) if m else None


# ---- qemu identity
real_ps = ns['_ps']
for answer, want_pid, what in (('n=0', None, 'no listener on 5554'), ('n=2', None, 'two owners'),
                               ('9876|qemu-system-x86_64-headless|2026-10-29T21:00:00.0000000+09:00', '9876', 'one qemu owner'),
                               ('9876|LdVBoxHeadless|2026-10-29T21:00:00.0000000+09:00', None, 'an owner that is not qemu')):
    ns['_ps'] = lambda cmd, timeout=8, a=answer: (0, a + '\n', 5)
    ns['qemu_identify']()
    check(f'qemu identity: {what} -> {"pid " + want_pid if want_pid else "unknown"}', ns['_QEMU']['pid'] == want_pid,
          str(ns['_QEMU']))

# ---- sample: known answers (this emulator's qemu known)
ns['_QEMU'].update(pid='9876', start='2026-10-29T21:00:00.0000000+09:00', why='test')
os.environ['KE_FAKE_SAMPLE_MODE'] = 'ok'
ns['_ps'] = lambda cmd, timeout=8: (0, '123456789\n', 5)  # the qemu above (9876 ... start) still the same
ns['sample']('t-ok')
l = last_sample()
check('sample: PSS / loadavg / %CPU (by the header, toybox S[%CPU]) / qemu CPU parsed from known answers',
      field(l, 'app_pss_kb') == '123456' and field(l, 'app_cpu_pct') == '37.5' and
      field(l, 'emu_loadavg') == '"7.42 3.82 2.10 1/1901 18785"' and field(l, 'qemu_cpu_ms') == '123456789' and
      field(l, 'miss') == '[]', l[9:])
check('sample: its own length and a monotonic stamp are recorded', field(l, 'sample_ms') is not None and
      field(l, 'mono_ms') is not None and field(l, 'epoch_ms') is not None)

# ---- a changed qemu (start time no longer matches): no reading, a miss
ns['_ps'] = lambda cmd, timeout=8: (0, '\n', 5)
ns['sample']('t-qemu-changed')
l = last_sample()
check('sample: a qemu whose start time changed gives "?" and a miss (never a number of another process)',
      field(l, 'qemu_cpu_ms') == '?' and 'qemu:gone-or-changed' in (field(l, 'miss') or ''), l[9:])

# ---- unknown top format
os.environ['KE_FAKE_SAMPLE_MODE'] = 'topfmt'
ns['sample']('t-topfmt')
l = last_sample()
check('sample: a top with no %CPU column gives "?" and a miss (no fixed column guessed)',
      field(l, 'app_cpu_pct') == '?' and 'top:format' in (field(l, 'miss') or ''), l[9:])

# ---- a slow call: bounded, recorded
os.environ['KE_FAKE_SAMPLE_MODE'] = 'memslow'
t0 = time.monotonic()
ns['sample']('t-memslow')
took = time.monotonic() - t0
l = last_sample()
check('sample: a call past its deadline (6 s) is cut, recorded as a miss, PSS "?" -- the sample stays bounded',
      field(l, 'app_pss_kb') == '?' and 'dumpsys:None' in (field(l, 'miss') or '') and took < 9,
      f'took={took:.1f}s ' + l[9:])

io = os.path.join(tmp, 'avd', 'sample_io')
kept = sorted(os.listdir(io)) if os.path.isdir(io) else []
check("r12: every call's stdout file is this run's own (<OUT>/sample_io), created new and kept",
      len(kept) >= 6 and all(re.match(r'^\d+-\d{6}\.out$', k) for k in kept), f'{len(kept)} files in {io}')
ns['_ps'] = real_ps
print(f'\n{checks} checks, {fails} failed\nRESULT: {"PASS" if fails == 0 else "FAIL"}')
sys.exit(0 if fails == 0 else 1)
