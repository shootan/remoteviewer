p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\stubs\fake_ui_adb.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''  what oa_avd.main asks before the app starts (install, pm clear, pm path, dumpsys package, pull of base.apk =
  OA_FAKE_ADB_APK) -- so the stub can drive arm S from the install to 4 and see what it does after.
"""''', '''  what oa_avd.main asks before the app starts (install, pm clear, pm path, dumpsys package, pull of base.apk =
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
"""''')
rep('''import json
import os
import shutil
import sys
''', '''import json
import os
import shutil
import sys
import time
''')
rep('''TABS = {'listWindowsButton': (0, 380, 400, 430, 'Windows'), 'listDevicesButton': (420, 380, 660, 430, 'Desktop')}''',
    '''TABS = {'listWindowsButton': (0, 380, 400, 430, 'Windows'), 'listDevicesButton': (420, 380, 660, 430, 'Desktop'),
        'listSettingsButton': (680, 380, 1040, 430, 'Settings')}
CONTENT = (0, 136, 1080, 2400)
SH_ROW = {'settingsSharpOffButton': (40, 1300, 330, 1400, '끄기', 'off'),
          'settingsSharpWeakButton': (370, 1300, 660, 1400, '약하게', 'weak'),
          'settingsSharpStrongButton': (700, 1300, 1040, 1400, '강하게', 'strong')}
SH = os.environ.get('OA_FAKE_SH', 'ok')''')
rep('''def nodes():
    if state.get('scene') == 'login':
        return [(k, v[:4], v[4]) for k, v in LOGIN.items()]
    if state.get('scene') == 'hosts':
        out = [('hostCardName', HOSTCARD, 'oa-pc-a')]
        if state.get('dialog'):
            out.append(('android:id/button1', DIALOG_OK, 'OK'))
        return out
    out = [(k, v[:4], v[4]) for k, v in TABS.items()]
    if state.get('scene') != 'viewer':
        out.append(('targetCardTitle', CARD, card_text()))''', '''def nodes():
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
        out.append(('targetCardTitle', CARD, card_text()))''')
rep('''def node_xml(rid, b, text):
    full = rid if ':' in rid else f'{PKG}:id/{rid}'
    return (f'<node resource-id="{full}" text="{text}" content-desc="" enabled="true" clickable="true" '
            f'bounds="[{b[0]},{b[1]}][{b[2]},{b[3]}]" />')''', '''def node_xml(rid, b, text):
    full = rid if ':' in rid else f'{PKG}:id/{rid}'
    sel = 'true' if rid in SH_ROW and SH_ROW[rid][5] == state.get('sh', 'weak') else 'false'
    pw = 'true' if rid == 'loginPasswordInput' else 'false'
    return (f'<node resource-id="{full}" text="{text}" content-desc="" enabled="true" clickable="true" '
            f'selected="{sel}" password="{pw}" bounds="[{b[0]},{b[1]}][{b[2]},{b[3]}]" />')''')
rep('''    elif rid == 'listDevicesButton':
        state['tab'] = 'desktop'
        app('[tab_switch] tab=desktop')''', '''    elif rid == 'listDevicesButton':
        state['tab'] = 'desktop'
        app('[tab_switch] tab=desktop')
    elif rid == 'listSettingsButton':
        state['tab'] = 'settings'
        app('[tab_switch] tab=settings')
    elif rid == 'settingsSharpOffButton':
        if SH != 'nolog':
            app('[sharpness_select] level=off s=0.0 origin=settings path=none effective=off')
        if SH != 'noselect':
            state['sh'] = 'off\'''')
rep('''            if variant != 'novideo':
                app('[video_debug] surface=on codec=on in=3 out=3')
            state['scene'] = 'viewer\'''', '''            if state.get('sh', 'weak') == 'off' and SH != 'start':
                app('[sharpen_path] path=direct requested=off effective=off fallback=none')
            elif SH == 'start':
                app('[sharpen_path] path=sharpen gen=1 requested=off s=0.0')
                app('[sharpen_start] gen=1 s=0.0 faults=none')
            else:
                app('[sharpen_path] path=direct requested=weak effective=off fallback=gl_unavailable')
            if variant != 'novideo':
                app('[video_debug] surface=on codec=on in=3 out=3')
            state['scene'] = 'viewer'
            state['viewer_t'] = time.time()''')
rep('''elif args[:1] == ['pull']:
    if args[1].endswith('base.apk'):
        shutil.copyfile(os.environ['OA_FAKE_ADB_APK'], args[2])
    else:
        shutil.copyfile(os.environ['OA_FAKE_APPLOG'], args[2])''', '''elif args[:1] == ['pull']:
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
        shutil.copyfile(os.environ['OA_FAKE_APPLOG'], args[2])''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
