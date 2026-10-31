p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_avd.py'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep("""        mark('E10')
        n0 = cursor()
        ka_avd.adb('shell', 'input', 'keyevent', '3')""", """        mark('E10')
        # ke_r17: after E7c..E9 no lease is left (C went in E7f, out of the recent three) -- one made here first, so
        # leaving has something to release
        tap('card:' + H['c'])
        select('E10 C (ordinary)')
        to_list('E10 C')
        verdict('E10 C stands by before leaving', (wait_card(H['c'], '대기 중', 40) or ('',))[0] == '대기 중')
        n0 = cursor()
        ka_avd.adb('shell', 'input', 'keyevent', '3')""")
rep("""                re.search(r'\\[standby_release_all\\] reason=app_stopped leases=\\d+', log) is not None and""",
    """                re.search(r'\\[standby_release_all\\] reason=app_stopped leases=[1-9]', log) is not None and""")
open(p, 'w', encoding='utf-8').write(s)
print('ok')
