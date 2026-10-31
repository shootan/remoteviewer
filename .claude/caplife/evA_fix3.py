p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''        choose_level(level)
        time.sleep(4)
        pic, t = picture('4_%s' % level)
        lines = log_lines()
        path = 'direct' if level == 'off' else 'sharpen'
        took = any('sharpen_path' in l and ('path=%s' % path) in l for l in lines) or (level != 'off' and any('sharpen_first_output' in l for l in lines))''',
    '''        choose_level(level)
        time.sleep(6)
        pic, t = picture('4_%s' % level)
        lines = log_lines()
        path = 'direct' if level == 'off' else 'sharpen'
        # a path change logs sharpen_path; a strength change on a live renderer shows in the 1 s latency line
        took = any('sharpen_path' in l and ('path=%s' % path) in l for l in lines) or \\
            any('sh_latency' in l and ('level=%s ' % level) in l and ('path=%s ' % path) in l for l in lines)''')

rep('''    check(not forbidden(), 'the whole run: no forbidden control (window list / thumbnail / effectful) at the host', str(forbidden()))
    # 5. the tripwire itself (positive control): a window-list request on the control port is refused and logged
    try:
        import socket, struct
        sk = socket.create_connection(('127.0.0.1', fx.control_port), timeout=5)
        sk.sendall(struct.pack('<IHHIIQ', 0x31435052, 28, 24, 1, 0, 0))
        time.sleep(2)
        sk.close()
    except Exception as e:
        print('      tripwire probe: %r' % e, flush=True)
    time.sleep(1)
    check('28' in forbidden(), 'TRIPWIRE: a window-list request at this host is logged FORBIDDEN (and not answered)', str(forbidden()))
''', '''    check(not forbidden(), 'the whole run: no forbidden control (window list / thumbnail / effectful) at the host', str(forbidden()))


def scenario_prelogin_tripwire(fx, out):
    """apk-0232 evidence A, the tripwire's own control: GNLINK_STREAM_TEST_PRELOGIN_ROLE=2 advertises the role but KEEPS
    the user-data feature bits a real logon-screen worker strips (clipboard ...). The real APK then polls the clipboard,
    a message the logon screen refuses: the host must log it FORBIDDEN and close the link -- never answer it."""
    hlog = lambda: open(os.path.join(out, 'host.log'), 'rb').read().decode(errors='replace')
    launch(fx.port)
    ok = tap(rid='loginManualButton', timeout=30) and tap(rid='connectButton', timeout=20)
    check(ok, 'tripwire control: connected (manual)')
    time.sleep(8)
    fb = re.findall(r'prelogin-role FORBIDDEN control type=(\\d+)', hlog())
    check('role advertised' in hlog() and fb, 'TRIPWIRE: a refused message from the real app is logged FORBIDDEN', str(fb))
    check('tcp client disconnected' in hlog(), 'TRIPWIRE: ...and the link is closed (not answered)')
''')
rep('''        elif args.scenario == 'prelogin_ui_control':''', '''        elif args.scenario == 'prelogin_tripwire':
            scenario_prelogin_tripwire(fx, args.out)
        elif args.scenario == 'prelogin_ui_control':''')
rep('''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b', 'ua2_desktop', 'prelogin_ui', 'prelogin_ui_control') else None,''',
    '''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b', 'ua2_desktop', 'prelogin_ui', 'prelogin_ui_control', 'prelogin_tripwire') else None,''')
rep('''                                                                   if args.scenario in ('prelogin_ui', 'prelogin_ui_control') else ()),''',
    '''                                                                   if args.scenario in ('prelogin_ui', 'prelogin_ui_control', 'prelogin_tripwire') else ()),''')
rep('''                                                                  if args.scenario == 'prelogin_ui' else None))''',
    '''                                                                  if args.scenario == 'prelogin_ui' else
                                                                  {'GNLINK_STREAM_TEST_PRELOGIN_ROLE': '2'} if args.scenario == 'prelogin_tripwire'
                                                                  else None))''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

h = 'C:/work/gnlink-wt/apk-prelogin-select/apps/native_poc/src/host_control_session.cpp'
t = open(h, 'rb').read().decode('utf-8')
c2 = '\r\n' in t
t = t.replace('\r\n', '\n')
o1 = '''  const bool testPreloginRole = [] {
    wchar_t v[8] = {};
    return GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_PRELOGIN_ROLE", v, 8) > 0 && v[0] == L'1';
  }();'''
n1 = '''  // =2: the tripwire's own control -- the role advertised but the user-data feature bits KEPT (a real worker strips
  // them), so a correct app sends a message the logon screen refuses and the tripwire must catch it.
  wchar_t testPreloginRoleValue[8] = {};
  const bool testPreloginRoleSet =
      GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_PRELOGIN_ROLE", testPreloginRoleValue, 8) > 0;
  const bool testPreloginRole = testPreloginRoleSet && (testPreloginRoleValue[0] == L'1' || testPreloginRoleValue[0] == L'2');
  const bool testPreloginRoleKeepsBits = testPreloginRole && testPreloginRoleValue[0] == L'2';'''
assert t.count(o1) == 1
t = t.replace(o1, n1)
o2 = '''      logonScreenSurface = logonScreenSurface || testPreloginRole;'''
assert t.count(o2) == 1
t = t.replace(o2, '''      logonScreenSurface = logonScreenSurface || (testPreloginRole && !testPreloginRoleKeepsBits);''')
if c2:
    t = t.replace('\n', '\r\n')
open(h, 'wb').write(t.encode('utf-8'))
print('ok')
