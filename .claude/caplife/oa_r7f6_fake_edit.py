p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\stubs\fake_ui_adb.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''       OA_FAKE_PW_TEXT: the login password field's text (to see that a record never writes it).
"""''', '''       OA_FAKE_PW_TEXT: the login password field's text (to see that a record never writes it).
  r7f (6): OA_FAKE_PULL_FAIL=1  a pull of the app log fails (exit 1, nothing written).
       arm P: `am start ... --es host` -> the app's start (auto_login NO_CREDENTIAL; loginManualButton), the manual
       button -> the connect form (connectButton), connect -> the targets (login_manual, targets_ready; the host: a TCP
       control client).
"""''')
rep('''LOGIN = {''', '''MANUAL = (40, 1000, 1040, 1100)
CONNECT = (40, 1200, 1040, 1300)
LOGIN = {''')
rep('''    if state.get('scene') == 'hosts':
        out = root + [('hostCardName', HOSTCARD, 'oa-pc-a')]''', '''    if state.get('scene') == 'start':
        return root + [('loginManualButton', MANUAL, '수동 연결')]
    if state.get('scene') == 'connect':
        return root + [('connectButton', CONNECT, '연결')]
    if state.get('scene') == 'hosts':
        out = root + [('hostCardName', HOSTCARD, 'oa-pc-a')]''')
rep('''    if rid == 'loginButton':''', '''    if rid == 'loginManualButton':
        state['scene'] = 'connect'
    elif rid == 'connectButton':
        state['scene'] = 'targets'
        app('[login_manual] host=10.0.2.2')
        app('[targets_ready] origin=connect ms=1 items=1 gen=1')
        host('client connected transport=tcp')
    elif rid == 'loginButton':''')
rep('''elif args[:1] == ['pull']:
    if args[1].endswith('base.apk'):
        shutil.copyfile(os.environ['OA_FAKE_ADB_APK'], args[2])
    else:''', '''elif cmd.startswith('shell am start') and '--es' in args:
    state['scene'] = 'start'
    app('[auto_login] NO_CREDENTIAL')
    json.dump(state, open(state_path, 'w', encoding='utf-8'))
elif args[:1] == ['pull'] and not args[1].endswith('base.apk') and os.environ.get('OA_FAKE_PULL_FAIL') == '1':
    sys.stderr.write('adb: error: failed to stat remote object: Permission denied (fake)\\n')
    sys.exit(1)
elif args[:1] == ['pull']:
    if args[1].endswith('base.apk'):
        shutil.copyfile(os.environ['OA_FAKE_ADB_APK'], args[2])
    else:''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
