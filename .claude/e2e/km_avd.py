"""keepalive-multi r1: the real APK UI on the isolated AVD (gnlink_kbd_test / emulator-5614, cold, read-only, `pm clear`)
against km_fixture.ps1's two fail-closed pinned TEST hosts km-pc-a (magenta square) and km-pc-b (cyan square). Every
tap is on a shown, enabled element (ka_avd); every counter comes from the app's own diagnostics log, per entry handle.

  1 sign in; both PCs listed
  2 A: new connect -> first selection -> first frame (screenshot)                       [baseline: new connect]
  3 PC 목록: A parked (confirmed); its card says kept, with its own 끊기
  4 B: a NEW connect beside A (A not ended) -> selection -> first frame (screenshot)
  5 PC 목록: B parked; A and B both kept
  6 A again: reuse of A's own entry -- no new connect / observe / punch / Hello for it -- selection -> first frame
  7 PC 목록, B again: reuse of B -> selection -> first frame
  8 PC 목록, A's own 끊기: A ends (user_disconnect), B stays kept
  9 B again: still a reuse after A's ending
 10 연결 해제 (B) -> HOSTS; sign-out -> login

  python -I km_avd.py <fixture_root> <apk> <out_dir>
"""
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('KA_SERIAL', 'emulator-5614')
import ka_avd  # noqa: E402

ROOT, APK, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(OUT, exist_ok=True)
SDK = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk')
EMU = os.path.join(SDK, 'emulator', 'emulator.exe')
_, PW = open(os.path.join(ROOT, 'fixture_account.txt'), encoding='ascii').read().split()
A, B = 'km-pc-a', 'km-pc-b'
TARGET = 'tcard:remote60 mobile abr'
PKG = ka_avd.PKG
STEPS = open(os.path.join(OUT, 'steps.txt'), 'a', encoding='utf-8')
results = []
timing = {}


def step(msg):
    line = f'{time.strftime("%H:%M:%S")} {msg}'
    print(line, flush=True)
    STEPS.write(line + '\n')
    STEPS.flush()


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


def tap(key):
    step(ka_avd.tap(key))


def shot(name):
    png = ka_avd.adb('exec-out', 'screencap', '-p', binary=True)
    p = os.path.join(OUT, name + '.png')
    open(p, 'wb').write(png)
    step(f'  screenshot {p}')


def login():
    if not ka_avd.wait('loginIdInput', 40):
        return False
    tap('loginIdInput')
    ka_avd.adb('shell', 'input', 'keycombination', '113', '29', check=False)
    ka_avd.adb('shell', 'input', 'keyevent', '67', check=False)
    ka_avd.adb('shell', 'input', 'text', 'katest')
    tap('loginPasswordInput')
    ka_avd.adb('shell', 'input', 'text', PW)
    ka_avd.adb('shell', 'input', 'keyevent', '4', check=False)
    tap('loginButton')
    return ka_avd.wait('card:' + A, 30) is not None and ka_avd.wait('card:' + B, 10) is not None


def select(tag):
    n0 = applog().count('[select_ready]')
    if not ka_avd.wait(TARGET, 30):
        verdict(f'{tag}: the target is shown', False)
        return None
    tap(TARGET)
    t0 = time.time()
    while time.time() - t0 < 25:
        log = applog()
        if log.count('[select_ready]') > n0:
            ms = int(re.findall(r'\[select_ready\].* ms=(-?\d+)', log)[-1])
            verdict(f'{tag}: selection -> first frame', True, f'{ms} ms')
            time.sleep(1.0)
            return ms
        time.sleep(0.5)
    verdict(f'{tag}: selection -> first frame within 25 s', False)
    return None


def menu(button):
    if not ka_avd.wait(button, 2):
        tap('viewerMenuButton')
        ka_avd.wait(button, 8)
    tap(button)


def entry_counters(handle):
    """The last 'handle=H gen=.. connects=.. hellos=.. observes=.. punches=..' the app logged for that entry."""
    hits = re.findall(r'handle=%s gen=(\d+) connects=(\d+) hellos=(\d+) observes=(\d+) punches=(\d+)' % handle, applog())
    return hits[-1] if hits else None


def handle_of(host_name, log=None):
    log = log if log is not None else applog()
    hits = re.findall(r'\[directory_connect\] host=%s id=\S+ handle=(\d+)' % re.escape(host_name), log)
    return hits[-1] if hits else None


def last_targets_ready(origin):
    hits = re.findall(r'\[targets_ready\] origin=%s ms=(\d+)' % origin, applog())
    return int(hits[-1]) if hits else None


def card_button(host_name, button_id):
    """The button of that host's card (the row whose name is host_name), shown and enabled."""
    nodes = ka_avd.dump()
    name = next((n for n in nodes if n['id'] == 'hostCardName' and n['text'] == host_name), None)
    if not name:
        return None
    y1, y2 = name['bounds'][1], name['bounds'][3]
    for n in nodes:
        if n['id'] == button_id and n['bounds'][1] <= y2 + 40 and n['bounds'][3] >= y1 - 40 and n['enabled']:
            return n
    return None


def card_detail(host_name):
    nodes = ka_avd.dump()
    name = next((n for n in nodes if n['id'] == 'hostCardName' and n['text'] == host_name), None)
    if not name:
        return ''
    y1, y2 = name['bounds'][1], name['bounds'][3]
    for n in nodes:
        if n['id'] in ('hostCardDetail',) and n['bounds'][1] >= y1 - 5 and n['bounds'][1] <= y2 + 60:
            return n['text']
    return ''


def to_hosts(tag):
    menu('viewerMenuHostsButton')
    ok = ka_avd.wait('card:' + A, 15) is not None
    t0 = time.time()
    confirmed = False
    while time.time() - t0 < 6:
        if applog().count('[park_confirmed]') > to_hosts.confirms:
            confirmed = True
            break
        time.sleep(0.5)
    to_hosts.confirms = applog().count('[park_confirmed]')
    verdict(f'{tag}: PC 목록 -> HOSTS, the PC on screen parked and confirmed', ok and confirmed)


to_hosts.confirms = 0


def main():
    step(f'km run apk={APK}')
    t0 = time.time()
    emu = subprocess.Popen([EMU, '-avd', 'gnlink_kbd_test', '-read-only', '-no-snapshot', '-no-window', '-no-audio',
                            '-no-boot-anim', '-port', '5614', '-gpu', 'swiftshader_indirect'],
                           stdout=open(os.path.join(OUT, 'emulator.log'), 'w'),
                           stderr=open(os.path.join(OUT, 'emulator.err'), 'w'))
    logcat = None
    try:
        adb('wait-for-device', timeout=200)
        end = time.time() + 300
        while time.time() < end and adb('shell', 'getprop', 'sys.boot_completed').strip() != '1':
            time.sleep(3)
        step(f'booted (cold) in {int(time.time() - t0)} s')
        step('install: ' + subprocess.run([ka_avd.ADB, '-s', ka_avd.SERIAL, 'install', '-r', '-d', APK],
                                          capture_output=True, timeout=180).stdout.decode(errors='replace').strip())
        for k in ('window_animation_scale', 'transition_animation_scale', 'animator_duration_scale'):
            adb('shell', 'settings', 'put', 'global', k, '0')
        ka_avd.adb('shell', 'pm', 'clear', PKG, check=False)
        adb('logcat', '-c')
        logcat = subprocess.Popen([ka_avd.ADB, '-s', ka_avd.SERIAL, 'logcat', '-b', 'all', '-v', 'threadtime'],
                                  stdout=open(os.path.join(OUT, 'logcat.txt'), 'wb'), stderr=subprocess.DEVNULL)
        ka_avd.adb('shell', 'am', 'start', '-n', f'{PKG}/.MainActivity')
        time.sleep(3)
        verdict('1 signed in; both PCs listed', login())

        # 2 A, a new connection
        tap('card:' + A)
        ms = select('2 A new connect')
        hA = handle_of(A)
        timing['A_connect_targets_ms'] = last_targets_ready('connect')
        timing['A_first_select_ms'] = ms
        shot('2_viewer_A')
        cA = entry_counters(hA)
        step(f'  A handle={hA} counters={cA}')

        # 3 A parked
        to_hosts('3')
        time.sleep(1.0)
        detail = card_detail(A)
        btn = card_button(A, 'hostCardDisconnect')
        verdict('3 A\'s card says kept, with its own 끊기', '남음' in detail and btn is not None, repr(detail))
        shot('3_hosts_A_kept')

        # 4 B, a new connection beside A
        tap('card:' + B)
        ms = select('4 B new connect (A kept)')
        hB = handle_of(B)
        timing['B_connect_targets_ms'] = last_targets_ready('connect')
        timing['B_first_select_ms'] = ms
        shot('4_viewer_B')
        log = applog()
        verdict('4 B is a different entry; A was not ended by it',
                hB is not None and hB != hA and not re.search(r'\[keep_end\] handle=%s ' % hA, log), f'A={hA} B={hB}')
        cB = entry_counters(hB)

        # 5 B parked: both kept
        to_hosts('5')
        time.sleep(1.0)
        both = card_button(A, 'hostCardDisconnect') is not None and card_button(B, 'hostCardDisconnect') is not None
        verdict('5 A and B both kept (each card with its own 끊기)', both, f'A={card_detail(A)!r} B={card_detail(B)!r}')
        shot('5_hosts_both_kept')

        # 6 A again: reuse
        n_reuse = applog().count('[park_reuse]')
        tap('card:' + A)
        time.sleep(2)
        log = applog()
        reuse_lines = re.findall(r'\[park_reuse\] (.*)', log)
        cA2 = entry_counters(hA)
        verdict('6 A again: park_reuse of A\'s own entry, no new connect / observe / punch / Hello for it',
                log.count('[park_reuse]') > n_reuse and f'handle={hA} ' in reuse_lines[-1] and cA2 == cA,
                f'{cA} -> {cA2}')
        ms = select('6 A reused')
        timing['A_reuse_targets_ms'] = last_targets_ready('reuse')
        timing['A_reuse_select_ms'] = ms
        shot('6_viewer_A_again')
        # 6b routing: a tap on the picture and 화질 BEST while A is on screen (B parked) -- km_host_route.py checks the
        # hosts' own logs: A gets the input-trace and runtime-config 6000000 in this window, B nothing.
        timing['route_A_start'] = time.time()
        ka_avd.adb('shell', 'input', 'tap', '1200', '540')
        time.sleep(1.0)
        menu('viewerQualityBestButton')
        time.sleep(3.0)
        timing['route_A_end'] = time.time()
        step('6b routed a tap and BEST to A (host logs judge it)')

        # 7 B again
        to_hosts('7')
        n_reuse = applog().count('[park_reuse]')
        tap('card:' + B)
        time.sleep(2)
        log = applog()
        reuse_lines = re.findall(r'\[park_reuse\] (.*)', log)
        cB2 = entry_counters(hB)
        verdict('7 B again: park_reuse of B\'s own entry, its counters unchanged',
                log.count('[park_reuse]') > n_reuse and f'handle={hB} ' in reuse_lines[-1] and cB2 == cB, f'{cB} -> {cB2}')
        ms = select('7 B reused')
        timing['B_reuse_targets_ms'] = last_targets_ready('reuse')
        timing['B_reuse_select_ms'] = ms
        shot('7_viewer_B_again')
        time.sleep(2.0)  # B's resume applies the user's current setting (BEST) to B -- before the window
        timing['route_B_start'] = time.time()
        ka_avd.adb('shell', 'input', 'tap', '1200', '540')
        time.sleep(2.0)
        timing['route_B_end'] = time.time()
        step('7b routed a tap to B (host logs judge it)')

        # 8 A's own 끊기
        to_hosts('8')
        btn = card_button(A, 'hostCardDisconnect')
        if btn:
            x1, y1, x2, y2 = btn['bounds']
            ka_avd.adb('shell', 'input', 'tap', str((x1 + x2) // 2), str((y1 + y2) // 2))
            step(f'tapped A\'s 끊기 at {(x1 + x2) // 2},{(y1 + y2) // 2}')
        time.sleep(1.5)
        log = applog()
        verdict('8 A\'s 끊기 ends A only; B still kept',
                btn is not None and re.search(r'\[keep_end\] handle=%s host=%s reason=user_disconnect' % (hA, A), log) is not None
                and card_button(A, 'hostCardDisconnect') is None and card_button(B, 'hostCardDisconnect') is not None)
        shot('8_hosts_A_ended')

        # 9 B again after A's ending
        n_reuse = applog().count('[park_reuse]')
        tap('card:' + B)
        time.sleep(2)
        log = applog()
        cB3 = entry_counters(hB)
        verdict('9 B still a reuse after A ended', log.count('[park_reuse]') > n_reuse and cB3 == cB, f'{cB} -> {cB3}')
        select('9 B reused again')

        # 10 연결 해제 + sign-out
        menu('viewerMenuDisconnectButton')
        ka_avd.wait('android:id/button1', 8)
        tap('android:id/button1')
        home = ka_avd.wait('card:' + B, 15) is not None
        verdict('10 연결 해제 -> HOSTS', home and re.search(r'\[keep_end\] handle=%s reason=' % hB, applog()) is not None)
        tap('hostsLogoutButton')
        verdict('10 sign-out -> the login screen', ka_avd.wait('loginIdInput', 15) is not None)
        open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
    finally:
        if logcat:
            logcat.terminate()
        adb('emu', 'kill', timeout=30)
        try:
            emu.wait(60)
        except subprocess.TimeoutExpired:
            emu.kill()
        try:
            lc = open(os.path.join(OUT, 'logcat.txt'), encoding='utf-8', errors='replace').read()
            binds = re.findall(r'remote60_android_direct: (decoder (?:bound binding=\d+|unbound))', lc)
            step('  logcat decoder bindings: ' + ' | '.join(binds))
            firsts = re.findall(r'(\d\d:\d\d:\d\d\.\d+).*remote60_android_direct: (MediaCodec started.*|queued h264 frame count=1 .*)', lc)
            starts = sum(1 for _, l in firsts if l.startswith('MediaCodec started'))
            first_inputs = [l for _, l in firsts if l.startswith('queued h264 frame count=1 ')]
            verdict('every new codec\'s first input is an IDR (key=1)',
                    starts > 0 and len(first_inputs) == starts and all(' key=1 ' in l for l in first_inputs),
                    f'{starts} codecs, first inputs {[l.split(" key=")[1][:1] for l in first_inputs]}')
        except Exception as e:
            verdict('logcat read', False, str(e))
        json.dump({'results': results, 'timing': timing}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        step(f'timing {timing}')
        step(f'run end: {sum(1 for _, ok, _ in results if ok)}/{len(results)} PASS')
    return 0 if results and all(ok for _, ok, _ in results) else 1


if __name__ == '__main__':
    sys.exit(main())
