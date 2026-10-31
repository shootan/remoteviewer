p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_avd.py'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


# a fault for one host (its fault file, read at the moment it applies)
rep("def dirlog():", '''def fault(k, text):
    """The pinned test host's faults (host_pinned_select_seam.hpp): e.g. pong_delay_ms=2000 holds the park fence's Pong."""
    open(os.path.join(ROOT, f'host_{k}', 'fault.txt'), 'w', encoding='ascii').write(text)
    step(f'  host {k} fault: {text or "(none)"}')


def dirlog():''')

# E7b: the host's own input log is the oracle; the fence paused the picture (no tail to show)
a = s[s.index("        verdict('E7b Ctrl held, then PC 목록"):s.index("        shot('E7b_list_settling_or_after')")]
b = r'''        hl = hostlog('a')
        down = hl.rfind('key=17 kind=5')
        up = hl.find('key=17 kind=6', down if down >= 0 else 0)
        ended = hl.find('udp control session ended', up if up >= 0 else 0)
        verdict('E7b Ctrl held, then PC 목록: the key up reached the host BEFORE that session ended (the host\'s own input '
                'log), the session ended through the fence',
                held_tapped and down >= 0 and up > down and (ended < 0 or ended > up) and
                re.search(r'\[list_return_fence\] handle=\d+ begin', log) is not None and
                re.search(r'\[list_return_end\] handle=\d+ how=fenced', log) is not None,
                ' | '.join(re.findall(r'\[list_return_(?:fence|end)\][^\n]*', log))[:200])
        st = wait_card(H['a'], '대기 중', 15)
        verdict('E7b the fence paused the old picture before the end: A stands by, its card claims no cleanup of the PC '
                '("대기 연결 있음 · 누르면 엽니다")', st is not None and st[0] == '대기 중' and '바로' not in st[1] and
                '준비됨' not in st[1], str(st) + ' settling seen=' + str(settling) + ' | ' +
                ' | '.join(re.findall(r'\[standby_(?:back|tail_quiet)\][^\n]*', since(n0)))[:160])
'''
s = s.replace(a, b)
rep("""        held = False
        if not ka_avd.wait('viewerKeyboardButton', 2):""", """        held_tapped = False
        if not ka_avd.wait('viewerKeyboardButton', 2):""")
rep("""                if ka_avd.wait('text:Ctrl', 6):
                    i0 = host_inputs()
                    tap('text:Ctrl')
                    time.sleep(2.5)
                    held = host_inputs() > i0 >= 0
                    step(f'  host inputEvents {i0} -> {host_inputs()} (Ctrl down)')""",
    """                if ka_avd.wait('text:Ctrl', 6):
                    tap('text:Ctrl')
                    held_tapped = True
                    time.sleep(1.5)""")
rep("""        shot('E7b_ctrl_held')
        i1 = host_inputs()
        n0, _ = to_list('E7b')""", """        shot('E7b_ctrl_held')
        n0, _ = to_list('E7b')""")
rep("""        time.sleep(3)
        log = since(n0)
        i2 = host_inputs()
""", """        time.sleep(3)
        log = since(n0)
""")

# E7c / E7d: the fence's Pong held 2 s on A -- its hand-back window, so the tap on A is held for sure
rep("""        select('E7c A by activation')
        n0, _ = to_list('E7c A')""", """        select('E7c A by activation')
        fault('a', 'pong_delay_ms=2000')  # A's fence takes 2 s: its socket is not back yet when A is tapped
        n0, _ = to_list('E7c A')""")
rep("""        select('E7c B (the newest tap)')
        time.sleep(8)""", """        select('E7c B (the newest tap)')
        fault('a', '')
        time.sleep(8)""")
rep("""        select('E7d A by activation')
        n0, _ = to_list('E7d A')""", """        select('E7d A by activation')
        fault('a', 'pong_delay_ms=2000')
        n0, _ = to_list('E7d A')""")
rep("""        card_end(H['a'])       # its 끊기
        time.sleep(9)""", """        card_end(H['a'])       # its 끊기
        fault('a', '')
        time.sleep(9)""")
open(p, 'w', encoding='utf-8').write(s)
print('ok')
