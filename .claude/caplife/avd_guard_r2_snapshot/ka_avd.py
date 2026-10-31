"""keepalive r1: drives the real APK UI on the isolated AVD (gnlink_kbd_test, its own port; every adb
call is -s emulator-<port>, no other device is touched). The UI is found through uiautomator (resource
ids / text), touched with `input tap` at the element's centre after checking it is visible and enabled.

  python -I ka_avd.py <out_dir> <command> [args]
    ui                      dump the screen's elements (id, text, enabled, bounds)
    tap <id|text:...>       tap an element by resource id (or visible text) -- refused if not shown/enabled
    type <id> <text>        focus a field and type into it
    wait <id|text:...> [s]  wait until it is shown
    shot <name>             screenshot to <out>/<name>.png
    log <name>              pull the app's diagnostics log to <out>/<name>.log
    back                    the system back key
"""
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

SERIAL = os.environ.get('KA_SERIAL', 'emulator-5614')
ADB = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk', 'platform-tools', 'adb.exe')
PKG = 'com.remote60.androiddirect'
LOG_PATH = f'/sdcard/Android/data/{PKG}/files/android_direct_client_session.log'


def adb(*args, check=True, binary=False):
    r = subprocess.run([ADB, '-s', SERIAL, *args], capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError(f'adb {args}: {r.stderr.decode(errors="replace")}')
    return r.stdout if binary else r.stdout.decode('utf-8', errors='replace')


def dump():
    for attempt in range(12):
        if attempt in (4, 8):
            adb('shell', 'pkill', '-f', 'uiautomator', check=False)  # a dump left running by a killed run
        out = adb('exec-out', 'uiautomator', 'dump', '/dev/tty', check=False)
        i = out.find('<?xml')
        j = out.rfind('</hierarchy>')
        if i >= 0 and j > i:
            root = ET.fromstring(out[i:j + len('</hierarchy>')])
            nodes = []
            for n in root.iter('node'):
                b = re.findall(r'\d+', n.get('bounds', ''))
                if len(b) != 4:
                    continue
                x1, y1, x2, y2 = map(int, b)
                nodes.append({
                    'id': n.get('resource-id', '').replace(PKG + ':id/', ''),
                    'text': n.get('text', ''),
                    'desc': n.get('content-desc', ''),
                    'enabled': n.get('enabled') == 'true',
                    'clickable': n.get('clickable') == 'true',
                    'bounds': (x1, y1, x2, y2),
                })
            return nodes
        time.sleep(0.5)
    raise RuntimeError('uiautomator dump failed')


def find(nodes, key):
    if key.startswith('tcard:'):
        want = key[6:]
        hits = [n for n in nodes if n['id'] == 'targetCardTitle' and want in n['text']]
    elif key.startswith('card:'):
        want = key[5:]
        hits = [n for n in nodes if n['id'] == 'hostCardName' and n['text'] == want]
    elif key.startswith('text:'):
        want = key[5:]
        hits = [n for n in nodes if want in n['text']]
    else:
        hits = [n for n in nodes if n['id'] == key]
    hits = [n for n in hits if n['bounds'][2] > n['bounds'][0] and n['bounds'][3] > n['bounds'][1]]
    return hits[0] if hits else None


def tap(key):
    n = find(dump(), key)
    if not n:
        raise RuntimeError(f'not on screen: {key}')
    if not n['enabled']:
        raise RuntimeError(f'shown but disabled: {key}')
    x1, y1, x2, y2 = n['bounds']
    x, y = (x1 + x2) // 2, (y1 + y2) // 2
    adb('shell', 'input', 'tap', str(x), str(y))
    return f'tapped {key} at {x},{y} text={n["text"]!r}'


def wait(key, sec=20.0):
    end = time.time() + sec
    while time.time() < end:
        n = find(dump(), key)
        if n:
            return n
        time.sleep(0.5)
    return None


def main():
    out, cmd, args = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    if cmd == 'ui':
        for n in dump():
            if n['id'] or n['text']:
                print(f"{n['id']:<32} en={int(n['enabled'])} {n['bounds']} {n['text'][:60]!r}")
    elif cmd == 'tap':
        print(tap(args[0]))
    elif cmd == 'type':
        print(tap(args[0]))
        time.sleep(0.4)
        adb('shell', 'input', 'keycombination', '113', '29', check=False)  # ctrl+a, so typing replaces
        adb('shell', 'input', 'keyevent', '67', check=False)
        adb('shell', 'input', 'text', args[1].replace(' ', '%s'))
        print('typed into', args[0])
    elif cmd == 'wait':
        n = wait(args[0], float(args[1]) if len(args) > 1 else 20.0)
        print(('shown ' + str(n['bounds']) + ' ' + repr(n['text'])) if n else 'NOT SHOWN ' + args[0])
        sys.exit(0 if n else 1)
    elif cmd == 'shot':
        png = adb('exec-out', 'screencap', '-p', binary=True)
        p = os.path.join(out, args[0] + '.png')
        open(p, 'wb').write(png)
        print(p)
    elif cmd == 'log':
        p = os.path.join(out, args[0] + '.log')
        adb('pull', LOG_PATH, p)
        print(p)
    elif cmd == 'back':
        adb('shell', 'input', 'keyevent', '4')
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == '__main__':
    main()
