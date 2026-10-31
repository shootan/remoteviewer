p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''def pick_desktop():''', '''def scroll_menu_sheet_up():
    """r4: back to the sheet's top (크게 보기 ...) after it was scrolled down to its lower buttons."""
    adb('shell', 'input', 'swipe', '1900', '500', '1900', '950', '600', check_rc=False)
    time.sleep(0.8)


def pick_desktop():''')

# the red top border: one row (x1.33: one or two) at the placement's top
rep('''        # the pattern's top border row is red: the first rows of the picture
        top = img[int(t) + 1:int(t) + 4, int(l) + 20:int(r) - 20].reshape(-1, 3).mean(axis=0)
        check(inside.size and float(inside.std()) > 10 and top[0] > 60 and top[1] < 60,
              label + ': the picture is drawn where B placed it (the red top border at the placement\\'s top)',
              'rect=%s view=%s top_rgb=%s' % ((int(l), int(t), int(r), int(b)), vb, [round(float(v)) for v in top]))''',
    '''        # the pattern's top border is a red row: it must be at the placement's top (within a row)
        rows = [img[y, int(l) + 20:int(r) - 20].reshape(-1, 3).mean(axis=0) for y in range(max(0, int(t) - 1), min(h, int(t) + 3))]
        red = [y for y, v in zip(range(max(0, int(t) - 1), min(h, int(t) + 3)), rows) if v[0] > 45 and v[0] > v[1] + 25 and v[0] > v[2] + 15]
        check(inside.size and float(inside.std()) > 10 and red,
              label + ': the picture is drawn where B placed it (the red top border at the placement\\'s top)',
              'rect=%s view=%s rows=%s' % ((int(l), int(t), int(r), int(b)), vb, [[round(float(c)) for c in v] for v in rows]))''')

# 크게 보기 after the sheet was scrolled down for the rotate button
rep('''    # 3. 크게보기 + 화면 이동: a pan moves the picture, sends nothing
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):''',
    '''    # 3. 크게보기 + 화면 이동: a pan moves the picture, sends nothing
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10, reveal=scroll_menu_sheet_up):''')
rep('''        if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):
            tap(rid='viewerMenuCloseButton', timeout=10)
        time.sleep(3)
    else:
        check(False, '크게 보기 could be chosen')''', '''        if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10, reveal=scroll_menu_sheet_up):
            tap(rid='viewerMenuCloseButton', timeout=10)
        time.sleep(3)
    else:
        check(False, '크게 보기 could be chosen')
        tap(rid='viewerMenuCloseButton', timeout=5)  # never leave the sheet over the picture''')

# resize: the placement before is read before the log is cleared
rep('''            adb('logcat', '-c', check_rc=False)
            before = display_pass_place()
            adb('shell', 'wm', 'size',''', '''            before = display_pass_place()
            adb('logcat', '-c', check_rc=False)
            adb('shell', 'wm', 'size',''')

# the fence: the tap right after the choice (the sheet closes itself or quickly), a longer hold
rep('''holdplace:6000'), level_before='strong', window=True)
    check(ok, 'fence: connected (B, placements taken 6 s late)')''', '''holdplace:8000'), level_before='strong', window=True)
    check(ok, 'fence: connected (B, placements taken 8 s late)')''')
rep('''    time.sleep(10)
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):
        tap(rid='viewerMenuCloseButton', timeout=10)
        vb = view_bounds()''', '''    time.sleep(12)
    vb = view_bounds()
    adb('logcat', '-c', check_rc=False)
    if tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerMenuViewButton', timeout=10):
        tap(rid='viewerMenuCloseButton', timeout=2)''')
rep('''        time.sleep(9)
        tap_lime('fence_after_shown')''', '''        time.sleep(10)
        check(display_pass_place() is not None and display_pass_place()[2] > 1920 + 50,
              'fence: 크게 보기 was the layout held (its placement is the enlarged one)', str(display_pass_place()))
        tap_lime('fence_after_shown')''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
