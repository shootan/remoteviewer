p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

# connected expectations (r2 wording, window target)
rep('''    check(st and '지금 PC가 쓰는 방식' in st['text'] and (d['selected'] or w['selected']),
          'connected: the line says what the HOST runs and that button is lit (not the stored WGC by itself)', str(st))
    host_lit = 'WGC' if (w and w['selected']) else 'DXGI'
    # a window target is WGC whatever: the reason is said
    check(st and ('창 캡처는 WGC' in st['text'] or host_lit == 'DXGI'), 'the reason is shown when the host runs WGC for a window', str(st))''',
    '''    # r2 (Codex N2/N3): a window is being captured -- the line says WGC NOW (not the dormant desktop backend), the scope
    # only because the host confirms it, and the lit button is this connection's desktop method as handled (DXGI)
    check(st and st['text'] == '지금 창 캡처: WGC · 이 연결에만 적용' and d['selected'] and not w['selected'],
          'connected (window target): "지금 창 캡처: WGC · 이 연결에만 적용", DXGI (this connection\\'s desktop method) lit', str(st))''')
rep('''    st = c['settingsDesktopBackendStatusText']
    check(st and '요청: GDI' in st['text'], 'the line says what was asked AND what the host applies', str(st))''',
    '''    st = c['settingsDesktopBackendStatusText']
    g = c['settingsDesktopBackendGdiButton']
    check(st and st['text'] == '요청: GDI · 바탕화면을 볼 때 적용 · 지금 창 캡처: WGC · 이 연결에만 적용' and g and g['selected'],
          'after the tap: GDI is kept for the desktop (handled, lit) while the window is captured with WGC -- not "applied"',
          str(st))''')
rep('''    check(st and '요청:' not in st['text'] and '지금 PC가 쓰는 방식' in st['text'],
          'reconnected: no choice of its own -- the line shows the host\\'s current one only', str(st))''',
    '''    check(st and st['text'] == '지금 창 캡처: WGC · 이 연결에만 적용',
          'reconnected: no choice of its own -- the host\\'s own again', str(st))
    ui = [l for l in log_lines() if 'desktop_backend_ui' in l]
    states = [re.search(r'state=(\\w+)', l).group(1) for l in ui if re.search(r'state=(\\w+)', l)]
    check('NO_ANSWER_HOST' not in states and (not states or states[0] in ('CHECKING', 'NOT_CONNECTED')),
          'the first pong (delayed 4 s) is waited for: CHECKING first, never shown as an older host', str(states[:6]))''')

# the old host scenario
rep('''def scenario_race(fx, out):''', '''def scenario_ua2_old(fx, out, new_host_exe):
    """ua2-apk r2 (Codex N3): an OLDER host (no backend status): the change goes out on the old wire, nothing is lit,
    nothing is confirmed and no session scope is promised -- then a NEW host on a new connection gets nothing of it."""
    ok = connect_to_pattern(fx.port, best_quality=False, window=True)
    check(ok, 'old host: connected and viewing (window pick)')
    for _ in range(3):
        if find(dump(), rid='listSettingsButton') is not None:
            break
        adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
        time.sleep(2)
    show_backend_controls()
    time.sleep(3)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2old_1_connected.png'))
    st = c['settingsDesktopBackendStatusText']
    lit = [k for k in ('settingsDesktopBackendDxgiButton', 'settingsDesktopBackendWgcButton', 'settingsDesktopBackendGdiButton')
           if c[k] and c[k]['selected']]
    check(st and st['text'] == '이 PC는 적용 결과를 알려주지 않습니다 · 이번 연결에만 적용되는지도 보장할 수 없습니다' and not lit,
          'old host: "알려주지 않습니다 · 보장할 수 없습니다", nothing lit', str(st) + ' lit=' + str(lit))
    time.sleep(2)
    gdi = find(dump(), rid='settingsDesktopBackendGdiButton')
    tap_node(gdi)
    time.sleep(3)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2old_2_after_pick.png'))
    st = c['settingsDesktopBackendStatusText']
    lit = [k for k in ('settingsDesktopBackendDxgiButton', 'settingsDesktopBackendWgcButton', 'settingsDesktopBackendGdiButton')
           if c[k] and c[k]['selected']]
    check(backend_requests(fx) == ['gdi'], 'old host: the change went out on the old wire (one request)', str(backend_requests(fx)))
    check(st and st['text'].startswith('요청: GDI 보냄') and '보장할 수 없습니다' in st['text'] and not lit,
          'old host: after the request still no confirmation, nothing lit, the scope still not promised', str(st) + ' lit=' + str(lit))
    tap(rid='listDisconnectButton', timeout=10)
    time.sleep(3)
    # a NEW host, a new connection
    fx2 = Fixture(new_host_exe, os.path.join(out, 'newhost'), static=False, layered_size=(1440, 810))
    try:
        ok = connect_to_pattern(fx2.port, best_quality=False, window=True)
        check(ok, 'new host: connected')
        time.sleep(3)
        check(backend_requests(fx2) == [], 'new host: nothing of the old connection\\'s request reached it', str(backend_requests(fx2)))
        for _ in range(3):
            if find(dump(), rid='listSettingsButton') is not None:
                break
            adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
            time.sleep(2)
        show_backend_controls()
        time.sleep(3)
        st = ua2_controls()['settingsDesktopBackendStatusText']
        screencap(os.path.join(out, 'ua2old_3_new_host.png'))
        check(st and st['text'] == '지금 창 캡처: WGC · 이 연결에만 적용', 'new host: its own state, no request carried', str(st))
    finally:
        fx2.close()


def scenario_race(fx, out):''')
rep('''    ap.add_argument('--seconds', type=int, default=60)''', '''    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--new-host-exe', default='')  # ua2_old: the second (new) host''')
rep('''                                                                      layered_size=(1440, 810) if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2') else None,''',
    '''                                                                      layered_size=(1440, 810) if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old') else None,''')
rep('''        elif args.scenario == 'ua2':''', '''        elif args.scenario == 'ua2_old':
            scenario_ua2_old(fx, args.out, args.new_host_exe)
        elif args.scenario == 'ua2':''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
