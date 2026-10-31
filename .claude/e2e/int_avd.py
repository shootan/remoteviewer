"""int/apk-0.2.30: the short AVD bundle on the integrated fixture APK (gnlink_kbd_test / emulator-5614, a cold
-read-only boot, `pm clear`), against int_fixture.ps1's fail-closed pinned TEST host. Every tap is on a shown, enabled
element (ka_avd). logcat is kept, so each new codec's first input (key=1) can be read.

  1 cold first selection -> first frame (select_ready)
  2 viewer menu BEST -> [quality_select] 6000 sent; the host's runtime-config-applied bitrate=6000000 is matched after
  3 viewer menu PC 목록 (park) -> HOSTS -> the same host -> park_reuse (no new connect) -> a new selection -> first frame
  4 viewer menu 연결 해제 -> HOSTS, then sign-out -> the login screen

  python -I int_avd.py <fixture_root> <apk> <out_dir>
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
HOST = 'ka-test-pc'
TARGET = 'tcard:remote60 mobile abr'
PKG = ka_avd.PKG
STEPS = open(os.path.join(OUT, 'steps.txt'), 'a', encoding='utf-8')
results = []


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
    return ka_avd.wait('card:' + HOST, 30) is not None


def select(tag):
    n0 = applog().count('[select_ready]')
    if not ka_avd.wait(TARGET, 30):
        verdict(f'{tag}: the target is shown', False)
        return False
    tap(TARGET)
    t0 = time.time()
    while time.time() - t0 < 25:
        log = applog()
        if log.count('[select_ready]') > n0:
            ms = re.findall(r'\[select_ready\].* ms=(-?\d+)', log)[-1]
            verdict(f'{tag}: selection -> first frame', True, f'{ms} ms')
            return True
        time.sleep(0.5)
    verdict(f'{tag}: selection -> first frame within 25 s', False)
    return False


def menu(button):
    if not ka_avd.wait(button, 2):
        tap('viewerMenuButton')
        ka_avd.wait(button, 8)
    tap(button)


def counters():
    hits = re.findall(r'(connects=\d+ hellos=\d+ observes=\d+ punches=\d+)', applog())
    return hits[-1] if hits else ''


def main():
    step(f'int run apk={APK}')
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
        verdict('signed in', login())
        tap('card:' + HOST)
        select('1 cold first selection')
        c_first = counters()

        before = len(re.findall(r'\[quality_select\]', applog()))
        menu('viewerQualityBestButton')
        time.sleep(1.5)
        qs = re.findall(r'\[quality_select\] origin=(\w+) bitrateKbps=(\d+) fps=\d+ sent=(\w+)', applog())
        verdict('2 viewer menu BEST: quality_select origin=menu 6000 sent=true (queued)',
                len(qs) > before and qs[-1] == ('menu', '6000', 'true'), str(qs[-1:]))
        time.sleep(3)

        menu('viewerMenuHostsButton')
        parked = ka_avd.wait('card:' + HOST, 15) is not None
        time.sleep(2)
        verdict('3 PC 목록 -> HOSTS, the session parked', parked and '[park_begin]' in applog())
        tap('card:' + HOST)
        time.sleep(3)
        reused = '[park_reuse]' in applog()
        verdict('3 the same host again: park_reuse, no new connect / observe / punch / hello',
                reused and counters() == c_first, f'{c_first} -> {counters()}')
        select('3 a new selection on the reused connection')

        menu('viewerMenuDisconnectButton')
        # the product asks first (confirmDisconnectFromViewer): its positive button, by id
        ka_avd.wait('android:id/button1', 8)
        tap('android:id/button1')
        home = ka_avd.wait('card:' + HOST, 15) is not None
        verdict('4 연결 해제 (confirmed) -> HOSTS, the session ended', home and '[viewer_disconnect]' in applog() and '[disconnect_tap]' in applog())
        tap('hostsLogoutButton')
        verdict('4 sign-out -> the login screen', ka_avd.wait('loginIdInput', 15) is not None)
        open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
    finally:
        if logcat:
            logcat.terminate()
        adb('emu', 'kill', timeout=30)
        try:
            emu.wait(60)
        except subprocess.TimeoutExpired:
            emu.kill()
        # every codec this run started, and its first input
        try:
            lc = open(os.path.join(OUT, 'logcat.txt'), encoding='utf-8', errors='replace').read()
            firsts = re.findall(r'(\d\d:\d\d:\d\d\.\d+).*remote60_android_direct: (MediaCodec started.*|queued h264 frame count=1 .*|'
                                r'selection first output.*|delta before the codec.*)', lc)
            for t, line in firsts:
                step(f'  logcat {t} {line[:90]}')
            starts = sum(1 for _, l in firsts if l.startswith('MediaCodec started'))
            first_inputs = [l for _, l in firsts if l.startswith('queued h264 frame count=1 ')]
            verdict('every new codec\'s first input is an IDR (key=1)',
                    starts > 0 and len(first_inputs) == starts and all(' key=1 ' in l for l in first_inputs),
                    f'{starts} codecs, first inputs {[l.split(" key=")[1][:1] for l in first_inputs]}')
        except Exception as e:
            verdict('logcat read', False, str(e))
        json.dump(results, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        step(f'run end: {sum(1 for _, ok, _ in results if ok)}/{len(results)} PASS')
    return 0 if results and all(ok for _, ok, _ in results) else 1


if __name__ == '__main__':
    sys.exit(main())
