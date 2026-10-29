"""old-APK lane r7: a FAKE adb for oa_pick_test.py only (no device, no adb server). It plays 0.2.32's targets screen
(b554e76 activity_main.xml listWindowsButton / listDevicesButton, target_card.xml targetCardTitle; the Windows tab lists
the host's windows by title, the Desktop tab ONE card "Desktop" = id 0, MainActivity.kt:4513-4545) and what the app and
the PinnedSelectTest host log when a card / the picture is tapped. Every call is appended to OA_FAKE_ADB_LOG.
  OA_FAKE_UI_STATE    a JSON file: {"tab": "desktop" | "windows"}
  OA_FAKE_APPLOG      the app's diagnostics log (what `pull` of it returns)
  OA_FAKE_HOSTLOG     the host's log (appended to)
  OA_FAKE_UI_VARIANT  ok          the Windows card is the pinned window: request id=pinned gen 1 tab=WINDOWS, applied=1
                                  streamGen 2, select_ack gen 1 streamGen 2, select_ready gen 1, then video out > 0
                      wrongid     the Windows card's request goes out as id 0 tab=DESKTOP: the host refuses (the seam)
                      zeroready   a request for id 0 that the host applies AND a frame follows (select_ready id 0)
                      nopinned    the Windows tab lists only another window ("Notepad")
                      genmismatch as ok, but the app's select_ack names streamGen 7 (not the host's 2)
                      novideo     as ok, but no video_debug after select_ready
  OA_FAKE_INPUT       what the host logs for a tap on the picture (1200,540):
                      pair (move, down, up inside 480x270) | none | uponly (an up alone) | outside (down/up at 900,700)
                      | moveonly | downonly (no up) | moved (up elsewhere) | pretap (a pair logged at the pick, none after)
  r7d: OA_FAKE_IME        the soft keyboard as `dumpsys input_method` says it: hidden | shown | unknown (the ui state's
                          "ime" overrides it once set)
       OA_FAKE_NOVIDEO=1  the viewer shows no video view (videoTextureView)
       OA_FAKE_BACK       what BACK does on the PC list with the keyboard up: ime (the keyboard takes it and hides) |
                          leak (the app takes it: the exit question, android:id/button1; the keyboard stays)
  The ui state's "scene": "hosts" (the PC list: hostCardName oa-pc-a), "targets" (default) or "viewer" (set by a pick).
  r7e: "login" too (loginIdInput / loginPasswordInput / loginButton; Sign-in -> the PC list, the card -> the targets), and
  what oa_avd.main asks before the app starts (install, pm clear, pm path, dumpsys package, pull of base.apk =
  OA_FAKE_ADB_APK) -- so the stub can drive arm S from the install to 4 and see what it does after.
  r7f: the Settings tab (listSettingsButton) with 0.2.32's sharpening row (settingsSharpOffButton / Weak / Strong; the
  requested level "selected", weak until changed); the app's content box android:id/content (0,136)-(1080,2400).
       OA_FAKE_SH   ok (default) | norow (no sharpening row shown) | offscreen (Off below the content box) | nolog (the
                    tap logs no sharpness_select) | noselect (logged, but the level stays weak) | start (the picture
                    still goes through the sharpen path: sharpen_path path=sharpen + sharpen_start)
                    At the picture: requested off -> sharpen_path path=direct requested=off; still weak -> path=direct
                    requested=weak effective=off fallback=... (a fallback, not SH off).
       In the viewer every pull of the app log adds a video_debug line with out one higher (OA_FAKE_FROZEN=1: not);
       OA_FAKE_DROP_SEC=<s>: that long after the picture the connection breaks (the app: scene HOSTS, status error; the
       host: udp control session ended) and the PC list shows the error text (statusText).
       OA_FAKE_PW_TEXT: the login password field's text (to see that a record never writes it).
"""
import json
import os
import shutil
import sys
import time

PINNED = int(os.environ.get('OA_FAKE_PINNED', '80809746'))
TITLE = 'remote60 mobile abr target pid 43948 92abc8fe'
PKG = 'com.remote60.androiddirect'
TABS = {'listWindowsButton': (0, 380, 400, 430, 'Windows'), 'listDevicesButton': (420, 380, 660, 430, 'Desktop'),
        'listSettingsButton': (680, 380, 1040, 430, 'Settings')}
CONTENT = (0, 136, 1080, 2400)
SH_ROW = {'settingsSharpOffButton': (40, 1300, 330, 1400, '끄기', 'off'),
          'settingsSharpWeakButton': (370, 1300, 660, 1400, '약하게', 'weak'),
          'settingsSharpStrongButton': (700, 1300, 1040, 1400, '강하게', 'strong')}
SH = os.environ.get('OA_FAKE_SH', 'ok')
CARD = (100, 1000, 470, 1100)
HOSTCARD = (40, 400, 1040, 560)
VIDEO = (0, 136, 1080, 2400)
DIALOG_OK = (700, 1300, 1000, 1400)

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


def host(line, tag='[control] '):
    with open(os.environ['OA_FAKE_HOSTLOG'], 'a', encoding='utf-8') as f:
        f.write('10-30 00:00:00.000 [native-video-host]' + tag + line + '\n')


def card_text():
    if state['tab'] == 'desktop':
        return 'Desktop'
    return 'Notepad' if variant == 'nopinned' else 'remote60 mobile abr…'


LOGIN = {'loginIdInput': (40, 480, 1040, 560, 'ID'), 'loginPasswordInput': (40, 630, 1040, 720, 'Password'),
         'loginButton': (40, 800, 1040, 900, 'Sign In')}


def nodes():
    root = [('android:id/content', CONTENT, '')]
    if state.get('scene') == 'login':
        return root + [(k, v[:4], os.environ.get('OA_FAKE_PW_TEXT', v[4]) if k == 'loginPasswordInput' else v[4])
                       for k, v in LOGIN.items()]
    if state.get('scene') == 'hosts':
        out = root + [('hostCardName', HOSTCARD, 'oa-pc-a')]
        if state.get('err'):
            out.append(('statusText', (40, 300, 1040, 380), '연결이 끊겼습니다 (peer lost)'))
        if state.get('dialog'):
            out.append(('android:id/button1', DIALOG_OK, 'OK'))
        return out
    out = root + [(k, v[:4], v[4]) for k, v in TABS.items()]
    if state.get('scene') != 'viewer' and state['tab'] == 'settings':
        if SH != 'norow':
            for k, v in SH_ROW.items():
                b = v[:4] if not (SH == 'offscreen' and k == 'settingsSharpOffButton') else (40, 2450, 330, 2550)
                out.append((k, b, v[4]))
    elif state.get('scene') != 'viewer':
        out.append(('targetCardTitle', CARD, card_text()))
    elif os.environ.get('OA_FAKE_NOVIDEO') != '1':
        out.append(('videoTextureView', VIDEO, ''))
    return out


def node_xml(rid, b, text):
    full = rid if ':' in rid else f'{PKG}:id/{rid}'
    sel = 'true' if rid in SH_ROW and SH_ROW[rid][5] == state.get('sh', 'weak') else 'false'
    pw = 'true' if rid == 'loginPasswordInput' else 'false'
    return (f'<node resource-id="{full}" text="{text}" content-desc="" enabled="true" clickable="true" '
            f'selected="{sel}" password="{pw}" bounds="[{b[0]},{b[1]}][{b[2]},{b[3]}]" />')


def ime():
    return state.get('ime', os.environ.get('OA_FAKE_IME', 'hidden'))


def picture_tap():
    mode = os.environ.get('OA_FAKE_INPUT', 'pair')
    seq = state.get('inseq', 0)
    events = {'pair': [(1, 240, 135), (2, 240, 135), (3, 240, 135)], 'none': [], 'uponly': [(3, 240, 135)],
              'outside': [(2, 900, 700), (3, 900, 700)], 'moveonly': [(1, 240, 135), (1, 241, 136)],
              'downonly': [(1, 240, 135), (2, 240, 135)], 'moved': [(2, 240, 135), (3, 300, 200)],
              'pretap': []}[mode]
    for kind, x, y in events:
        seq += 1
        host(f'seq={seq} kind={kind} x={x} y={y} buttons={1 if kind == 2 else 0}', tag='[input-trace] ')
    state['inseq'] = seq


cmd = ' '.join(args)
if args[:1] == ['version']:
    print('Android Debug Bridge version 1.0.41 (fake, oa_pick_test)')
elif cmd.startswith('exec-out uiautomator'):
    sys.stdout.buffer.write(('<?xml version="1.0" encoding="UTF-8"?><hierarchy rotation="0">' +
                             ''.join(node_xml(*n) for n in nodes()) + '</hierarchy>').encode('utf-8'))
elif cmd.startswith('shell input tap'):
    x, y = int(args[3]), int(args[4])
    hit = [n for n in nodes() if n[1][0] <= x <= n[1][2] and n[1][1] <= y <= n[1][3] and n[0] != 'android:id/content']
    rid = hit[0][0] if hit else ''
    if rid == 'loginButton':
        state['scene'] = 'hosts'
        app('[login_attempt] server=http://10.0.2.2:18410 id=oatest')
    elif rid == 'hostCardName':
        state['scene'] = 'targets'
        app('[directory_chosen] 10.0.2.2:18443|relay')
        app('[targets_ready] origin=connect ms=1 items=1 gen=1')
        host('client connected transport=udp', tag=' ')
    elif rid == 'listWindowsButton':
        state['tab'] = 'windows'
        app('[tab_switch] tab=windows')
        host(f'window-list seq=2 count=1 selectedId={PINNED}')
    elif rid == 'listDevicesButton':
        state['tab'] = 'desktop'
        app('[tab_switch] tab=desktop')
    elif rid == 'listSettingsButton':
        state['tab'] = 'settings'
        app('[tab_switch] tab=settings')
    elif rid == 'settingsSharpOffButton':
        if SH != 'nolog':
            app('[sharpness_select] level=off s=0.0 origin=settings path=none effective=off')
        if SH != 'noselect':
            state['sh'] = 'off'
    elif rid == 'targetCardTitle':
        if state['tab'] == 'windows' and variant in ('ok', 'genmismatch', 'novideo'):
            tid, label, tab = PINNED, TITLE, 'WINDOWS'
        elif state['tab'] == 'windows' and variant == 'nopinned':
            tid, label, tab = 4242, 'Notepad', 'WINDOWS'
        else:
            tid, label, tab = 0, 'Desktop', 'DESKTOP'
        app(f'[select_request] targetId={tid} label={label} tab={tab} gen=1 origin=tap expected=0x0')
        host(f'window-select seq=1 requestedId={tid} pending')
        if tid == PINNED or variant == 'zeroready':
            host(f'window-select seq=1 requestedId={tid} applied=1 selectedId={tid} streamGen=2 reason=ok title={TITLE}')
            app(f'[select_ack] targetId={tid} gen=1 streamGen={7 if variant == "genmismatch" else 2} hostSendQpcUs=1 title={TITLE}')
            app(f'[select_ready] targetId={tid} gen=1 lastOutUs=1 title={TITLE} ms=900')
            if state.get('sh', 'weak') == 'off' and SH != 'start':
                app('[sharpen_path] path=direct requested=off effective=off fallback=none')
            elif SH == 'start':
                app('[sharpen_path] path=sharpen gen=1 requested=off s=0.0')
                app('[sharpen_start] gen=1 s=0.0 faults=none')
            else:
                app('[sharpen_path] path=direct requested=weak effective=off fallback=gl_unavailable')
            if variant != 'novideo':
                app('[video_debug] surface=on codec=on in=3 out=3')
            state['scene'] = 'viewer'
            state['viewer_t'] = time.time()
            if os.environ.get('OA_FAKE_INPUT') == 'pretap':  # a down/up BEFORE the picture tap: not its events
                for kind in (2, 3):
                    state['inseq'] = state.get('inseq', 0) + 1
                    host(f"seq={state['inseq']} kind={kind} x=240 y=135 buttons=0", tag='[input-trace] ')
        else:
            host(f'window-select seq=1 requestedId={tid} applied=0 reason=pinned_test_seam_refused')
            app(f'[select_failed] targetId={tid} gen=1 seq=1 status=window_select_failed: pinned_test_only')
    elif rid == 'videoTextureView':
        picture_tap()
    json.dump(state, open(state_path, 'w', encoding='utf-8'))
elif cmd.startswith('shell dumpsys input_method'):
    st = ime()
    if st == 'shown':
        print('  mInputShown=true mVisibleBound=true\n  mWindowVisible=true mDecorViewVisible=true')
    elif st == 'hidden':
        print('  mInputShown=false mVisibleBound=false\n  mWindowVisible=false mDecorViewVisible=false')
    else:
        print('  (no state lines this build prints)')
elif cmd == 'shell input keyevent 4':
    if ime() == 'shown' and os.environ.get('OA_FAKE_BACK', 'ime') == 'ime':
        state['ime'] = 'hidden'
    elif state.get('scene') == 'hosts':
        state['dialog'] = True
    json.dump(state, open(state_path, 'w', encoding='utf-8'))
elif args[:1] == ['pull']:
    if args[1].endswith('base.apk'):
        shutil.copyfile(os.environ['OA_FAKE_ADB_APK'], args[2])
    else:
        if state.get('scene') == 'viewer':  # r7f: the picture goes on (or breaks) while the viewer is up
            drop = os.environ.get('OA_FAKE_DROP_SEC')
            if drop and time.time() - state.get('viewer_t', 0) >= float(drop):
                app('[scene] HOSTS')
                app('[status] error: 연결이 끊겼습니다 (peer lost)')
                host('udp control session ended epoch=1 resumed=0 reason=none')
                state['scene'] = 'hosts'
                state['err'] = True
            elif os.environ.get('OA_FAKE_FROZEN') != '1' and os.environ.get('OA_FAKE_UI_VARIANT') != 'novideo':
                state['vout'] = state.get('vout', 3) + 1
                app(f"[video_debug] surface=on codec=on in={state['vout']} out={state['vout']}")
            json.dump(state, open(state_path, 'w', encoding='utf-8'))
        shutil.copyfile(os.environ['OA_FAKE_APPLOG'], args[2])
elif args[:1] == ['install']:
    print('Performing Streamed Install\nSuccess')
elif cmd.startswith('shell pm clear'):
    print('Success')
elif cmd.startswith('shell pm path'):
    print('package:/data/app/~~fake/com.remote60.androiddirect-1/base.apk')
elif cmd.startswith('shell dumpsys package'):
    print('    versionCode=31 minSdk=28 targetSdk=34\n    versionName=0.2.32')
elif cmd.startswith('exec-out screencap'):
    sys.stdout.buffer.write(b'\x89PNG fake')
sys.exit(0)
