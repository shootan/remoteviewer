p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\stubs\fake_ui_adb.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''                      | moveonly | downonly (no up) | moved (up elsewhere) | pretap (a pair logged at the pick, none after)
''', '''                      | moveonly | downonly (no up) | moved (up elsewhere) | pretap (a pair logged at the pick, none after)
  r7d: OA_FAKE_IME        the soft keyboard as `dumpsys input_method` says it: hidden | shown | unknown (the ui state's
                          "ime" overrides it once set)
       OA_FAKE_NOVIDEO=1  the viewer shows no video view (videoTextureView)
       OA_FAKE_BACK       what BACK does on the PC list with the keyboard up: ime (the keyboard takes it and hides) |
                          leak (the app takes it: the exit question, android:id/button1; the keyboard stays)
  The ui state's "scene": "hosts" (the PC list: hostCardName oa-pc-a), "targets" (default) or "viewer" (set by a pick).
''')
rep('''CARD = (100, 1000, 470, 1100)
''', '''CARD = (100, 1000, 470, 1100)
HOSTCARD = (40, 400, 1040, 560)
VIDEO = (0, 136, 1080, 2400)
DIALOG_OK = (700, 1300, 1000, 1400)
''')
rep('''def nodes():
    out = [(k, v[:4], v[4]) for k, v in TABS.items()]
    if state.get('scene') != 'viewer':
        out.append(('targetCardTitle', CARD, card_text()))
    return out


def node_xml(rid, b, text):
    return (f'<node resource-id="{PKG}:id/{rid}" text="{text}" content-desc="" enabled="true" clickable="true" '
            f'bounds="[{b[0]},{b[1]}][{b[2]},{b[3]}]" />')
''', '''def nodes():
    if state.get('scene') == 'hosts':
        out = [('hostCardName', HOSTCARD, 'oa-pc-a')]
        if state.get('dialog'):
            out.append(('android:id/button1', DIALOG_OK, 'OK'))
        return out
    out = [(k, v[:4], v[4]) for k, v in TABS.items()]
    if state.get('scene') != 'viewer':
        out.append(('targetCardTitle', CARD, card_text()))
    elif os.environ.get('OA_FAKE_NOVIDEO') != '1':
        out.append(('videoTextureView', VIDEO, ''))
    return out


def node_xml(rid, b, text):
    full = rid if ':' in rid else f'{PKG}:id/{rid}'
    return (f'<node resource-id="{full}" text="{text}" content-desc="" enabled="true" clickable="true" '
            f'bounds="[{b[0]},{b[1]}][{b[2]},{b[3]}]" />')


def ime():
    return state.get('ime', os.environ.get('OA_FAKE_IME', 'hidden'))
''')
rep('''    elif state.get('scene') == 'viewer' and (x, y) == (1200, 540):
        picture_tap()
    json.dump(state, open(state_path, 'w', encoding='utf-8'))
''', '''    elif rid == 'videoTextureView':
        picture_tap()
    json.dump(state, open(state_path, 'w', encoding='utf-8'))
elif cmd.startswith('shell dumpsys input_method'):
    st = ime()
    if st == 'shown':
        print('  mInputShown=true mVisibleBound=true\\n  mWindowVisible=true mDecorViewVisible=true')
    elif st == 'hidden':
        print('  mInputShown=false mVisibleBound=false\\n  mWindowVisible=false mDecorViewVisible=false')
    else:
        print('  (no state lines this build prints)')
elif cmd == 'shell input keyevent 4':
    if ime() == 'shown' and os.environ.get('OA_FAKE_BACK', 'ime') == 'ime':
        state['ime'] = 'hidden'
    elif state.get('scene') == 'hosts':
        state['dialog'] = True
    json.dump(state, open(state_path, 'w', encoding='utf-8'))
''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
