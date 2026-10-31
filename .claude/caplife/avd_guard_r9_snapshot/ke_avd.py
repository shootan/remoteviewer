"""keepalive-r2 ② e, kept model A (codex_keepalive_r2_kept_model.md, codex_ka_e_switch_decision.md ⓐ): the REAL APK UI on
the shared AVD (emulator-5554, under the measuring slot) against ke_fixture.ps1 -- a real server.js (relay on) and four
pinned TEST hosts: ke-pc-a / -b / -c (this branch: standby entries) and ke-pc-old (a build from before standby).
Every tap on a shown, enabled element (ka_avd). Judged from the app's own diagnostics log, the directory's log and
each host's own log.

OFF build (Preconnect.AUTO_PREPARE = false, the shipped value):
  E1 sign in, four PCs; the app prepares nothing on its own
  E2 A by the ordinary connect -> first frame -> PC 목록: the session ends, the FIRST transition (a new lease; its
     directory requests are real) -> A "대기 중" (a confirmed entry on a live lease)
  E3 A again: activation of THAT lease -- no directory connect / observe / punch / ordinary Hello; the host admits it
     by activation; first frame
  E4 PC 목록: back to the same lease (Disconnect, then SessionOver) -> A "대기 중" again, the same lease (no new start)
  E5 M1: on A (by activation) a UA-2 backend request (GDI) is made; PC 목록 -> B (ordinary) -> PC 목록 (B's first
     transition) -> A (activation): a NEW session -- its backend state starts with no request, nothing re-sent
  E6 the steady reuse B -> A -> B: activations only (no directory connect, no capability fetch)
  E7 another device takes A (an ordinary Hello over the relay) while A stands by: no automatic re-take, A's card
     unchanged, no activation; then the user's tap on A takes it (by activation)
  E8 the older host: ordinary connect -> PC 목록 -> its lease gets no answer -> released, said so (an unanswered lease
     is not taken for an older host); tap again: the ordinary connect; PC 목록 tries again -> unanswered again
  E9 a card's 끊기 on B: its lease released at the host, not made again
  E10 leaving the app (HOME): every lease released (the hosts' entries end); back: nothing prepared (OFF)
  E11 sign-out with a lease: released
ON build (-Pgnlink.testStandbyAutoPrepare=true, installed over with the app's data kept):
  E12 back in front: the recent PCs are prepared on their own (each its own lease, READY), the screen not touched;
      a tap on one activates it

  python -I ke_avd.py <fixture_root> <apk_off> <apk_on> <out_dir>
"""
import json
import os
import re
import subprocess
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('KA_SERIAL', 'emulator-5554')
import ka_avd  # noqa: E402

ROOT, APK_OFF, APK_ON, OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
os.makedirs(OUT, exist_ok=True)
_, PW = open(os.path.join(ROOT, 'fixture_account.txt'), encoding='ascii').read().split()
REL = r'C:\work\gnlink-wt\feat-apk-0233\build\apps\native_poc\Release'
# 2026-10-28 (test-injection-finding): the older host (a build from before standby -- and before the power policy)
# cannot pass the shared injection / power guard: the old-host cases are NOT RUN, and the fixture starts no old host.
OLD_HOST = False
H = {k: f'ke-pc-{k}' for k in (('a', 'b', 'c', 'old') if OLD_HOST else ('a', 'b', 'c'))}
TARGET = 'tcard:remote60 mobile abr'
PKG = ka_avd.PKG
STEPS = open(os.path.join(OUT, 'steps.txt'), 'a', encoding='utf-8')
results = []
not_run = []  # (case, why): recorded apart -- never counted as a PASS
marks = {}


def step(msg):
    line = f'{time.strftime("%H:%M:%S")} {msg}'
    print(line, flush=True)
    STEPS.write(line + '\n')
    STEPS.flush()


def mark(name):
    marks[name] = time.time()


def skip_case(name, why):
    not_run.append((name, why))
    step(f'NOT RUN  {name}: {why}')


def verdict(name, ok, detail=''):
    results.append((name, bool(ok), detail))
    step(('PASS  ' if ok else 'FAIL  ') + name + ('  ' + detail if detail else ''))


def adb(*a, timeout=60):
    return subprocess.run([ka_avd.ADB, '-s', ka_avd.SERIAL, *a], capture_output=True,
                          timeout=timeout).stdout.decode('utf-8', errors='replace')


def applog():
    p = os.path.join(OUT, '_cur.log')
    try:
        ka_avd.adb('pull', ka_avd.LOG_PATH, p)
        return open(p, encoding='utf-8', errors='replace').read()
    except Exception:
        return ''


def cursor():
    lines = [l for l in applog().splitlines() if l.strip()]
    return lines[-1] if lines else ''


def since(cur):
    log = applog()
    i = log.rfind(cur) if cur else -1
    return log[i + len(cur):] if i >= 0 else log


_host_logs = {}


def hostlog(k):
    """The host's live log, at the EXACT path its harness named in this run's readiness (AVD guard r2 N2: never a glob
    by pid). The readiness must name this run (ROOT's folder name) and host k."""
    p = _host_logs.get(k)
    if not p:
        try:
            ready = open(os.path.join(ROOT, k, 'directoryhost_ready.txt'), encoding='utf-8').read().strip()
        except Exception:
            return ''
        m = re.match(r'^ready run=(\S+) harness_pid=\d+ host_pid=\d+ host_log=(\S+)$', ready)
        if not m or m.group(1) != os.path.basename(os.path.normpath(ROOT)) + '-' + k:
            raise RuntimeError(f"host {k}: a readiness that is not this run's: {ready}")
        p = _host_logs[k] = m.group(2)
    try:
        return open(p, encoding='utf-8', errors='replace').read()
    except Exception:
        return ''


def fault(k, text):
    """The pinned test host's faults (host_pinned_select_seam.hpp): e.g. pong_delay_ms=2000 holds the park fence's Pong."""
    open(os.path.join(ROOT, k, 'fault.txt'), 'w', encoding='ascii').write(text)
    step(f'  host {k} fault: {text or "(none)"}')


def centers():
    """Where each card (and its 끊기) is on the list right now -- for taps that must land within a second of a
    screen change, faster than a UI dump (~3 s each)."""
    nodes = ka_avd.dump()
    out = {}
    for k, name in H.items():
        nm = next((n for n in nodes if n['id'] == 'hostCardName' and n['text'] == name), None)
        if not nm:
            continue
        x1, y1, x2, y2 = nm['bounds']
        out[k] = ((x1 + x2) // 2, (y1 + y2) // 2)
        end = next((n for n in nodes if n['id'] == 'hostCardDisconnect' and n['bounds'][1] <= y2 + 60 and
                    n['bounds'][3] >= y1 - 60), None)
        if end:
            ex1, ey1, ex2, ey2 = end['bounds']
            out[k + ':end'] = ((ex1 + ex2) // 2, (ey1 + ey2) // 2)
    return out


def fast_list_then(taps):
    """PC 목록 from the viewer, then -- with no dump in between -- the given taps on cached list positions."""
    n0 = cursor()
    menu('viewerMenuHostsButton')
    # the list is portrait, the viewer landscape: taps before the rotation lands miss (ke_r13) -- after it, and still
    # inside the hand-back window (the fence's Pong held 2 s, its budget 2.5 s)
    time.sleep(1.3)
    for label, (x, y) in taps:
        ka_avd.adb('shell', 'input', 'tap', str(x), str(y))
        step(f'  fast tap {label} at {x},{y}')
        time.sleep(0.3)
    return n0


def fast_from_targets(taps):
    """PC 목록 from the TARGETS screen (portrait, like the list: no rotation to wait out -- ke_r13/r14 lost taps sent
    while the viewer's landscape turned into the list's portrait), then at once the given taps on cached positions."""
    n0 = cursor()
    tap('listHostsButton')
    for label, (x, y) in taps:
        time.sleep(0.3)
        ka_avd.adb('shell', 'input', 'tap', str(x), str(y))
        step(f'  fast tap {label} at {x},{y}')
    return n0


def list_positions():
    """One dump of the list: each card's centre (and its 끊기), 모두 끊기 and 로그아웃 -- for taps within a fence (<= 2.5 s),
    faster than a dump. Cards move with the 모두 끊기 banner: taken in the state the fence will be in."""
    nodes = ka_avd.dump()
    out = {}
    for k, name in H.items():
        nm = next((n for n in nodes if n['id'] == 'hostCardName' and n['text'] == name), None)
        if not nm:
            continue
        x1, y1, x2, y2 = nm['bounds']
        out[k] = ((x1 + x2) // 2, (y1 + y2) // 2)
        end = next((n for n in nodes if n['id'] == 'hostCardDisconnect' and n['bounds'][1] <= y2 + 60 and
                    n['bounds'][3] >= y1 - 60), None)
        if end:
            ex1, ey1, ex2, ey2 = end['bounds']
            out[k + ':end'] = ((ex1 + ex2) // 2, (ey1 + ey2) // 2)
    for rid in ('hostsParkDisconnectButton', 'hostsLogoutButton'):
        n = next((n for n in nodes if n['id'] == rid and n['enabled']), None)
        if n:
            x1, y1, x2, y2 = n['bounds']
            out[rid] = ((x1 + x2) // 2, (y1 + y2) // 2)
    return out


def end_position(pos, k):
    """Where a card's 끊기 is when it shows: its own, or the same offset from its name as another card's."""
    if k + ':end' in pos:
        return pos[k + ':end']
    ref = next((r for r in H if r + ':end' in pos), None)
    if ref is None:  # none shows one now: the column and offset seen in ke_r16 (A: name 324,686 -> 끊기 880,717)
        return 880, pos[k][1] + 31
    return pos[ref + ':end'][0], pos[k][1] + pos[ref + ':end'][1] - pos[ref][1]


def dirlog():
    try:
        return open(os.path.join(ROOT, 'dir', 'server_stdout.txt'), encoding='utf-8', errors='replace').read()
    except Exception:
        return ''


def tap(key):
    step(ka_avd.tap(key))


def shot(name):
    png = ka_avd.adb('exec-out', 'screencap', '-p', binary=True)
    open(os.path.join(OUT, name + '.png'), 'wb').write(png)


def login():
    if not ka_avd.wait('loginIdInput', 40):
        return False
    tap('loginIdInput')
    ka_avd.adb('shell', 'input', 'keycombination', '113', '29', check=False)
    ka_avd.adb('shell', 'input', 'keyevent', '67', check=False)
    ka_avd.adb('shell', 'input', 'text', 'ketest')
    tap('loginPasswordInput')
    ka_avd.adb('shell', 'input', 'text', PW)
    ka_avd.adb('shell', 'input', 'keyevent', '4', check=False)
    tap('loginButton')
    return all(ka_avd.wait('card:' + H[k], 30 if k == 'a' else 5) is not None for k in H)


def select(tag):
    n0 = cursor()
    # the targets screen keeps the tab it was left on (ke_r5: E5's Settings tab hid every later target card)
    if not ka_avd.wait(TARGET, 8) and ka_avd.wait('listWindowsButton', 2):
        tap('listWindowsButton')
    if not ka_avd.wait(TARGET, 30):
        verdict(f'{tag}: the target is shown', False)
        return None
    tap(TARGET)
    t0 = time.time()
    while time.time() - t0 < 25:
        log = since(n0)
        if '[select_ready]' in log:
            ms = int(re.findall(r'\[select_ready\].* ms=(-?\d+)', log)[-1])
            verdict(f'{tag}: selection -> first frame', True, f'{ms} ms')
            time.sleep(1.0)
            return ms
        time.sleep(0.5)
    verdict(f'{tag}: selection -> first frame within 25 s', False)
    return None


def menu(button):
    """The viewer menu's [button]: the controls woken, the menu opened, the button tapped -- the whole of it tried up
    to three times (ke_r11, ke_r12: a faded control or a slow dump while the video plays lost one step)."""
    for attempt in range(3):
        if ka_avd.wait(button, 2):
            tap(button)
            return
        for _ in range(3):
            if ka_avd.wait('viewerMenuButton', 2):
                break
            ka_avd.adb('shell', 'input', 'tap', '1200', '540')
            time.sleep(1.0)
        n = ka_avd.wait('viewerMenuButton', 2)
        if n:
            tap('viewerMenuButton')
            if ka_avd.wait(button, 8):
                tap(button)
                return
        step(f'  menu: {button} not reached (attempt {attempt + 1}), again')
        if ka_avd.wait('viewerMenuCloseButton', 1):  # a half-open menu closes (never BACK: that leaves the viewer)
            tap('viewerMenuCloseButton')
        time.sleep(1.0)
    raise RuntimeError(f'menu button not reached: {button}')


def to_list(tag):
    """PC 목록 from the viewer or the targets screen."""
    n0 = cursor()
    nodes = ka_avd.dump()
    if any(x['id'] == 'listHostsButton' for x in nodes) and not any(x['id'] == 'viewerMenuButton' for x in nodes):
        tap('listHostsButton')
    else:
        menu('viewerMenuHostsButton')
    ok = ka_avd.wait('card:' + H['a'], 15) is not None
    time.sleep(0.5)
    return n0, ok


def card(host_name):
    """(state label, detail line, 끊기 shown) of a PC's card, as rendered."""
    nodes = ka_avd.dump()
    name = next((n for n in nodes if n['id'] == 'hostCardName' and n['text'] == host_name), None)
    if not name:
        return None
    y1, y2 = name['bounds'][1], name['bounds'][3]
    near = [n for n in nodes if n['bounds'][1] <= y2 + 60 and n['bounds'][3] >= y1 - 60]
    get = lambda rid: next((n['text'] for n in near if n['id'] == rid), '')
    return get('hostCardState'), get('hostCardDetail'), any(n['id'] == 'hostCardDisconnect' for n in near)


def wait_card(host_name, state, sec=30):
    end = time.time() + sec
    c = None
    while time.time() < end:
        c = card(host_name)
        if c and c[0] == state:
            return c
        time.sleep(1)
    return c


def card_end(host_name):
    nodes = ka_avd.dump()
    name = next((n for n in nodes if n['id'] == 'hostCardName' and n['text'] == host_name), None)
    y1, y2 = name['bounds'][1], name['bounds'][3]
    btn = next(n for n in nodes if n['id'] == 'hostCardDisconnect' and n['bounds'][1] <= y2 + 60 and n['bounds'][3] >= y1 - 60
               and n['enabled'])
    x1, b1, x2, b2 = btn['bounds']
    ka_avd.adb('shell', 'input', 'tap', str((x1 + x2) // 2), str((b1 + b2) // 2))
    step(f'tapped 끊기 of {host_name}')


def count(pattern, text):
    return len(re.findall(pattern, text))


def start_app(apk, clear):
    step('install: ' + subprocess.run([ka_avd.ADB, '-s', ka_avd.SERIAL, 'install', '-r', '-d', apk],
                                      capture_output=True, timeout=180).stdout.decode(errors='replace').strip())
    if clear:
        rc, out, err = ka_avd.run('shell', 'pm', 'clear', PKG)  # AVD r6: one call, its exit code and both texts
        step(f'  pm clear: rc={rc} stdout={out.strip()[:80]!r} stderr={err.strip()[:120]!r}')
        time.sleep(2)  # ke_r8: an am start right after pm clear was dropped by the clear's own force-stop
    for attempt in range(3):
        ka_avd.adb('shell', 'am', 'start', '-n', f'{PKG}/.MainActivity')
        time.sleep(3)
        if any(n['id'] in ('loginIdInput', 'hostCardName', 'hostsLogoutButton') or n['id'].startswith('list')
               for n in ka_avd.dump()):
            return
        step(f'  the app is not in front after am start (attempt {attempt + 1}): started again')


def env_note(tag):
    """decoder-liveness r1 (2): the emulator's state at this point, recorded only -- uptime, load, this boot's id."""
    up = adb('shell', 'cat', '/proc/uptime').split()
    la = adb('shell', 'cat', '/proc/loadavg').strip()
    boot = adb('shell', 'cat', '/proc/sys/kernel/random/boot_id').strip()
    step(f'  env[{tag}]: uptime_s={up[0] if up else "?"} loadavg="{la}" boot_id={boot}')


def e2_watch(sh_off):
    """decoder-liveness r1 (Codex codex_apk_decoder_liveness_design_r1.md (2)): E2 once with the REAL sharpening
    pipeline off -- Settings' OFF (SharpnessLevel.OFF: the decoder writes straight to the TextureView), not strength 0,
    not AUTO_PREPARE. The effective path is judged from the app's own log. Nothing after E2 runs.
    AVD r9 (Codex codex_apk_decoder_liveness_src_r5.md): the same E2 + 60 s watch with the DEFAULT sharpening
    (sh_off=False, plan e2-sh) -- Settings untouched; what was requested, what was effective and any fallback are
    recorded from the app's own log, and a run that went direct is said to be NOT an SH-on render check."""
    tag = 'E2-SHOFF' if sh_off else 'E2-SH'
    env_note('before E2')
    pkg = adb('shell', 'dumpsys', 'package', PKG)
    step('  app: ' + ' '.join(re.findall(r'versionName=\S+|versionCode=\d+', pkg)[:2]))
    step('  emulator: ' + adb('shell', 'getprop', 'ro.build.fingerprint').strip() +
         ' qemu=' + adb('shell', 'getprop', 'ro.kernel.qemu').strip())
    mark('E2')
    n_sh = cursor()
    tap('card:' + H['a'])
    if not ka_avd.wait(TARGET, 30):
        verdict('E2-SHOFF A: the targets are shown', False)
        return
    if sh_off:
        if ka_avd.wait('listSettingsButton', 10):
            tap('listSettingsButton')
            for _ in range(10):
                if ka_avd.wait('settingsSharpOffButton', 1):
                    break
                ka_avd.adb('shell', 'input', 'swipe', '540', '1800', '540', '900', '300', check=False)
            if ka_avd.wait('settingsSharpOffButton', 2):
                tap('settingsSharpOffButton')
                time.sleep(1.5)
        sel = re.findall(r'\[sharpness_select\][^\n]*', since(n_sh))
        shot('E2_shoff_settings')
        verdict('E2-SHOFF the sharpening pipeline is set OFF in Settings (level=off, effective=off)',
                any('level=off' in s and 'effective=off' in s for s in sel), ' | '.join(sel)[:200])
    else:
        step('  E2-SH: Settings untouched -- the default sharpening of a cleared app')
    first_ms = select('E2 A (ordinary connect, sharpening pipeline ' + ('OFF)' if sh_off else 'DEFAULT)'))
    log = since(n_sh)
    paths = re.findall(r'\[sharpen_(?:path|start|fallback|direct_skipped|producer_stuck)\][^\n]*', log)
    if sh_off:
        verdict('E2-SHOFF the viewer used the direct path only (requested=off, no sharpen_start)',
                any('path=direct requested=off' in p for p in paths) and not any(p.startswith('[sharpen_start]') for p in paths),
                ' | '.join(paths)[:300])
    else:
        # AVD r9: requested / effective / fallback, from the app's own lines -- recorded, and a run that went direct is
        # never counted as an SH-on render check
        requested = re.findall(r'requested=(\S+)', ' '.join(paths))
        fallback = [p for p in paths if p.startswith('[sharpen_fallback]') or p.startswith('[sharpen_producer_stuck]')]
        started = any(p.startswith('[sharpen_start]') for p in paths)
        effective = 'sharpen' if started and not fallback else 'direct'
        step(f'  E2-SH requested={requested[-1] if requested else "?"} effective={effective} '
             f'fallback={" | ".join(fallback)[:200] if fallback else "none"}')
        if effective != 'sharpen':
            step('  E2-SH NOTE: this run went DIRECT -- it is NOT an SH-on render check')
        verdict('E2-SH the effective sharpening path is known from the app log (requested / effective / fallback)',
                bool(paths) and bool(requested), f'effective={effective} | ' + ' | '.join(paths)[:260])
    env_note('after E2 select')
    # AVD r8 (Codex design r3 D2): the session watched, untouched, for 60 s after its first frame -- the FIRST end, if
    # any, recorded as it is (so a later menu step cannot look like the first failure).
    if first_ms is not None:
        n_watch = cursor()
        t_watch = time.time()
        first_end = None
        while time.time() - t_watch < 60:
            time.sleep(5)
            seen = re.findall(r'[^\n]*\[(?:keep_end|scene)\][^\n]*', since(n_watch))
            ends = [l for l in seen if '[keep_end]' in l or '[scene] HOSTS' in l]
            if ends and first_end is None:
                first_end = ends[0]
                step(f'  FIRST END at +{time.time() - t_watch:.1f}s of the watch: {first_end[:200]}')
        watched = since(n_watch)
        traces = len(re.findall(r'\[liveness_trace\]', watched))
        verdict(f'{tag} the session stayed up 60 s after its first frame (no keep_end, still the viewer)',
                first_end is None, f'liveness_trace lines={traces}' + (f' first end: {first_end[:160]}' if first_end else ''))
        env_note('after the 60 s watch')
    n0, ok = to_list('E2')
    st = wait_card(H['a'], '대기 중', 40)
    log = since(n0)
    shot('E2_list_A_ready')
    verdict('E2 PC 목록: the session ends, the FIRST transition (a new lease, its directory requests logged)',
            ok and '[list_return]' in log and 'decision=FIRST_TRANSITION' in log and '[standby_first_transition]' in log
            and re.search(r'\[standby_capability\] host=\S+ inc=\d+ ok=true', log) is not None,
            ' | '.join(re.findall(r'\[standby_capability\][^\n]*', log))[:200])
    verdict('E2 A shows 대기 중 (a confirmed entry on a live lease)', st is not None and st[0] == '대기 중', str(st))
    env_note('end')
    for case in ('E3', 'E4', 'E5', 'E6', 'E7', 'E7b', 'E7c', 'E7e', 'E7f', 'E7g', 'E7h', 'E8', 'E9', 'E10', 'E11', 'E12',
                 'E13', 'E13b'):
        skip_case(case, f'plan {"e2-shoff" if sh_off else "e2-sh"}: E1 and E2 only (decoder-liveness r1 (2) / r5)')


def main():
    step(f'ke run apk_off={APK_OFF} apk_on={APK_ON}')
    logcat = None
    try:
        for k in ('window_animation_scale', 'transition_animation_scale', 'animator_duration_scale'):
            adb('shell', 'settings', 'put', 'global', k, '0')
        adb('shell', 'input', 'keyevent', 'KEYCODE_WAKEUP')
        adb('shell', 'wm', 'dismiss-keyguard')
        adb('logcat', '-c')
        digest, version = ka_avd.adb_identity()
        step(f'  adb client {ka_avd.ADB} sha256={digest[:16]} {version} serial={ka_avd.SERIAL}')
        logcat = subprocess.Popen([ka_avd.ADB, '-s', ka_avd.SERIAL, 'logcat', '-b', 'all', '-v', 'threadtime'],
                                  stdout=open(os.path.join(OUT, 'logcat.txt'), 'wb'), stderr=subprocess.DEVNULL)
        # AVD r8 (Codex design r3 D3): logcat's end is said when it happens -- from there on there is NO logcat (a gap),
        # which is not "no events". Its stop at the run's end is ours and says so.
        import threading
        logcat_stopping = threading.Event()

        def _logcat_watch(proc):
            rc = proc.wait()
            if not logcat_stopping.is_set():
                step(f'  LOGCAT ENDED rc={rc} -- no logcat from here (a gap, not an absence of events)')
        threading.Thread(target=_logcat_watch, args=(logcat,), daemon=True).start()
        start_app(APK_OFF, clear=True)

        # E1
        n0 = cursor()
        verdict('E1 signed in; four PCs listed', login())
        time.sleep(4)
        log = since(n0)
        verdict('E1 OFF: the app prepares nothing on its own', '[standby_start]' not in log and '[preconnect_off]' in log,
                ' | '.join(re.findall(r'\[preconnect_off\][^\n]*', log)))

        plan = os.environ.get('KE_PLAN', 'full')
        step(f'plan: {plan}')
        # AVD r9 (Codex src r5): e2-sh -- the default sharpening -- next to e2-shoff; each stops after E2's watch.
        # `full` is never used for these runs (it goes on to E3 and beyond).
        if plan in ('e2-shoff', 'e2-sh'):
            e2_watch(sh_off=plan == 'e2-shoff')
            open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
            return 0 if results and all(ok for _, ok, _ in results) else 1
        if plan != 'full':
            raise RuntimeError(f'unknown KE_PLAN: {plan}')

        # E2 -- A ordinary, then the first transition
        mark('E2')
        tap('card:' + H['a'])
        select('E2 A (ordinary connect)')
        n0, ok = to_list('E2')
        st = wait_card(H['a'], '대기 중', 40)
        log = since(n0)
        shot('E2_list_A_ready')
        verdict('E2 PC 목록: the session ends, the FIRST transition (a new lease, its directory requests logged)',
                ok and '[list_return]' in log and 'decision=FIRST_TRANSITION' in log and '[standby_first_transition]' in log
                and re.search(r'\[standby_capability\] host=\S+ inc=\d+ ok=true', log) is not None,
                ' | '.join(re.findall(r'\[standby_capability\][^\n]*', log))[:200])
        verdict('E2 A shows 대기 중 (a confirmed entry on a live lease)', st is not None and st[0] == '대기 중', str(st))
        incA = re.findall(r'\[standby_start\] host=\S+ inc=(\d+)', log)

        # E3 -- A by activation
        mark('E3')
        n0 = cursor()
        dir0 = count(r'/api/connect|connect=', dirlog())
        tap('card:' + H['a'])
        ms = select('E3 A by activation')
        log = since(n0)
        verdict('E3 activation of THAT lease: no directory connect / observe / punch',
                '[standby_activate]' in log and '[directory_connect]' not in log and '[directory_observed]' not in log and
                '[directory_chosen]' not in log and re.search(r'\[directory_connect_committed\]', log) is not None,
                ' | '.join(re.findall(r'\[standby_activate\][^\n]*', log))[:200])
        shot('E3_viewer_A')

        # E4 -- back to the same lease
        n0, ok = to_list('E4')
        st = wait_card(H['a'], '대기 중', 30)
        log = since(n0)
        verdict('E4 PC 목록 after an activation: back to the same lease (no new lease, no capability fetch)',
                ok and 'decision=BACK_TO_STANDBY' in log and '[standby_return]' in log and '[standby_start]' not in log and
                '[standby_capability_begin]' not in log and st is not None and st[0] == '대기 중', str(st))

        # E5 -- M1: a request on A's session, B, then A again: a new session, no request carried over
        mark('E5')
        tap('card:' + H['a'])
        ka_avd.wait(TARGET, 30)  # A by activation: its targets (the request is made from its settings tab)
        n_req = cursor()
        req_ok = False
        if ka_avd.wait('listSettingsButton', 10):
            tap('listSettingsButton')
            for _ in range(8):
                if ka_avd.wait('settingsDesktopBackendGdiButton', 1):
                    break
                ka_avd.adb('shell', 'input', 'swipe', '540', '1800', '540', '900', '300', check=False)
            if ka_avd.wait('settingsDesktopBackendGdiButton', 2):
                tap('settingsDesktopBackendGdiButton')
                time.sleep(1.5)
                req_ok = '[desktop_backend_request] backend=gdi' in since(n_req)
                shot('E5_settings_A_gdi_requested')
        verdict("E5 a backend request (GDI) is made on A's session", req_ok,
                ' | '.join(re.findall(r'\[desktop_backend_(?:tap|request|failed)\][^\n]*', since(n_req)))[:200])
        to_list('E5 A')
        tap('card:' + H['b'])
        select('E5 B (ordinary)')
        to_list('E5 B')
        wait_card(H['b'], '대기 중', 40)
        n0 = cursor()
        hostA_req0 = count(r'desktop-backend-request seq=\d+ backend=', hostlog('a'))
        tap('card:' + H['a'])
        select('E5 A again by activation')
        time.sleep(3)
        log = since(n0)
        ui = re.findall(r'\[desktop_backend_ui\][^\n]*', log)
        verdict('E5 M1: A again is a NEW session -- its backend state starts with no request, nothing re-sent',
                '[desktop_backend_session_reset] reason=commit' in log and '[desktop_backend_request]' not in log and
                '[park_reuse]' not in log and (not ui or 'asked=none' in ui[-1]) and
                count(r'desktop-backend-request seq=\d+ backend=', hostlog('a')) == hostA_req0,
                (ui[-1] if ui else 'no ui line')[:200])
        shot('E5_viewer_A_again')
        to_list('E5 A again')
        wait_card(H['a'], '대기 중', 30)

        # E6 -- steady reuse B -> A -> B: activations only
        mark('E6')
        n0 = cursor()
        for k in ('b', 'a', 'b'):
            tap('card:' + H[k])
            select(f'E6 {k.upper()} by activation')
            to_list(f'E6 {k.upper()}')
            wait_card(H[k], '대기 중', 30)
        log = since(n0)
        verdict('E6 steady reuse B -> A -> B: 3 activations, no directory connect, no capability fetch',
                count(r'\[standby_activate\]', log) == 3 and '[directory_connect]' not in log and
                '[standby_capability_begin]' not in log and '[directory_observed]' not in log,
                f'activations={count(r"[[]standby_activate[]]", log)}')

        # E7 -- another device takes A while it stands by
        mark('E7')
        n0 = cursor()
        rc = subprocess.run([os.path.join(REL, 'ka_relay_client_test.exe'), '--dir-url', 'http://127.0.0.1:18410',
                             '--id', 'ketest', '--pw', PW, '--host-name', H['a'], '--relay-ip', '10.0.2.2',
                             '--relay-port', '18443', '--sec', '6', '--out', os.path.join(OUT, 'takeover_a.txt')],
                            capture_output=True, timeout=120, env=dict(os.environ, REMOTE60_ALLOW_HOST_E2E='1'))
        step(f'  other device on A: exit={rc.returncode}')
        time.sleep(8)
        log = since(n0)
        st = card(H['a'])
        verdict('E7 another device used A: no automatic re-take (no activation, no connect), A still stands by',
                '[standby_activate]' not in log and '[directory_connect]' not in log and st is not None and st[0] == '대기 중',
                str(st))
        tap('card:' + H['a'])
        select('E7 the user\'s tap takes A by activation')
        to_list('E7 A')
        wait_card(H['a'], '대기 중', 30)

        # ---------------------------------------------------------------- r2 (Codex codex_ka_e_src_r1.md)
        # E7g -- another device takes A while A is ON SCREEN: the session ends, no automatic re-take, back to standby
        mark('E7g')
        tap('card:' + H['a'])
        select('E7g A by activation (on screen)')
        n0 = cursor()
        rc = subprocess.run([os.path.join(REL, 'ka_relay_client_test.exe'), '--dir-url', 'http://127.0.0.1:18410',
                             '--id', 'ketest', '--pw', PW, '--host-name', H['a'], '--relay-ip', '10.0.2.2',
                             '--relay-port', '18443', '--sec', '6', '--out', os.path.join(OUT, 'takeover_a_active.txt')],
                            capture_output=True, timeout=120, env=dict(os.environ, REMOTE60_ALLOW_HOST_E2E='1'))
        step(f'  other device on A (active): exit={rc.returncode}')
        time.sleep(15)
        log = since(n0)
        if not ka_avd.wait('card:' + H['a'], 3):
            ka_avd.adb('shell', 'input', 'keyevent', '4', check=False)
            ka_avd.wait('card:' + H['a'], 10)
        st = wait_card(H['a'], '대기 중', 20)
        verdict('E7g another device took A while it was on screen: the session ended, NO automatic re-take, A stands by',
                re.search(r'\[keep_end\] handle=\d+ host=\S+ reason=peer_lost', log) is not None and
                '[standby_activate]' not in log and '[directory_connect]' not in log and st is not None and st[0] == '대기 중',
                str(st) + ' | ' + ' | '.join(re.findall(r'\[keep_end\][^\n]*', log))[:160])

        # E7b -- held input released by the fence before the session ends; the old session's tail shown as settling
        mark('E7b')
        tap('card:' + H['a'])
        select('E7b A by activation')
        def host_inputs():
            v = re.findall(r'inputEvents=(\d+)', hostlog('a'))
            return int(v[-1]) if v else -1
        held_tapped = False
        if not ka_avd.wait('viewerKeyboardButton', 2):
            ka_avd.adb('shell', 'input', 'tap', '1200', '540')
        if ka_avd.wait('viewerKeyboardButton', 4):
            tap('viewerKeyboardButton')
            if ka_avd.wait('viewerKeyboardTabPcKeys', 6):
                tap('viewerKeyboardTabPcKeys')
                if ka_avd.wait('text:Ctrl', 6):
                    tap('text:Ctrl')
                    held_tapped = True
                    time.sleep(1.5)
        shot('E7b_ctrl_held')
        n0, _ = to_list('E7b')
        settling = None
        end = time.time() + 6
        while time.time() < end and settling is None:
            c = card(H['a'])
            if c and '정리 중' in c[1]:
                settling = c
            time.sleep(0.5)
        time.sleep(3)
        log = since(n0)
        hl = hostlog('a')
        down = hl.rfind('key=17 kind=5')
        up = hl.find('key=17 kind=6', down if down >= 0 else 0)
        ended = hl.find('udp control session ended', up if up >= 0 else 0)
        verdict('E7b Ctrl held, then PC 목록: the key up reached the host BEFORE that session ended (the host\'s own input '
                'log), the session ended through the fence',
                held_tapped and down >= 0 and up > down and (ended < 0 or ended > up) and
                re.search(r'\[list_return_fence\] handle=\d+ begin', log) is not None and
                re.search(r'\[list_return_end\] handle=\d+ how=fenced', log) is not None,
                ' | '.join(re.findall(r'\[list_return_(?:fence|end)\][^\n]*', log))[:200])
        st = wait_card(H['a'], '대기 중', 15)
        verdict('E7b the fence paused the old picture before the end: A stands by, its card claims no cleanup of the PC '
                '("대기 연결 있음 · 누르면 엽니다")', st is not None and st[0] == '대기 중' and '바로' not in st[1] and
                '준비됨' not in st[1], str(st) + ' settling seen=' + str(settling) + ' | ' +
                ' | '.join(re.findall(r'\[standby_(?:back|tail_quiet)\][^\n]*', since(n0)))[:160])
        shot('E7b_list_settling_or_after')

        # E7c -- N1: A's tap held (its socket still coming back), then B: B is the newest intent, A never runs
        mark('E7c')
        pos = centers()
        tap('card:' + H['a'])
        verdict('E7c A by activation: its targets', ka_avd.wait(TARGET, 30) is not None)
        fault('a', 'pong_delay_ms=2000')  # A's fence takes 2 s: its socket is not back yet when A is tapped
        n0 = fast_from_targets([('A (to be held)', pos['a']), ('B (the newest)', pos['b'])])
        fault('a', '')
        select('E7c B (the newest tap)')
        time.sleep(8)
        log = since(n0)
        verdict('E7c N1: A\'s held tap is superseded by the newer tap on B -- A never activates afterwards',
                ('[standby_tap_held]' in log or '[park_tap_held]' in log or '[tap_held_fence]' in log) and
                re.search(r'\[standby_activate\] host=' + re.escape(H['b']), log) is not None and
                re.search(r'\[standby_activate\] host=' + re.escape(H['a']), log) is None and
                '[keep_activate_refused]' not in log,
                ' | '.join(re.findall(r'\[(?:standby_tap_held|park_tap_held|tap_held_superseded|park_tap_resumed)\][^\n]*', log))[:200])
        to_list('E7c B')
        wait_card(H['b'], '대기 중', 30)

        # E7e -- another tap DURING a first transition: B opens, C's new standby still comes up
        mark('E7e')
        tap('card:' + H['c'])
        select('E7e C (ordinary)')
        n0, _ = to_list('E7e C (first transition)')
        tap('card:' + H['b'])  # at once, while C's first lease is still being prepared
        select('E7e B by activation during C\'s first transition')
        log = since(n0)
        verdict('E7e a tap during C\'s first transition: B opens by activation, C\'s lease is not disturbed',
                '[standby_first_transition]' in log and
                re.search(r'\[standby_activate\] host=' + re.escape(H['b']), log) is not None)
        to_list('E7e B')
        st = wait_card(H['c'], '대기 중', 40)
        verdict('E7e ...and C\'s first transition completes: C stands by', st is not None and st[0] == '대기 중', str(st))

        # E7d -- N1: A's tap held, then A's 끊기: the held tap never runs, A is not made again
        mark('E7d')
        pos = centers()
        tap('card:' + H['a'])
        verdict('E7d A by activation: its targets', ka_avd.wait(TARGET, 30) is not None)
        fault('a', 'pong_delay_ms=2000')
        # A's card shows its 끊기 while its lease exists: its position from before (the list does not move)
        n0 = fast_from_targets([('A (to be held)', pos['a']), ('A 끊기', pos.get('a:end', pos['a']))])
        fault('a', '')
        time.sleep(9)
        log = since(n0)
        st = card(H['a'])
        verdict('E7d N1: A\'s held tap, then A\'s 끊기 -- the held tap never runs, A is released and not made again',
                ('[standby_tap_held]' in log or '[park_tap_held]' in log or '[tap_held_fence]' in log) and
                re.search(r'\[standby_activate\] host=' + re.escape(H['a']), log) is None and
                re.search(r'\[standby_release\] host=\S+ reason=user_disconnect', log) is not None and
                '[park_tap_resumed]' not in log and (st is None or st[0] != '대기 중'),
                str(st) + ' | ' + ' | '.join(re.findall(r'\[(?:tap_held_dropped|standby_tap_held|park_tap_held|standby_release)\][^\n]*', log))[:200])

        if not OLD_HOST:
            skip_case('E7f', 'needs the older host (old-host cases NOT RUN since 2026-10-28)')
            skip_case('E8', 'the older host itself (old-host cases NOT RUN since 2026-10-28)')
        if OLD_HOST:
            # E7f -- 끊기 DURING a first transition: its late capability goes nowhere, nothing made again
            mark('E7f')
            # ke_r16: the old host's lease lives ~4 s (unanswered), a UI dump takes ~3 s -- so no dump between the list
            # and the tap: the 끊기 position comes from a card that shows one now (same column, same offset to its name)
            pos = centers()
            ref = next(k for k in H if k + ':end' in pos)
            old_end = (pos[ref + ':end'][0], pos['old'][1] + pos[ref + ':end'][1] - pos[ref][1])
            tap('card:' + H['old'])
            select('E7f old host (ordinary)')
            n0 = cursor()
            menu('viewerMenuHostsButton')
            end = time.time() + 12
            while time.time() < end and '[standby_capability_begin] host=' not in since(n0):
                time.sleep(0.2)
            time.sleep(0.8)  # the list is portrait again; the lease is being prepared (ADMITTING) for ~3 s more
            ka_avd.adb('shell', 'input', 'tap', str(old_end[0]), str(old_end[1]))
            step(f'  fast tap old 끊기 at {old_end[0]},{old_end[1]}')
            time.sleep(10)
            log = since(n0)
            verdict('E7f 끊기 during a first transition: the lease released, nothing provided after it, not made again',
                    re.search(r'\[standby_release\] host=\S+ reason=user_disconnect', log) is not None and
                    log.count('[standby_start]') == 1 and
                    # ke_r18: the Provide itself is logged under the book's lock (standby_provided) -- the capability's own
                    # line is written after it and may land after a 끊기 that came after the Provide
                    re.search(r'\[standby_provided\] host=\S+ inc=\d+ ok=true', log.split('reason=user_disconnect', 1)[-1]) is None,
                    ' | '.join(re.findall(r'\[standby_(?:start|release|capability|provided)\][^\n]*', log))[:320])

            # E8 -- the older host
            mark('E8')
            n0 = cursor()
            tap('card:' + H['old'])
            select('E8 old host (ordinary connect)')
            to_list('E8 old')
            end = time.time() + 40
            while time.time() < end and '[standby_release] host=' not in since(n0):
                time.sleep(1)
            time.sleep(1)
            log = since(n0)
            st = card(H['old'])
            verdict('E8 the older host answers no StandbyHello: unanswered -> its lease released, said so (not taken for old)',
                    re.search(r'\[standby_state\] host=\S+ exists=true state=UNSUPPORTED', log) is not None and
                    re.search(r'\[standby_release\] host=\S+ reason=unanswered', log) is not None and
                    (st is None or st[0] != '대기 중'), str(st))
            n1 = cursor()
            tap('card:' + H['old'])
            select('E8 old host again (ordinary)')
            to_list('E8 old again')
            time.sleep(1.5)
            log = since(n1)
            end = time.time() + 40
            while time.time() < end and count(r'reason=unanswered', since(n1)) < 1:
                time.sleep(1)
            log = since(n1)
            verdict('E8 again: the ordinary connect; PC 목록 tries again (an unanswered lease is not taken for an older '
                    'host) -> unanswered again, released; nothing kept', '[directory_connect]' in log and
                    'decision=FIRST_TRANSITION' in log and count(r'reason=unanswered', log) == 1 and
                    '[standby_activate]' not in log)

        # E9 -- a card's 끊기
        mark('E9')
        n0 = cursor()
        relB0 = count(r'\[standby\] released', hostlog('b'))
        card_end(H['b'])
        time.sleep(4)
        log = since(n0)
        st = card(H['b'])
        verdict('E9 끊기 on B: its lease released, its card is an ordinary one, not made again',
                re.search(r'\[standby_release\] host=\S+ reason=user_disconnect', log) is not None and
                (st is None or st[0] != '대기 중') and '[standby_start]' not in log, str(st))

        # E7h (r3 R1, Codex codex_ka_e_src_r2.md): an ordinary C's "PC 목록" is still fencing (its host holds the Pong
        # 2 s) when C's 끊기 / 모두 끊기 / leaving the app / sign-out comes -- the list return is dropped with it and no
        # lease for C is made afterwards (not by its end, not later)
        def fence_case(tag, action, label, needs_banner=True):  # ke_r20: B stands by in every case (a 끊기 to measure)
            if needs_banner:
                # 모두 끊기 shows while a lease exists: B stands by first (ordinary, then PC 목록 from its targets)
                if (card(H['b']) or ('',))[0] != '대기 중':
                    tap('card:' + H['b'])
                    ka_avd.wait(TARGET, 30)
                    tap('listHostsButton')
                    wait_card(H['b'], '대기 중', 40)
            pos = list_positions()
            if (card(H['c']) or ('', '', False))[2]:
                card_end(H['c'])  # C must connect the ordinary way (no lease of it)
                time.sleep(2)
                pos = list_positions()
            tap('card:' + H['c'])
            verdict(f'E7h {tag}: C (ordinary) -- its targets', ka_avd.wait(TARGET, 30) is not None)
            fault('c', 'pong_delay_ms=2000')
            n0 = cursor()
            tap('listHostsButton')
            time.sleep(0.4)
            if action == 'home':
                ka_avd.adb('shell', 'input', 'keyevent', '3')
                step('  fast HOME')
            else:
                xy = end_position(pos, 'c') if action == 'c:end' else pos[action]
                ka_avd.adb('shell', 'input', 'tap', str(xy[0]), str(xy[1]))
                step(f'  fast tap {label} at {xy[0]},{xy[1]}')
            fault('c', '')
            time.sleep(8)
            if action == 'home':
                ka_avd.adb('shell', 'am', 'start', '-n', f'{PKG}/.MainActivity')
                time.sleep(4)
            if action == 'hostsLogoutButton':
                login()
                time.sleep(3)
            log = since(n0)
            verdict(f'E7h {tag}: {label} during C\'s fence -- the list return dropped, no lease for C made afterwards',
                    '[list_return_fence] handle=' in log and ' begin' in log and '[list_return_dropped]' in log and
                    '[standby_first_transition]' not in log and
                    re.search(r'\[standby_start\] host=\S+ inc=\d+ reason=first_transition', log) is None and
                    '[list_return_end]' not in log,
                    ' | '.join(re.findall(r'\[(?:list_return_fence|list_return_dropped|list_return_end|standby_start|'
                                          r'standby_start_skipped|standby_first_transition|keep_end_all)\][^\n]*', log))[:400])

        mark('E7h')
        fence_case('a', 'c:end', 'C 끊기')
        fence_case('b', 'hostsParkDisconnectButton', '모두 끊기')
        fence_case('c', 'home', 'leaving the app (HOME)')
        fence_case('d', 'hostsLogoutButton', '로그아웃')

        # E10 -- leaving the app
        mark('E10')
        # ke_r17: after E7c..E9 no lease is left (C went in E7f, out of the recent three) -- one made here first, so
        # leaving has something to release
        tap('card:' + H['c'])
        select('E10 C (ordinary)')
        to_list('E10 C')
        verdict('E10 C stands by before leaving', (wait_card(H['c'], '대기 중', 40) or ('',))[0] == '대기 중')
        n0 = cursor()
        ka_avd.adb('shell', 'input', 'keyevent', '3')
        time.sleep(5)
        ka_avd.adb('shell', 'am', 'start', '-n', f'{PKG}/.MainActivity')
        time.sleep(6)
        log = since(n0)
        verdict('E10 leaving the app releases every lease; back in front (OFF): nothing prepared',
                re.search(r'\[standby_release_all\] reason=app_stopped leases=[1-9]', log) is not None and
                '[standby_start]' not in log, ' | '.join(re.findall(r'\[standby_release_all\][^\n]*', log)))

        # E13 (keepalive-r2 e, Codex ka_e r3 decision 2) -- an activation that FAILS for real (the pinned test host's
        # fail_activation_once fault: that one StandbyActivate refused as an unknown entry), then the same tap's ordinary
        # connect. First the positive control: the same steps, no fault armed -> the activation succeeds.
        mark('E13')
        fault_c = os.path.join(ROOT, 'c', 'fault.txt')
        open(fault_c, 'w', encoding='ascii').write('')  # nothing armed
        tap('card:' + H['c'])
        select('E13 control: C (ordinary)')
        to_list('E13 control C')
        ready = (wait_card(H['c'], '대기 중', 40) or ('',))[0] == '대기 중'
        n0 = cursor()
        tap('card:' + H['c'])
        select('E13 control: C by activation')
        log = since(n0)
        verdict('E13 control (no fault): C stands by, the tap activates its lease -- no failure, no ordinary connect',
                ready and '[standby_activate]' in log and re.search(r'\[directory_connect_started\] \S+ \S+ gen=[1-9]\d* standby=1', log)
                is not None and '[standby_activate_failed]' not in log and
                'fail_activation applied' not in hostlog('c'),
                ' | '.join(re.findall(r'\[(?:standby_activate|standby_activate_failed|directory_connect_started)\][^\n]*', log))[:300])
        to_list('E13 C again')
        ready = (wait_card(H['c'], '대기 중', 40) or ('',))[0] == '대기 중'
        token = 'e13-' + str(int(time.time()))
        open(fault_c, 'w', encoding='ascii').write(f'fail_activation_once={token}\n')  # this episode only
        step(f'  armed fail_activation_once={token} on C')
        n0 = cursor()
        hl0 = len(hostlog('c'))
        tap('card:' + H['c'])
        select('E13 C: activation fails, the same tap connects ordinarily')
        log = since(n0)
        hl = hostlog('c')[hl0:]
        open(fault_c, 'w', encoding='ascii').write('')  # disarmed (the token is spent anyway)
        verdict('E13 the fault applied once on C (its log names this token)',
                ready and hl.count('fault: fail_activation applied token=' + token) == 1, hl[-300:])
        # AVD guard r2 N3: the failure may come before the generation (gen=0) or -- what the Unknown answer does -- after
        # it, at the establish step (step=establish): either is this tap's (mine=true); the phase it reached is recorded
        fails = re.findall(r'\[standby_activate_failed\] handle=\d+ op=\d+ gen=(\d+) why=\S* mine=(\w+)( step=establish)?', log)
        phase = ('establish' if fails and fails[0][2] else 'pre-adopt') if fails else 'none'
        verdict('E13 the failure is real and still this tap\'s (one failure, mine=true; its phase recorded)',
                len(fails) == 1 and fails[0][1] == 'true' and (fails[0][2] != '' or fails[0][0] == '0'),
                f'phase={phase} ' + ' | '.join(re.findall(r'\[standby_activate_failed\][^\n]*', log))[:200])
        after = log.split('[standby_activate_failed]', 1)[-1]
        verdict('E13 the same tap\'s ordinary connect, ONCE, opens C (a directory connect after the failure, its own '
                'generation, the first frame reached)',
                after.count('[directory_connect] ') == 1 and
                re.search(r'\[directory_connect_started\] \S+ \S+ gen=[1-9]\d*(?!\d| standby=1)', after) is not None and
                '[select_ready]' in after,
                ' | '.join(re.findall(r'\[(?:directory_connect|directory_connect_started|standby_release|select_ready)\][^\n]*', after))[:300])
        to_list('E13 C after the fallback')

        # E13b -- the same fault, but the user taps ANOTHER PC (B) right after C: C's failed activation is no longer the
        # current tap -- no fallback for C (no ordinary connect of C), B opens by its own tap
        ready = (wait_card(H['c'], '대기 중', 40) or ('',))[0] == '대기 중'
        token = 'e13b-' + str(int(time.time()))
        open(fault_c, 'w', encoding='ascii').write(f'fail_activation_once={token}\n')
        step(f'  armed fail_activation_once={token} on C (then a new tap on B)')
        pos = centers()
        n0 = cursor()
        hl0 = len(hostlog('c'))
        tap('card:' + H['c'])
        if 'b' in pos:
            ka_avd.adb('shell', 'input', 'tap', str(pos['b'][0]), str(pos['b'][1]))  # the new tap, at once (no dump)
            step(f'  fast tap B at {pos["b"][0]},{pos["b"][1]}')
        select('E13b B (the new tap)')
        log = since(n0)
        open(fault_c, 'w', encoding='ascii').write('')
        hl = hostlog('c')[hl0:]
        c_id = re.search(r'\[standby_activate\] host=' + re.escape(H['c']) + r' id=(\S+) handle=(\d+)', log)
        c_handle = c_id.group(2) if c_id else None
        verdict('E13b the fault applied on C (its activation refused)', ready and c_handle is not None and
                hl.count('fault: fail_activation applied token=' + token) == 1, hl[-200:])
        verdict('E13b no fallback for C: its failure is not the current tap (no ordinary connect of C after it)',
                c_handle is not None and
                re.search(r'\[standby_activate_failed\] handle=' + c_handle + r' \S+ \S+ why=\S* mine=true', log) is None and
                re.search(r'\[directory_connect\] [^\n]*host=' + re.escape(H['c']), log.split('[standby_activate]', 1)[-1]) is None,
                ' | '.join(re.findall(r'\[(?:standby_activate_failed|directory_connect_stale|directory_connect)\][^\n]*', log))[:300])
        to_list('E13b B')

        # E11 -- sign-out with a lease
        mark('E11')
        ka_avd.wait('card:' + H['a'], 20)
        tap('card:' + H['c'])
        select('E11 C (ordinary)')
        to_list('E11 C')
        wait_card(H['c'], '대기 중', 40)
        n0 = cursor()
        tap('hostsLogoutButton')
        time.sleep(4)
        log = since(n0)
        verdict('E11 sign-out with a lease: released', re.search(r'\[standby_release_all\] reason=sign_out leases=1', log)
                is not None, ' | '.join(re.findall(r'\[standby_release_all\][^\n]*', log)))

        if not OLD_HOST:
            skip_case('E12', 'its recent three include the older host (old-host cases NOT RUN since '
                             '2026-10-28); to be redesigned without it')
        if OLD_HOST:
            # E12 -- the ON build: the recent PCs on their own
            mark('E12')
            verdict('E12 signed in again (OFF)', login())
            time.sleep(3)
            ka_avd.adb('shell', 'input', 'keyevent', '3')
            time.sleep(3)
            n0 = cursor()
            start_app(APK_ON, clear=False)
            if ka_avd.wait('loginIdInput', 5):
                login()
            end = time.time() + 60
            while time.time() < end and count(r'\[standby_capability\] host=\S+ inc=\d+ ok=true', since(n0)) < 3:
                time.sleep(2)
            time.sleep(3)
            log = since(n0)
            states = {k: card(H[k]) for k in ('a', 'b', 'c', 'old')}
            # the recent three by now (E8 used the older host twice, E11 used C): C, old, A -- B dropped out of them
            verdict('E12 ON: the recent PCs are prepared on their own (C, old, A -- not B, no longer recent); A and C '
                    'READY, the older host unanswered and released; the screen untouched',
                    count(r'\[standby_start\] host=\S+ inc=\d+ reason=auto_', log) == 3 and
                    states['a'] is not None and states['a'][0] == '대기 중' and
                    states['c'] is not None and states['c'][0] == '대기 중' and
                    states['b'] is not None and states['b'][0] != '대기 중' and
                    re.search(r'\[standby_release\] host=\S+ reason=unanswered', log) is not None and
                    '[standby_activate]' not in log, str(states))
            shot('E12_list_auto')
            tap('card:' + H['a'])
            select('E12 A by activation (prepared on its own)')
            log = since(n0)
            verdict('E12 the tap activates the prepared lease', '[standby_activate]' in log and '[directory_connect]' not in log)
            to_list('E12 A')
        open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
    except Exception:
        # AVD r5 (ke-r22): the exception itself, whole, in the run folder -- and what the screen showed -- before it
        # propagates (the bundle keeps stderr too, avd_stderr.txt). Never a password: adb errors are redacted.
        tb = traceback.format_exc()
        open(os.path.join(OUT, 'avd_traceback.txt'), 'w', encoding='utf-8').write(tb)
        step('EXCEPTION ' + tb.strip().splitlines()[-1][:300] + ' (in full: avd_traceback.txt)')
        try:
            shot('exception')
            step('  screenshot at the exception: exception.png')
        except Exception as e:
            step(f'  no screenshot at the exception ({e})')
        raise
    finally:
        if logcat:
            try:
                logcat_stopping.set()
            except NameError:
                pass
            logcat.terminate()
        ka_avd.adb('shell', 'am', 'force-stop', PKG, check=False)
        json.dump({'results': results, 'not_run': not_run, 'marks': marks}, open(os.path.join(OUT, 'result.json'), 'w'),
                  indent=1)
        step(f'run end: {sum(1 for _, ok, _ in results if ok)}/{len(results)} PASS, {len(not_run)} NOT RUN')
    return 0 if results and all(ok for _, ok, _ in results) else 1


if __name__ == '__main__':
    sys.exit(main())
