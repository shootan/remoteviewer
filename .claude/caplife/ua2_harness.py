p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep('''def scenario_race(fx, out):''', '''def ua2_controls():
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
    return re.findall(r'desktop-backend-request seq=\\d+ backend=(\\w+)', log)


def scenario_ua2(fx, out):
    """ua2-apk r1 (Codex codex_rel_0163_r1.md §1): the desktop capture backend is this connection's only. Through
    the real APK UI against the test host: an OLD stored global value is not sent; before a connection the controls
    are off; connected, the buttons show what the HOST runs; a pick goes to this connection; a new connection asks
    nothing and shows the host default again; the old key is gone; the sharpness choice is kept."""
    # An older APK's stored state: WGC kept globally (re-sent on every connect), and 강하게 chosen.
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    prefs = ('<?xml version="1.0" encoding="utf-8" standalone="yes" ?>\\n<map>\\n'
             '    <int name="desktop_backend" value="2" />\\n    <string name="sharpness">strong</string>\\n</map>\\n')
    tmp = os.path.join(out, 'seed_prefs.xml')
    with open(tmp, 'w', encoding='utf-8') as fh:
        fh.write(prefs)
    adb('push', tmp, '/data/local/tmp/gnlink_seed_prefs.xml')
    adb('shell', 'run-as', PKG, 'sh', '-c',
        'mkdir -p shared_prefs && cp /data/local/tmp/gnlink_seed_prefs.xml shared_prefs/remote60_android_direct.xml')
    seeded = adb('shell', 'run-as', PKG, 'cat', 'shared_prefs/remote60_android_direct.xml', check_rc=False)
    check('desktop_backend' in seeded and 'strong' in seeded, 'an older APK\\'s stored WGC (global) and 강하게 are seeded', seeded[-160:])

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
    check(st and '지금 PC가 쓰는 방식' in st['text'] and (d['selected'] or w['selected']),
          'connected: the line says what the HOST runs and that button is lit (not the stored WGC by itself)', str(st))
    host_lit = 'WGC' if (w and w['selected']) else 'DXGI'
    # a window target is WGC whatever: the reason is said
    check(st and ('창 캡처는 WGC' in st['text'] or host_lit == 'DXGI'), 'the reason is shown when the host runs WGC for a window', str(st))
    # pick GDI (a real tap on the rendered button)
    gdi = find(dump(), rid='settingsDesktopBackendGdiButton')
    tap_node(gdi)
    time.sleep(4)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2_3_after_pick.png'))
    req = backend_requests(fx)
    check(req == ['gdi'], 'the tap sent ONE change request, for this connection (gdi)', str(req))
    st = c['settingsDesktopBackendStatusText']
    check(st and '요청: GDI' in st['text'], 'the line says what was asked AND what the host applies', str(st))
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
    check(st and '요청:' not in st['text'] and '지금 PC가 쓰는 방식' in st['text'],
          'reconnected: no choice of its own -- the line shows the host\\'s current one only', str(st))
    prefs_after = adb('shell', 'run-as', PKG, 'cat', 'shared_prefs/remote60_android_direct.xml', check_rc=False)
    check('desktop_backend' not in prefs_after, 'the old global key is gone from the app\\'s storage', prefs_after[-200:])
    check('strong' in prefs_after, 'the sharpness choice (강하게) is kept', prefs_after[-200:])
    lines = [l for l in log_lines() if 'desktop_backend' in l]
    with open(os.path.join(out, 'ua2_app_lines.txt'), 'w', encoding='utf-8') as fh:
        fh.write('\\n'.join(lines))


def scenario_race(fx, out):''')
rep('''                                                                      layered_size=(1440, 810) if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab') else None,''',
    '''                                                                      layered_size=(1440, 810) if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2') else None,''')
rep('''        elif args.scenario == 'touch_ab':''', '''        elif args.scenario == 'ua2':
            scenario_ua2(fx, args.out)
        elif args.scenario == 'touch_ab':''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
