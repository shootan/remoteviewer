"""sh-filter r1/r2 -- the phone viewer's sharpening, on an AVD, through the real APK UI.

What runs and what it is:
  * the host: a GNLinkStream TEST build (GNLinkStreamMigrationTest; capture/encode are the product's)
    bound to 127.0.0.1 only (no firewall prompt), input injection OFF, capturing the DESKTOP (DXGI) with
    the topmost pattern window (automation/sh_pattern_window.ps1, 1920x1080) filling it;
  * the phone: the AVD `gnlink_kbd_test` (x86_64, API 34, SwiftShader software GL), the APK under test
    installed as is; the AVD reaches the host's loopback at 10.0.2.2; manual IP connect (TCP control);
  * the UI is driven through uiautomator (resource ids) and adb taps -- the app's own buttons.
The AVD is not the user's phone: its decoder (goldfish: no 1088 crop), GPU, colour and heat are not the
SM-S948N's. Seam scenarios (faults, identity, touch, hold, viewer_never) need the -Pgnlink.shTestSeam=true
APK; compare/lifecycle/latency/resize run the seam-free one.

Usage: python sh_filter_avd.py --host-exe <GNLinkStreamMigrationTest.exe> --apk <apk> --out <dir>
        [--scenario compare|identity|faults|lifecycle|latency|touch|hold|viewer_never|resize]
Prints PASS/FAIL lines and a final RESULT line; exit 0 only when every check passed.
"""

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

SDK = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk')
ADB = os.path.join(SDK, 'platform-tools', 'adb.exe')
EMULATOR = os.path.join(SDK, 'emulator', 'emulator.exe')
AVD = 'gnlink_kbd_test'
PKG = 'com.remote60.androiddirect'
ACT = PKG + '/.MainActivity'
PATTERN_TITLE = 'GNLINK-SH-PATTERN'
HERE = os.path.dirname(os.path.abspath(__file__))

checks = []


def check(ok, label, detail=''):
    checks.append(bool(ok))
    print(('PASS' if ok else 'FAIL') + '  ' + label + (('  ' + detail) if detail else ''), flush=True)
    return ok


def adb(*args, check_rc=True, timeout=120, binary=False):
    for attempt in range(3):
        r = subprocess.run([ADB, *args], capture_output=True, timeout=timeout)
        # A daemon hiccup (seen once: "cannot connect to daemon") is retried; anything else is the answer.
        if r.returncode == 0 or b'daemon' not in r.stderr:
            break
        time.sleep(2)
    if check_rc and r.returncode != 0:
        raise RuntimeError('adb %s: %s' % (' '.join(args), r.stderr.decode(errors='replace')))
    return r.stdout if binary else r.stdout.decode(errors='replace')


def free_tcp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


# ---------------------------------------------------------------- emulator
def ensure_emulator():
    devices = adb('devices')
    if 'emulator-' in devices and '\tdevice' in devices:
        return None
    proc = subprocess.Popen([EMULATOR, '-avd', AVD, '-no-window', '-no-audio', '-no-boot-anim',
                             '-no-snapshot-save', '-gpu', 'swiftshader_indirect'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    adb('wait-for-device', timeout=300)
    for _ in range(180):
        if adb('shell', 'getprop', 'sys.boot_completed', check_rc=False).strip() == '1':
            break
        time.sleep(2)
    adb('shell', 'settings', 'put', 'global', 'window_animation_scale', '0', check_rc=False)
    adb('shell', 'settings', 'put', 'global', 'transition_animation_scale', '0', check_rc=False)
    adb('shell', 'settings', 'put', 'global', 'animator_duration_scale', '0', check_rc=False)
    adb('shell', 'input', 'keyevent', 'KEYCODE_WAKEUP', check_rc=False)
    adb('shell', 'wm', 'dismiss-keyguard', check_rc=False)
    return proc


# ---------------------------------------------------------------- UI
def dump():
    adb('shell', 'uiautomator', 'dump', '/sdcard/sh_ui.xml', check_rc=False, timeout=60)
    xml = adb('shell', 'cat', '/sdcard/sh_ui.xml', check_rc=False)
    try:
        return ET.fromstring(xml[xml.index('<?xml'):] if '<?xml' in xml else xml)
    except Exception:
        return None


def bounds(node):
    m = re.match(r'\[(\d+),(\d+)\]\[(\d+),(\d+)\]', node.get('bounds', ''))
    return tuple(int(v) for v in m.groups()) if m else None


def find(root, rid=None, text=None):
    if root is None:
        return None
    for n in root.iter('node'):
        if rid and n.get('resource-id') == PKG + ':id/' + rid and n.get('enabled') == 'true':
            b = bounds(n)
            if b and b[2] > b[0] and b[3] > b[1]:
                return n
        if text and text in (n.get('text') or ''):
            return n
    return None


def find_exact(root, text):
    if root is None:
        return None
    for n in root.iter('node'):
        if (n.get('text') or '') == text:
            return n
    return None


def tap_node(n):
    x0, y0, x1, y1 = bounds(n)
    adb('shell', 'input', 'tap', str((x0 + x1) // 2), str((y0 + y1) // 2))


def tap(rid=None, text=None, timeout=20, reveal=None):
    end = time.time() + timeout
    while time.time() < end:
        n = find(dump(), rid, text)
        if n is not None:
            tap_node(n)
            return True
        if reveal:
            reveal()
        time.sleep(1)
    return False


def wait_for(rid=None, text=None, timeout=30):
    end = time.time() + timeout
    while time.time() < end:
        if find(dump(), rid, text) is not None:
            return True
        time.sleep(1)
    return False


def screencap(path):
    png = adb('exec-out', 'screencap', '-p', binary=True, timeout=60)
    with open(path, 'wb') as f:
        f.write(png)
    return np.asarray(Image.open(path).convert('RGB')).astype(np.int32)


def log_lines(tag_filter=None):
    out = adb('logcat', '-d', '-v', 'brief', check_rc=False, timeout=60)
    lines = [l for l in out.splitlines() if 'remote60' in l or 'android_direct' in l or 'gnlink' in l.lower()]
    if tag_filter:
        lines = [l for l in lines if tag_filter in l]
    return lines


# ---------------------------------------------------------------- host + pattern
class Fixture:
    def __init__(self, host_exe, out, static, input_log=None, layered_size=None, pulse_ms=200):
        self.out = out
        self.port = free_udp_port()
        self.control_port = free_tcp_port()  # manual connect uses a TCP control link (loopback only)
        # sh-strength r2: layered_size=(w, h) -> the pattern is a LAYERED window outside every monitor (it covers
        # nothing on this PC's screen); the APK must pick it as a window (WGC).
        self.pattern_log = open(os.path.join(out, 'pattern.log'), 'wb')
        self.pattern = subprocess.Popen(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                         os.path.join(HERE, 'sh_pattern_window.ps1'), '-Seconds', '1800']
                                        + (['-Static'] if static else [])
                                        + (['-InputLog', input_log] if input_log else [])
                                        + (['-Layered', '-Width', str(layered_size[0]), '-Height', str(layered_size[1]),
                                            '-PulseMs', str(pulse_ms)] if layered_size else []),
                                        stdout=self.pattern_log, stderr=subprocess.STDOUT)
        time.sleep(3)
        stage = os.path.join(out, 'host')
        os.makedirs(stage, exist_ok=True)
        exe = os.path.join(stage, 'GNLinkStreamMigrationTest.exe')
        shutil.copyfile(host_exe, exe)
        env = dict(os.environ)
        env['REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE'] = '1'
        env['LOCALAPPDATA'] = os.path.join(stage, 'localappdata')
        os.makedirs(env['LOCALAPPDATA'], exist_ok=True)
        self.host_log = open(os.path.join(out, 'host.log'), 'wb')
        cmd = [exe, '--transport', 'udp', '--codec', 'h264', '--bind-address', '127.0.0.1',
               '--bind-port', str(self.port), '--control-port', str(self.control_port), '--seconds', '1800']
        # Input injection stays OFF except for touch_host, where it is the background-message mode: posted
        # to the CAPTURED WINDOW (the pattern window, which logs what it receives) -- never SendInput on a
        # window target. The scenario checks the target is that window before it sends a single tap.
        cmd += (['--enable-input-injection', '--input-injection-mode', 'background_message', '--input-log-every', '1']
                if input_log else ['--input-injection-mode', 'none'])
        # No --capture-window-*: that locks the host's selection and the APK's pick is refused. The APK picks
        # the pattern window from its own window list, as a user would.
        CONTROL_PORT[self.port] = self.control_port
        self.host = subprocess.Popen(cmd, cwd=stage, env=env, stdout=self.host_log, stderr=subprocess.STDOUT)
        time.sleep(2)

    def close(self):
        for p in (self.host, self.pattern):
            try:
                p.terminate()
                p.wait(10)
            except Exception:
                pass
        self.host_log.close()


# ---------------------------------------------------------------- app flow
CONTROL_PORT = {}


def launch(port, extras=()):
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    adb('logcat', '-c', check_rc=False)
    adb('shell', 'am', 'start', '-n', ACT, '--es', 'host', '10.0.2.2', '--ei', 'videoPort', str(port),
        '--ei', 'controlPort', str(CONTROL_PORT.get(port, port)), *extras)


def connect_to_pattern(port, extras=(), best_quality=True, level_before=None, before_pick=None, wait_viewer=True,
                       window=False):
    launch(port, extras)
    if not tap(rid='loginManualButton', timeout=30):
        return False
    if not tap(rid='connectButton', timeout=20):
        return False
    connected_at = time.time()
    # 최고화질 BEFORE the pick: the host then encodes the window at 1920x1080 (1:1 on the AVD's 1080-high
    # screen). Changing quality mid-view on this manual-connect path was seen to end the media session
    # (2026-10-22 run-compare4; not part of this change -- recorded in the report).
    if best_quality:
        if not tap(rid='listSettingsButton', timeout=20):
            return False
        if not tap(rid='settingsQualityBestButton', timeout=10):
            return False
    if level_before:
        rid = {'off': 'settingsSharpOffButton', 'weak': 'settingsSharpWeakButton', 'strong': 'settingsSharpStrongButton'}
        if not tap(rid='listSettingsButton', timeout=20) or not tap(rid=rid[level_before], timeout=10):
            return False
    # The DESKTOP (the monitor, DXGI), with the topmost pattern window filling it: the host then gets a
    # frame every time the screen changes (the moving square, ~30 fps). Capturing the window itself (WGC)
    # gave ~1 frame/s here, too few to measure anything and slow to answer a lost keyframe.
    if not tap(rid='listWindowsButton' if window else 'listDevicesButton', timeout=10):
        return False
    # A WORKAROUND for these filter tests, not evidence that manual connect works: on this manual (TCP-control)
    # path the host's UDP control session was seen to end ~10 s after connect ("udp control session ended
    # reason=none", 4 runs; host_startup_control.cpp has a 10 s UdpControlLink read timeout -- the likely
    # cause, not established) and to take a stream already playing inactive ("화면이 멈췄습니다"). Seen with
    # OFF too. Picking after that point keeps the filter runs on a steady stream. Ledger item (verifier).
    wait = 11.5 - (time.time() - connected_at)
    if wait > 0:
        time.sleep(wait)
    end = time.time() + 30
    card = None
    while time.time() < end and card is None:
        # Exact title: once the window is the host's current target, a header "지금 보는 대상: <title>" holds it too.
        card = find_exact(dump(), PATTERN_TITLE) if window else find(dump(), rid='targetCardTitle')
        if card is None:
            time.sleep(1)
    if card is None:
        return False
    if before_pick:
        before_pick()
    tap_node(card)
    if not wait_viewer:
        return True
    # The viewer is up once its menu button exists.
    return wait_for(rid='viewerMenuButton', timeout=40) or wait_for(rid='viewerRoot', timeout=5)


def reveal_controls():
    adb('shell', 'input', 'tap', '1200', '540', check_rc=False)


def scroll_menu_sheet():
    """The viewer menu sheet scrolls; its lower buttons (대상 선택 화면 ...) start below the screen edge."""
    adb('shell', 'input', 'swipe', '1900', '950', '1900', '600', '1500', check_rc=False)  # slow: no fling
    time.sleep(0.8)


def scroll_menu_sheet_up():
    """r4: back to the sheet's top (크게 보기 ...) after it was scrolled down to its lower buttons."""
    adb('shell', 'input', 'swipe', '1900', '500', '1900', '950', '600', check_rc=False)
    time.sleep(0.8)


def pick_desktop():
    """Pick the DESKTOP on the target list -- never the first card of whatever tab is open: on the window
    tab that is a real window of this PC."""
    return tap(rid='listDevicesButton', timeout=15) and tap(rid='targetCardTitle', timeout=20)


def choose_quality_best():
    """최고화질 (6000): the host keeps 1920x1080, so on the AVD's 1080-high screen the picture is 1:1."""
    if not tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls):
        return False
    ok = tap(rid='viewerQualityBestButton', timeout=10)
    tap(rid='viewerMenuCloseButton', timeout=10)
    return ok


def choose_level(level):
    """Through the viewer menu, as a user would: open, tap the level, close."""
    rid = {'off': 'viewerSharpOffButton', 'weak': 'viewerSharpWeakButton', 'strong': 'viewerSharpStrongButton'}[level]
    if not tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls):
        return False
    ok = tap(rid=rid, timeout=10)
    tap(rid='viewerMenuCloseButton', timeout=10)
    return ok


# ---------------------------------------------------------------- picture metrics
VIDEO_W, VIDEO_H = 1920, 1080
REGIONS = {
    'A_text': (60, 40, 900, 400), 'B_lowcontrast': (960, 40, 1880, 400), 'C_lines': (60, 440, 440, 700),
    'D_flat': (490, 450, 590, 690), 'E_checker': (1040, 440, 1400, 700), 'F_colourtext': (1440, 440, 1880, 700),
}


def video_origin(img):
    """Where the 1920x1080 picture sits on the screen: the start of its 1-px red top row.

    The letterbox around it is near black. The 1-px coloured lines come back dimmed and smeared by the
    4:2:0 encode (red top ~123,1,1, blue left ~0,0,111, yellow right ~250,255,125), so the thresholds
    are on hue, not on full intensity."""
    h, w, _ = img.shape
    row = img[0, :, :]
    # sh-strength r2: > 60 (was 80) -- at s=0.60 the 1-px red row next to white comes back darker (R ~80, measured).
    red = (row[:, 0] > 60) & (row[:, 1] < 50) & (row[:, 2] < 50)
    for x0 in range(0, w - VIDEO_W + 1):
        if red[x0 + 3:x0 + 40].all() and red[x0 + VIDEO_W - 40:x0 + VIDEO_W - 3].all() and not red[max(0, x0 - 3):x0].any():
            return x0
    return None


def crop(img, x0, region):
    l, t, r, b = region
    return img[t:b, x0 + l:x0 + r, :]


def edge_energy(a):
    g = a.mean(axis=2)
    lap = np.abs(4 * g[1:-1, 1:-1] - g[:-2, 1:-1] - g[2:, 1:-1] - g[1:-1, :-2] - g[1:-1, 2:])
    return float(lap.mean())


def metrics(img):
    x0 = video_origin(img)
    if x0 is None:
        return None
    m = {'x0': x0}
    for k, r in REGIONS.items():
        c = crop(img, x0, r)
        m[k + '_edge'] = round(edge_energy(c), 3)
    flat = crop(img, x0, REGIONS['D_flat'])
    m['D_flat_std'] = round(float(flat.std()), 3)
    chk = crop(img, x0, REGIONS['E_checker']).mean(axis=2)
    m['E_clip_frac'] = round(float(((chk <= 2) | (chk >= 253)).mean()), 4)
    # borders: the four 1-px lines must be where the 1920x1080 picture's edges are (no crop/flip/padding)
    top = img[0, x0 + 5:x0 + VIDEO_W - 5, :].mean(axis=0)
    bottom = img[VIDEO_H - 1, x0 + 5:x0 + VIDEO_W - 5, :].mean(axis=0)
    right = img[5:VIDEO_H - 5, x0 + VIDEO_W - 1, :].mean(axis=0)
    m['border_top_rgb'] = [round(float(v)) for v in top]
    m['border_bottom_rgb'] = [round(float(v)) for v in bottom]
    m['border_right_rgb'] = [round(float(v)) for v in right]
    return m


def borders_ok(m):
    """Top red, bottom green, right yellow, each where the 1920x1080 picture's edge is (hue thresholds, 4:2:0)."""
    t, b, r = m['border_top_rgb'], m['border_bottom_rgb'], m['border_right_rgb']
    return (t[0] > 60 and t[1] < 50 and t[2] < 50
            and b[1] > 150 and b[0] < b[1] - 80 and b[2] < b[1] - 80
            and r[0] > 150 and r[1] > 150 and r[2] < min(r[0], r[1]) - 60)


# ---------------------------------------------------------------- scenarios
def check_output_size(lines):
    """r2: the renderer's EGL output must be the picture's own size -- a different one is resampled twice
    (seen: 1970x1080 after the rotation reset the TextureView's buffer size)."""
    pairs = [(m.group(1), m.group(2)) for m in (re.search(r'src=(\d+x\d+) out=(\d+x\d+)', l) for l in lines) if m]
    bad = [p for p in pairs if p[0] != p[1]]
    check(pairs and not bad, 'the renderer drew at the picture size itself (src == out) in every progress line',
          'lines=%d mismatched=%s' % (len(pairs), bad[:3]))


def scenario_compare(fx, out, seam):
    """Acceptance 1/2: off -> weak -> strong -> off on the SAME static frame, chosen in the viewer menu."""
    check(connect_to_pattern(fx.port, ('--es', 'shTestFault', 'none') if seam else ()),
          'the APK connects to the test host and shows the pattern window (manual connect, real UI)')
    time.sleep(6)
    shots = {}
    for level in ['off', 'weak', 'strong', 'off2', 'weak2']:
        lv = level.rstrip('2')
        check(choose_level(lv), 'viewer menu: 선명도 ' + lv + ' chosen')
        time.sleep(3.5)  # the producer change (off<->filter) is a short rebind; then the static frame
        img = screencap(os.path.join(out, 'shot_%s.png' % level))
        shots[level] = metrics(img)
        print('      %s: %s' % (level, json.dumps(shots[level])), flush=True)
    for k in shots:
        check(shots[k] is not None, 'picture found on screen after ' + k)
    if all(shots.values()):
        for k in shots:
            check(borders_ok(shots[k]), 'borders where the 1920x1080 picture\'s edges are (crop/flip/padding) - ' + k,
                  str((shots[k]['border_top_rgb'], shots[k]['border_bottom_rgb'], shots[k]['border_right_rgb'])))
        check(len({shots[k]['x0'] for k in shots}) == 1, 'the picture is placed at the same screen x in every level (geometry unchanged)',
              str({k: shots[k]['x0'] for k in shots}))
        for r in ['A_text', 'B_lowcontrast', 'C_lines']:
            off, weak, strong = shots['off'][r + '_edge'], shots['weak'][r + '_edge'], shots['strong'][r + '_edge']
            check(strong > weak > off, 'edge energy rises off < 0.20 < 0.35 in ' + r, 'off=%s weak=%s strong=%s' % (off, weak, strong))
        check(abs(shots['off2']['A_text_edge'] - shots['off']['A_text_edge']) < 0.15 * shots['off']['A_text_edge'],
              'back to off: the picture returns to the unsharpened one (off again ~= off)',
              '%s vs %s' % (shots['off']['A_text_edge'], shots['off2']['A_text_edge']))
        check(shots['D_flat_std'] if False else shots['strong']['D_flat_std'] < shots['off']['D_flat_std'] + 2.0,
              'flat colour stays flat under 0.35', 'std off=%s strong=%s' % (shots['off']['D_flat_std'], shots['strong']['D_flat_std']))
    lines = log_lines()
    check(any('sharpen_path' in l and 'path=sharpen' in l for l in lines), 'the app logged the sharpening path (decoder -> renderer)')
    check_output_size(lines)
    check(any('sharpen_path' in l and 'path=direct' in l for l in lines), 'the app logged the direct path again after off')
    with open(os.path.join(out, 'compare_metrics.json'), 'w') as f:
        json.dump(shots, f, indent=1)


def scenario_identity(fx, out):
    """Seam: s forced to 0 through GL vs OFF (direct): geometry 1:1 and the GPU's YUV->RGB vs the compositor's."""
    check(connect_to_pattern(fx.port, ('--ef', 'shTestStrength', '0')), 'connected with the GL pass forced to s=0')
    time.sleep(6)
    choose_level('weak')
    time.sleep(3.5)
    gl0 = screencap(os.path.join(out, 'shot_gl_s0.png'))
    choose_level('off')
    time.sleep(3.5)
    off = screencap(os.path.join(out, 'shot_gl_off.png'))
    mg, mo = metrics(gl0), metrics(off)
    check(mg is not None and mo is not None and mg['x0'] == mo['x0'], 'GL s=0 and OFF place the picture identically')
    if mg and mo:
        x0 = mo['x0']
        a = gl0[0:VIDEO_H, x0:x0 + VIDEO_W, :]
        b = off[0:VIDEO_H, x0:x0 + VIDEO_W, :]
        d = np.abs(a - b)
        s = {'mean_abs': round(float(d.mean()), 3), 'p99_abs': int(np.percentile(d, 99)), 'max_abs': int(d.max()),
             'mean_signed_rgb': [round(float(v), 3) for v in (a - b).reshape(-1, 3).mean(axis=0)]}
        print('      GL s=0 vs OFF: %s' % json.dumps(s), flush=True)
        check(s['mean_abs'] < 3.0, 'GL s=0 is the same picture as OFF within encoder/screen noise (no shift, crop or colour change)',
              json.dumps(s))
        with open(os.path.join(out, 'identity_metrics.json'), 'w') as f:
            json.dump(s, f, indent=1)


FALLBACK_TEXT = '선명도 적용 불가, 원본 표시'


class ToastEvents:
    """`adb shell uiautomator events`: the accessibility event stream, in which every toast arrives as a
    TYPE_NOTIFICATION_STATE_CHANGED event carrying its text. The check is the EXACT fallback text; any other
    toast is recorded and does not count (r2 H5.4: r1's watcher accepted any toast window). While this runs,
    `uiautomator dump` is not used (one UiAutomation client at a time)."""

    def __init__(self):
        import threading
        self.lines = []
        self.p = subprocess.Popen([ADB, 'shell', 'uiautomator', 'events'], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        self.t = threading.Thread(target=self._read, daemon=True)
        self.t.start()
        time.sleep(1.5)  # the stream is up before the event it must not miss

    def _read(self):
        for raw in iter(self.p.stdout.readline, b''):
            self.lines.append(raw.decode('utf-8', errors='replace'))

    def stop(self):
        try:
            self.p.terminate()
            self.p.wait(10)
        except Exception:
            pass
        adb('shell', 'pkill', '-f', 'uiautomator', check_rc=False)
        self.t.join(5)
        return self.toasts()

    def toasts(self):
        out = []
        for l in self.lines:
            if 'TYPE_NOTIFICATION_STATE_CHANGED' not in l:
                continue
            i = l.find('Text: [')
            j = l.find(']; ', i)
            if i >= 0 and j > i:
                text = l[i + 7:j]
                # The event's text list is [the toast's message, the app's label].
                if text.endswith(', GNLink'):
                    text = text[:-len(', GNLink')]
                out.append(text)
        return out


def check_toast(prefix, toasts, shot_path):
    check(FALLBACK_TEXT in toasts, prefix + ': the fallback notice "' + FALLBACK_TEXT + '" was shown (exact text)',
          'toasts seen: ' + repr(toasts))
    others = [t for t in toasts if t != FALLBACK_TEXT]
    if others:
        print('      (other toasts in the same window, not counted: %r)' % others, flush=True)


def live_picture(out, name):
    a = screencap(os.path.join(out, name + '_1.png'))
    time.sleep(2)
    b = screencap(os.path.join(out, name + '_2.png'))
    ma = metrics(a)
    ok_pic = ma is not None and borders_ok(ma)
    moving = False
    if ma:
        x0 = ma['x0']
        moving = np.abs(a[760:1000, x0 + 60:x0 + 1880, :] - b[760:1000, x0 + 60:x0 + 1880, :]).mean() > 1.0
    return ok_pic, moving


def scenario_faults(fx, out):
    """Acceptance 4: GL fails -> 선명도 적용 불가, 원본 표시, and the ORIGINAL picture keeps coming."""
    for fault in ['context', 'shader', 'swap:30', 'never']:
        holder = {}
        ok = connect_to_pattern(fx.port, ('--es', 'shTestFault', fault),
                                before_pick=lambda: holder.setdefault('ev', ToastEvents()), wait_viewer=False)
        check(ok, 'connected with injected fault ' + fault)
        time.sleep(9)
        toasts = holder['ev'].stop() if 'ev' in holder else []
        check_toast(fault, toasts, None)
        lines = log_lines()
        fb = [l for l in lines if 'sharpen_fallback' in l]
        direct = [l for l in lines if 'sharpen_path' in l and 'path=direct' in l]
        check(fb and 'requested=weak' in fb[-1] and 'effective=off' in fb[-1],
              fault + ': fallback logged with requested=weak effective=off', fb[-1] if fb else 'none')
        check(direct, fault + ': the decoder was rebound DIRECTLY to the view')
        pic, moving = live_picture(out, 'fault_' + fault.replace(':', '_'))
        check(pic, fault + ': the original picture is on screen after the fallback')
        check(moving, fault + ': NEW frames keep arriving on the direct path (moving square moved)')
        check(not any('sharpen_start' in l for l in lines[lines.index(fb[-1]) + 1:]) if fb else False,
              fault + ': no retry loop in the same surface episode (no renderer started after the fallback)')


def scenario_viewer_never(fx, out):
    """r2 H2: off -> 0.20 IN the viewer, with a renderer that never draws: the first-output gate is armed by
    the producer change itself (not only by SWITCHING) and falls back within its bound."""
    check(connect_to_pattern(fx.port, ('--es', 'shTestFault', 'never'), level_before='off'),
          'connected with 끄기 (direct), renderer fault "never"')
    time.sleep(3)
    adb('logcat', '-c', check_rc=False)
    check(tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls), 'viewer menu opened')
    # the weak button's place, found before the stream of events starts (no dump while it runs)
    weak = None
    end = time.time() + 15
    while weak is None and time.time() < end:
        weak = find(dump(), rid='viewerSharpWeakButton')
    if not check(weak is not None, 'the 0.20 button is on the menu'):
        return
    x0, y0, x1, y1 = bounds(weak)
    ev = ToastEvents()
    adb('shell', 'input', 'tap', str((x0 + x1) // 2), str((y0 + y1) // 2), check_rc=False)
    time.sleep(7)
    toasts = ev.stop()
    tap(rid='viewerMenuCloseButton', timeout=10)
    lines = log_lines()
    starts = [l for l in lines if 'sharpen_start' in l]
    fb = [l for l in lines if 'sharpen_fallback' in l]
    check(starts, 'the renderer was started from the viewer (off -> 0.20)')
    check(fb and 'no_output_after_ready' in fb[-1], 'no output after ready -> fallback (gate armed in VIEWER)',
          fb[-1] if fb else 'none')
    check_toast('viewer off->weak never-draw', toasts, None)
    pic, moving = live_picture(out, 'viewer_never')
    check(pic and moving, 'the original picture is on screen and live after the fallback')


def scenario_hold(fx, out):
    """r2 H1: the renderer's release is held 20 s (seam), past the app's 2 s timeout. While it holds, the view's
    surface stays unconnected whatever is asked (a level change in the menu, the viewer's own periodic sync);
    when it lets go, the request is carried out once. A dark-mode recreate during a held release frees that
    old surface exactly once, later. (A menu choice takes ~9 s here, so one request fits in the hold.)"""
    check(connect_to_pattern(fx.port, ('--es', 'shTestFault', 'hold:20000')), 'connected (0.20), releases held 20 s')
    time.sleep(3)
    adb('logcat', '-c', check_rc=False)
    choose_level('off')       # release #1 held
    choose_level('weak')      # asked while blocked: remembered only
    time.sleep(16)            # the hold (20 s from the first choice) ends; then the request
    lines = log_lines()
    with open(os.path.join(out, 'hold_phase1.txt'), 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(lines))
    to = [i for i, l in enumerate(lines) if 'sharpen_release_timeout' in l]
    rel = [i for i, l in enumerate(lines) if 'sharpen_released' in l]
    check(to, 'the release timed out at 2 s (the surface is BLOCKED, not NONE)', lines[to[0]] if to else 'none')
    if to and rel:
        between = lines[to[0]:rel[0]]
        bad = [l for l in between if 'sharpen_start' in l or ('sharpen_path' in l and 'path=direct' in l)]
        check(not bad, 'no producer connected between the timeout and the late release', '; '.join(bad[:2]))
        check('detached=1' in lines[rel[0]], 'the late release reports its EGL surface destroyed', lines[rel[0]])
        after = lines[rel[0]:]
        directs = [l for l in after if 'sharpen_path' in l and 'path=direct' in l]
        starts = [l for l in after if 'sharpen_start' in l]
        selects = [l for l in between if 'sharpness_select' in l and 'path=BLOCKED' in l]
        check(selects, 'the 0.20 choice arrived while the surface was BLOCKED', selects[0] if selects else 'none')
        check(len(starts) == 1 and not directs, 'then that request (0.20 -> a NEW renderer) is done once',
              'direct=%d start=%d' % (len(directs), len(starts)))
    else:
        check(False, 'a late release was logged', 'release lines: %d' % len(rel))
    pic, moving = live_picture(out, 'hold_after')
    check(pic and moving, 'the picture is back and live after the late release')

    # A surface destroyed while its renderer's release is held.
    choose_level('weak')
    time.sleep(4)
    adb('logcat', '-c', check_rc=False)
    choose_level('off')                     # held release on surface A (15 s)
    time.sleep(2.5)                         # past the timeout: A is BLOCKED
    adb('shell', 'cmd', 'uimode', 'night', 'yes', check_rc=False)
    time.sleep(6)
    adb('shell', 'cmd', 'uimode', 'night', 'no', check_rc=False)
    time.sleep(16)                          # A's held release ends -> A is freed (once)
    lines = log_lines()
    freed = [l for l in lines if 'sharpen_surface_released' in l]
    ids = [re.search(r'surface=(\d+)', l).group(1) for l in freed if re.search(r'surface=(\d+)', l)]
    check(freed and len(ids) == len(set(ids)), 'each app-owned surface released exactly once',
          '; '.join(l[l.index('[sharpen'):] for l in freed[:4]) if freed else 'none')
    stuck = [l for l in lines if 'sharpen_producer_stuck' in l]
    check(not stuck, 'no surface reported stuck')
    if not wait_for(rid='viewerMenuButton', timeout=5):
        pick_desktop() and wait_for(rid='viewerMenuButton', timeout=40)
        time.sleep(3)
    # A new session's first seconds can come at 1280x720 before the 최고화질 setting reaches the host; the
    # picture check is for the 1920x1080 one, so it is retried for a while.
    pic = moving = False
    for attempt in range(6):
        pic, moving = live_picture(out, 'hold_recreate')
        if pic and moving:
            break
        time.sleep(3)
    check(pic and moving, 'after the recreate the picture is on screen and live')
    bound = [l for l in log_lines() if 'viewer_surface_bound' in l]
    check(len(bound) <= 6, 'no viewer_surface_bound repeated every tick while a surface was held', 'count=%d' % len(bound))


def top_resumed():
    for l in adb('shell', 'dumpsys', 'activity', 'activities', check_rc=False, timeout=30).splitlines():
        if 'topResumedActivity' in l:
            return l.strip()
    return ''


def scenario_lifecycle(fx, out):
    """Surface churn with the filter on, then the picture is still live and nothing leaked.

    - 5x off<->weak in the viewer menu (producer switch both ways)
    - 3x viewer -> 대상 선택 화면 -> pick again (the app unbinds the view: releaseVideoSurface)
    - 3x HOME -> back, each checked to have really left and come back (topResumedActivity)
    - 1x dark mode on/off: uiMode is not in configChanges, so the activity is recreated and Android
      destroys the TextureView's surface (onSurfaceTextureDestroyed -- the app-releases path, contract 4)
    No rotation step: the viewer locks landscape on this AVD; rotation stays in the phone procedure."""
    check(connect_to_pattern(fx.port), 'connected (weak, the default)')
    time.sleep(4)
    for i in range(5):
        choose_level('off'); time.sleep(2)
        choose_level('weak'); time.sleep(2)
    for i in range(3):
        ok = tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls)
        ok = ok and tap(rid='viewerBackButton', timeout=45, reveal=scroll_menu_sheet)  # 대상 선택 화면
        ok = ok and wait_for(rid='listDevicesButton', timeout=20)
        time.sleep(1.5)
        ok = ok and pick_desktop() and wait_for(rid='viewerMenuButton', timeout=40)
        check(ok, 'viewer -> 대상 선택 화면 -> picked again (%d)' % (i + 1))
        time.sleep(3)
    for i in range(3):
        adb('shell', 'input', 'keyevent', 'KEYCODE_HOME'); time.sleep(2.5)
        away = PKG not in top_resumed()
        adb('shell', 'am', 'start', '-n', ACT); time.sleep(4)
        back = PKG in top_resumed()
        check(away and back, 'HOME really left the app and it came back (%d)' % (i + 1))
    adb('shell', 'cmd', 'uimode', 'night', 'yes', check_rc=False); time.sleep(6)
    adb('shell', 'cmd', 'uimode', 'night', 'no', check_rc=False); time.sleep(8)
    lines = log_lines()
    destroyed = [l for l in lines if 'viewer_surface_released' in l and 'destroyed=1' in l]
    check(any('appReleases=1' in l for l in destroyed),
          'the recreate destroyed the surface while the filter owned it (onSurfaceTextureDestroyed, appReleases=1)',
          destroyed[-1] if destroyed else 'none')
    pid = adb('shell', 'pidof', PKG, check_rc=False).strip()
    check(bool(pid), 'the app is still running after the churn (no crash)')
    errs = [l for l in lines if 'sharpen_release_timeout' in l or 'sharpen_fallback' in l or 'FATAL EXCEPTION' in l]
    check(not errs, 'no release timeout, no fallback, no crash over the churn', '; '.join(errs[:3]))
    starts = len([l for l in lines if 'sharpen_start' in l])
    print('      renderer starts over the run: %d' % starts, flush=True)
    if not wait_for(rid='viewerMenuButton', timeout=5):
        # after the recreate the activity may come back on the list: pick again, as the user would
        pick_desktop() and wait_for(rid='viewerMenuButton', timeout=40)
        time.sleep(3)
    a = screencap(os.path.join(out, 'life_1.png')); time.sleep(2); b = screencap(os.path.join(out, 'life_2.png'))
    ma = metrics(a)
    check(ma is not None and borders_ok(ma), 'the picture is on screen after the churn')
    if ma:
        x0 = ma['x0']
        check(np.abs(a[760:1000, x0 + 60:x0 + 1880, :] - b[760:1000, x0 + 60:x0 + 1880, :]).mean() > 1.0,
              'and it is live (new frames)')
    lines = log_lines()
    last = [l for l in lines if 'sharpen_path' in l]
    check(last and 'path=sharpen' in last[-1], 'and the filter is on it again (weak kept)', last[-1] if last else 'none')
    threads = adb('shell', 'ps', '-T', '-A', check_rc=False)
    n = len([l for l in threads.splitlines() if 'gnlink-sharpen' in l])
    check(n <= 1, 'no orphan render threads (gnlink-sharpen-* alive: %d)' % n)


def scenario_latency(fx, out, seconds):
    """S4 on the AVD: off / 0.20 / 0.35 / off, `seconds` each, same moving input. The numbers are the WHOLE
    segment's (sh_latency_segment, closed by the next level change) -- not a median of 5 s windows."""
    check(connect_to_pattern(fx.port), 'connected')
    adb('logcat', '-G', '16M', check_rc=False)
    adb('logcat', '-c', check_rc=False)
    for level in ['off', 'weak', 'strong', 'off2', 'weak2']:
        choose_level(level.rstrip('2'))
        time.sleep(seconds)
        print('      %s done' % level, flush=True)
    lines = [l for l in log_lines() if 'sh_latency_segment' in l]
    segs = []
    for l in lines:
        f = dict(kv.split('=', 1) for kv in l[l.index('sh_latency_segment') + 19:].split() if '=' in kv)
        segs.append(f)
        print('        ' + l[l.index('sh_latency_segment'):], flush=True)
    steady = [f for f in segs if int(f.get('n', '0')) >= 100]
    check(len(steady) >= 4, 'whole segments with >=100 steady frames were closed for the levels', str(len(steady)))
    with open(os.path.join(out, 'latency_segments.json'), 'w') as fh:
        json.dump(segs, fh, indent=1)


def lime_target(img):
    """The pattern's lime block (x 610..730, y 440..700 of the picture) as it is on screen: centre of the
    pixels that are clearly lime in rows that have many of them (the 1-px lime border row is excluded)."""
    m = (img[:, :, 1] > 200) & (img[:, :, 0] < 90) & (img[:, :, 2] < 90)
    count = m.sum(axis=1)
    # the block's rows only: the 1-px lime border row runs the picture's whole width
    rows = np.where((count >= 30) & (count <= 400))[0]
    if len(rows) == 0:
        return None
    ys, xs = np.nonzero(m[rows, :])
    return int(round(xs.mean())), int(round(rows[ys].mean()))


def seam_inputs():
    out = []
    for l in log_lines('seam_input'):
        g = re.search(r'kind=(\d+) x=(-?\d+) y=(-?\d+) buttons=(\d+) queued=(\d)', l)
        if g:
            out.append(tuple(int(v) for v in g.groups()))
    return out


def scenario_touch(fx, out):
    """r2 H5.1: a tap on a point of the picture becomes THAT point of the remote screen -- with 끄기, 0.20,
    0.35, and in 크게 보기. Observed at the app's queue to native (seam log: what nativeQueueInputEvent got);
    the host runs with input injection OFF, so the host's own receipt is not observed here."""
    check(connect_to_pattern(fx.port), 'connected')
    time.sleep(3)

    def tap_lime(label, one_to_one=True):
        img = screencap(os.path.join(out, 'touch_%s.png' % label))
        t = lime_target(img)
        mm = metrics(img) if one_to_one else None
        if not check(t is not None, label + ': the lime block is found on screen'):
            return
        adb('logcat', '-c', check_rc=False)
        adb('shell', 'input', 'tap', str(t[0]), str(t[1]), check_rc=False)
        time.sleep(1.5)
        ev = [e for e in seam_inputs() if e[4] == 1 and (e[1] or e[2])]
        inside = [e for e in ev if 610 <= e[1] <= 730 and 440 <= e[2] <= 700]
        near = [e for e in ev if abs(e[1] - 670) <= 6 and abs(e[2] - 570) <= 6]
        check(ev and len(inside) == len(ev), label + ': every queued event lands inside the lime block (610..730, 440..700)',
              'screen=%s events=%s' % (t, ev[:4]))
        check(near, label + ': at its centre (670,570) within 6 px', 'events=%s' % ev[:4])
        if one_to_one and mm and ev:
            # 1:1 view: the remote point is exactly the tapped screen point minus the picture's origin
            exp = (t[0] - mm['x0'], t[1])
            check(all(abs(e[1] - exp[0]) <= 1 and abs(e[2] - exp[1]) <= 1 for e in ev),
                  label + ': remote = tapped screen point - picture origin (within 1 px)',
                  'expected=%s events=%s' % (exp, ev[:2]))

    for level in ['off', 'weak', 'strong']:
        choose_level(level)
        time.sleep(3)
        tap_lime(level)
    # 크게 보기 (the viewport enlarges the picture; touches must follow)
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):
        tap(rid='viewerMenuCloseButton', timeout=10)
        time.sleep(3)
        tap_lime('large_weak', one_to_one=False)
    else:
        check(False, '크게 보기 could be chosen')


def scenario_resize(fx, out):
    """r2 H5.3: a picture-size change while the filter is live -- switching from the 1920x1080 desktop to a
    second, 1280x720 pattern window through the viewer's own window sheet (SWITCHING, where the app defers the
    output-buffer resize). The renderer must step by each frame's own visible size (src= in sh_latency): the
    new 1280x720 as soon as its frames come, not the output buffer's size. (A quality change alone keeps the
    desktop at 1920x1080 here, so it does not test this.)"""
    small = subprocess.Popen(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                              os.path.join(HERE, 'sh_pattern_window.ps1'), '-Seconds', '600', '-Left', '320',
                              '-Top', '180', '-Width', '1280', '-Height', '720', '-Title', 'GNLINK-SH-SMALL'])
    try:
        time.sleep(3)
        check(connect_to_pattern(fx.port), 'connected (0.20, 최고화질, desktop 1920x1080)')
        time.sleep(6)
        adb('logcat', '-c', check_rc=False)
        ok = tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and             tap(rid='viewerMenuTargetsButton', timeout=45, reveal=scroll_menu_sheet)
        check(ok, 'the viewer window sheet opened')
        check(tap(text='GNLINK-SH-SMALL', timeout=30), 'the 1280x720 window picked in the viewer')
        time.sleep(15)
        lines = log_lines()
        sizes = [re.search(r'video=(\d+)x(\d+)', l) for l in lines if '[video_size]' in l]
        sizes = [(int(m.group(1)), int(m.group(2))) for m in sizes if m]
        srcs = [re.search(r'src=(\d+)x(\d+) out=(\d+)x(\d+)', l) for l in lines if 'sh_latency' in l]
        srcs = [tuple(int(v) for v in m.groups()) for m in srcs if m]
        deferred = [l for l in lines if 'viewer_surface_resize_deferred' in l]
        print('      video sizes: %s  renderer src/out: %s  deferred: %d' % (sizes, srcs, len(deferred)), flush=True)
        check(sizes and sizes[-1] == (1280, 720), 'the picture size changed to 1280x720', str(sizes))
        check(srcs and srcs[-1][:2] == (1280, 720), 'the renderer steps by the NEW visible size (1280x720)', str(srcs[-1:]))
        ne = [int(m.group(1)) for m in (re.search(r'srcNeOut=(\d+)', l) for l in lines if 'sh_latency' in l) if m]
        print('      frames drawn while output size != frame size (step = the frame size): %s' % ne, flush=True)
        fb = [l for l in lines if 'sharpen_fallback' in l]
        check(not fb, 'no fallback across the size change', fb[-1] if fb else '')
        check(any('sharpen_path' in l and 'path=sharpen' in l for l in lines) or
              not any('sharpen_path' in l and 'path=direct' in l for l in lines),
              'the filter stayed on (no direct path taken)')
        a = screencap(os.path.join(out, 'resize_1.png'))
        check(float(a.std()) > 10.0, 'a picture is on screen after the change')
    finally:
        try:
            small.terminate(); small.wait(10)
        except Exception:
            pass


def fixture_visible(pattern):
    """The fixture's visible picture (SharpenShaderFixture.pixel), rows 0..39 of the 64x48 buffer."""
    v = np.zeros((40, 64, 3), dtype=np.float64)
    for y in range(40):
        for x in range(64):
            if pattern == 'alt':
                v[y, x] = (40 + 2 * x + (x % 2) * 50, 40 + 3 * y + (y % 2) * 50, 170 - x - y + ((x + y) % 2) * 30)
            elif pattern == 'text':
                if 8 <= x <= 39 and 4 <= y <= 35:
                    g = 228 if (x // 2) % 2 == 0 else 28
                elif y == 20:
                    g = 228
                elif x >= 44 and 4 <= y <= 35:
                    g = 120 if (x + y) % 2 == 0 else 136
                else:
                    g = 28
                v[y, x] = (g, g, (g * 3 + 255) // 4)
            else:
                v[y, x] = (32 + 3 * x, 40 + 4 * y, 200 - 2 * x - y)
    return v


def kernel(img, s):
    """out = (1+4s)C - s(L+R+T+B), each neighbour clamped to the picture's own edge, clamped 0..255."""
    p = np.pad(img, ((1, 1), (1, 1), (0, 0)), mode='edge')
    c = p[1:-1, 1:-1]
    n = p[1:-1, :-2] + p[1:-1, 2:] + p[:-2, 1:-1] + p[2:, 1:-1]
    return np.clip((1 + 4 * s) * c - s * n, 0, 255)


def scenario_shader(fx, out):
    """r3 (Codex R3): the production renderer/shader on a frame with a REAL crop (64x48 buffer, 64x40 visible,
    magenta padding) and buffer transforms (mirror, quarter turn), fed through its input BufferQueue by the
    seam fixture; output read back from an ImageReader. Checked against the kernel on the visible picture."""
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    adb('shell', 'run-as', PKG, 'sh', '-c', 'rm -f files/sh_fixture*', check_rc=False)
    adb('logcat', '-c', check_rc=False)
    adb('shell', 'am', 'start', '-n', ACT, '--es', 'shTestFixture', 'shader')
    done = None
    for _ in range(60):
        lines = log_lines('sh_fixture_done')
        if lines:
            done = lines[-1]
            break
        time.sleep(1)
    if not check(done is not None, 'the fixture ran (sh_fixture_done)'):
        return
    summary = json.loads(done[done.index('{'):])
    print('      ' + json.dumps(summary)[:600], flush=True)
    results = {}
    for c in summary.get('cases', []):
        name = c['name']
        raw = adb('exec-out', 'run-as', PKG, 'cat', 'files/sh_fixture_%s.rgba' % name, binary=True, timeout=60)
        w, h = c['outW'], c['outH']
        ok_size = len(raw) == w * h * 4 and c['note'] == 'ok'
        check(ok_size, name + ': output read back (%dx%d)' % (w, h), 'note=%s bytes=%d' % (c['note'], len(raw)))
        if not ok_size:
            continue
        got = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 4)[:, :, :3].astype(np.float64)
        vis = fixture_visible(c.get('pattern', 'linear'))
        if c['transform'] == 0:
            disp = vis
        elif c['transform'] == 1:
            disp = vis[:, ::-1]
        else:  # ROT_90: the source turned 90 degrees clockwise
            disp = np.rot90(vis, k=-1)
        exp = kernel(disp, c['strength'])
        err = np.abs(got - exp)
        # orientation: the other candidates must be clearly worse
        alts = {'none': vis, 'mirror': vis[:, ::-1], 'rot_cw': np.rot90(vis, k=-1), 'rot_ccw': np.rot90(vis, k=1),
                'flip_v': vis[::-1]}
        alt_err = {k: float(np.abs(got - kernel(a, c['strength'])).mean()) for k, a in alts.items()
                   if a.shape == got.shape}
        best = min(alt_err, key=alt_err.get)
        edge_rows = {'first_row': float(err[0].max()), 'last_row': float(err[-1].max()),
                     'first_col': float(err[:, 0].max()), 'last_col': float(err[:, -1].max())}
        is_mag = lambda a: (a[:, :, 0] > 200) & (a[:, :, 1] < 60) & (a[:, :, 2] > 200)
        magenta = float((is_mag(got) & ~is_mag(exp)).sum())  # magenta where the picture has none
        results[name] = {'max_err': float(err.max()), 'mean_err': round(float(err.mean()), 3), 'edges': edge_rows,
                         'orientation_best': best, 'alt_mean_err': {k: round(v, 2) for k, v in alt_err.items()},
                         'magenta_pixels': magenta}
        print('      %s: %s' % (name, json.dumps(results[name])), flush=True)
        want = {0: 'none', 1: 'mirror', 4: 'rot_cw'}[c['transform']]
        if c.get('control'):
            # A deliberately broken renderer (seam fault): the checks below must catch it.
            check(err.max() > 6 or magenta > 0, name + ': NEGATIVE CONTROL -- the broken renderer is caught',
                  'max=%.1f magenta=%d edges=%s' % (err.max(), magenta, edge_rows))
            continue
        check(best == want, name + ': orientation is the buffer transform\'s (%s)' % want, 'best=%s' % best)
        check(magenta == 0, name + ': no padding (magenta) pixel reached the output')
        check(err.max() <= 6, name + ': every output pixel, edges included, matches the kernel on the visible picture (max |d| <= 6)',
              'max=%.1f edges=%s' % (err.max(), edge_rows))
    with open(os.path.join(out, 'shader_fixture.json'), 'w') as fh:
        json.dump(results, fh, indent=1)


def shader_model(src, s, out_w, out_h):
    """sh-strength r2: what the production shader computes, on the CPU -- each output pixel centre mapped onto the
    visible picture (the B placement covers the whole output; out == visible size is A), the five taps one SOURCE
    pixel apart, each clamped half a pixel inside the picture, bilinear with clamp-to-edge, ONE final clamp.
    At out == visible size this is kernel()."""
    h, w = src.shape[:2]
    uu = (np.arange(out_w) + 0.5) / out_w
    vv = (np.arange(out_h) + 0.5) / out_h
    U, V = np.meshgrid(uu, vv)

    def samp(a, b):
        a = np.clip(a, 0.5 / w, 1 - 0.5 / w)
        b = np.clip(b, 0.5 / h, 1 - 0.5 / h)
        x = a * w - 0.5
        y = b * h - 0.5
        x0 = np.clip(np.floor(x).astype(int), 0, w - 1)
        y0 = np.clip(np.floor(y).astype(int), 0, h - 1)
        x1 = np.clip(x0 + 1, 0, w - 1)
        y1 = np.clip(y0 + 1, 0, h - 1)
        fx = (x - np.floor(x))[..., None]
        fy = (y - np.floor(y))[..., None]
        top = src[y0, x0] * (1 - fx) + src[y0, x1] * fx
        bot = src[y1, x0] * (1 - fx) + src[y1, x1] * fx
        return top * (1 - fy) + bot * fy

    c = samp(U, V)
    n = samp(U + 1.0 / w, V) + samp(U - 1.0 / w, V) + samp(U, V + 1.0 / h) + samp(U, V - 1.0 / h)
    return np.clip((1 + 4 * s) * c - s * n, 0, 255)


def upscale_bilinear(img, out_w, out_h):
    """A's last step stood in on the CPU: the compositor's enlargement of the source-size output (a model --
    the real compositor's filter is the device's; the AVD compare captures the real one)."""
    return shader_model(img, 0.0, out_w, out_h)


def picture_stats(img, region):
    """Edge energy (mean |horizontal gradient|), and the clipped share (0 or 255 in any channel), over `region`."""
    y0, y1, x0, x1 = region
    a = img[y0:y1, x0:x1]
    g = np.abs(np.diff(a.mean(axis=2), axis=1)).mean()
    clip = float(((a <= 0.5) | (a >= 254.5)).any(axis=2).mean())
    return round(float(g), 3), round(clip, 4)


def scenario_strength_ab(fx, out):
    """sh-strength r2 (Codex §4/§4B): one input frame through the production renderer -- A at 0/0.20/0.35/0.60 and B
    (display-size, one pass) at x1.33/x2 -- read back and checked against shader_model; then the strength effect
    (A s vs A 0) and the position effect (A s enlarged afterwards vs B s at the same size) on the same frame."""
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    adb('shell', 'run-as', PKG, 'sh', '-c', 'rm -f files/sh_fixture*', check_rc=False)
    adb('logcat', '-c', check_rc=False)
    adb('shell', 'am', 'start', '-n', ACT, '--es', 'shTestFixture', 'strengthab')
    done = None
    for _ in range(120):
        lines = log_lines('sh_fixture_done')
        if lines:
            done = lines[-1]
            break
        time.sleep(1)
    if not check(done is not None, 'the fixture ran (sh_fixture_done)'):
        return
    # r4: the whole summary from the app's file -- logcat cuts a line past ~4 KB
    raw_summary = adb('exec-out', 'run-as', PKG, 'cat', 'files/sh_fixture.json', binary=True, timeout=60)
    summary = json.loads(raw_summary.decode('utf-8') if raw_summary else done[done.index('{'):])
    vis = fixture_visible('text')
    got = {}
    results = {}
    pan_case = None
    for c in summary.get('cases', []):
        name = c['name']
        if name.endswith('_noring'):
            check(c['note'] == 'no_output' and c.get('heldNoSource', 0) >= 1 and c.get('drawn', 0) == 0,
                  name + ': B without a source size draws NOTHING (held, no output-size fallback)',
                  'note=%s held=%s drawn=%s' % (c['note'], c.get('heldNoSource'), c.get('drawn')))
            continue
        if c.get('flow') == 'layout_fail':
            # sh-strength r4 (Codex codex_sh_strength_r3.md N1 a): never video into a buffer of the wrong size --
            # bounded cleared swaps, then the renderer fails (the policy's way back to direct)
            check(c['note'].startswith('failed:layout') and c.get('drawn', -1) == 0 and 2 <= c.get('clearSwaps', 0) <= 120,
                  name + ': the output never the view\'s size -> NO video frame drawn, bounded cleared swaps, then failed (-> direct)',
                  'note=%s drawn=%s clearSwaps=%s' % (c['note'][:90], c.get('drawn'), c.get('clearSwaps')))
            continue
        if c.get('flow') == 'pan_redraw':
            pan_case = c
            continue
        raw = adb('exec-out', 'run-as', PKG, 'cat', 'files/sh_fixture_%s.rgba' % name, binary=True, timeout=60)
        w, h = c['outW'], c['outH']
        ok = len(raw) == w * h * 4 and c['note'] == 'ok'
        check(ok, name + ': output read back (%dx%d)' % (w, h), 'note=%s bytes=%d src=%s' % (c['note'], len(raw), c.get('src')))
        if not ok:
            continue
        img = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 4)[:, :, :3].astype(np.float64)
        got[name] = img
        # r3: the case's own picture -- the pattern it was fed, turned as its buffer transform turns it
        src = fixture_visible(c.get('pattern', 'text'))
        if c['transform'] == 4:
            src = np.rot90(src, k=-1)
        exp = shader_model(src, c['strength'], w, h)
        is_mag = lambda a: (a[:, :, 0] > 200) & (a[:, :, 1] < 60) & (a[:, :, 2] > 200)
        if (is_mag(img) & ~is_mag(exp)).sum() > 0:
            check(False, name + ': no padding (magenta) pixel reaches the output', str(int((is_mag(img) & ~is_mag(exp)).sum())))
        err = np.abs(img - exp)
        results[name] = {'size': [w, h], 'max_err': float(err.max()), 'mean_err': round(float(err.mean()), 3)}
        if c.get('control'):
            check(err.max() > 6, name + ': NEGATIVE CONTROL -- output-pixel taps are caught', 'max=%.1f' % err.max())
            continue
        check(err.max() <= 6, name + ': matches the shader model on the same frame (max |d| <= 6)',
              'max=%.1f mean=%.3f' % (err.max(), err.mean()))
    # ---- r4 (N1 b): a pan with no new decoder frame -> the latched frame redrawn once at the new place
    pc = pan_case
    if pc is not None:
        name = pc['name']
        raw = adb('exec-out', 'run-as', PKG, 'cat', 'files/sh_fixture_%s.rgba' % name, binary=True, timeout=60)
        w, h = pc['outW'], pc['outH']
        base = got.get('textB133_s35')
        ok = len(raw) == w * h * 4 and pc['note'] == 'ok' and base is not None
        check(pc['note'] == 'ok' and pc.get('redrawsAfterPan') == 1 and pc.get('drawn') == 1 and pc.get('drawnAtPan') == 1,
              name + ': no new frame, a moved placement (size check lying 3x) -> exactly ONE redraw, no new frame drawn',
              'note=%s redraws=%s drawn=%s clearSwaps=%s' % (pc['note'][:60], pc.get('redrawsAfterPan'), pc.get('drawn'), pc.get('clearSwaps')))
        check(pc.get('clearSwaps', 0) >= 1 and pc.get('lastTs') == pc.get('firstTs') and pc.get('firstTs', 0) > 0,
              name + ': the redraw keeps the frame\'s timestamp (after the cleared swaps)',
              'first=%s last=%s' % (pc.get('firstTs'), pc.get('lastTs')))
        if ok:
            img = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 4)[:, :, :3].astype(np.float64)
            dx = int(round(w / 4.0))
            moved = np.abs(img[:, :w - dx - 2] - base[:, dx:w - 2])
            right = img[:, w - dx + 2:]
            check(moved.max() <= 6 and right.max() <= 8,
                  name + ': the redraw shows the picture at the NEW place (shifted by %d px, black beyond)' % dx,
                  'moved max=%.1f right max=%.1f' % (moved.max(), right.max()))
        else:
            check(False, name + ': the redraw read back', 'bytes=%d base=%s' % (len(raw), base is not None))
    else:
        check(False, 'r4: the pan redraw case ran')

    # ---- the comparisons (same input frame)
    bars = lambda w, h: (int(4 * h / 40), int(36 * h / 40), int(8 * w / 64), int(40 * w / 64))
    low = lambda w, h: (int(4 * h / 40), int(36 * h / 40), int(44 * w / 64), w)
    table = {}
    for name, img in got.items():
        h, w = img.shape[:2]
        e_bar, c_bar = picture_stats(img, bars(w, h))
        e_low, _ = picture_stats(img, low(w, h))
        table[name] = {'bars_edge': e_bar, 'bars_clipped': c_bar, 'lowcontrast_edge': e_low}
    a0 = got.get('textA_s00')
    for st in ('20', '35', '60'):
        a = got.get('textA_s' + st)
        if a is not None and a0 is not None:
            d = np.abs(a - a0)
            table['textA_s' + st]['vs_A0_mean'] = round(float(d.mean()), 3)
            table['textA_s' + st]['vs_A0_p99'] = round(float(np.percentile(d, 99)), 2)
    if got.get('textA_s20') is not None and got.get('textA_s35') is not None:
        d = np.abs(got['textA_s35'] - got['textA_s20'])
        check(d.max() > 6, 'A: 0.35 and 0.20 are DIFFERENT final pixels on the same frame (the strength reaches the output)',
              'mean=%.3f max=%.1f' % (d.mean(), d.max()))
    if got.get('textA_s60') is not None and got.get('textA_s35') is not None:
        d = np.abs(got['textA_s60'] - got['textA_s35'])
        check(d.max() > 6, 'A: 0.60 differs from 0.35 on the same frame', 'mean=%.3f max=%.1f' % (d.mean(), d.max()))
    for scale in ('B133', 'B2'):
        for st in ('00', '20', '35', '60'):
            b = got.get('text%s_s%s' % (scale, st))
            a = got.get('textA_s' + st)
            if b is None or a is None:
                continue
            h, w = b.shape[:2]
            a_up = upscale_bilinear(a, w, h)
            d = np.abs(b - a_up)
            e_aup, c_aup = picture_stats(a_up, bars(w, h))
            table['text%s_s%s' % (scale, st)].update({
                'vs_Aup_mean': round(float(d.mean()), 3), 'vs_Aup_p99': round(float(np.percentile(d, 99)), 2),
                'vs_Aup_max': round(float(d.max()), 1), 'Aup_bars_edge': e_aup, 'Aup_bars_clipped': c_aup})
    print('      ' + json.dumps(table), flush=True)
    with open(os.path.join(out, 'strength_ab.json'), 'w') as fh:
        json.dump({'checks': results, 'table': table}, fh, indent=1)
    for name, img in got.items():
        Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).resize(
            (img.shape[1] * 4, img.shape[0] * 4), Image.NEAREST).save(os.path.join(out, name + '_x4.png'))


CMP_SCALE = 4.0 / 3.0  # a 1440x810 pattern window shown 1920x1080 on the AVD's 1080-high screen
CMP_REGIONS = {'A_text': (60, 40, 900, 400), 'B_lowcontrast': (960, 40, 1430, 400), 'C_lines': (60, 440, 440, 700),
               'D_flat': (490, 450, 590, 690), 'E_checker': (1040, 440, 1400, 700)}


def cmp_metrics(img):
    """metrics() for the x1.33 picture: the same pattern regions, scaled onto the screen."""
    x0 = video_origin(img)
    if x0 is None:
        return None
    m = {'x0': x0}
    for k, (l, t, r, b) in CMP_REGIONS.items():
        reg = img[int(t * CMP_SCALE):int(b * CMP_SCALE), x0 + int(l * CMP_SCALE):x0 + int(r * CMP_SCALE), :]
        m[k + '_edge'] = round(edge_energy(reg), 3)
        g = reg.mean(axis=2)
        m[k + '_clip'] = round(float(((g <= 2) | (g >= 253)).mean()), 4)
    top = img[0, x0 + 5:x0 + VIDEO_W - 5, :].mean(axis=0)
    bottom = img[VIDEO_H - 1, x0 + 5:x0 + VIDEO_W - 5, :].mean(axis=0)
    right = img[5:VIDEO_H - 5, x0 + VIDEO_W - 1, :].mean(axis=0)
    m['border_top_rgb'] = [round(float(v)) for v in top]
    m['border_bottom_rgb'] = [round(float(v)) for v in bottom]
    m['border_right_rgb'] = [round(float(v)) for v in right]
    return m


def scenario_compare_ab(fx, out, offline=False):
    """sh-strength r2 (Codex §4/§4B): the REAL APK path on the AVD, the same static picture (an off-screen layered
    1440x810 pattern window, captured as a window) shown x1.33 -- A (source-size pass, the compositor enlarges) at
    off/0.20/0.35/0.60 and B (display-size pass, one draw) at 0.20/0.35/0.60, plus B's double-transform negative
    control. Each configuration is its own launch (B is a launch extra of the test build)."""
    configs = [
        ('A_off', 'off', ()), ('A_s20', 'weak', ()), ('A_s35', 'strong', ('--ef', 'shTestStrength', '0.35')),
        ('A_s60', 'strong', ()),
        ('B_s20', 'weak', ('--ez', 'shTestDisplayPass', 'true')),
        ('B_s35', 'strong', ('--ez', 'shTestDisplayPass', 'true', '--ef', 'shTestStrength', '0.35')),
        ('B_s60', 'strong', ('--ez', 'shTestDisplayPass', 'true')),
        ('Bdouble_s35', 'strong', ('--ez', 'shTestDisplayPassDouble', 'true', '--ef', 'shTestStrength', '0.35')),
    ]
    shots = {}
    imgs = {}
    for name, level, extras in configs:
        if offline:
            # The judgement again on the screenshots a run saved (no AVD, no host): --scenario compare_ab_offline.
            img = np.asarray(Image.open(os.path.join(out, 'shot_%s.png' % name)).convert('RGB'))
            imgs[name] = img
            shots[name] = cmp_metrics(img)
            print('      %s: %s' % (name, json.dumps(shots[name])), flush=True)
            continue
        ok = connect_to_pattern(fx.port, extras, level_before=level, window=True)
        check(ok, name + ': the APK connects and shows the pattern window (real UI, window pick)')
        time.sleep(6)
        img = screencap(os.path.join(out, 'shot_%s.png' % name))
        imgs[name] = img
        shots[name] = cmp_metrics(img)
        lines = log_lines()
        place = [l[l.index('sharpen_display_pass'):] for l in lines if 'sharpen_display_pass' in l]
        sizes = [l[l.index('sharpen_'):][:160] for l in lines if 'sharpen_path' in l or 'sh_output' in l][-2:]
        print('      %s: %s | %s | %s' % (name, json.dumps(shots[name]), place[-1:] , sizes), flush=True)
        if name.startswith('B'):
            check(bool(place), name + ': the app ran the display-size pass (placement logged)', str(place[-1:]))
    good = [k for k in shots if not k.startswith('Bdouble')]
    for k in good:
        check(shots[k] is not None and borders_ok(shots[k]), k + ': the picture is found with its four borders where the x1.33 picture\'s edges are',
              str(shots[k] and (shots[k]['border_top_rgb'], shots[k]['border_bottom_rgb'], shots[k]['border_right_rgb'])))
    if all(shots[k] for k in good):
        check(len({shots[k]['x0'] for k in good}) == 1, 'A and B place the picture at the same screen x (no double placement)',
              str({k: shots[k]['x0'] for k in good}))
        # the same picture, pixel for pixel in placement: flat colour identical, and B s vs A s differ only at edges
        for st in ('s20', 's35', 's60'):
            a, b = imgs['A_' + st].astype(np.float64), imgs['B_' + st].astype(np.float64)
            x0 = shots['A_' + st]['x0']
            l, t, r, bb = CMP_REGIONS['D_flat']
            fa = a[int(t * CMP_SCALE):int(bb * CMP_SCALE), x0 + int(l * CMP_SCALE):x0 + int(r * CMP_SCALE)]
            fb = b[int(t * CMP_SCALE):int(bb * CMP_SCALE), x0 + int(l * CMP_SCALE):x0 + int(r * CMP_SCALE)]
            check(np.abs(fa.mean(axis=(0, 1)) - fb.mean(axis=(0, 1))).max() < 4,
                  'A vs B %s: flat colour the same (colour / geometry of the two paths agree)' % st,
                  '%s vs %s' % (fa.mean(axis=(0, 1)).round(1), fb.mean(axis=(0, 1)).round(1)))
        for path in ('A', 'B'):
            seq = [('A_off' if path == 'A' else None), path + '_s20', path + '_s35', path + '_s60']
            seq = [k for k in seq if k]
            vals = [shots[k]['A_text_edge'] for k in seq]
            check(all(x < y for x, y in zip(vals, vals[1:])), path + ': text edge energy rises with the strength', str(dict(zip(seq, vals))))
    d = shots.get('Bdouble_s35')
    caught = d is None or not borders_ok(d) or (shots.get('B_s35') and d['x0'] != shots['B_s35']['x0'])
    check(caught, 'NEGATIVE CONTROL: B with the TextureView placement applied as well (double transform) is caught',
          str(d and {'x0': d['x0'], 'borders': (d['border_top_rgb'], d['border_bottom_rgb'])}))
    with open(os.path.join(out, 'compare_ab.json'), 'w') as fh:
        json.dump(shots, fh, indent=1)


def scenario_latency_ab(fx, out, seconds):
    """sh-strength r2 cost (Codex §4B: 1 pass with more output pixels is not free): the same changing input (the
    off-screen layered pattern, a block flipped every 33 ms) shown x1.33 -- A off (direct), A 0.60, B 0.60, each its
    own launch, `seconds` long. Read from the app's own 5 s lines: handoff->latch window p95, GL draw wall time,
    frames drawn / skipped."""
    configs = [('A_off', 'off', ()), ('A_s60', 'strong', ()), ('B_s60', 'strong', ('--ez', 'shTestDisplayPass', 'true'))]
    res = {}
    for name, level, extras in configs:
        ok = connect_to_pattern(fx.port, extras, level_before=level, window=True)
        check(ok, name + ': connected and viewing the pattern window')
        adb('logcat', '-c', check_rc=False)
        time.sleep(seconds)
        lines = [l[l.index('[sh_latency] '):] for l in log_lines() if '[sh_latency] ' in l]
        draws, p95s, drawn, skipped, redraws = [], [], 0, 0, 0
        for l in lines:
            f = dict(kv.split('=', 1) for kv in l.replace('|', ' ').split() if '=' in kv)
            if 'drawWallUs' in f and f.get('glDrawn', '0') != '0':
                draws.append(int(f['drawWallUs']))
            # the WINDOW's p95 is the first p95us of the line (the segment's comes after 'segment:')
            win = l.split('| segment:')[0]
            wf = dict(kv.split('=', 1) for kv in win.split() if '=' in kv)
            if 'p95us' in wf:
                try:
                    p95s.append(int(wf['p95us']))
                except ValueError:
                    pass
            drawn += int(f.get('glDrawn', '0'))
            skipped += int(f.get('glSkipped', '0'))
            redraws += int(f.get('glRedraws', '0'))  # r3 (N2): redraws of a latched frame (no new picture)
        res[name] = {'lines': len(lines), 'drawWallUs': draws, 'window_p95us': p95s,
                     'glDrawn': drawn, 'glSkipped': skipped, 'glRedraws': redraws, 'sample': lines[-1][:400] if lines else ''}
        print('      %s: %s' % (name, json.dumps(res[name])), flush=True)
        check(len(lines) >= 3, name + ': the app reported its 5 s latency lines', str(len(lines)))
    with open(os.path.join(out, 'latency_ab.json'), 'w') as fh:
        json.dump(res, fh, indent=1)


def ua2_controls():
    """The three backend buttons and the line under them, as rendered (uiautomator: visible bounds, enabled,
    selected, text)."""
    root = dump()
    out = {}
    for rid in ('settingsDesktopBackendDxgiButton', 'settingsDesktopBackendWgcButton', 'settingsDesktopBackendGdiButton',
                'settingsDesktopBackendStatusText'):
        n = None
        for node in (root.iter('node') if root is not None else []):
            if node.get('resource-id') == PKG + ':id/' + rid:
                n = node
                break
        out[rid] = None if n is None else {'enabled': n.get('enabled') == 'true', 'selected': n.get('selected') == 'true',
                                           'text': n.get('text') or '', 'bounds': n.get('bounds')}
    return out


def show_backend_controls():
    """The settings tab, scrolled until the backend buttons are on screen."""
    tap(rid='listSettingsButton', timeout=20)
    for _ in range(8):
        c = ua2_controls()
        if c['settingsDesktopBackendWgcButton'] and c['settingsDesktopBackendStatusText']:
            return c
        adb('shell', 'input', 'swipe', '540', '1800', '540', '900', '300', check_rc=False)
        time.sleep(1)
    return ua2_controls()


def backend_requests(fx):
    """The CHANGE requests the host logged (a question -- backend 0 -- is not logged as one)."""
    log = open(os.path.join(fx.out, 'host.log'), 'rb').read().decode(errors='replace')
    return re.findall(r'desktop-backend-request seq=\d+ backend=(\w+)', log)


def scenario_ua2_desktop(fx, out):
    """ua2-apk r3 (Codex codex_ua2_apk_src_r2.md N1, the desktop target): the settings line through the real APK UI
    while the DESKTOP is captured (DXGI at the test host) -- the host's own state first (no request: "지금 바탕화면
    캡처: DXGI", never a confirmed state before the host has one), then a GDI pick: "적용됨" ONLY if the host's answer
    says GDI captures now, else "접수 · 아직 이 방식으로 캡처하지 않음"; then back to DXGI. Each step's screen saved."""
    ok = connect_to_pattern(fx.port, best_quality=False)  # the desktop (no window pick)
    check(ok, 'connected and viewing the DESKTOP')
    time.sleep(3)
    for _ in range(3):
        if find(dump(), rid='listSettingsButton') is not None:
            break
        adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
        time.sleep(2)
    show_backend_controls()
    time.sleep(4)  # the screen asks the host (about every 2 s)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2d_1_idle.png'))
    st, d = c['settingsDesktopBackendStatusText'], c['settingsDesktopBackendDxgiButton']
    states = [re.search(r'state=(\w+)', l).group(1) for l in log_lines('desktop_backend_ui') if re.search(r'state=(\w+)', l)]
    print('      states so far: %s' % states, flush=True)
    check(st and st['text'].startswith('지금 바탕화면 캡처: DXGI') and d and d['selected'],
          'desktop, no request: the host\'s own -- "지금 바탕화면 캡처: DXGI", DXGI lit (IDLE)', str(st))
    check('IDLE' in states and all(x in ('CHECKING', 'IDLE') for x in states),
          'before the request only CHECKING (no state yet) and then IDLE were shown', str(states))

    def pick(rid, label, code):
        adb('logcat', '-c', check_rc=False)
        before = len(backend_requests(fx))
        for _ in range(2):
            tap_node(find(dump(), rid=rid))
            time.sleep(3)
            if len(backend_requests(fx)) > before:
                break
        time.sleep(4)
        c = ua2_controls()
        screencap(os.path.join(out, 'ua2d_%s.png' % label))
        st = c['settingsDesktopBackendStatusText']
        hlog = open(os.path.join(out, 'host.log'), 'rb').read().decode(errors='replace')
        ans = re.findall(r'desktop-backend-status seq=\d+ requested=(\d) effective=(\d) active=(\d) flags=0x([0-9a-f]+)', hlog)
        last = ans[-1] if ans else None
        print('      %s: host answer %s  line %r' % (label, last, st and st['text']), flush=True)
        if not check(last is not None and last[0] == code, label + ': the host handled the request (requested=%s)' % code, str(last)):
            return
        text = st['text'] if st else ''
        name = {'1': 'DXGI', '2': 'WGC', '3': 'GDI'}[code]
        if last[2] == code:
            check(text.startswith('요청: %s 적용됨' % name), label + ': captures with it now -> "적용됨"', text)
        elif last[1] != code and last[2] == last[1]:
            check(text.startswith('요청: %s 접수 · PC 정책으로' % name), label + ': the host\'s policy uses another -> "PC 정책으로"', text)
        else:
            check(text.startswith('요청: %s 접수 · 아직 이 방식으로 캡처하지 않음' % name),
                  label + ': handled, another captures now -> "접수 · 아직 …" (never "적용됨")', text)
        check('지금 바탕화면 캡처: %s' % {'1': 'DXGI', '2': 'WGC', '3': 'GDI'}.get(last[2], '?') in text,
              label + ': the line says what captures now (the host\'s active)', text)

    pick('settingsDesktopBackendGdiButton', '2_gdi', '3')
    pick('settingsDesktopBackendDxgiButton', '3_dxgi', '1')


def scenario_ua2(fx, out):
    """ua2-apk r1 (Codex codex_rel_0163_r1.md §1): the desktop capture backend is this connection's only. Through
    the real APK UI against the test host: an OLD stored global value is not sent; before a connection the controls
    are off; connected, the buttons show what the HOST runs; a pick goes to this connection; a new connection asks
    nothing and shows the host default again; the old key is gone; the sharpness choice is kept."""
    # An older APK's stored state: WGC kept globally (re-sent on every connect), and 강하게 chosen.
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    prefs = ('<?xml version="1.0" encoding="utf-8" standalone="yes" ?>\n<map>\n'
             '    <int name="desktop_backend" value="2" />\n    <string name="sharpness">strong</string>\n</map>\n')
    tmp = os.path.join(out, 'seed_prefs.xml')
    with open(tmp, 'w', encoding='utf-8') as fh:
        fh.write(prefs)
    adb('push', tmp, '/data/local/tmp/gnlink_seed_prefs.xml')
    # one string: adb shell joins its arguments and the quoting of sh -c would be lost
    adb('shell', "run-as %s sh -c 'mkdir -p shared_prefs && cp /data/local/tmp/gnlink_seed_prefs.xml "
                 "shared_prefs/remote60_android_direct.xml'" % PKG)
    seeded = adb('shell', 'run-as', PKG, 'cat', 'shared_prefs/remote60_android_direct.xml', check_rc=False)
    check('desktop_backend' in seeded and 'strong' in seeded, 'an older APK\'s stored WGC (global) and 강하게 are seeded', seeded[-160:])

    # before a connection
    launch(fx.port)
    tap(rid='loginManualButton', timeout=30)
    time.sleep(2)
    if find(dump(), rid='listSettingsButton') is not None:
        c = show_backend_controls()
        w = c['settingsDesktopBackendWgcButton']
        st = c['settingsDesktopBackendStatusText']
        screencap(os.path.join(out, 'ua2_1_not_connected.png'))
        check(w is not None and not w['enabled'] and st is not None and '연결한 뒤' in st['text'],
              'not connected: the backend buttons are shown DISABLED with "연결한 뒤 고를 수 있습니다"', str(c))
    else:
        screencap(os.path.join(out, 'ua2_1_not_connected.png'))
        print('      N/A  the settings tab is not reachable before a connection (manual connect screen)', flush=True)
    # connect (manual) -- the seeded WGC must not be sent
    ok = connect_to_pattern(fx.port, best_quality=False, window=True)
    check(ok, 'connected and viewing (window pick)')
    time.sleep(3)
    check(backend_requests(fx) == [], 'on connect the stored WGC was NOT sent (no backend change request at the host)',
          str(backend_requests(fx)))
    # the viewer -> the list -> settings
    for _ in range(3):
        if find(dump(), rid='listSettingsButton') is not None:
            break
        adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
        time.sleep(2)
    c = show_backend_controls()
    time.sleep(3)  # the screen asks the host (every 2 s)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2_2_connected.png'))
    w, d, st = c['settingsDesktopBackendWgcButton'], c['settingsDesktopBackendDxgiButton'], c['settingsDesktopBackendStatusText']
    check(d and w and d['enabled'] and w['enabled'], 'connected: the buttons are enabled', str(c))
    # r2 (Codex N2/N3): a window is being captured -- the line says WGC NOW (not the dormant desktop backend), the scope
    # only because the host confirms it, and the lit button is this connection's desktop method as handled (DXGI)
    check(st and st['text'] == '지금 창 캡처: WGC · 이 연결에만 적용' and d['selected'] and not w['selected'],
          'connected (window target): "지금 창 캡처: WGC · 이 연결에만 적용", DXGI (this connection\'s desktop method) lit', str(st))
    # pick GDI (a real tap on the rendered button)
    time.sleep(2)  # a scroll still settling takes the first tap to stop itself
    taps = 0
    for _ in range(2):
        gdi = find(dump(), rid='settingsDesktopBackendGdiButton')
        tap_node(gdi)
        taps += 1
        time.sleep(3)
        if any('desktop_backend_request' in l for l in log_lines()):
            break
    print('      taps on GDI until the app took one: %d (node %s)' % (taps, gdi.attrib if gdi is not None else None), flush=True)
    with open(os.path.join(out, 'ua2_tap_lines.txt'), 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(l for l in log_lines() if 'desktop_backend' in l or 'ViewRootImpl' in l or 'InputDispatcher' in l))
    time.sleep(1)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2_3_after_pick.png'))
    req = backend_requests(fx)
    check(req == ['gdi'], 'the tap sent ONE change request, for this connection (gdi)', str(req))
    st = c['settingsDesktopBackendStatusText']
    g = c['settingsDesktopBackendGdiButton']
    check(st and st['text'] == '요청: GDI · 바탕화면을 볼 때 적용 · 지금 창 캡처: WGC · 이 연결에만 적용' and g and g['selected'],
          'after the tap: GDI is kept for the desktop (handled, lit) while the window is captured with WGC -- not "applied"',
          str(st))
    # disconnect, connect again: nothing asked, host default shown
    tap(rid='listDisconnectButton', timeout=10)
    time.sleep(3)
    ok = connect_to_pattern(fx.port, best_quality=False, window=True)
    check(ok, 'connected again')
    time.sleep(3)
    req2 = backend_requests(fx)
    check(req2 == ['gdi'], 'the new connection asked NOTHING (still the one request of the first connection)', str(req2))
    for _ in range(3):
        if find(dump(), rid='listSettingsButton') is not None:
            break
        adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
        time.sleep(2)
    c = show_backend_controls()
    time.sleep(3)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2_4_reconnected.png'))
    st = c['settingsDesktopBackendStatusText']
    check(st and st['text'] == '지금 창 캡처: WGC · 이 연결에만 적용',
          'reconnected: no choice of its own -- the host\'s own again', str(st))
    ui = [l for l in log_lines() if 'desktop_backend_ui' in l]
    states = [re.search(r'state=(\w+)', l).group(1) for l in ui if re.search(r'state=(\w+)', l)]
    # (a delayed first pong cannot be staged on this manual TCP-control path: the client ends the session when its
    # first answer is late -- the CHECKING -> supported order is the unit test's)
    check('NO_ANSWER_HOST' not in states, 'a new host is never shown as an older one (states seen: %s)' % states[:6])
    prefs_after = adb('shell', 'run-as', PKG, 'cat', 'shared_prefs/remote60_android_direct.xml', check_rc=False)
    check('desktop_backend' not in prefs_after, 'the old global key is gone from the app\'s storage', prefs_after[-200:])
    check('strong' in prefs_after, 'the sharpness choice (강하게) is kept', prefs_after[-200:])
    lines = [l for l in log_lines() if 'desktop_backend' in l]
    with open(os.path.join(out, 'ua2_app_lines.txt'), 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(lines))


def scenario_ua2_old(fx, out, new_host_exe):
    """ua2-apk r2 (Codex N3): an OLDER host (no backend status): the change goes out on the old wire, nothing is lit,
    nothing is confirmed and no session scope is promised -- then a NEW host on a new connection gets nothing of it."""
    ok = connect_to_pattern(fx.port, best_quality=False, window=True)
    check(ok, 'old host: connected and viewing (window pick)')
    for _ in range(3):
        if find(dump(), rid='listSettingsButton') is not None:
            break
        adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
        time.sleep(2)
    show_backend_controls()
    time.sleep(3)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2old_1_connected.png'))
    st = c['settingsDesktopBackendStatusText']
    lit = [k for k in ('settingsDesktopBackendDxgiButton', 'settingsDesktopBackendWgcButton', 'settingsDesktopBackendGdiButton')
           if c[k] and c[k]['selected']]
    check(st and st['text'] == '이 PC는 적용 결과를 알려주지 않습니다 · 이번 연결에만 적용되는지도 보장할 수 없습니다' and not lit,
          'old host: "알려주지 않습니다 · 보장할 수 없습니다", nothing lit', str(st) + ' lit=' + str(lit))
    time.sleep(2)
    gdi = find(dump(), rid='settingsDesktopBackendGdiButton')
    tap_node(gdi)
    time.sleep(3)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2old_2_after_pick.png'))
    st = c['settingsDesktopBackendStatusText']
    lit = [k for k in ('settingsDesktopBackendDxgiButton', 'settingsDesktopBackendWgcButton', 'settingsDesktopBackendGdiButton')
           if c[k] and c[k]['selected']]
    check(backend_requests(fx) == ['gdi'], 'old host: the change went out on the old wire (one request)', str(backend_requests(fx)))
    check(st and st['text'].startswith('요청: GDI 보냄') and '보장할 수 없습니다' in st['text'] and not lit,
          'old host: after the request still no confirmation, nothing lit, the scope still not promised', str(st) + ' lit=' + str(lit))
    tap(rid='listDisconnectButton', timeout=10)
    time.sleep(3)
    # a NEW host, a new connection
    os.makedirs(os.path.join(out, 'newhost'), exist_ok=True)
    fx2 = Fixture(new_host_exe, os.path.join(out, 'newhost'), static=False, layered_size=(1440, 810))
    try:
        ok = connect_to_pattern(fx2.port, best_quality=False, window=True)
        check(ok, 'new host: connected')
        time.sleep(3)
        check(backend_requests(fx2) == [], 'new host: nothing of the old connection\'s request reached it', str(backend_requests(fx2)))
        for _ in range(3):
            if find(dump(), rid='listSettingsButton') is not None:
                break
            adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
            time.sleep(2)
        show_backend_controls()
        time.sleep(3)
        st = ua2_controls()['settingsDesktopBackendStatusText']
        screencap(os.path.join(out, 'ua2old_3_new_host.png'))
        check(st and st['text'] == '지금 창 캡처: WGC · 이 연결에만 적용', 'new host: its own state, no request carried', str(st))
    finally:
        fx2.close()


def node_state(rid):
    root = dump()
    for n in (root.iter('node') if root is not None else []):
        if n.get('resource-id') == PKG + ':id/' + rid:
            return {'selected': n.get('selected') == 'true', 'enabled': n.get('enabled') == 'true', 'bounds': n.get('bounds')}
    return None


def to_list():
    for _ in range(4):
        if find(dump(), rid='listSettingsButton') is not None:
            return True
        adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
        time.sleep(2)
    return find(dump(), rid='listSettingsButton') is not None


def settings_method(rid_to_tap=None):
    """The settings tab scrolled to the drawing-method buttons; optionally a real tap on one. Returns their state."""
    tap(rid='listSettingsButton', timeout=20)
    for _ in range(10):
        if node_state('settingsSharpMethodDisplayButton'):
            break
        adb('shell', 'input', 'swipe', '540', '1800', '540', '1000', '300', check_rc=False)
        time.sleep(1)
    time.sleep(1)
    if rid_to_tap:
        n = find(dump(), rid=rid_to_tap)
        if n is not None:
            tap_node(n)
        time.sleep(1.5)
    return node_state('settingsSharpMethodDefaultButton'), node_state('settingsSharpMethodDisplayButton')


def pick_pattern_again():
    tap(rid='listWindowsButton', timeout=10)
    end = time.time() + 30
    card = None
    while time.time() < end and card is None:
        card = find_exact(dump(), PATTERN_TITLE)
        if card is None:
            time.sleep(1)
    if card is None:
        return False
    tap_node(card)
    return wait_for(rid='viewerMenuButton', timeout=40) or wait_for(rid='viewerRoot', timeout=5)


def scenario_method_ui(fx, out):
    """sh-strength r3 (Codex codex_sh_strength_r2.md §2, §4.1): the drawing method through the NORMAL settings UI of the
    seam-free APK -- default A, a real tap on "화면 크기 (실험)" -> B draws (display pass logged, crisper edges), B -> off
    -> direct (placement unchanged) -> strong again = B, back to 기본 = A, and the choice restored after a restart."""
    adb('logcat', '-c', check_rc=False)
    ok = connect_to_pattern(fx.port, level_before='strong', window=True)
    check(ok, 'connected, 강하게 (0.60), the pattern window shown')
    time.sleep(5)
    a = cmp_metrics(screencap(os.path.join(out, 'method_1_default.png')))
    lines = log_lines()
    check(a is not None and not any('sharpen_display_pass' in l for l in lines),
          'default: drawn the A way (no display pass) -- a fresh install is A', str(a and a['A_text_edge']))
    to_list()
    d, b = settings_method()
    check(d and b and d['selected'] and not b['selected'], 'settings: 기본 selected, 화면 크기 not (rendered)', str((d, b)))
    d, b = settings_method('settingsSharpMethodDisplayButton')
    check(d and b and b['selected'] and not d['selected'], 'a real tap on 화면 크기 (실험): it is the selected one', str((d, b)))
    adb('logcat', '-c', check_rc=False)
    check(pick_pattern_again(), 'back in the viewer')
    time.sleep(5)
    bm = cmp_metrics(screencap(os.path.join(out, 'method_2_display.png')))
    lines = log_lines()
    dp = [l[l.index('sharpen_display_pass'):][:160] for l in lines if 'sharpen_display_pass' in l]
    check(any('mode=1 method=display' in l for l in dp), 'B is drawn because of the SETTING (no test extra): display pass logged', str(dp[-1:]))
    check(a and bm and borders_ok(bm) and bm['x0'] == a['x0'], 'B: the picture is where A put it (borders, x)', str(bm and (bm['x0'], bm['border_top_rgb'])))
    check(a and bm and bm['A_text_edge'] > a['A_text_edge'] * 1.15, 'B: text edges crisper than A at the same strength',
          '%s -> %s' % (a and a['A_text_edge'], bm and bm['A_text_edge']))
    # B -> off -> strong (B again), in the viewer menu
    choose_level('off')
    time.sleep(4)
    off = cmp_metrics(screencap(os.path.join(out, 'method_3_off.png')))
    lines = log_lines()
    check(off and borders_ok(off) and off['x0'] == a['x0'] and any('sharpen_path' in l and 'path=direct' in l for l in lines),
          'B -> 끄기: direct again, the picture where it was', str(off and off['x0']))
    adb('logcat', '-c', check_rc=False)
    choose_level('strong')
    time.sleep(5)
    again = cmp_metrics(screencap(os.path.join(out, 'method_4_display_again.png')))
    lines = log_lines()
    check(any('sharpen_display_pass' in l and 'mode=1' in l for l in lines) and again and again['A_text_edge'] > a['A_text_edge'] * 1.15,
          '끄기 -> 강하게: B again (a new renderer, the display pass)', str(again and again['A_text_edge']))
    # back to 기본
    to_list()
    d, b = settings_method('settingsSharpMethodDefaultButton')
    check(d and d['selected'], 'a real tap on 기본: selected', str((d, b)))
    adb('logcat', '-c', check_rc=False)
    check(pick_pattern_again(), 'back in the viewer (기본)')
    time.sleep(5)
    a2 = cmp_metrics(screencap(os.path.join(out, 'method_5_default_again.png')))
    lines = log_lines()
    check(a2 and not any('sharpen_display_pass' in l and 'mode=1' in l for l in lines) and abs(a2['A_text_edge'] - a['A_text_edge']) < 0.1 * a['A_text_edge'],
          '기본 again: A (no display pass), edges as before', str(a2 and a2['A_text_edge']))
    # the choice survives a restart
    to_list()
    settings_method('settingsSharpMethodDisplayButton')
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    time.sleep(2)
    ok = connect_to_pattern(fx.port, best_quality=False, window=True)
    check(ok, 'restarted and connected')
    time.sleep(5)
    lines = log_lines()
    check(any('sharpen_display_pass' in l and 'mode=1 method=display' in l for l in lines), 'after a restart the stored 화면 크기 is used')
    to_list()
    d, b = settings_method()
    check(b and b['selected'], '...and shown selected in settings', str((d, b)))


def scenario_race(fx, out):
    """r3 (Codex R2): the handoff entry and the decoder's buffer, published with a gap (native seam: 300 ms before, 2000 ms after;
    debug.gnlink.sh.*), on a STILL picture -- where a missed first latch is never followed by another frame.
    Fixed order (entry first): the first output is confirmed, no fallback. r2's order (buffer first) as the
    counter-case: the first latch finds no entry, its next latch is the same timestamp (a redraw), and the
    3 s gate falls back."""
    results = {}
    for order in ['before', 'after']:
        # The decoder releases a frame well before its due time (releaseOutputBufferAtTime; on the still
        # pattern handoff -> latch was 0.7-1.3 s, r3h), so an entry only 300 ms late still precedes the latch
        # and r2's order looks harmless. The counter-case needs a gap longer than that lead: 2000 ms (the
        # seam's maximum) for 'after'; 300 ms between entry and buffer for 'before'.
        delay = '2000' if order == 'after' else '300'
        adb('shell', 'setprop', 'debug.gnlink.sh.handoff_delay_ms', delay, check_rc=False)
        adb('shell', 'setprop', 'debug.gnlink.sh.handoff_order', order, check_rc=False)
        ok = connect_to_pattern(fx.port)
        check(ok, order + ': connected (0.20) to the still pattern')
        time.sleep(10)
        lines = log_lines()
        first = [l for l in lines if 'sharpen_first_output' in l]
        fb = [l for l in lines if 'sharpen_fallback' in l]
        lat = [l for l in lines if '[sh_latency]' in l]
        um = [int(m.group(1)) for m in (re.search(r'window: n=\d+ .*? unmatched=(\d+)', l) for l in lat) if m]
        results[order] = {'first_output': first[:1], 'fallback': fb[:1], 'unmatched_windows': um}
        print('      %s: first_output=%s fallback=%s unmatched per 5 s window=%s' % (order, first[:1], fb[:1], um), flush=True)
        with open(os.path.join(out, 'race_%s.txt' % order), 'w', encoding='utf-8') as fh:
            fh.write('\n'.join(lines))
        if order == 'before':
            check(first and not fb, 'entry published BEFORE the buffer: the first output is confirmed, no fallback',
                  (first or fb or ['none'])[0][-160:])
            check(first and 'latchesBefore=0' in first[0],
                  '...confirmed by the FIRST frame latched after the attach (latchesBefore=0)', (first or ['none'])[0][-120:])
        else:
            # The host answers the viewer's key requests with further frames here, so a missed first latch is
            # recovered by a later frame; on a picture that sends no further frame it would be the 3 s fallback.
            lb = re.search(r'latchesBefore=(\d+)', first[0]) if first else None
            check((lb and int(lb.group(1)) >= 1) or fb,
                  'COUNTER-CASE, r2 order (buffer first, entry 2000 ms later): the first frame latched did NOT confirm',
                  'first_output=%s fallback=%s' % ([f[-120:] for f in first[:1]], fb[:1]))
        adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    adb('shell', 'setprop', 'debug.gnlink.sh.handoff_delay_ms', '0', check_rc=False)
    adb('shell', 'setprop', 'debug.gnlink.sh.handoff_order', 'before', check_rc=False)
    with open(os.path.join(out, 'race.json'), 'w') as fh:
        json.dump(results, fh, indent=1)


def scenario_touch_host(fx, out, input_log, extras=(), prefix=''):
    """r3 (Codex R3.3): a tap on the picture -> the app's native queue (seam log) -> the HOST -> the pattern
    window, which records the client point it received. The capture target is the pattern WINDOW, so the
    host's background-message injection posts to it (no real cursor, no SendInput). Before any tap, the
    host log must show that window as the applied selection; otherwise nothing is tapped."""
    ok = connect_to_pattern(fx.port, extras, window=True)
    check(ok, prefix + 'connected (0.20), the pattern WINDOW picked')
    time.sleep(4)
    hlog = open(os.path.join(out, 'host.log'), 'rb').read().decode(errors='replace')
    sel = re.findall(r'window-select seq=\d+ requestedId=\d+ applied=1 selectedId=(\d+) streamGen=\d+ reason=ok title=(.*)', hlog)
    enabled = 'input injection enabled mode=background_message' in hlog
    target_ok = bool(sel) and sel[-1][1].strip().startswith(PATTERN_TITLE) and sel[-1][0] != '0'
    if not check(target_ok and enabled, 'GUARD: the host captures the pattern window and injects by message',
                 'selections=%s enabled=%s' % (sel[-2:], enabled)):
        return
    if os.path.exists(input_log):
        os.remove(input_log)

    def tap_lime(label, one_to_one=True):
        label = prefix + label
        img = screencap(os.path.join(out, 'touchhost_%s.png' % label))
        t = lime_target(img)
        if t is None:
            # r3: a session that dropped before the tap (connect screen, [status] error) is not a detection
            # miss -- name it so the run is read as INVALID, not as a touch failure.
            dropped = [l for l in log_lines() if '[status] error' in l]
            check(False, label + (': INVALID -- the session dropped before the tap' if dropped
                                  else ': the lime block is found on screen'), (dropped or [''])[-1][-120:])
            return
        before = open(input_log).read().splitlines() if os.path.exists(input_log) else []
        adb('logcat', '-c', check_rc=False)
        adb('shell', 'input', 'tap', str(t[0]), str(t[1]), check_rc=False)
        time.sleep(2.5)
        queued = [e for e in seam_inputs() if e[4] == 1 and e[0] in (2, 3)]
        lines = open(input_log).read().splitlines() if os.path.exists(input_log) else []
        got = [tuple(int(v) for v in l.split()[1:3]) for l in lines[len(before):] if l.startswith('down ')]
        check(queued, label + ': the app queued the press', str(queued[:2]))
        check(got, label + ': the pattern window RECEIVED a press through the host', str(lines[len(before):][:4]))
        if queued and got:
            q, r = queued[0], got[0]
            check(abs(q[1] - r[0]) <= 1 and abs(q[2] - r[1]) <= 1,
                  label + ': the host delivered the same point the app sent (window client px, within 1)',
                  'app=(%d,%d) window=(%d,%d)' % (q[1], q[2], r[0], r[1]))
            check(610 <= r[0] <= 730 and 440 <= r[1] <= 700, label + ': ...inside the lime block of the window',
                  'window=(%d,%d)' % r)

    for level in ['off', 'weak', 'strong']:
        choose_level(level)
        time.sleep(3)
        tap_lime(level)
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):
        tap(rid='viewerMenuCloseButton', timeout=10)
        time.sleep(3)
        tap_lime('large_weak', one_to_one=False)
    else:
        check(False, '크게 보기 could be chosen')
    with open(os.path.join(out, 'touch_host_received.txt'), 'w') as fh:
        fh.write(open(input_log).read() if os.path.exists(input_log) else '')


def view_bounds():
    """The video TextureView's rectangle on the screen (uiautomator bounds), or None."""
    n = find(dump(), rid='videoTextureView')
    if n is None:
        return None
    g = re.match(r'\[(\d+),(\d+)\]\[(\d+),(\d+)\]', n.get('bounds') or '')
    return tuple(int(v) for v in g.groups()) if g else None


def display_pass_place():
    """The last B placement the app logged: (left, top, width, height, viewW, viewH) in view pixels."""
    for l in reversed(log_lines('sharpen_display_pass')):
        g = re.search(r'place=(-?[\d.]+),(-?[\d.]+),([\d.]+)x([\d.]+) .*view=(\d+)x(\d+)', l)
        if g:
            return tuple(float(v) for v in g.groups())
    return None


def picture_rect():
    """The picture's rectangle on the SCREEN: the view's bounds plus the B placement inside it."""
    vb, pl = view_bounds(), display_pass_place()
    if vb is None or pl is None:
        return None
    return (vb[0] + pl[0], vb[1] + pl[1], vb[0] + pl[0] + pl[2], vb[1] + pl[1] + pl[3]), vb, pl


def scenario_layout_b(fx, out, input_log):
    """sh-strength r4 (Codex codex_sh_strength_r3.md: N1 c + the Activity-level behaviours the earlier scenarios did not
    run) -- B (display-size pass) through the REAL Activity, the pattern window captured, taps injected by the host:
      1. letterbox / corners: taps in the black outside the picture send nothing; just inside its corners -> (0,0)-ish
         and (W,H)-ish of the window;
      2. a real rotation (the viewer's rotate button -> portrait and back): the new view size reaches B's placement,
         the picture is where the placement says, no fallback, a tap lands right;
      3. 크게보기 + 화면 이동: a pan moves the picture (local, nothing sent);
      4. a display resize (wm size, this AVD only): B follows the new view, a tap lands right;
      5. the input fence: the renderer takes a placement 6 s late (seam holdplace) -- a tap in that gap is NOT sent
         (input_held_layout), the next one after the layout is shown is, at the right point;
      6. fallback: B's swap fails (seam swap:40) -> direct, the picture still placed, a tap still right.
    Scroll shimmer is NOT judged here (no oracle in this harness) -- said so in the report, not claimed."""
    base_extras = ('--ez', 'shTestDisplayPass', 'true')

    def guard():
        hlog = open(os.path.join(out, 'host.log'), 'rb').read().decode(errors='replace')
        sel = re.findall(r'window-select seq=\d+ requestedId=\d+ applied=1 selectedId=(\d+) streamGen=\d+ reason=ok title=(.*)', hlog)
        return bool(sel) and sel[-1][1].strip().startswith(PATTERN_TITLE) and 'input injection enabled mode=background_message' in hlog

    def received_since(before):
        lines = open(input_log).read().splitlines() if os.path.exists(input_log) else []
        return [tuple(int(v) for v in l.split()[1:3]) for l in lines[before:] if l.startswith('down ')]

    def log_count():
        return len(open(input_log).read().splitlines()) if os.path.exists(input_log) else 0

    def tap_at(x, y, settle=2.5):
        adb('logcat', '-c', check_rc=False)
        before = log_count()
        adb('shell', 'input', 'tap', str(int(x)), str(int(y)), check_rc=False)
        time.sleep(settle)
        queued = [e for e in seam_inputs() if e[4] == 1 and e[0] in (2, 3)]
        return queued, received_since(before)

    def tap_lime(label):
        img = screencap(os.path.join(out, 'layoutb_%s.png' % label))
        t = lime_target(img)
        if not check(t is not None, label + ': the lime block is on screen'):
            return
        q, got = tap_at(t[0], t[1])
        check(q and got and 610 <= got[0][0] <= 730 and 440 <= got[0][1] <= 700,
              label + ': a tap on the lime block reaches the window INSIDE the lime block',
              'queued=%s received=%s' % (q[:1], got[:1]))

    def corners(label):
        pr = picture_rect()
        if not check(pr is not None, label + ': the picture rectangle (view bounds + B placement) is known'):
            return
        (l, t, r, b), vb, pl = pr
        # outside the picture, inside the view: the letterbox side (whichever has room)
        outside = []
        if l - vb[0] >= 12:
            outside.append(((vb[0] + l) / 2, (t + b) / 2))
        if t - vb[1] >= 12:
            outside.append(((l + r) / 2, (vb[1] + t) / 2))
        if vb[3] - b >= 12:
            outside.append(((l + r) / 2, (b + vb[3]) / 2))
        for i, (x, y) in enumerate(outside):
            q, got = tap_at(x, y, settle=1.5)
            check(not q and not got, label + ': a tap in the letterbox (%d,%d) sends nothing' % (x, y), 'queued=%s got=%s' % (q[:1], got[:1]))
        check(outside, label + ': there is a letterbox to tap', str((vb, (l, t, r, b))))
        q, got = tap_at(l + 4, t + 4)
        check(q and got and got[0][0] <= 12 and got[0][1] <= 12, label + ': just inside the top-left corner -> the window\'s (0,0)',
              'queued=%s received=%s' % (q[:1], got[:1]))
        q, got = tap_at(r - 5, b - 5)
        check(q and got and got[0][0] >= 1440 - 14 and got[0][1] >= 810 - 14,
              label + ': just inside the bottom-right corner -> the window\'s (1440,810)', 'queued=%s received=%s' % (q[:1], got[:1]))

    def picture_where_placed(label):
        pr = picture_rect()
        img = screencap(os.path.join(out, 'layoutb_%s_pic.png' % label))
        if not check(pr is not None, label + ': B placement logged'):
            return
        (l, t, r, b), vb, pl = pr
        h, w = img.shape[:2]
        inside = img[int(t) + 6:int(b) - 6, int(l) + 6:int(r) - 6]
        # the pattern's top border is a red row: it must be at the placement's top (within a row)
        rows = [img[y, int(l) + 20:int(r) - 20].reshape(-1, 3).mean(axis=0) for y in range(max(0, int(t) - 1), min(h, int(t) + 3))]
        red = [y for y, v in zip(range(max(0, int(t) - 1), min(h, int(t) + 3)), rows) if v[0] > 45 and v[0] > v[1] + 25 and v[0] > v[2] + 15]
        check(inside.size and float(inside.std()) > 10 and red,
              label + ': the picture is drawn where B placed it (the red top border at the placement\'s top)',
              'rect=%s view=%s rows=%s' % ((int(l), int(t), int(r), int(b)), vb, [[round(float(c)) for c in v] for v in rows]))

    # ---- 1-4 in one B session
    ok = connect_to_pattern(fx.port, base_extras, level_before='strong', window=True)
    check(ok, 'B: connected, 강하게, the pattern window picked')
    time.sleep(5)
    if not check(guard(), 'GUARD: the host captures the pattern window and injects by message'):
        return
    lines = log_lines()
    check(any('sharpen_display_pass' in l and 'mode=1' in l for l in lines), 'B is drawing (display pass logged)')
    picture_where_placed('land')
    corners('landscape')
    tap_lime('landscape')
    # 2. rotation: the viewer's own button -> portrait
    adb('logcat', '-c', check_rc=False)
    rot = tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerRotateButton', timeout=15, reveal=scroll_menu_sheet)
    tap(rid='viewerMenuCloseButton', timeout=5)
    time.sleep(6)
    pl = display_pass_place()
    check(rot and pl is not None and pl[5] > pl[4], 'rotation: portrait, and B\'s placement has the new (portrait) view size',
          str(pl))
    lines = log_lines()
    check(not any('sharpen_fallback' in l for l in lines), 'rotation: no fallback (B stays)', '')
    picture_where_placed('portrait')
    corners('portrait')
    tap_lime('portrait')
    adb('logcat', '-c', check_rc=False)
    rot = tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerRotateButton', timeout=15, reveal=scroll_menu_sheet)
    tap(rid='viewerMenuCloseButton', timeout=5)
    time.sleep(6)
    pl = display_pass_place()
    check(rot and pl is not None and pl[4] > pl[5], 'rotation back: landscape placement again', str(pl))
    tap_lime('landscape_again')
    # 3. 크게보기 + 화면 이동: a pan moves the picture, sends nothing
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10, reveal=scroll_menu_sheet_up):
        tap(rid='viewerMenuCloseButton', timeout=10)
        time.sleep(3)
        chip = tap(rid='viewerPanChip', timeout=10, reveal=reveal_controls)
        time.sleep(1.5)
        img0 = screencap(os.path.join(out, 'layoutb_pan0.png'))
        t0 = lime_target(img0)
        adb('logcat', '-c', check_rc=False)
        vb = view_bounds()
        cx, cy = (vb[0] + vb[2]) // 2, (vb[1] + vb[3]) // 2
        adb('shell', 'input', 'swipe', str(cx), str(cy), str(cx - 300), str(cy - 150), '600', check_rc=False)
        time.sleep(1.0)
        img1 = screencap(os.path.join(out, 'layoutb_pan1.png'))
        t1 = lime_target(img1)
        q = [e for e in seam_inputs() if e[4] == 1]
        check(chip and t0 and t1 and t1[0] < t0[0] - 100 and not q,
              'pan (크게보기 + 화면 이동): the picture moved with the finger, nothing was sent',
              'lime %s -> %s queued=%s' % (t0, t1, q[:2]))
        tap(rid='viewerPanChip', timeout=10, reveal=reveal_controls)
        time.sleep(1)
        tap_lime('large_after_pan')
        if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10, reveal=scroll_menu_sheet_up):
            tap(rid='viewerMenuCloseButton', timeout=10)
        time.sleep(3)
    else:
        check(False, '크게 보기 could be chosen')
        tap(rid='viewerMenuCloseButton', timeout=5)  # never leave the sheet over the picture
    # 4. a display resize (this AVD only; restored in finally)
    size0 = adb('shell', 'wm', 'size', check_rc=False)
    try:
        g = re.search(r'Physical size: (\d+)x(\d+)', size0 or '')
        if check(g is not None, 'resize: the AVD\'s display size read', (size0 or '')[:80]):
            pw, ph = int(g.group(1)), int(g.group(2))
            before = display_pass_place()
            adb('logcat', '-c', check_rc=False)
            adb('shell', 'wm', 'size', '%dx%d' % (int(pw * 0.85), int(ph * 0.85)), check_rc=False)
            time.sleep(7)
            after = display_pass_place()
            check(after is not None and before is not None and (after[4], after[5]) != (before[4], before[5]),
                  'resize: B\'s placement follows the new view size', '%s -> %s' % (before, after))
            lines = log_lines()
            check(not any('sharpen_fallback' in l for l in lines), 'resize: no fallback')
            picture_where_placed('resized')
            tap_lime('resized')
    finally:
        adb('shell', 'wm', 'size', 'reset', check_rc=False)
        time.sleep(4)

    # ---- 5. the input fence: the GL side takes each placement 6 s late
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    ok = connect_to_pattern(fx.port, base_extras + ('--es', 'shTestFault', 'holdplace:8000'), level_before='strong', window=True)
    check(ok, 'fence: connected (B, placements taken 8 s late)')
    time.sleep(12)
    vb = view_bounds()
    adb('logcat', '-c', check_rc=False)
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):
        tap(rid='viewerMenuCloseButton', timeout=2)
        held_place = display_pass_place()  # read before the tap clears the log
        q, got = tap_at((vb[0] + vb[2]) // 2, (vb[1] + vb[3]) // 2, settle=1.0)
        held = log_lines('input_held_layout')
        check(not q and not got and held, 'fence: a tap while the new layout is not shown yet is NOT sent (held)',
              'queued=%s received=%s held=%s' % (q[:1], got[:1], held[-1:][0][-60:] if held else None))
        time.sleep(10)
        check(held_place is not None and held_place[2] > 1920 + 50,
              'fence: 크게 보기 was the layout held (its placement is the enlarged one)', str(held_place))
        tap_lime('fence_after_shown')
    else:
        check(False, 'fence: 크게 보기 could be chosen')

    # ---- 6. fallback: B's swap fails -> direct; the picture still placed, a tap still right
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    ok = connect_to_pattern(fx.port, base_extras + ('--es', 'shTestFault', 'swap:40'), level_before='strong', window=True)
    check(ok, 'fallback: connected (B, the swap fails after 40 frames)')
    time.sleep(10)
    lines = log_lines()
    check(any('sharpen_fallback' in l for l in lines) and any('sharpen_path' in l and 'path=direct' in l for l in lines),
          'fallback: B failed -> the direct path', ([l for l in lines if 'sharpen_fallback' in l] or [''])[-1][-120:])
    m = cmp_metrics(screencap(os.path.join(out, 'layoutb_fallback.png')))
    check(m is not None and borders_ok(m), 'fallback: the picture is still placed (borders where they belong)', str(m and m.get('x0')))
    tap_lime('fallback')


def scenario_touch_ab(fx, out, input_log):
    """sh-strength r2 (Codex §4B geometry): the same tap path as touch_host, on the x1.33 off-screen layered pattern
    window -- A, then B (display-size pass). The remote point the app computes is the viewport's either way; B
    must show the picture where those points are (no double placement) and the host must receive the same point."""
    scenario_touch_host(fx, out, input_log, (), 'A ')
    scenario_touch_host(fx, out, input_log, ('--ez', 'shTestDisplayPass', 'true'), 'B ')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host-exe', required=True)
    ap.add_argument('--apk', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--scenario', default='compare')
    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--new-host-exe', default='')  # ua2_old: the second (new) host
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    if args.scenario == 'compare_ab_offline':
        scenario_compare_ab(None, args.out, offline=True)
        print('RESULT: %s  (%d checks, %d failed)' % ('ALL PASS' if all(checks) else 'FAILED', len(checks), checks.count(False)))
        return 0 if all(checks) else 1
    emu = ensure_emulator()
    adb('install', '-r', '-g', args.apk, timeout=300)
    adb('shell', 'pm', 'clear', PKG, check_rc=False)
    # The shader fixture needs no host, no picture of this PC and no window on its screen.
    input_log = os.path.join(os.path.abspath(args.out), 'pattern_input.txt') if args.scenario in ('touch_host', 'touch_ab', 'layout_b') else None
    fx = None if args.scenario in ('shader', 'strength_ab') else Fixture(args.host_exe, args.out, static=args.scenario == 'race',
                                                                      layered_size=(1440, 810) if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b', 'ua2_desktop') else None,
                                                                      pulse_ms=33 if args.scenario == 'latency_ab' else 200,
                                                        input_log=input_log)
    try:
        if args.scenario == 'compare':
            scenario_compare(fx, args.out, seam=False)
        elif args.scenario == 'identity':
            scenario_identity(fx, args.out)
        elif args.scenario == 'faults':
            scenario_faults(fx, args.out)
        elif args.scenario == 'lifecycle':
            scenario_lifecycle(fx, args.out)
        elif args.scenario == 'latency':
            scenario_latency(fx, args.out, args.seconds)
        elif args.scenario == 'touch':
            scenario_touch(fx, args.out)
        elif args.scenario == 'hold':
            scenario_hold(fx, args.out)
        elif args.scenario == 'viewer_never':
            scenario_viewer_never(fx, args.out)
        elif args.scenario == 'resize':
            scenario_resize(fx, args.out)
        elif args.scenario == 'compare_ab_offline':
            scenario_compare_ab(fx, args.out, offline=True)
        elif args.scenario == 'method_ui':
            scenario_method_ui(fx, args.out)
        elif args.scenario == 'ua2_old':
            scenario_ua2_old(fx, args.out, args.new_host_exe)
        elif args.scenario == 'ua2':
            scenario_ua2(fx, args.out)
        elif args.scenario == 'ua2_desktop':
            scenario_ua2_desktop(fx, args.out)
        elif args.scenario == 'touch_ab':
            scenario_touch_ab(fx, args.out, input_log)
        elif args.scenario == 'layout_b':
            scenario_layout_b(fx, args.out, input_log)
        elif args.scenario == 'latency_ab':
            scenario_latency_ab(fx, args.out, args.seconds)
        elif args.scenario == 'compare_ab':
            scenario_compare_ab(fx, args.out)
        elif args.scenario == 'strength_ab':
            scenario_strength_ab(fx, args.out)
        elif args.scenario == 'shader':
            scenario_shader(fx, args.out)
        elif args.scenario == 'race':
            scenario_race(fx, args.out)
        elif args.scenario == 'touch_host':
            scenario_touch_host(fx, args.out, input_log)
    except Exception as e:
        check(False, 'scenario raised: %r' % e)
    finally:
        try:
            with open(os.path.join(args.out, 'app_log.txt'), 'w', encoding='utf-8') as f:
                f.write('\n'.join(log_lines()))
        except Exception:
            pass
        adb('shell', 'am', 'force-stop', PKG, check_rc=False)
        if fx is not None:
            fx.close()
    n, f = len(checks), checks.count(False)
    print('RESULT: %s  (%d checks, %d failed)' % ('ALL PASS' if f == 0 else 'FAILED', n, f), flush=True)
    sys.exit(0 if f == 0 and n > 0 else 1)


if __name__ == '__main__':
    main()
