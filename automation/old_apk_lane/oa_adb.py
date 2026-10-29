"""rel-0164 old-APK lane: a copy of the AVD harness r8's ka_avd.py (the adb client pinned, every call's secrets
cleaned, the UI dump / find / tap / wait helpers) -- unchanged but this line.
keepalive r1: drives the real APK UI on the isolated AVD (gnlink_kbd_test, its own port; every adb
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
import hashlib
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

SERIAL = os.environ.get('KA_SERIAL', 'emulator-5614')
ADB = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk', 'platform-tools', 'adb.exe')
# AVD r8 (Codex design r3 D3): this ONE client, by absolute path, for every call (no PATH lookup anywhere). With
# KE_ADB_SHA256 set (ke_slot passes its pin) a different file refuses to run at all.
ADB_SHA256_PIN = os.environ.get('KE_ADB_SHA256', '').lower()


def adb_identity():
    """(sha256 of the client file, its `adb version` lines) -- `adb version` reads the client only, no server."""
    digest = hashlib.sha256(open(ADB, 'rb').read()).hexdigest()
    r = subprocess.run([ADB, 'version'], capture_output=True, timeout=30)
    lines = [l.strip() for l in r.stdout.decode('utf-8', errors='replace').splitlines() if l.strip()][:2]
    return digest, ' / '.join(lines)


if ADB_SHA256_PIN and adb_identity()[0] != ADB_SHA256_PIN:
    raise RuntimeError(f'the adb client {ADB} is not the pinned one (KE_ADB_SHA256)')
PKG = 'com.remote60.androiddirect'
LOG_PATH = f'/sdcard/Android/data/{PKG}/files/android_direct_client_session.log'


def _secrets(args):
    """The values that must never be shown: what follows `input text` (it can be a password)."""
    return [args[i + 2] for i in range(len(args) - 2) if args[i] == 'input' and args[i + 1] == 'text' and args[i + 2]]


def _shown(args):
    """The arguments as an error may show them: what follows `input text` is redacted."""
    out = list(args)
    for i in range(len(out) - 1):
        if out[i] == 'input' and out[i + 1] == 'text' and i + 2 < len(out):
            out[i + 2] = '<redacted>'
    return tuple(out)


def _clean(text, args):
    """AVD r6 (Codex codex_avd_r5_scripts_src.md N1): adb's own words with every secret of this call removed -- adb (or
    the shell behind it) may repeat the input in its error; argv being redacted is not enough."""
    for secret in _secrets(args):
        text = text.replace(secret, '<redacted>')
    return text


def run(*args):
    """One adb call: (exit code, stdout, stderr), both texts cleaned of this call's secrets. The call itself is not
    changed (its argv carries the real value)."""
    r = subprocess.run([ADB, '-s', SERIAL, *args], capture_output=True)
    return (r.returncode, _clean(r.stdout.decode('utf-8', errors='replace'), args),
            _clean(r.stderr.decode('utf-8', errors='replace'), args))


def adb(*args, check=True, binary=False):
    r = subprocess.run([ADB, '-s', SERIAL, *args], capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError(f'adb {_shown(args)} (exit {r.returncode}): '
                           f'{_clean(r.stderr.decode(errors="replace"), args)}')
    return r.stdout if binary else r.stdout.decode('utf-8', errors='replace')


def dump():
    last = ''
    for attempt in range(12):
        if attempt in (4, 8):
            adb('shell', 'pkill', '-f', 'uiautomator', check=False)  # a dump left running by a killed run
        out = adb('exec-out', 'uiautomator', 'dump', '/dev/tty', check=False)
        last = out.strip()[-160:]
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
    raise RuntimeError(f'uiautomator dump failed (12 tries; the last answer: {last!r})')  # AVD r5: say what it said


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
