p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_avd.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


# --- helpers, before menu()
rep('''def menu(button):''', '''def ime_state():
    """r7d: the soft keyboard, from `dumpsys input_method` -- ('shown' | 'hidden' | 'unknown', the flags read). Any of
    mInputShown / mWindowVisible / mDecorViewVisible / mIsInputViewShown true = shown; mInputShown false and none true =
    hidden; anything else = unknown (judged as not safe: no tap)."""
    out = adb('shell', 'dumpsys', 'input_method')
    flags = re.findall(r'(mInputShown|mWindowVisible|mDecorViewVisible|mIsInputViewShown|isInputViewShown)=(true|false)', out)
    trues = sorted({k for k, v in flags if v == 'true'})
    if trues:
        return 'shown', trues
    if any(k == 'mInputShown' for k, _ in flags):
        return 'hidden', []
    return 'unknown', []


def viewer_tap_point():
    """r7d: where a tap on the picture may land -- the centre of the video view (videoTextureView, b554e76
    activity_main.xml) as the UI dump shows it, and only with no soft keyboard up (the r7c run: the sign-in keyboard
    covered the lower 2/3 and the fixed 1200,540 went into it). -> ((x, y), detail) or (None, why): no tap then."""
    st, why = ime_state()
    if st != 'hidden':
        return None, f'soft keyboard {st} {why}'
    views = [n for n in ka.dump() if n['id'] == 'videoTextureView' and n['bounds'][2] > n['bounds'][0]
             and n['bounds'][3] > n['bounds'][1]]
    if len(views) != 1:
        return None, f'video view on screen: {len(views)}'
    x1, y1, x2, y2 = views[0]['bounds']
    return ((x1 + x2) // 2, (y1 + y2) // 2), f'video view {views[0]["bounds"]}, keyboard hidden'


def hide_ime_on_hosts(tag):
    """r7d: the sign-in leaves its keyboard up (the r7c run: onStartInput inputType=0x81, still up in the viewer). It is
    put away HERE, on the PC list, before anything is picked: BACK while it is shown goes to the keyboard. If BACK reached
    the app instead, on this scene it only opens the exit question (b554e76 MainActivity.kt handleSystemBack: HOSTS ->
    showExitConfirmDialog) -- seen and judged; in the viewer it would leave the viewer (handleViewerBack), so BACK is
    never sent there."""
    st, why = ime_state()
    step(f'  {tag}: soft keyboard {st} {why}')
    if st == 'hidden':
        return True
    if st == 'unknown':
        verdict(f'{tag}: the soft keyboard state is known (dumpsys input_method)', False)
        return False
    ka.adb('shell', 'input', 'keyevent', '4')
    time.sleep(1.0)
    st2, why2 = ime_state()
    listed = ka.wait('card:' + HOST, 5) is not None
    dialog = ka.wait('android:id/button1', 1) is not None
    ok = st2 == 'hidden' and listed and not dialog
    verdict(f'{tag}: the sign-in keyboard put away on the PC list (BACK while it was shown); the list still there, no '
            'dialog', ok, f'after: keyboard {st2} {why2} listed={listed} dialog={dialog}')
    return ok


def menu(button):''')

# --- menu: the wake tap only at a safe point
rep('''        for _ in range(3):
            if ka.wait('viewerMenuButton', 2):
                break
            ka.adb('shell', 'input', 'tap', '1200', '540')
            time.sleep(1.0)''', '''        for _ in range(3):
            if ka.wait('viewerMenuButton', 2):
                break
            pt, why = viewer_tap_point()  # r7d: the controls woken on the picture, never through a keyboard
            if pt is None:
                step(f'  menu: no wake tap ({why})')
                break
            ka.adb('shell', 'input', 'tap', str(pt[0]), str(pt[1]))
            time.sleep(1.0)''')

# --- 4: the tap at a safe point, else none
rep('''    hl = host_log()
    h0 = len(hl)
    ka.adb('shell', 'input', 'tap', '1200', '540')
    step('  one tap on the window picture at 1200,540')
    time.sleep(3)''', '''    hl = host_log()
    pt, why = viewer_tap_point()
    if pt is None:
        verdict('4 the tap on the window reaches the host as its own button down -> up, one position, inside the window',
                False, f'NO tap: {why}')
        return False
    h0 = len(host_log())
    ka.adb('shell', 'input', 'tap', str(pt[0]), str(pt[1]))
    step(f'  one tap on the window picture at {pt[0]},{pt[1]} ({why})')
    time.sleep(3)''')

# --- P-5d: the same for its wake tap
rep('''    if ka.wait('viewerMenuButton', 2) is None:
        ka.adb('shell', 'input', 'tap', '1200', '540')
        time.sleep(1.0)''', '''    if ka.wait('viewerMenuButton', 2) is None:
        pt, why = viewer_tap_point()  # r7d
        if pt is not None:
            ka.adb('shell', 'input', 'tap', str(pt[0]), str(pt[1]))
            time.sleep(1.0)
        else:
            step(f'  5d: no wake tap ({why})')''')

# --- S: put the sign-in keyboard away before the card is tapped
rep('''            if not listed:
                return 1
            n0 = cursor()
            hs0 = len(host_log())
            tap('card:' + HOST)''', '''            if not listed:
                return 1
            if not hide_ime_on_hosts('1b the sign-in keyboard'):  # r7d
                return 1
            n0 = cursor()
            hs0 = len(host_log())
            tap('card:' + HOST)''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
