"""sh-filter r1 -- the phone viewer's sharpening, on an AVD, through the real APK UI.

What runs and what it is:
  * the host: a GNLinkStream TEST build (GNLinkStreamMigrationTest; capture/encode are the product's)
    bound to 127.0.0.1 only (no firewall prompt), input injection OFF, capturing ONE window by title
    (automation/sh_pattern_window.ps1: 1920x1080, mostly off screen) -- so the phone decodes 1920x1088
    and crops to 1080 when its decoder does;
  * the phone: the AVD `gnlink_kbd_test` (x86_64, API 34, software GL), the APK under test installed
    as is; the AVD reaches the host's loopback at 10.0.2.2;
  * the UI is driven through uiautomator (resource ids) and adb taps -- the app's own buttons.
The AVD is not the user's phone: its decoder, GPU, colour and heat are not the SM-S948N's.

Usage: python sh_filter_avd.py --host-exe <GNLinkStreamMigrationTest.exe> --apk <apk> --out <dir>
        [--scenario compare|faults|lifecycle|latency] [--seam]
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
    def __init__(self, host_exe, out, static):
        self.out = out
        self.port = free_udp_port()
        self.control_port = free_tcp_port()  # manual connect uses a TCP control link (loopback only)
        self.pattern = subprocess.Popen(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                         os.path.join(HERE, 'sh_pattern_window.ps1'), '-Seconds', '1800']
                                        + (['-Static'] if static else []))
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
               '--bind-port', str(self.port), '--control-port', str(self.control_port), '--input-injection-mode', 'none', '--seconds', '1800']
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


def connect_to_pattern(port, extras=(), best_quality=True):
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
    # The DESKTOP (the monitor, DXGI), with the topmost pattern window filling it: the host then gets a
    # frame every time the screen changes (the moving square, ~30 fps). Capturing the window itself (WGC)
    # gave ~1 frame/s here, too few to measure anything and slow to answer a lost keyframe.
    if not tap(rid='listDevicesButton', timeout=10):
        return False
    # Pick only after the host's UDP control link has timed out. On this manual (TCP-control) path the app
    # never pings the UDP control channel, so the host's 10 s read timeout (host_startup_control.cpp,
    # UdpControlLink 10000) ends that session 10 s after connect and marks the stream INACTIVE -- a stream
    # already playing then freezes for good ("화면이 멈췄습니다"). Seen with OFF as well as with the filter
    # (2026-10-22 debug2-off-low, run-compare7/8); not part of this change, recorded in the report.
    wait = 11.5 - (time.time() - connected_at)
    if wait > 0:
        time.sleep(wait)
    if not tap(rid='targetCardTitle', timeout=30):
        return False
    # The viewer is up once its menu button exists.
    return wait_for(rid='viewerMenuButton', timeout=40) or wait_for(rid='viewerRoot', timeout=5)


def reveal_controls():
    adb('shell', 'input', 'tap', '1200', '540', check_rc=False)


def scroll_menu_sheet():
    """The viewer menu sheet scrolls; its lower buttons (대상 선택 화면 ...) start below the screen edge."""
    adb('shell', 'input', 'swipe', '1900', '950', '1900', '600', '1500', check_rc=False)  # slow: no fling
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
    red = (row[:, 0] > 80) & (row[:, 1] < 50) & (row[:, 2] < 50)
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
    return (t[0] > 80 and t[1] < 50 and t[2] < 50
            and b[1] > 150 and b[0] < b[1] - 80 and b[2] < b[1] - 80
            and r[0] > 150 and r[1] > 150 and r[2] < min(r[0], r[1]) - 60)


# ---------------------------------------------------------------- scenarios
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


class ToastWatcher:
    """Polls the window list for a Toast window and screenshots the screen while one is up.

    The fallback notice (선명도 적용 불가, 원본 표시) is a 2 s Toast; it can be up and gone before the viewer
    is, so it is watched from before the connect. The shot is the evidence that the user saw it."""

    def __init__(self, path):
        import threading
        self.path, self.seen, self.stop_ev = path, None, threading.Event()
        self.t = threading.Thread(target=self.run, daemon=True)
        self.t.start()

    def run(self):
        while not self.stop_ev.is_set() and self.seen is None:
            w = adb('shell', 'dumpsys', 'window', 'windows', check_rc=False, timeout=30)
            hit = [l.strip() for l in w.splitlines() if 'Window{' in l and 'Toast' in l]
            if hit:
                screencap(self.path)
                self.seen = hit[0]
                return
            time.sleep(0.2)

    def close(self):
        self.stop_ev.set()
        self.t.join(30)
        return self.seen


def scenario_faults(fx, out):
    """Acceptance 4: GL fails -> 선명도 적용 불가, 원본 표시, and the ORIGINAL picture keeps coming."""
    for fault in ['context', 'shader', 'swap:30', 'never']:
        tw = ToastWatcher(os.path.join(out, 'fault_%s_toast.png' % fault.replace(':', '_')))
        check(connect_to_pattern(fx.port, ('--es', 'shTestFault', fault)), 'connected with injected fault ' + fault)
        time.sleep(8)
        seen = tw.close()
        check(seen is not None, fault + ': the fallback notice was on screen (Toast window; see fault_*_toast.png)', seen or 'none')
        lines = log_lines()
        fb = [l for l in lines if 'sharpen_fallback' in l]
        direct = [l for l in lines if 'sharpen_path' in l and 'path=direct' in l]
        check(fb and 'requested=weak' in fb[-1] and 'effective=off' in fb[-1],
              fault + ': fallback logged with requested=weak effective=off', fb[-1] if fb else 'none')
        check(direct, fault + ': the decoder was rebound DIRECTLY to the view')
        a = screencap(os.path.join(out, 'fault_%s_1.png' % fault.replace(':', '_')))
        time.sleep(2)
        b = screencap(os.path.join(out, 'fault_%s_2.png' % fault.replace(':', '_')))
        ma = metrics(a)
        check(ma is not None and borders_ok(ma), fault + ': the original picture is on screen after the fallback')
        # the moving square: two frames 2 s apart differ inside region G -> new frames keep arriving
        if ma:
            x0 = ma['x0']
            g1 = a[760:1000, x0 + 60:x0 + 1880, :]
            g2 = b[760:1000, x0 + 60:x0 + 1880, :]
            check(np.abs(g1 - g2).mean() > 1.0, fault + ': NEW frames keep arriving on the direct path (moving square moved)')
        check(not any('sharpen_start' in l for l in lines[lines.index(fb[-1]) + 1:]) if fb else False,
              fault + ': no retry loop in the same surface episode (no renderer started after the fallback)')


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
    """S4 on the AVD: sh_latency windows for off and weak and strong, same moving input."""
    check(connect_to_pattern(fx.port), 'connected')
    results = {}
    for level in ['off', 'weak', 'strong', 'off2']:
        choose_level(level.rstrip('2'))
        time.sleep(4)
        adb('logcat', '-c', check_rc=False)
        time.sleep(seconds)
        lines = [l for l in log_lines() if 'sh_latency' in l]
        results[level] = lines
        print('      %s: %d windows' % (level, len(lines)), flush=True)
        for l in lines:
            print('        ' + l[l.index('sh_latency'):], flush=True)
    with open(os.path.join(out, 'latency_raw.json'), 'w') as f:
        json.dump(results, f, indent=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host-exe', required=True)
    ap.add_argument('--apk', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--scenario', default='compare')
    ap.add_argument('--seconds', type=int, default=60)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    emu = ensure_emulator()
    adb('install', '-r', '-g', args.apk, timeout=300)
    adb('shell', 'pm', 'clear', PKG, check_rc=False)
    fx = Fixture(args.host_exe, args.out, static=False)  # the moving square keeps frames coming (no stall overlay)
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
    except Exception as e:
        check(False, 'scenario raised: %r' % e)
    finally:
        try:
            with open(os.path.join(args.out, 'app_log.txt'), 'w', encoding='utf-8') as f:
                f.write('\n'.join(log_lines()))
        except Exception:
            pass
        adb('shell', 'am', 'force-stop', PKG, check_rc=False)
        fx.close()
    n, f = len(checks), checks.count(False)
    print('RESULT: %s  (%d checks, %d failed)' % ('ALL PASS' if f == 0 else 'FAILED', n, f), flush=True)
    sys.exit(0 if f == 0 and n > 0 else 1)


if __name__ == '__main__':
    main()
