p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\stubs\fake_ui_adb.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''  The ui state's "scene": "hosts" (the PC list: hostCardName oa-pc-a), "targets" (default) or "viewer" (set by a pick).
''', '''  The ui state's "scene": "hosts" (the PC list: hostCardName oa-pc-a), "targets" (default) or "viewer" (set by a pick).
  r7e: "login" too (loginIdInput / loginPasswordInput / loginButton; Sign-in -> the PC list, the card -> the targets), and
  what oa_avd.main asks before the app starts (install, pm clear, pm path, dumpsys package, pull of base.apk =
  OA_FAKE_ADB_APK) -- so the stub can drive arm S from the install to 4 and see what it does after.
''')
rep('''def nodes():
    if state.get('scene') == 'hosts':''', '''LOGIN = {'loginIdInput': (40, 480, 1040, 560, 'ID'), 'loginPasswordInput': (40, 630, 1040, 720, 'Password'),
         'loginButton': (40, 800, 1040, 900, 'Sign In')}


def nodes():
    if state.get('scene') == 'login':
        return [(k, v[:4], v[4]) for k, v in LOGIN.items()]
    if state.get('scene') == 'hosts':''')
rep('''    if rid == 'listWindowsButton':''', '''    if rid == 'loginButton':
        state['scene'] = 'hosts'
        app('[login_attempt] server=http://10.0.2.2:18410 id=oatest')
    elif rid == 'hostCardName':
        state['scene'] = 'targets'
        app('[directory_chosen] 10.0.2.2:18443|relay')
        app('[targets_ready] origin=connect ms=1 items=1 gen=1')
        host('client connected transport=udp', tag=' ')
    elif rid == 'listWindowsButton':''')
rep('''elif args[:1] == ['pull']:
    shutil.copyfile(os.environ['OA_FAKE_APPLOG'], args[2])
''', '''elif args[:1] == ['pull']:
    if args[1].endswith('base.apk'):
        shutil.copyfile(os.environ['OA_FAKE_ADB_APK'], args[2])
    else:
        shutil.copyfile(os.environ['OA_FAKE_APPLOG'], args[2])
elif args[:1] == ['install']:
    print('Performing Streamed Install\\nSuccess')
elif cmd.startswith('shell pm clear'):
    print('Success')
elif cmd.startswith('shell pm path'):
    print('package:/data/app/~~fake/com.remote60.androiddirect-1/base.apk')
elif cmd.startswith('shell dumpsys package'):
    print('    versionCode=31 minSdk=28 targetSdk=34\\n    versionName=0.2.32')
''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
