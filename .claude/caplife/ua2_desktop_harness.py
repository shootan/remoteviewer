p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''def scenario_ua2(fx, out):''', r'''def scenario_ua2_desktop(fx, out):
    """ua2-apk r3 (Codex codex_ua2_apk_src_r2.md N1, the desktop target): the settings line through the real APK UI
    while the DESKTOP is captured (DXGI at the test host) -- the host's own state first (no request: "지금 바탕화면
    캡처: DXGI", never a confirmed state before the host has one), then a GDI pick: "적용됨" ONLY if the host's answer
    says GDI captures now, else "접수 · 아직 이 방식으로 캡처하지 않음"; then back to DXGI. Each step's screen saved."""
    ok = connect_to_pattern(fx.port, best_quality=False)  # the desktop (no window pick)
    check(ok, 'connected and viewing the DESKTOP')
    time.sleep(3)
    for _ in range(3):
        if find(dump(), rid='listSettingsButton') is not None:
            break
        adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
        time.sleep(2)
    show_backend_controls()
    time.sleep(4)  # the screen asks the host (about every 2 s)
    c = ua2_controls()
    screencap(os.path.join(out, 'ua2d_1_idle.png'))
    st, d = c['settingsDesktopBackendStatusText'], c['settingsDesktopBackendDxgiButton']
    states = [re.search(r'state=(\w+)', l).group(1) for l in log_lines('desktop_backend_ui') if re.search(r'state=(\w+)', l)]
    print('      states so far: %s' % states, flush=True)
    check(st and st['text'].startswith('지금 바탕화면 캡처: DXGI') and d and d['selected'],
          'desktop, no request: the host\'s own -- "지금 바탕화면 캡처: DXGI", DXGI lit (IDLE)', str(st))
    check('IDLE' in states and all(x in ('CHECKING', 'IDLE') for x in states),
          'before the request only CHECKING (no state yet) and then IDLE were shown', str(states))

    def pick(rid, label, code):
        adb('logcat', '-c', check_rc=False)
        before = len(backend_requests(fx))
        for _ in range(2):
            tap_node(find(dump(), rid=rid))
            time.sleep(3)
            if len(backend_requests(fx)) > before:
                break
        time.sleep(4)
        c = ua2_controls()
        screencap(os.path.join(out, 'ua2d_%s.png' % label))
        st = c['settingsDesktopBackendStatusText']
        hlog = open(os.path.join(out, 'host.log'), 'rb').read().decode(errors='replace')
        ans = re.findall(r'desktop-backend-status seq=\d+ requested=(\d) effective=(\d) active=(\d) flags=0x([0-9a-f]+)', hlog)
        last = ans[-1] if ans else None
        print('      %s: host answer %s  line %r' % (label, last, st and st['text']), flush=True)
        if not check(last is not None and last[0] == code, label + ': the host handled the request (requested=%s)' % code, str(last)):
            return
        text = st['text'] if st else ''
        name = {'1': 'DXGI', '2': 'WGC', '3': 'GDI'}[code]
        if last[2] == code:
            check(text.startswith('요청: %s 적용됨' % name), label + ': captures with it now -> "적용됨"', text)
        elif last[1] != code and last[2] == last[1]:
            check(text.startswith('요청: %s 접수 · PC 정책으로' % name), label + ': the host\'s policy uses another -> "PC 정책으로"', text)
        else:
            check(text.startswith('요청: %s 접수 · 아직 이 방식으로 캡처하지 않음' % name),
                  label + ': handled, another captures now -> "접수 · 아직 …" (never "적용됨")', text)
        check('지금 바탕화면 캡처: %s' % {'1': 'DXGI', '2': 'WGC', '3': 'GDI'}.get(last[2], '?') in text,
              label + ': the line says what captures now (the host\'s active)', text)

    pick('settingsDesktopBackendGdiButton', '2_gdi', '3')
    pick('settingsDesktopBackendDxgiButton', '3_dxgi', '1')


def scenario_ua2(fx, out):''')
rep('''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b') else None,''',
    '''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b', 'ua2_desktop') else None,''')
rep('''        elif args.scenario == 'ua2':
            scenario_ua2(fx, args.out)''', '''        elif args.scenario == 'ua2':
            scenario_ua2(fx, args.out)
        elif args.scenario == 'ua2_desktop':
            scenario_ua2_desktop(fx, args.out)''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
