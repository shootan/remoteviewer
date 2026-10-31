"""keepalive-multi r2 V1: the real APK UI on the isolated AVD against km2_fixture.ps1's six fail-closed pinned TEST
hosts. Product policy (3 PCs, 5 min). Every tap on a shown, enabled element (ka_avd); judged from the app's own
diagnostics log here, and from the hosts' own logs afterwards (km2_hosts.py).

  P1 sign in, six PCs; baseline cost (nothing kept)
  P2 A new, first frame, PC 목록 (A kept)
  P3 M2: /connect held 6 s -- tap B, then A: A resumed at once, B superseded (its late answer adopts nothing)
  P4 M2: /connect held 6 s -- tap C, then B: B connects, C superseded; PC 목록 (A, B kept)
  P5 old host: km-pc-old connects, says it cannot be kept, no PC 목록; 연결 해제 -> A, B still kept
  P6 another device takes B over the relay (ka_relay_client_test): B ends (peer_lost), no reconnect, A kept
  P7 B again (new), 크게보기 + BEST on B; C new (viewer not large, BEST applied to C); PC 목록 -> A, B, C kept (limit)
  P8 M1: X's host stopped -- tap X: started, never established -> given back, 0 evicted, A, B, C kept
  P9 M1 + LRU + direct: D (direct path) established -> exactly A (LRU) evicted; first frame; PC 목록
  P10 cost at the limit (3 kept, list): app threads / memory, device rx/tx bytes
  P11 /connect held 8 s -- tap A, then leave the app (HOME): everything ends (epoch), the late answer adopts nothing
  P12 /connect held 8 s -- tap A, then sign out: the late answer adopts nothing
  P13 sign in; C new, park; D new; the network goes (airplane): everything ends

  python -I km2_avd.py <fixture_root> <apk> <out_dir>
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
REL = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build-local', 'apps', 'native_poc', 'Release')
H = {k: f'km-pc-{k}' for k in ('a', 'b', 'c', 'd', 'x', 'old')}
TARGET = 'tcard:remote60 mobile abr'
PKG = ka_avd.PKG
STEPS = open(os.path.join(OUT, 'steps.txt'), 'a', encoding='utf-8')
results = []
marks = {}
cost = {}


def step(msg):
    line = f'{time.strftime("%H:%M:%S")} {msg}'
    print(line, flush=True)
    STEPS.write(line + '\n')
    STEPS.flush()


def mark(name):
    marks[name] = time.time()


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
    # the last line logged so far -- the app's log rotates, so an offset into it is not stable
    lines = [l for l in applog().splitlines() if l.strip()]
    return lines[-1] if lines else ''


def since(cur):
    log = applog()
    i = log.rfind(cur) if cur else -1
    return log[i + len(cur):] if i >= 0 else log


def tap(key):
    step(ka_avd.tap(key))


def shot(name):
    png = ka_avd.adb('exec-out', 'screencap', '-p', binary=True)
    open(os.path.join(OUT, name + '.png'), 'wb').write(png)


def delay(ms):
    open(os.path.join(ROOT, 'delay_connect'), 'w').write(str(ms))
    step(f'  /connect held {ms} ms')


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
    return all(ka_avd.wait('card:' + H[k], 30 if k == 'a' else 5) is not None for k in H)


def select(tag):
    n0 = cursor()
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
    if not ka_avd.wait(button, 2):
        if not ka_avd.wait('viewerMenuButton', 2):
            # the viewer's controls faded: one tap on the picture brings them back
            ka_avd.adb('shell', 'input', 'tap', '1200', '540')
            ka_avd.wait('viewerMenuButton', 4)
        tap('viewerMenuButton')
        ka_avd.wait(button, 8)
    tap(button)


def handle_of(host_name, log=None):
    log = log if log is not None else applog()
    hits = re.findall(r'\[directory_connect\] host=%s id=\S+ handle=(\d+)' % re.escape(host_name), log)
    return hits[-1] if hits else None


def card_button(host_name, button_id='hostCardDisconnect'):
    nodes = ka_avd.dump()
    name = next((n for n in nodes if n['id'] == 'hostCardName' and n['text'] == host_name), None)
    if not name:
        return None
    y1, y2 = name['bounds'][1], name['bounds'][3]
    for n in nodes:
        if n['id'] == button_id and n['bounds'][1] <= y2 + 40 and n['bounds'][3] >= y1 - 40 and n['enabled']:
            return n
    return None


def kept_set():
    nodes = ka_avd.dump()
    out = set()
    for k, name in H.items():
        nm = next((n for n in nodes if n['id'] == 'hostCardName' and n['text'] == name), None)
        if not nm:
            continue
        y1, y2 = nm['bounds'][1], nm['bounds'][3]
        if any(n['id'] == 'hostCardDisconnect' and n['bounds'][1] <= y2 + 40 and n['bounds'][3] >= y1 - 40 for n in nodes):
            out.add(k)
    return out


def to_hosts(tag):
    n0 = cursor()
    nodes = ka_avd.dump()
    if any(x['id'] == 'listHostsButton' for x in nodes) and not any(x['id'] == 'viewerMenuButton' for x in nodes):
        tap('listHostsButton')  # the targets screen has its own PC 목록
    else:
        menu('viewerMenuHostsButton')
    ok = ka_avd.wait('card:' + H['a'], 15) is not None
    t0 = time.time()
    while time.time() - t0 < 6 and '[park_confirmed]' not in since(n0):
        time.sleep(0.5)
    verdict(f'{tag}: PC 목록 -> HOSTS, parked and confirmed', ok and '[park_confirmed]' in since(n0))
    time.sleep(0.8)


def app_pid():
    return adb('shell', 'pidof', PKG).strip().split(' ')[0]


def measure(name, sec=20):
    """App threads / RSS / PSS, and the device's rx/tx bytes over `sec` (the app is the only thing talking)."""
    pid = app_pid()
    def netdev():
        rx = tx = 0
        for line in adb('shell', 'cat', '/proc/net/dev').splitlines()[2:]:
            if ':' not in line:
                continue
            iface, rest = line.split(':', 1)
            if iface.strip() in ('lo',):
                continue
            f = rest.split()
            rx += int(f[0]); tx += int(f[8])
        return rx, tx
    r0, t0 = netdev()
    s0 = time.time()
    time.sleep(sec)
    r1, t1 = netdev()
    dt = time.time() - s0
    status = adb('shell', 'cat', f'/proc/{pid}/status')
    thr = re.search(r'Threads:\s+(\d+)', status)
    rss = re.search(r'VmRSS:\s+(\d+)', status)
    mem = adb('shell', 'dumpsys', 'meminfo', PKG)
    pss = re.search(r'TOTAL PSS:\s+(\d+)', mem) or re.search(r'TOTAL\s+(\d+)', mem)
    cost[name] = {'rx_Bps': round((r1 - r0) / dt), 'tx_Bps': round((t1 - t0) / dt),
                  'threads': int(thr.group(1)) if thr else None, 'rss_kB': int(rss.group(1)) if rss else None,
                  'pss_kB': int(pss.group(1)) if pss else None}
    step(f'  cost {name}: {cost[name]}')


def main():
    step(f'km2 run apk={APK}')
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

        # P1
        verdict('P1 signed in; six PCs listed', login())
        measure('nothing_kept_list', 15)

        # P2
        tap('card:' + H['a'])
        select('P2 A new')
        hA = handle_of(H['a'])
        to_hosts('P2')

        # P3 -- M2: B held, then A
        delay(6000)
        mark('P3_start')
        n0 = cursor()
        tap('card:' + H['b'])
        time.sleep(1.5)
        hB0 = handle_of(H['b'])
        tap('card:' + H['a'])
        t_tap = time.time()
        got = ka_avd.wait(TARGET, 10)
        reuse_ms = int((time.time() - t_tap) * 1000)
        time.sleep(7)  # B's held /connect answer comes back now
        log = since(n0)
        verdict('P3 M2: B (still connecting) superseded by the tap on A',
                re.search(r'\[directory_connect_superseded\] handle=%s ' % hB0, log) is not None, f'B handle={hB0}')
        verdict('P3 A resumed at once (its own connection), not after B\'s timeout',
                got is not None and re.search(r'\[park_reuse\].*handle=%s ' % hA, log) is not None and reuse_ms < 5000,
                f'{reuse_ms} ms to the targets')
        verdict('P3 B\'s late answer adopts nothing (no punch / native connect / start for B)',
                re.search(r'\[directory_connect_started\] handle=%s ' % hB0, log) is None and
                '[directory_chosen]' not in log, 'stale lines: ' +
                ' | '.join(re.findall(r'\[directory_connect_stale\] handle=%s [^\n]*' % hB0, log))[:200])
        mark('P3_end')
        to_hosts('P3')

        # P4 -- M2: C held, then B
        mark('P4_start')
        n0 = cursor()
        tap('card:' + H['c'])
        time.sleep(1.5)
        hC0 = handle_of(H['c'])
        tap('card:' + H['b'])
        select('P4 B (newest) connects after its held answer')
        delay(0)
        time.sleep(1)
        log = since(n0)
        hB = handle_of(H['b'])
        verdict('P4 M2: C superseded, B is the one adopted, A untouched',
                re.search(r'\[directory_connect_superseded\] handle=%s ' % hC0, log) is not None and
                re.search(r'\[directory_connect_committed\] handle=%s ' % hB, log) is not None and
                re.search(r'\[directory_connect_started\] handle=%s ' % hC0, log) is None and
                re.search(r'\[keep_end\] handle=%s ' % hA, log) is None, f'C={hC0} B={hB}')
        mark('P4_end')
        to_hosts('P4')
        verdict('P4 A and B kept', kept_set() >= {'a', 'b'}, str(sorted(kept_set())))

        # P5 -- old host
        n0 = cursor()
        tap('card:' + H['old'])
        time.sleep(4)
        shown = ka_avd.wait(TARGET, 20)
        time.sleep(2)
        log = since(n0)
        nodes = ka_avd.dump()
        hosts_btn = any(n['id'] == 'listHostsButton' for n in nodes)
        verdict('P5 old host: connects, says it cannot be kept, no PC 목록',
                shown is not None and '[keep_unsupported]' in log and not hosts_btn)
        tap('listDisconnectButton')
        ka_avd.wait('card:' + H['a'], 15)
        time.sleep(1.5)
        verdict('P5 연결 해제 of the old host ends it only; A and B still kept', kept_set() >= {'a', 'b'},
                str(sorted(kept_set())))

        # P6 -- another device takes B (relay)
        mark('P6_start')
        n0 = cursor()
        rc = subprocess.run([os.path.join(REL, 'ka_relay_client_test.exe'), '--dir-url', 'http://127.0.0.1:18310',
                             '--id', 'katest', '--pw', PW, '--host-name', H['b'], '--relay-ip', '10.0.2.2',
                             '--relay-port', '18343', '--sec', '6', '--out', os.path.join(OUT, 'takeover_b.txt')],
                            capture_output=True, timeout=120, env=dict(os.environ, REMOTE60_ALLOW_HOST_E2E='1'))
        step(f'  other device on B: exit={rc.returncode} ' + rc.stdout.decode(errors='replace').strip().splitlines()[-1:][0]
             if rc.stdout else f'  other device exit={rc.returncode}')
        time.sleep(6)
        log = since(n0)
        verdict('P6 B taken by another device: B ends (peer_lost), no reconnect, A kept',
                re.search(r'\[keep_end\] handle=%s host=%s reason=peer_lost' % (hB, H['b']), log) is not None and
                '[directory_connect]' not in log and 'a' in kept_set(), str(sorted(kept_set())))
        mark('P6_end')

        # P7 -- B new; large view + BEST on B; C new; limit reached
        tap('card:' + H['b'])
        select('P7 B new again')
        hB = handle_of(H['b'])
        mark('P7_B_settings')
        menu('viewerMenuViewButton')
        time.sleep(1)
        large = ka_avd.wait('viewerFullViewChip', 4) is not None
        menu('viewerQualityBestButton')
        time.sleep(2)
        verdict('P7 크게보기 on B', large)
        to_hosts('P7 B')
        mark('P7_C_start')
        tap('card:' + H['c'])
        select('P7 C new')
        hC = handle_of(H['c'])
        time.sleep(1.5)
        not_large = ka_avd.wait('viewerFullViewChip', 1) is None
        mark('P7_C_shown')
        verdict('P7 C\'s viewer opens at full view (B\'s 크게보기 stayed with B)', not_large)
        shot('P7_viewer_C')
        to_hosts('P7 C')
        verdict('P7 A, B, C kept (the limit)', kept_set() == {'a', 'b', 'c'}, str(sorted(kept_set())))

        # P8 -- M1: X never answers
        open(os.path.join(ROOT, 'host_x', 'stop'), 'w').close()
        time.sleep(4)
        mark('P8_start')
        n0 = cursor()
        tap('card:' + H['x'])
        t0 = time.time()
        while time.time() - t0 < 30 and '[directory_connect_failed]' not in since(n0):
            time.sleep(1)
        time.sleep(1)
        log = since(n0)
        hX = handle_of(H['x'], log)
        verdict('P8 M1: X started but never established -> given back, 0 evicted',
                re.search(r'\[directory_connect_failed\] handle=%s .*evicted=0' % hX, log) is not None and
                '[keep_end]' not in log, ' | '.join(re.findall(r'\[directory_connect_(?:started|failed)\][^\n]*', log))[:300])
        verdict('P8 A, B, C still kept', kept_set() == {'a', 'b', 'c'}, str(sorted(kept_set())))
        mark('P8_end')

        # P9 -- D direct, established -> LRU A
        n0 = cursor()
        tap('card:' + H['d'])
        select('P9 D new (direct)')
        log = since(n0)
        hD = handle_of(H['d'], log)
        chosen = re.findall(r'\[directory_chosen\] ([^\n]*)', log)
        verdict('P9 D took the DIRECT path', bool(chosen) and 'private' in chosen[-1], str(chosen[-1:]))
        verdict('P9 M1: D established -> exactly ONE eviction, and it is A (used longest ago)',
                re.search(r'\[directory_connect_committed\] handle=%s .*evicted=1' % hD, log) is not None and
                re.findall(r'\[keep_end\] handle=(\d+) host=([^ ]*) reason=lru', log) == [(hA, H['a'])],
                str(re.findall(r'\[keep_end\][^\n]*', log)))
        shot('P9_viewer_D')
        mark('P9_end')
        to_hosts('P9')
        verdict('P9 B, C, D kept', kept_set() == {'b', 'c', 'd'}, str(sorted(kept_set())))

        # P10 -- cost at the limit
        measure('three_kept_list', 20)

        # P11 -- held /connect + leave the app
        delay(8000)
        mark('P11_start')
        n0 = cursor()
        tap('card:' + H['a'])
        time.sleep(1.5)
        ka_avd.adb('shell', 'input', 'keyevent', '3')  # HOME
        time.sleep(11)
        ka_avd.adb('shell', 'am', 'start', '-n', f'{PKG}/.MainActivity')
        time.sleep(3)
        log = since(n0)
        hA2 = handle_of(H['a'], log)
        verdict('P11 leaving the app ends everything (new epoch), the held connect adopts nothing',
                '[keep_end_all] reason=app_stopped' in log and
                re.search(r'\[directory_connect_started\] handle=%s ' % hA2, log) is None and kept_set() == set(),
                f'kept after={sorted(kept_set())}')
        mark('P11_end')

        # P12 -- held /connect + sign-out
        mark('P12_start')
        n0 = cursor()
        tap('card:' + H['a'])
        time.sleep(1.5)
        tap('hostsLogoutButton')
        time.sleep(10)
        log = since(n0)
        hA3 = handle_of(H['a'], log)
        verdict('P12 sign-out during a held connect: nothing adopted, login screen',
                ka_avd.wait('loginIdInput', 5) is not None and '[keep_end_all] reason=sign_out' in log and
                re.search(r'\[directory_connect_started\] handle=%s ' % hA3, log) is None)
        delay(0)
        mark('P12_end')

        # P13 -- network change
        verdict('P13 signed in again; nothing kept', login() and kept_set() == set(), str(sorted(kept_set())))
        measure('nothing_kept_list_settled', 20)  # the first baseline runs while the emulator is still settling
        tap('card:' + H['c'])
        select('P13 C new')
        to_hosts('P13')
        tap('card:' + H['d'])
        select('P13 D new')
        mark('P13_net')
        n0 = cursor()
        adb('shell', 'cmd', 'connectivity', 'airplane-mode', 'enable')
        time.sleep(8)
        log = since(n0)
        adb('shell', 'cmd', 'connectivity', 'airplane-mode', 'disable')
        verdict('P13 the network goes: everything ends (the PC on screen too)',
                re.search(r'\[keep_end_all\] reason=network_(?:lost|changed)', log) is not None,
                ' | '.join(re.findall(r'\[keep_end_all\][^\n]*', log)))
        time.sleep(5)
        open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
    finally:
        if logcat:
            logcat.terminate()
        adb('emu', 'kill', timeout=30)
        try:
            emu.wait(60)
        except subprocess.TimeoutExpired:
            emu.kill()
        json.dump({'results': results, 'marks': marks, 'cost': cost}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        step(f'cost {cost}')
        step(f'run end: {sum(1 for _, ok, _ in results if ok)}/{len(results)} PASS')
    return 0 if results and all(ok for _, ok, _ in results) else 1


if __name__ == '__main__':
    sys.exit(main())
