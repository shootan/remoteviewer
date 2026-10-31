p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep('''def scenario_race(fx, out):''', '''def node_state(rid):
    root = dump()
    for n in (root.iter('node') if root is not None else []):
        if n.get('resource-id') == PKG + ':id/' + rid:
            return {'selected': n.get('selected') == 'true', 'enabled': n.get('enabled') == 'true', 'bounds': n.get('bounds')}
    return None


def to_list():
    for _ in range(4):
        if find(dump(), rid='listSettingsButton') is not None:
            return True
        adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
        time.sleep(2)
    return find(dump(), rid='listSettingsButton') is not None


def settings_method(rid_to_tap=None):
    """The settings tab scrolled to the drawing-method buttons; optionally a real tap on one. Returns their state."""
    tap(rid='listSettingsButton', timeout=20)
    for _ in range(10):
        if node_state('settingsSharpMethodDisplayButton'):
            break
        adb('shell', 'input', 'swipe', '540', '1800', '540', '1000', '300', check_rc=False)
        time.sleep(1)
    time.sleep(1)
    if rid_to_tap:
        n = find(dump(), rid=rid_to_tap)
        if n is not None:
            tap_node(n)
        time.sleep(1.5)
    return node_state('settingsSharpMethodDefaultButton'), node_state('settingsSharpMethodDisplayButton')


def pick_pattern_again():
    tap(rid='listWindowsButton', timeout=10)
    end = time.time() + 30
    card = None
    while time.time() < end and card is None:
        card = find_exact(dump(), PATTERN_TITLE)
        if card is None:
            time.sleep(1)
    if card is None:
        return False
    tap_node(card)
    return wait_for(rid='viewerMenuButton', timeout=40) or wait_for(rid='viewerRoot', timeout=5)


def scenario_method_ui(fx, out):
    """sh-strength r3 (Codex codex_sh_strength_r2.md §2, §4.1): the drawing method through the NORMAL settings UI of the
    seam-free APK -- default A, a real tap on "화면 크기 (실험)" -> B draws (display pass logged, crisper edges), B -> off
    -> direct (placement unchanged) -> strong again = B, back to 기본 = A, and the choice restored after a restart."""
    adb('logcat', '-c', check_rc=False)
    ok = connect_to_pattern(fx.port, level_before='strong', window=True)
    check(ok, 'connected, 강하게 (0.60), the pattern window shown')
    time.sleep(5)
    a = cmp_metrics(screencap(os.path.join(out, 'method_1_default.png')))
    lines = log_lines()
    check(a is not None and not any('sharpen_display_pass' in l for l in lines),
          'default: drawn the A way (no display pass) -- a fresh install is A', str(a and a['A_text_edge']))
    to_list()
    d, b = settings_method()
    check(d and b and d['selected'] and not b['selected'], 'settings: 기본 selected, 화면 크기 not (rendered)', str((d, b)))
    d, b = settings_method('settingsSharpMethodDisplayButton')
    check(d and b and b['selected'] and not d['selected'], 'a real tap on 화면 크기 (실험): it is the selected one', str((d, b)))
    adb('logcat', '-c', check_rc=False)
    check(pick_pattern_again(), 'back in the viewer')
    time.sleep(5)
    bm = cmp_metrics(screencap(os.path.join(out, 'method_2_display.png')))
    lines = log_lines()
    dp = [l[l.index('sharpen_display_pass'):][:160] for l in lines if 'sharpen_display_pass' in l]
    check(any('mode=1 method=display' in l for l in dp), 'B is drawn because of the SETTING (no test extra): display pass logged', str(dp[-1:]))
    check(a and bm and borders_ok(bm) and bm['x0'] == a['x0'], 'B: the picture is where A put it (borders, x)', str(bm and (bm['x0'], bm['border_top_rgb'])))
    check(a and bm and bm['A_text_edge'] > a['A_text_edge'] * 1.15, 'B: text edges crisper than A at the same strength',
          '%s -> %s' % (a and a['A_text_edge'], bm and bm['A_text_edge']))
    # B -> off -> strong (B again), in the viewer menu
    choose_level('off')
    time.sleep(4)
    off = cmp_metrics(screencap(os.path.join(out, 'method_3_off.png')))
    lines = log_lines()
    check(off and borders_ok(off) and off['x0'] == a['x0'] and any('sharpen_path' in l and 'path=direct' in l for l in lines),
          'B -> 끄기: direct again, the picture where it was', str(off and off['x0']))
    adb('logcat', '-c', check_rc=False)
    choose_level('strong')
    time.sleep(5)
    again = cmp_metrics(screencap(os.path.join(out, 'method_4_display_again.png')))
    lines = log_lines()
    check(any('sharpen_display_pass' in l and 'mode=1' in l for l in lines) and again and again['A_text_edge'] > a['A_text_edge'] * 1.15,
          '끄기 -> 강하게: B again (a new renderer, the display pass)', str(again and again['A_text_edge']))
    # back to 기본
    to_list()
    d, b = settings_method('settingsSharpMethodDefaultButton')
    check(d and d['selected'], 'a real tap on 기본: selected', str((d, b)))
    adb('logcat', '-c', check_rc=False)
    check(pick_pattern_again(), 'back in the viewer (기본)')
    time.sleep(5)
    a2 = cmp_metrics(screencap(os.path.join(out, 'method_5_default_again.png')))
    lines = log_lines()
    check(a2 and not any('sharpen_display_pass' in l and 'mode=1' in l for l in lines) and abs(a2['A_text_edge'] - a['A_text_edge']) < 0.1 * a['A_text_edge'],
          '기본 again: A (no display pass), edges as before', str(a2 and a2['A_text_edge']))
    # the choice survives a restart
    to_list()
    settings_method('settingsSharpMethodDisplayButton')
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    time.sleep(2)
    ok = connect_to_pattern(fx.port, best_quality=False, window=True)
    check(ok, 'restarted and connected')
    time.sleep(5)
    lines = log_lines()
    check(any('sharpen_display_pass' in l and 'mode=1 method=display' in l for l in lines), 'after a restart the stored 화면 크기 is used')
    to_list()
    d, b = settings_method()
    check(b and b['selected'], '...and shown selected in settings', str((d, b)))


def scenario_race(fx, out):''')
rep('''                                                                      layered_size=(1440, 810) if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old') else None,''',
    '''                                                                      layered_size=(1440, 810) if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui') else None,''')
rep('''        elif args.scenario == 'ua2_old':''', '''        elif args.scenario == 'method_ui':
            scenario_method_ui(fx, args.out)
        elif args.scenario == 'ua2_old':''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
