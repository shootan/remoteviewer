"""old-APK lane r7: a FAKE adb for oa_pick_test.py only (no device, no adb server). It plays 0.2.32's targets screen
(b554e76 activity_main.xml listWindowsButton / listDevicesButton, target_card.xml targetCardTitle; the Windows tab lists
the host's windows by title, the Desktop tab ONE card "Desktop" = id 0, MainActivity.kt:4513-4545) and what the app and
the PinnedSelectTest host log when a card is tapped. Every call is appended to OA_FAKE_ADB_LOG.
  OA_FAKE_UI_STATE    a JSON file: {"tab": "desktop" | "windows"}
  OA_FAKE_APPLOG      the app's diagnostics log (what `pull` of it returns)
  OA_FAKE_HOSTLOG     the host's log (appended to)
  OA_FAKE_UI_VARIANT  ok        the Windows card is the pinned window: request id=pinned tab=WINDOWS, applied=1, ready
                      wrongid   the Windows card's request goes out as id 0 tab=DESKTOP: the host refuses (the seam)
                      zeroready a request for id 0 that the host applies AND a frame follows (select_ready id 0)
                      nopinned  the Windows tab lists only another window ("Notepad")
"""
import json
import os
import re
import shutil
import sys

PINNED = 80809746
TITLE = 'remote60 mobile abr target pid 43948 92abc8fe'
PKG = 'com.remote60.androiddirect'
TABS = {'listWindowsButton': (0, 380, 400, 430, 'Windows'), 'listDevicesButton': (420, 380, 660, 430, 'Desktop')}
CARD = (100, 1000, 470, 1100)

args = sys.argv[1:]
if args[:1] == ['-s']:
    args = args[2:]
with open(os.environ['OA_FAKE_ADB_LOG'], 'a', encoding='utf-8') as f:
    f.write(' '.join(args) + '\n')
variant = os.environ.get('OA_FAKE_UI_VARIANT', 'ok')
state_path = os.environ['OA_FAKE_UI_STATE']
state = json.load(open(state_path, encoding='utf-8'))


def app(line):
    with open(os.environ['OA_FAKE_APPLOG'], 'a', encoding='utf-8') as f:
        f.write('2026-10-30 00:00:00.000 ' + line + '\n')


def host(line):
    with open(os.environ['OA_FAKE_HOSTLOG'], 'a', encoding='utf-8') as f:
        f.write('10-30 00:00:00.000 [native-video-host][control] ' + line + '\n')


def card_text():
    if state['tab'] == 'desktop':
        return 'Desktop'
    return 'Notepad' if variant == 'nopinned' else 'remote60 mobile abr…'


def nodes():
    out = [(k, v[:4], v[4]) for k, v in TABS.items()]
    out.append(('targetCardTitle', CARD, card_text()))
    return out


def node_xml(rid, b, text):
    return (f'<node resource-id="{PKG}:id/{rid}" text="{text}" content-desc="" enabled="true" clickable="true" '
            f'bounds="[{b[0]},{b[1]}][{b[2]},{b[3]}]" />')


cmd = ' '.join(args)
if args[:1] == ['version']:
    print('Android Debug Bridge version 1.0.41 (fake, oa_pick_test)')
elif cmd.startswith('exec-out uiautomator'):
    sys.stdout.buffer.write(('<?xml version="1.0" encoding="UTF-8"?><hierarchy rotation="0">' +
                             ''.join(node_xml(*n) for n in nodes()) + '</hierarchy>').encode('utf-8'))
elif cmd.startswith('shell input tap'):
    x, y = int(args[3]), int(args[4])
    hit = [n for n in nodes() if n[1][0] <= x <= n[1][2] and n[1][1] <= y <= n[1][3]]
    rid = hit[0][0] if hit else ''
    if rid == 'listWindowsButton':
        state['tab'] = 'windows'
        app('[tab_switch] tab=windows')
        host(f'window-list seq=2 count=1 selectedId={PINNED}')
    elif rid == 'listDevicesButton':
        state['tab'] = 'desktop'
        app('[tab_switch] tab=desktop')
    elif rid == 'targetCardTitle':
        if state['tab'] == 'windows' and variant == 'ok':
            tid, label, tab = PINNED, TITLE, 'WINDOWS'
        elif state['tab'] == 'windows' and variant == 'nopinned':
            tid, label, tab = 4242, 'Notepad', 'WINDOWS'
        else:
            tid, label, tab = 0, 'Desktop', 'DESKTOP'
        app(f'[select_request] targetId={tid} label={label} tab={tab} gen=1 origin=tap expected=0x0')
        host(f'window-select seq=1 requestedId={tid} pending')
        if tid == PINNED or variant == 'zeroready':
            host(f'window-select seq=1 requestedId={tid} applied=1 selectedId={tid} streamGen=2 reason=ok title={TITLE}')
            app('[video_debug] surface=on codec=on in=3 out=3')
            app(f'[select_ready] targetId={tid} gen=1 lastOutUs=1 title={TITLE} ms=900')
        else:
            host(f'window-select seq=1 requestedId={tid} applied=0 selectedId={PINNED} streamGen=1 '
                 'reason=pinned_test_seam_refused title=')
            app(f'[select_failed] targetId={tid} gen=1 seq=1 status=window_select_failed: pinned_test_only')
    json.dump(state, open(state_path, 'w', encoding='utf-8'))
elif args[:1] == ['pull']:
    shutil.copyfile(os.environ['OA_FAKE_APPLOG'], args[2])
elif cmd.startswith('exec-out screencap'):
    sys.stdout.buffer.write(b'\x89PNG fake')
sys.exit(0)
