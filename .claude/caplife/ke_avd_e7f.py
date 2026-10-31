p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_avd.py'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep("""        mark('E7f')
        tap('card:' + H['old'])
        select('E7f old host (ordinary)')
        n0, _ = to_list('E7f old (first transition)')
        # its lease starts once the fence settled (its card then shows 끊기) -- tapped then, while it is being prepared
        end = time.time() + 8
        while time.time() < end and not ((card(H['old']) or ('', '', False))[2]):
            time.sleep(0.3)
        card_end(H['old'])
        time.sleep(10)""", """        mark('E7f')
        # ke_r16: the old host's lease lives ~4 s (unanswered), a UI dump takes ~3 s -- so no dump between the list
        # and the tap: the 끊기 position comes from a card that shows one now (same column, same offset to its name)
        pos = centers()
        ref = next(k for k in H if k + ':end' in pos)
        old_end = (pos[ref + ':end'][0], pos['old'][1] + pos[ref + ':end'][1] - pos[ref][1])
        tap('card:' + H['old'])
        select('E7f old host (ordinary)')
        n0 = cursor()
        menu('viewerMenuHostsButton')
        end = time.time() + 12
        while time.time() < end and '[standby_capability_begin] host=' not in since(n0):
            time.sleep(0.2)
        time.sleep(0.8)  # the list is portrait again; the lease is being prepared (ADMITTING) for ~3 s more
        ka_avd.adb('shell', 'input', 'tap', str(old_end[0]), str(old_end[1]))
        step(f'  fast tap old 끊기 at {old_end[0]},{old_end[1]}')
        time.sleep(10)""")
open(p, 'w', encoding='utf-8').write(s)
print('ok')
