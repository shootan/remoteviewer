"""keepalive-multi r2 V1 (TTL): the TEST policy APK (park limit 45 s, -Pgnlink.testKeepParkTtlMs) -- each PC expires on its own. (helpers from km2_avd.py)

the real APK UI on the isolated AVD against km2_fixture.ps1's six fail-closed pinned TEST
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


TTL_S = 45


def main():
    step(f'km2 TTL run apk={APK}')
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
        verdict('T1 signed in', login())
        verdict('T1 the TEST policy is in force (45 s)', 'parkTtlMs=45000' in applog())
        # A, then B
        tap('card:' + H['a'])
        select('T2 A new')
        hA = handle_of(H['a'])
        to_hosts('T2 A')
        mark('A_park1')
        tap('card:' + H['b'])
        select('T3 B new')
        hB = handle_of(H['b'])
        to_hosts('T3 B')
        mark('B_park')
        # A again, and stay on it past B's limit
        tap('card:' + H['a'])
        select('T4 A reused')
        mark('A_active')
        wait_until = marks['B_park'] + TTL_S + 8
        n0 = cursor()
        while time.time() < wait_until:
            time.sleep(2)
        log = since(n0)
        mark('after_B_deadline')
        m = re.search(r'\[keep_end\] handle=%s host=%s reason=(\w+)' % (hB, H['b']), log)
        verdict('T5 B (parked) expired on its own limit while A was in use', m is not None and m.group(1) == 'ttl',
                m.group(1) if m else 'no keep_end for B')
        verdict('T5 A (on screen) untouched', re.search(r'\[keep_end\] handle=%s ' % hA, log) is None)
        to_hosts('T6 A')
        mark('A_park2')
        verdict('T6 A kept, B not', kept_set() == {'a'}, str(sorted(kept_set())))
        n0 = cursor()
        tap('card:' + H['b'])
        select('T6 B again')
        log = since(n0)
        hB2 = handle_of(H['b'], log)
        verdict('T6 B after its expiry is a NEW connection (new handle), not a reuse',
                hB2 is not None and hB2 != hB and '[park_reuse]' not in log, f'{hB} -> {hB2}')
        to_hosts('T6 B')
        # A's first park began more than 45 s ago, its second one less: still kept (re-activation renewed it)
        mark('A_reuse2')
        age1 = time.time() - marks['A_park1']
        age2 = time.time() - marks['A_park2']
        n0 = cursor()
        tap('card:' + H['a'])
        time.sleep(2)
        log = since(n0)
        verdict('T7 A reused after its FIRST park would have expired (re-activation started a new limit)',
                age1 > TTL_S and age2 < TTL_S and re.search(r'\[park_reuse\].*handle=%s ' % hA, log) is not None,
                f'first park {age1:.0f} s ago, second {age2:.0f} s ago')
        open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
    finally:
        if logcat:
            logcat.terminate()
        adb('emu', 'kill', timeout=30)
        try:
            emu.wait(60)
        except subprocess.TimeoutExpired:
            emu.kill()
        json.dump({'results': results, 'marks': marks}, open(os.path.join(OUT, 'result.json'), 'w'), indent=1)
        step(f'run end: {sum(1 for _, ok, _ in results if ok)}/{len(results)} PASS')
    return 0 if results and all(ok for _, ok, _ in results) else 1


if __name__ == '__main__':
    sys.exit(main())
