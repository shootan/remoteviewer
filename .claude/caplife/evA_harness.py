p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''    def __init__(self, host_exe, out, static, input_log=None, layered_size=None, pulse_ms=200):''',
    '''    def __init__(self, host_exe, out, static, input_log=None, layered_size=None, pulse_ms=200, host_args=(), host_env=None):''')
rep('''        env['REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE'] = '1'
''', '''        env['REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE'] = '1'
        env.update(host_env or {})
''')
rep('''        CONTROL_PORT[self.port] = self.control_port
        self.host = subprocess.Popen(cmd,''', '''        cmd += list(host_args)
        CONTROL_PORT[self.port] = self.control_port
        self.host = subprocess.Popen(cmd,''')

rep('''def scenario_ua2_desktop(fx, out):''', r'''def scenario_prelogin_ui(fx, out, role=True):
    """apk-0232 evidence A (Codex codex_apk0232_evidence_design.md): the REAL APK UI against a test host that advertises
    the logon-screen role (GNLINK_STREAM_TEST_PRELOGIN_ROLE=1, test build only) with the prelogin allowlist as a
    tripwire, its source fixed from startup on the off-screen pattern window (--capture-window-title; UI = the desktop,
    source = a WGC pattern -- not the real worker's DXGI logon screen). The app must take its LOCAL desktop selection:
    the list -> the PC -> its desktop card -> the first IDR -> the decoder -> the picture on screen; cancel before it
    shows and pick again on the same connection; SH 끄기/약하게/강하게/화면 크기 on that connection -- with NO window
    list / window select / thumbnail ever reaching the host. role=False is the negative control: an ordinary host, the
    same steps -- the app then sends its WindowSelect and the no-WindowSelect check must FAIL."""
    tag = '' if role else 'CONTROL '
    hlog = lambda: open(os.path.join(out, 'host.log'), 'rb').read().decode(errors='replace')
    selects = lambda: len(re.findall(r'window-select seq=\d+', hlog()))
    forbidden = lambda: re.findall(r'prelogin-role FORBIDDEN control type=(\d+)', hlog())
    if role:
        check('prelogin-role advertised' in hlog(), 'the test host advertises the logon-screen role (test seam on)')
    launch(fx.port)
    ok = tap(rid='loginManualButton', timeout=30) and tap(rid='connectButton', timeout=20)
    connected_at = time.time()
    check(ok, tag + 'connected (manual)')
    if not tap(rid='listDevicesButton', timeout=15):
        check(False, tag + 'the PC list could be shown')
        return
    wait = 11.5 - (time.time() - connected_at)  # the manual-connect workaround (see connect_to_pattern)
    if wait > 0:
        time.sleep(wait)

    def desktop_card():
        end = time.time() + 30
        while time.time() < end:
            n = find(dump(), rid='targetCardTitle')
            if n is not None:
                return n
            time.sleep(1)
        return None

    def picture(label):
        img = screencap(os.path.join(out, 'prelogin_%s.png' % label))
        t = lime_target(img)
        return t is not None and float(img.std()) > 10, t

    # 1. the desktop pick -> the local selection -> the picture
    card = desktop_card()
    screencap(os.path.join(out, 'prelogin_0_list.png'))
    if not check(card is not None, tag + 'the PC\'s desktop card is on the list'):
        return
    adb('logcat', '-c', check_rc=False)
    tap_node(card)
    shown = wait_for(rid='viewerMenuButton', timeout=40)
    time.sleep(3)
    pic, t = picture('1_shown')
    lines = log_lines()
    local = [l for l in lines if 'selection local (logon screen)' in l and 'begun=1' in l]
    check(shown and pic, tag + 'the desktop pick shows the picture (decoded pattern on screen)', 'lime=%s' % (t,))
    check(local, tag + 'the app took its LOCAL selection (logon screen)', (local or [''])[-1][-100:])
    check(selects() == 0, tag + 'no WindowSelect reached the host', 'window-select lines=%d' % selects())
    if not role:
        return
    # 2. cancel before the picture shows, then the same connection picks again
    adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
    time.sleep(3)
    card = desktop_card()
    if check(card is not None, 'back on the list'):
        b = card.get('bounds')
        g = re.match(r'\[(\d+),(\d+)\]\[(\d+),(\d+)\]', b or '')
        cx, cy = (int(g.group(1)) + int(g.group(3))) // 2, (int(g.group(2)) + int(g.group(4))) // 2
        adb('logcat', '-c', check_rc=False)
        # the tap and the cancel in one shell call: the cancel lands before the first IDR is shown
        adb('shell', 'input tap %d %d; input keyevent KEYCODE_BACK' % (cx, cy), check_rc=False)
        time.sleep(1)
        screencap(os.path.join(out, 'prelogin_2_cancelled.png'))
        early = find(dump(), rid='targetCardTitle') is not None
        time.sleep(6)
        late = find(dump(), rid='targetCardTitle') is not None and find(dump(), rid='viewerMenuButton') is None
        lines = log_lines()
        aborted = any('selection aborted' in l for l in lines)
        completed = [l for l in lines if 'window_selected' in l and 'logon screen' in l]
        check(early and late and aborted, 'cancel before the picture: the list stays, no late frame opens the viewer',
              'early=%s late=%s aborted=%s completed=%s' % (early, late, aborted, completed[-1:]))
        adb('logcat', '-c', check_rc=False)
        tap_node(desktop_card())
        shown = wait_for(rid='viewerMenuButton', timeout=40)
        time.sleep(3)
        pic, t = picture('3_again')
        local = [l for l in log_lines() if 'selection local (logon screen)' in l and 'begun=1' in l]
        check(shown and pic and local, 'picked again on the SAME connection: its own local selection, the picture shown',
              'lime=%s local=%s' % (t, (local or [''])[-1][-80:]))
    # 3. SH on this connection: 끄기 / 약하게 / 강하게, then 화면 크기 (실험) and back to 기본
    for level in ('off', 'weak', 'strong'):
        adb('logcat', '-c', check_rc=False)
        choose_level(level)
        time.sleep(4)
        pic, t = picture('4_%s' % level)
        lines = log_lines()
        path = 'direct' if level == 'off' else 'sharpen'
        took = any('sharpen_path' in l and ('path=%s' % path) in l for l in lines) or (level != 'off' and any('sharpen_first_output' in l for l in lines))
        check(pic and took, 'SH %s on this connection: the picture shown, the %s path' % (level, path), 'lime=%s' % (t,))
    to_list()
    settings_method('settingsSharpMethodDisplayButton')
    adb('logcat', '-c', check_rc=False)
    if tap(rid='listDevicesButton', timeout=10):
        tap_node(desktop_card())
    shown = wait_for(rid='viewerMenuButton', timeout=40)
    time.sleep(5)
    pic, t = picture('5_display')
    lines = log_lines()
    check(shown and pic and any('sharpen_display_pass' in l and 'mode=1 method=display' in l for l in lines),
          'SH 화면 크기 (실험): B drawn on this connection (display pass), the picture shown', 'lime=%s' % (t,))
    to_list()
    settings_method('settingsSharpMethodDefaultButton')
    # 4. the whole run: nothing the logon screen refuses ever reached the host
    check(selects() == 0, 'the whole run: no WindowSelect at the host', str(selects()))
    check(not forbidden(), 'the whole run: no forbidden control (window list / thumbnail / effectful) at the host', str(forbidden()))
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


def scenario_ua2_desktop(fx, out):''')

rep('''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b', 'ua2_desktop') else None,''',
    '''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b', 'ua2_desktop', 'prelogin_ui', 'prelogin_ui_control') else None,''')
rep('''                                                        input_log=input_log)''', '''                                                        input_log=input_log,
                                                        # evidence A: the source fixed from startup (no WindowSelect needed),
                                                        # the logon-screen role for prelogin_ui only (the control: an ordinary host)
                                                        host_args=(('--capture-window-title', PATTERN_TITLE)
                                                                   if args.scenario in ('prelogin_ui', 'prelogin_ui_control') else ()),
                                                        host_env=({'GNLINK_STREAM_TEST_PRELOGIN_ROLE': '1'}
                                                                  if args.scenario == 'prelogin_ui' else None))''')
rep('''        elif args.scenario == 'ua2_desktop':
            scenario_ua2_desktop(fx, args.out)''', '''        elif args.scenario == 'ua2_desktop':
            scenario_ua2_desktop(fx, args.out)
        elif args.scenario == 'prelogin_ui':
            scenario_prelogin_ui(fx, args.out)
        elif args.scenario == 'prelogin_ui_control':
            scenario_prelogin_ui(fx, args.out, role=False)''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
