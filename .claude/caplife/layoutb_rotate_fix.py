p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''    # 2. rotation: the viewer's own button -> portrait
    adb('logcat', '-c', check_rc=False)
    rot = tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerRotateButton', timeout=15, reveal=scroll_menu_sheet)
    tap(rid='viewerMenuCloseButton', timeout=5)
    time.sleep(6)''', '''    def rotate_to(portrait):
        """The viewer's own rotate button, until the screen IS that way (a missed tap on the sheet is retried once)."""
        for _ in range(2):
            tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerRotateButton', timeout=15, reveal=scroll_menu_sheet)
            tap(rid='viewerMenuCloseButton', timeout=5)
            time.sleep(6)
            h, w = screencap(os.path.join(out, 'layoutb_rot_probe.png')).shape[:2]
            if (h > w) == portrait:
                return True
        return False

    # 2. rotation: the viewer's own button -> portrait
    adb('logcat', '-c', check_rc=False)
    rot = rotate_to(True)''')
rep('''    adb('logcat', '-c', check_rc=False)
    rot = tap(rid='viewerMenuButton', timeout=15, reveal=reveal_controls) and tap(rid='viewerRotateButton', timeout=15, reveal=scroll_menu_sheet)
    tap(rid='viewerMenuCloseButton', timeout=5)
    time.sleep(6)
    pl = display_pass_place()
    check(rot and pl is not None and pl[4] > pl[5], 'rotation back: landscape placement again', str(pl))''', '''    adb('logcat', '-c', check_rc=False)
    rot = rotate_to(False)
    pl = display_pass_place()
    check(rot and pl is not None and pl[4] > pl[5], 'rotation back: landscape placement again', str(pl))''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
