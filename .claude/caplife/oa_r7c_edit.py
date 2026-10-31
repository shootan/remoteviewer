p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_avd.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)


start = s.index('def judge_input(host_since, size):')
end = s.index("''' + s", start) if False else s.index('def targets_ui(name):')
s = s[:start] + '''def judge_input(host_since, size, aim=None):
    """r7 (4): the tap's own events, from the host's [input-trace] lines written AFTER it (the caller passes only the
    log past the offset taken just before the tap). The PinnedSelectTest seam logs every pointer event (injection none;
    host_control_session.cpp:1296, kinds: 1 move, 2 button down, 3 button up, 4 wheel). PASS only if the FIRST button
    down after the tap is followed by a button up (a higher seq) at the SAME position (within 2 px), both inside the
    pinned window's picture (0 <= x < W, 0 <= y < H; W x H = the host's [keyframe] size). Moves alone, an up without
    its down, a down without an up, a pair outside the window or at two positions: FAIL.
    LIMIT: 0.2.32's DIRECT tap sends the view point mapped into the picture (b554e76 MainActivity.kt:3093-3110,
    resolveViewerContentRect); that mapping depends on the device's view layout and is NOT judged here. `aim` (the
    expected picture point for a tap at the view's centre) is only recorded: the distance is reported, not judged."""
    ev = [(int(q), int(k), int(x), int(y)) for q, k, x, y in
          re.findall(r'\\[input-trace\\] seq=(\\d+) kind=(\\d+) x=(-?\\d+) y=(-?\\d+)', host_since)]
    inside = (lambda x, y: size is not None and 0 <= x < size[0] and 0 <= y < size[1])
    downs = [e for e in ev if e[1] == 2]
    down = downs[0] if downs else None
    up = None
    if down:
        ups = [e for e in ev if e[1] == 3 and e[0] > down[0]]
        up = ups[0] if ups else None
    same = bool(down and up and abs(down[2] - up[2]) <= 2 and abs(down[3] - up[3]) <= 2)
    ok = bool(down and up and same and inside(down[2], down[3]) and inside(up[2], up[3]))
    where = ''
    if down and aim:
        where = f' aim={aim} off={abs(down[2] - aim[0])},{abs(down[3] - aim[1])} (recorded, not judged)'
    detail = (f'window={size} events after the tap={len(ev)} kinds={[e[1] for e in ev][:8]} '
              + (f'down seq={down[0]} at {down[2]},{down[3]}' if down else 'no down')
              + (f' -> up seq={up[0]} at {up[2]},{up[3]}' if up else ' -> no up')
              + ('' if not (down and up) else (' same position' if same else ' MOVED')) + where)
    return ok, detail


''' + s[end:]

rep('''    ok, detail = judge_input(host_log()[h0:], window_size(hl))''',
    '''    size = window_size(hl)
    ok, detail = judge_input(host_log()[h0:], size, (size[0] // 2, size[1] // 2) if size else None)''')
rep('''    verdict('4 the tap on the window reaches the host as its own button down -> up inside the window '
            '([input-trace]; not injected: input none)', ok, detail[:300])''',
    '''    verdict('4 the tap on the window reaches the host as its own button down -> up, one position, inside the window '
            '([input-trace] after the tap; not injected: input none)', ok, detail[:340])''')

# optional: the Windows tab itself judged visible and enabled
rep('''    if not ka.wait('listWindowsButton', 30):
        verdict(f'{tag}: the targets screen is shown', False)
        return None
    pinned, title, why = None, None, \'\'''',
    '''    tab = ka.wait('listWindowsButton', 30)
    if not tab:
        verdict(f'{tag}: the targets screen is shown (listWindowsButton visible)', False)
        return None
    tab_note = f"windowsTab shown={tab['bounds']} enabled={int(tab['enabled'])}"
    if not tab['enabled']:
        verdict(f'{tag}: the Windows tab (listWindowsButton) is visible and enabled', False, tab_note)
        return None
    pinned, title, why = None, None, \'\'''')
rep('''    verdict(f'{tag}: the pinned window picked (id {pinned}, Windows tab) -> first frame', True,
            f'{detail[:300]}''',
    '''    verdict(f'{tag}: the pinned window picked (id {pinned}, Windows tab) -> first frame', True,
            f'{tab_note} {detail[:300]}''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
