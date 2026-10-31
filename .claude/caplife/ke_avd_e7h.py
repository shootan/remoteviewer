p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_avd.py'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep("def dirlog():", '''def list_positions():
    """One dump of the list: each card's centre (and its 끊기), 모두 끊기 and 로그아웃 -- for taps within a fence (<= 2.5 s),
    faster than a dump. Cards move with the 모두 끊기 banner: taken in the state the fence will be in."""
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
    for rid in ('hostsParkDisconnectButton', 'hostsLogoutButton'):
        n = next((n for n in nodes if n['id'] == rid and n['enabled']), None)
        if n:
            x1, y1, x2, y2 = n['bounds']
            out[rid] = ((x1 + x2) // 2, (y1 + y2) // 2)
    return out


def end_position(pos, k):
    """Where a card's 끊기 is when it shows: its own, or the same offset from its name as another card's."""
    if k + ':end' in pos:
        return pos[k + ':end']
    ref = next(r for r in H if r + ':end' in pos)
    return pos[ref + ':end'][0], pos[k][1] + pos[ref + ':end'][1] - pos[ref][1]


def dirlog():''')

rep("""        # E10 -- leaving the app
        mark('E10')""", """        # E7h (r3 R1, Codex codex_ka_e_src_r2.md): an ordinary C's "PC 목록" is still fencing (its host holds the Pong
        # 2 s) when C's 끊기 / 모두 끊기 / leaving the app / sign-out comes -- the list return is dropped with it and no
        # lease for C is made afterwards (not by its end, not later)
        def fence_case(tag, action, label, needs_banner=False):
            if needs_banner:
                # 모두 끊기 shows while a lease exists: B stands by first (ordinary, then PC 목록 from its targets)
                if (card(H['b']) or ('',))[0] != '대기 중':
                    tap('card:' + H['b'])
                    ka_avd.wait(TARGET, 30)
                    tap('listHostsButton')
                    wait_card(H['b'], '대기 중', 40)
            pos = list_positions()
            if (card(H['c']) or ('', '', False))[2]:
                card_end(H['c'])  # C must connect the ordinary way (no lease of it)
                time.sleep(2)
                pos = list_positions()
            tap('card:' + H['c'])
            verdict(f'E7h {tag}: C (ordinary) -- its targets', ka_avd.wait(TARGET, 30) is not None)
            fault('c', 'pong_delay_ms=2000')
            n0 = cursor()
            tap('listHostsButton')
            time.sleep(0.4)
            if action == 'home':
                ka_avd.adb('shell', 'input', 'keyevent', '3')
                step('  fast HOME')
            else:
                xy = end_position(pos, 'c') if action == 'c:end' else pos[action]
                ka_avd.adb('shell', 'input', 'tap', str(xy[0]), str(xy[1]))
                step(f'  fast tap {label} at {xy[0]},{xy[1]}')
            fault('c', '')
            time.sleep(8)
            if action == 'home':
                ka_avd.adb('shell', 'am', 'start', '-n', f'{PKG}/.MainActivity')
                time.sleep(4)
            if action == 'hostsLogoutButton':
                login()
                time.sleep(3)
            log = since(n0)
            verdict(f'E7h {tag}: {label} during C\\'s fence -- the list return dropped, no lease for C made afterwards',
                    '[list_return_fence] handle=' in log and ' begin' in log and '[list_return_dropped]' in log and
                    '[standby_first_transition]' not in log and
                    re.search(r'\\[standby_start\\] host=\\S+ inc=\\d+ reason=first_transition', log) is None and
                    '[list_return_end]' not in log,
                    ' | '.join(re.findall(r'\\[(?:list_return_fence|list_return_dropped|list_return_end|standby_start|'
                                          r'standby_start_skipped|standby_first_transition|keep_end_all)\\][^\\n]*', log))[:400])

        mark('E7h')
        fence_case('a', 'c:end', 'C 끊기')
        fence_case('b', 'hostsParkDisconnectButton', '모두 끊기', needs_banner=True)
        fence_case('c', 'home', 'leaving the app (HOME)')
        fence_case('d', 'hostsLogoutButton', '로그아웃')

        # E10 -- leaving the app
        mark('E10')""")
open(p, 'w', encoding='utf-8').write(s)
print('ok')
