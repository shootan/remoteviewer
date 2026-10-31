p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_avd.py'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep("def dirlog():", '''def fast_from_targets(taps):
    """PC 목록 from the TARGETS screen (portrait, like the list: no rotation to wait out -- ke_r13/r14 lost taps sent
    while the viewer's landscape turned into the list's portrait), then at once the given taps on cached positions."""
    n0 = cursor()
    tap('listHostsButton')
    for label, (x, y) in taps:
        time.sleep(0.3)
        ka_avd.adb('shell', 'input', 'tap', str(x), str(y))
        step(f'  fast tap {label} at {x},{y}')
    return n0


def dirlog():''')

rep("""        tap('card:' + H['a'])
        select('E7c A by activation')
        fault('a', 'pong_delay_ms=2000')  # A's fence takes 2 s: its socket is not back yet when A is tapped
        n0 = fast_list_then([('A (to be held)', pos['a']), ('B (the newest)', pos['b'])])""",
    """        tap('card:' + H['a'])
        verdict('E7c A by activation: its targets', ka_avd.wait(TARGET, 30) is not None)
        fault('a', 'pong_delay_ms=2000')  # A's fence takes 2 s: its socket is not back yet when A is tapped
        n0 = fast_from_targets([('A (to be held)', pos['a']), ('B (the newest)', pos['b'])])""")
rep("""        tap('card:' + H['a'])
        select('E7d A by activation')
        fault('a', 'pong_delay_ms=2000')""", """        tap('card:' + H['a'])
        verdict('E7d A by activation: its targets', ka_avd.wait(TARGET, 30) is not None)
        fault('a', 'pong_delay_ms=2000')""")
rep("""        n0 = fast_list_then([('A (to be held)', pos['a']), ('A 끊기', pos.get('a:end', pos['a']))])""",
    """        n0 = fast_from_targets([('A (to be held)', pos['a']), ('A 끊기', pos.get('a:end', pos['a']))])""")
open(p, 'w', encoding='utf-8').write(s)
print('ok')
