p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_avd.py'
s = open(p, encoding='utf-8').read()

anchor = "        # E8 -- the older host\n"
assert s.count(anchor) == 1
block = r'''        # ---------------------------------------------------------------- r2 (Codex codex_ka_e_src_r1.md)
        # E7g -- another device takes A while A is ON SCREEN: the session ends, no automatic re-take, back to standby
        mark('E7g')
        tap('card:' + H['a'])
        select('E7g A by activation (on screen)')
        n0 = cursor()
        rc = subprocess.run([os.path.join(REL, 'ka_relay_client_test.exe'), '--dir-url', 'http://127.0.0.1:18410',
                             '--id', 'ketest', '--pw', PW, '--host-name', H['a'], '--relay-ip', '10.0.2.2',
                             '--relay-port', '18443', '--sec', '6', '--out', os.path.join(OUT, 'takeover_a_active.txt')],
                            capture_output=True, timeout=120, env=dict(os.environ, REMOTE60_ALLOW_HOST_E2E='1'))
        step(f'  other device on A (active): exit={rc.returncode}')
        time.sleep(15)
        log = since(n0)
        if not ka_avd.wait('card:' + H['a'], 3):
            ka_avd.adb('shell', 'input', 'keyevent', '4', check=False)
            ka_avd.wait('card:' + H['a'], 10)
        st = wait_card(H['a'], '대기 중', 20)
        verdict('E7g another device took A while it was on screen: the session ended, NO automatic re-take, A stands by',
                re.search(r'\[keep_end\] handle=\d+ host=\S+ reason=peer_lost', log) is not None and
                '[standby_activate]' not in log and '[directory_connect]' not in log and st is not None and st[0] == '대기 중',
                str(st) + ' | ' + ' | '.join(re.findall(r'\[keep_end\][^\n]*', log))[:160])

        # E7b -- held input released by the fence before the session ends; the old session's tail shown as settling
        mark('E7b')
        tap('card:' + H['a'])
        select('E7b A by activation')
        def host_inputs():
            v = re.findall(r'inputEvents=(\d+)', hostlog('a'))
            return int(v[-1]) if v else -1
        held = False
        if not ka_avd.wait('viewerKeyboardButton', 2):
            ka_avd.adb('shell', 'input', 'tap', '1200', '540')
        if ka_avd.wait('viewerKeyboardButton', 4):
            tap('viewerKeyboardButton')
            if ka_avd.wait('viewerKeyboardTabPcKeys', 6):
                tap('viewerKeyboardTabPcKeys')
                if ka_avd.wait('text:Ctrl', 6):
                    i0 = host_inputs()
                    tap('text:Ctrl')
                    time.sleep(2.5)
                    held = host_inputs() > i0 >= 0
                    step(f'  host inputEvents {i0} -> {host_inputs()} (Ctrl down)')
        shot('E7b_ctrl_held')
        i1 = host_inputs()
        n0, _ = to_list('E7b')
        settling = None
        end = time.time() + 6
        while time.time() < end and settling is None:
            c = card(H['a'])
            if c and '정리 중' in c[1]:
                settling = c
            time.sleep(0.5)
        time.sleep(3)
        log = since(n0)
        i2 = host_inputs()
        verdict('E7b Ctrl held, then PC 목록: the fence ends the session (its releases sent first) -- the host got the key up',
                held and re.search(r'\[list_return_fence\] handle=\d+ begin', log) is not None and
                re.search(r'\[list_return_end\] handle=\d+ how=fenced', log) is not None and i2 > i1,
                f'host inputEvents {i1} -> {i2} | ' + ' | '.join(re.findall(r'\[list_return_(?:fence|end)\][^\n]*', log))[:200])
        verdict('E7b the old session\'s tail is shown as such ("이전 연결 정리 중일 수 있음"), then quiet, its datagrams counted',
                settling is not None and wait_card(H['a'], '대기 중', 20) is not None and
                re.search(r'\[standby_tail_quiet\] host=\S+ strays=[1-9]', since(n0)) is not None,
                str(settling) + ' | ' + ' | '.join(re.findall(r'\[standby_tail_quiet\][^\n]*', since(n0)))[:120])
        shot('E7b_list_settling_or_after')

        # E7c -- N1: A's tap held (its socket still coming back), then B: B is the newest intent, A never runs
        mark('E7c')
        tap('card:' + H['a'])
        select('E7c A by activation')
        n0, _ = to_list('E7c A')
        tap('card:' + H['a'])   # held: A's last session is still being fenced / ended
        time.sleep(0.3)
        tap('card:' + H['b'])   # the newest intent
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

        # E7e -- another tap DURING a first transition: B opens, C's new standby still comes up
        mark('E7e')
        tap('card:' + H['c'])
        select('E7e C (ordinary)')
        n0, _ = to_list('E7e C (first transition)')
        tap('card:' + H['b'])  # at once, while C's first lease is still being prepared
        select('E7e B by activation during C\'s first transition')
        log = since(n0)
        verdict('E7e a tap during C\'s first transition: B opens by activation, C\'s lease is not disturbed',
                '[standby_first_transition]' in log and
                re.search(r'\[standby_activate\] host=' + re.escape(H['b']), log) is not None)
        to_list('E7e B')
        st = wait_card(H['c'], '대기 중', 40)
        verdict('E7e ...and C\'s first transition completes: C stands by', st is not None and st[0] == '대기 중', str(st))

        # E7d -- N1: A's tap held, then A's 끊기: the held tap never runs, A is not made again
        mark('E7d')
        tap('card:' + H['a'])
        select('E7d A by activation')
        n0, _ = to_list('E7d A')
        tap('card:' + H['a'])  # held
        time.sleep(0.3)
        card_end(H['a'])       # its 끊기
        time.sleep(9)
        log = since(n0)
        st = card(H['a'])
        verdict('E7d N1: A\'s held tap, then A\'s 끊기 -- the held tap never runs, A is released and not made again',
                re.search(r'\[standby_activate\] host=' + re.escape(H['a']), log) is None and
                re.search(r'\[standby_release\] host=\S+ reason=user_disconnect', log) is not None and
                '[park_tap_resumed]' not in log and (st is None or st[0] != '대기 중'),
                str(st) + ' | ' + ' | '.join(re.findall(r'\[(?:tap_held_dropped|standby_tap_held|park_tap_held|standby_release)\][^\n]*', log))[:200])

        # E7f -- 끊기 DURING a first transition: its late capability goes nowhere, nothing made again
        mark('E7f')
        tap('card:' + H['old'])
        select('E7f old host (ordinary)')
        n0, _ = to_list('E7f old (first transition)')
        card_end(H['old'])  # at once, while its lease is being prepared
        time.sleep(10)
        log = since(n0)
        verdict('E7f 끊기 during a first transition: the lease released, nothing provided after it, not made again',
                re.search(r'\[standby_release\] host=\S+ reason=user_disconnect', log) is not None and
                log.count('[standby_start]') == 1 and
                re.search(r'\[standby_capability\] host=\S+ inc=\d+ ok=true', log.split('reason=user_disconnect', 1)[-1]) is None,
                ' | '.join(re.findall(r'\[standby_(?:start|release|capability)\][^\n]*', log))[:240])

'''
s = s.replace(anchor, block + anchor)
open(p, 'w', encoding='utf-8').write(s)
print('ok')
