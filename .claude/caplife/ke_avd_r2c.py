p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_avd.py'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep("def dirlog():", '''def centers():
    """Where each card (and its 끊기) is on the list right now -- for taps that must land within a second of a
    screen change, faster than a UI dump (~3 s each)."""
    nodes = ka_avd.dump()
    out = {}
    for k, name in H.items():
        nm = next((n for n in nodes if n['id'] == 'hostCardName' and n['text'] == name), None)
        if not nm:
            continue
        x1, y1, x2, y2 = nm['bounds']
        out[k] = ((x1 + x2) // 2, (y1 + y2) // 2)
        end = next((n for n in nodes if n['id'] == 'hostCardDisconnect' and n['bounds'][1] <= y2 + 60 and
                    n['bounds'][3] >= y1 - 60), None)
        if end:
            ex1, ey1, ex2, ey2 = end['bounds']
            out[k + ':end'] = ((ex1 + ex2) // 2, (ey1 + ey2) // 2)
    return out


def fast_list_then(taps):
    """PC 목록 from the viewer, then -- with no dump in between -- the given taps on cached list positions."""
    if not ka_avd.wait('viewerMenuHostsButton', 2):
        if not ka_avd.wait('viewerMenuButton', 2):
            ka_avd.adb('shell', 'input', 'tap', '1200', '540')
            ka_avd.wait('viewerMenuButton', 4)
        tap('viewerMenuButton')
        ka_avd.wait('viewerMenuHostsButton', 8)
    n0 = cursor()
    tap('viewerMenuHostsButton')
    for label, (x, y) in taps:
        ka_avd.adb('shell', 'input', 'tap', str(x), str(y))
        step(f'  fast tap {label} at {x},{y}')
        time.sleep(0.3)
    return n0


def dirlog():''')

# E7c: cached positions, the held A tap and the B tap right after PC 목록
a = s[s.index("        # E7c -- N1:"):s.index("        # E7e --")]
b = r'''        # E7c -- N1: A's tap held (its socket still coming back), then B: B is the newest intent, A never runs
        mark('E7c')
        pos = centers()
        tap('card:' + H['a'])
        select('E7c A by activation')
        fault('a', 'pong_delay_ms=2000')  # A's fence takes 2 s: its socket is not back yet when A is tapped
        n0 = fast_list_then([('A (to be held)', pos['a']), ('B (the newest)', pos['b'])])
        fault('a', '')
        select('E7c B (the newest tap)')
        time.sleep(8)
        log = since(n0)
        verdict('E7c N1: A\'s held tap is superseded by the newer tap on B -- A never activates afterwards',
                ('[standby_tap_held]' in log or '[park_tap_held]' in log) and
                re.search(r'\[standby_activate\] host=' + re.escape(H['b']), log) is not None and
                re.search(r'\[standby_activate\] host=' + re.escape(H['a']), log) is None and
                '[park_tap_resumed]' not in log,
                ' | '.join(re.findall(r'\[(?:standby_tap_held|park_tap_held|tap_held_superseded|park_tap_resumed)\][^\n]*', log))[:200])
        to_list('E7c B')
        wait_card(H['b'], '대기 중', 30)

'''
s = s.replace(a, b)

a = s[s.index("        # E7d -- N1:"):s.index("        # E7f --")]
b = r'''        # E7d -- N1: A's tap held, then A's 끊기: the held tap never runs, A is not made again
        mark('E7d')
        pos = centers()
        tap('card:' + H['a'])
        select('E7d A by activation')
        fault('a', 'pong_delay_ms=2000')
        # A's card shows its 끊기 while its lease exists: its position from before (the list does not move)
        n0 = fast_list_then([('A (to be held)', pos['a']), ('A 끊기', pos.get('a:end', pos['a']))])
        fault('a', '')
        time.sleep(9)
        log = since(n0)
        st = card(H['a'])
        verdict('E7d N1: A\'s held tap, then A\'s 끊기 -- the held tap never runs, A is released and not made again',
                ('[standby_tap_held]' in log or '[park_tap_held]' in log) and
                re.search(r'\[standby_activate\] host=' + re.escape(H['a']), log) is None and
                re.search(r'\[standby_release\] host=\S+ reason=user_disconnect', log) is not None and
                '[park_tap_resumed]' not in log and (st is None or st[0] != '대기 중'),
                str(st) + ' | ' + ' | '.join(re.findall(r'\[(?:tap_held_dropped|standby_tap_held|park_tap_held|standby_release)\][^\n]*', log))[:200])

'''
s = s.replace(a, b)
open(p, 'w', encoding='utf-8').write(s)
print('ok')
