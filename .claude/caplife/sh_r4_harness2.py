p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''def scenario_touch_ab(fx, out, input_log):''', r'''def view_bounds():
    """The video TextureView's rectangle on the screen (uiautomator bounds), or None."""
    n = find(dump(), rid='videoTextureView')
    if n is None:
        return None
    g = re.match(r'\[(\d+),(\d+)\]\[(\d+),(\d+)\]', n.get('bounds') or '')
    return tuple(int(v) for v in g.groups()) if g else None


def display_pass_place():
    """The last B placement the app logged: (left, top, width, height, viewW, viewH) in view pixels."""
    for l in reversed(log_lines('sharpen_display_pass')):
        g = re.search(r'place=(-?[\d.]+),(-?[\d.]+),([\d.]+)x([\d.]+) .*view=(\d+)x(\d+)', l)
        if g:
            return tuple(float(v) for v in g.groups())
    return None


def picture_rect():
    """The picture's rectangle on the SCREEN: the view's bounds plus the B placement inside it."""
    vb, pl = view_bounds(), display_pass_place()
    if vb is None or pl is None:
        return None
    return (vb[0] + pl[0], vb[1] + pl[1], vb[0] + pl[0] + pl[2], vb[1] + pl[1] + pl[3]), vb, pl


def scenario_layout_b(fx, out, input_log):
    """sh-strength r4 (Codex codex_sh_strength_r3.md: N1 c + the Activity-level behaviours the earlier scenarios did not
    run) -- B (display-size pass) through the REAL Activity, the pattern window captured, taps injected by the host:
      1. letterbox / corners: taps in the black outside the picture send nothing; just inside its corners -> (0,0)-ish
         and (W,H)-ish of the window;
      2. a real rotation (the viewer's rotate button -> portrait and back): the new view size reaches B's placement,
         the picture is where the placement says, no fallback, a tap lands right;
      3. 크게보기 + 화면 이동: a pan moves the picture (local, nothing sent);
      4. a display resize (wm size, this AVD only): B follows the new view, a tap lands right;
      5. the input fence: the renderer takes a placement 6 s late (seam holdplace) -- a tap in that gap is NOT sent
         (input_held_layout), the next one after the layout is shown is, at the right point;
      6. fallback: B's swap fails (seam swap:40) -> direct, the picture still placed, a tap still right.
    Scroll shimmer is NOT judged here (no oracle in this harness) -- said so in the report, not claimed."""
    base_extras = ('--ez', 'shTestDisplayPass', 'true')

    def guard():
        hlog = open(os.path.join(out, 'host.log'), 'rb').read().decode(errors='replace')
        sel = re.findall(r'window-select seq=\d+ requestedId=\d+ applied=1 selectedId=(\d+) streamGen=\d+ reason=ok title=(.*)', hlog)
        return bool(sel) and sel[-1][1].strip().startswith(PATTERN_TITLE) and 'input injection enabled mode=background_message' in hlog

    def received_since(before):
        lines = open(input_log).read().splitlines() if os.path.exists(input_log) else []
        return [tuple(int(v) for v in l.split()[1:3]) for l in lines[before:] if l.startswith('down ')]

    def log_count():
        return len(open(input_log).read().splitlines()) if os.path.exists(input_log) else 0

    def tap_at(x, y, settle=2.5):
        adb('logcat', '-c', check_rc=False)
        before = log_count()
        adb('shell', 'input', 'tap', str(int(x)), str(int(y)), check_rc=False)
        time.sleep(settle)
        queued = [e for e in seam_inputs() if e[4] == 1 and e[0] in (2, 3)]
        return queued, received_since(before)

    def tap_lime(label):
        img = screencap(os.path.join(out, 'layoutb_%s.png' % label))
        t = lime_target(img)
        if not check(t is not None, label + ': the lime block is on screen'):
            return
        q, got = tap_at(t[0], t[1])
        check(q and got and 610 <= got[0][0] <= 730 and 440 <= got[0][1] <= 700,
              label + ': a tap on the lime block reaches the window INSIDE the lime block',
              'queued=%s received=%s' % (q[:1], got[:1]))

    def corners(label):
        pr = picture_rect()
        if not check(pr is not None, label + ': the picture rectangle (view bounds + B placement) is known'):
            return
        (l, t, r, b), vb, pl = pr
        # outside the picture, inside the view: the letterbox side (whichever has room)
        outside = []
        if l - vb[0] >= 12:
            outside.append(((vb[0] + l) / 2, (t + b) / 2))
        if t - vb[1] >= 12:
            outside.append(((l + r) / 2, (vb[1] + t) / 2))
        if vb[3] - b >= 12:
            outside.append(((l + r) / 2, (b + vb[3]) / 2))
        for i, (x, y) in enumerate(outside):
            q, got = tap_at(x, y, settle=1.5)
            check(not q and not got, label + ': a tap in the letterbox (%d,%d) sends nothing' % (x, y), 'queued=%s got=%s' % (q[:1], got[:1]))
        check(outside, label + ': there is a letterbox to tap', str((vb, (l, t, r, b))))
        q, got = tap_at(l + 4, t + 4)
        check(q and got and got[0][0] <= 12 and got[0][1] <= 12, label + ': just inside the top-left corner -> the window\'s (0,0)',
              'queued=%s received=%s' % (q[:1], got[:1]))
        q, got = tap_at(r - 5, b - 5)
        check(q and got and got[0][0] >= 1440 - 14 and got[0][1] >= 810 - 14,
              label + ': just inside the bottom-right corner -> the window\'s (1440,810)', 'queued=%s received=%s' % (q[:1], got[:1]))

    def picture_where_placed(label):
        pr = picture_rect()
        img = screencap(os.path.join(out, 'layoutb_%s_pic.png' % label))
        if not check(pr is not None, label + ': B placement logged'):
            return
        (l, t, r, b), vb, pl = pr
        h, w = img.shape[:2]
        inside = img[int(t) + 6:int(b) - 6, int(l) + 6:int(r) - 6]
        # the pattern's top border row is red: the first rows of the picture
        top = img[int(t) + 1:int(t) + 4, int(l) + 20:int(r) - 20].reshape(-1, 3).mean(axis=0)
        check(inside.size and float(inside.std()) > 10 and top[0] > 60 and top[1] < 60,
              label + ': the picture is drawn where B placed it (the red top border at the placement\'s top)',
              'rect=%s view=%s top_rgb=%s' % ((int(l), int(t), int(r), int(b)), vb, [round(float(v)) for v in top]))

    # ---- 1-4 in one B session
    ok = connect_to_pattern(fx.port, base_extras, level_before='strong', window=True)
    check(ok, 'B: connected, 강하게, the pattern window picked')
    time.sleep(5)
    if not check(guard(), 'GUARD: the host captures the pattern window and injects by message'):
        return
    lines = log_lines()
    check(any('sharpen_display_pass' in l and 'mode=1' in l for l in lines), 'B is drawing (display pass logged)')
    picture_where_placed('land')
    corners('landscape')
    tap_lime('landscape')
    # 2. rotation: the viewer's own button -> portrait
    adb('logcat', '-c', check_rc=False)
    rot = tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerRotateButton', timeout=15, reveal=scroll_menu_sheet)
    tap(rid='viewerMenuCloseButton', timeout=5)
    time.sleep(6)
    pl = display_pass_place()
    check(rot and pl is not None and pl[5] > pl[4], 'rotation: portrait, and B\'s placement has the new (portrait) view size',
          str(pl))
    lines = log_lines()
    check(not any('sharpen_fallback' in l for l in lines), 'rotation: no fallback (B stays)', '')
    picture_where_placed('portrait')
    corners('portrait')
    tap_lime('portrait')
    adb('logcat', '-c', check_rc=False)
    rot = tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerRotateButton', timeout=15, reveal=scroll_menu_sheet)
    tap(rid='viewerMenuCloseButton', timeout=5)
    time.sleep(6)
    pl = display_pass_place()
    check(rot and pl is not None and pl[4] > pl[5], 'rotation back: landscape placement again', str(pl))
    tap_lime('landscape_again')
    # 3. 크게보기 + 화면 이동: a pan moves the picture, sends nothing
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):
        tap(rid='viewerMenuCloseButton', timeout=10)
        time.sleep(3)
        chip = tap(rid='viewerPanChip', timeout=10, reveal=reveal_controls)
        time.sleep(1.5)
        img0 = screencap(os.path.join(out, 'layoutb_pan0.png'))
        t0 = lime_target(img0)
        adb('logcat', '-c', check_rc=False)
        vb = view_bounds()
        cx, cy = (vb[0] + vb[2]) // 2, (vb[1] + vb[3]) // 2
        adb('shell', 'input', 'swipe', str(cx), str(cy), str(cx - 300), str(cy - 150), '600', check_rc=False)
        time.sleep(1.0)
        img1 = screencap(os.path.join(out, 'layoutb_pan1.png'))
        t1 = lime_target(img1)
        q = [e for e in seam_inputs() if e[4] == 1]
        check(chip and t0 and t1 and t1[0] < t0[0] - 100 and not q,
              'pan (크게보기 + 화면 이동): the picture moved with the finger, nothing was sent',
              'lime %s -> %s queued=%s' % (t0, t1, q[:2]))
        tap(rid='viewerPanChip', timeout=10, reveal=reveal_controls)
        time.sleep(1)
        tap_lime('large_after_pan')
        if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):
            tap(rid='viewerMenuCloseButton', timeout=10)
        time.sleep(3)
    else:
        check(False, '크게 보기 could be chosen')
    # 4. a display resize (this AVD only; restored in finally)
    size0 = adb('shell', 'wm', 'size', check_rc=False)
    try:
        g = re.search(r'Physical size: (\d+)x(\d+)', size0 or '')
        if check(g is not None, 'resize: the AVD\'s display size read', (size0 or '')[:80]):
            pw, ph = int(g.group(1)), int(g.group(2))
            adb('logcat', '-c', check_rc=False)
            before = display_pass_place()
            adb('shell', 'wm', 'size', '%dx%d' % (int(pw * 0.85), int(ph * 0.85)), check_rc=False)
            time.sleep(7)
            after = display_pass_place()
            check(after is not None and before is not None and (after[4], after[5]) != (before[4], before[5]),
                  'resize: B\'s placement follows the new view size', '%s -> %s' % (before, after))
            lines = log_lines()
            check(not any('sharpen_fallback' in l for l in lines), 'resize: no fallback')
            picture_where_placed('resized')
            tap_lime('resized')
    finally:
        adb('shell', 'wm', 'size', 'reset', check_rc=False)
        time.sleep(4)

    # ---- 5. the input fence: the GL side takes each placement 6 s late
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    ok = connect_to_pattern(fx.port, base_extras + ('--es', 'shTestFault', 'holdplace:6000'), level_before='strong', window=True)
    check(ok, 'fence: connected (B, placements taken 6 s late)')
    time.sleep(10)
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):
        tap(rid='viewerMenuCloseButton', timeout=10)
        vb = view_bounds()
        q, got = tap_at((vb[0] + vb[2]) // 2, (vb[1] + vb[3]) // 2, settle=1.0)
        held = log_lines('input_held_layout')
        check(not q and not got and held, 'fence: a tap while the new layout is not shown yet is NOT sent (held)',
              'queued=%s received=%s held=%s' % (q[:1], got[:1], held[-1:][0][-60:] if held else None))
        time.sleep(9)
        tap_lime('fence_after_shown')
    else:
        check(False, 'fence: 크게 보기 could be chosen')

    # ---- 6. fallback: B's swap fails -> direct; the picture still placed, a tap still right
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    ok = connect_to_pattern(fx.port, base_extras + ('--es', 'shTestFault', 'swap:40'), level_before='strong', window=True)
    check(ok, 'fallback: connected (B, the swap fails after 40 frames)')
    time.sleep(10)
    lines = log_lines()
    check(any('sharpen_fallback' in l for l in lines) and any('sharpen_path' in l and 'path=direct' in l for l in lines),
          'fallback: B failed -> the direct path', ([l for l in lines if 'sharpen_fallback' in l] or [''])[-1][-120:])
    m = cmp_metrics(screencap(os.path.join(out, 'layoutb_fallback.png')))
    check(m is not None and borders_ok(m), 'fallback: the picture is still placed (borders where they belong)', str(m and m.get('x0')))
    tap_lime('fallback')


def scenario_touch_ab(fx, out, input_log):''')

rep('''    input_log = os.path.join(os.path.abspath(args.out), 'pattern_input.txt') if args.scenario in ('touch_host', 'touch_ab') else None''',
    '''    input_log = os.path.join(os.path.abspath(args.out), 'pattern_input.txt') if args.scenario in ('touch_host', 'touch_ab', 'layout_b') else None''')
rep('''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui') else None,''',
    '''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b') else None,''')
rep('''        elif args.scenario == 'touch_ab':
            scenario_touch_ab(fx, args.out, input_log)''', '''        elif args.scenario == 'touch_ab':
            scenario_touch_ab(fx, args.out, input_log)
        elif args.scenario == 'layout_b':
            scenario_layout_b(fx, args.out, input_log)''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
