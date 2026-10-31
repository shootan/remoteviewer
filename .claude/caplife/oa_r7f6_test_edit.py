p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_pick_test.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''         avd=None, sh='ok', drop=None, frozen=False, pw_text=None):''',
    '''         avd=None, sh='ok', drop=None, frozen=False, pw_text=None, arm='S'):''')
rep('''        f.write(CAPTURE + f'10-30 00:00:00.000 [native-video-host][control] window-list seq=1 count=1 selectedId={listed_id}\\n')''',
    '''        f.write(CAPTURE + f'10-30 00:00:00.000 [native-video-host][control] window-list seq=1 count=1 selectedId={listed_id}\\n'
                '10-30 00:00:00.000 [native-video-host] waiting client bindPort=50000\\n')''')
rep('''        f.write(f'ready run={run_id} hosts=a standby=none controlPort=0 relay=10.0.2.2\\n')''',
    '''        f.write(f'ready run={run_id} hosts=a standby=none controlPort={18460 if arm == "P" else 0} '
                f'relay={"127.0.0.1" if arm == "P" else "10.0.2.2"}\\n')''')
rep('''                       'OA_FAKE_FROZEN': '1' if frozen else '0', 'OA_FAKE_PW_TEXT': pw_text or 'Password'})
    sys.argv = ['oa_avd.py', root, 'S', os.path.join(root, 'none.apk'), os.path.join(root, 'avd')]''',
    '''                       'OA_FAKE_FROZEN': '1' if frozen else '0', 'OA_FAKE_PW_TEXT': pw_text or 'Password',
                       'OA_FAKE_PULL_FAIL': '0'})
    sys.argv = ['oa_avd.py', root, arm, os.path.join(root, 'none.apk'), os.path.join(root, 'avd')]''')
rep('''    def run_flow(tag, avd=None, **kw):
        m, p = case(tag, 'ok', input_mode=kw.pop('input_mode', 'none'), scene='login', avd=avd, **kw)''',
    '''    def run_flow(tag, avd=None, **kw):
        m, p = case(tag, 'ok', input_mode=kw.pop('input_mode', 'none'), scene=kw.pop('scene', 'login'), avd=avd, **kw)''')

new = r'''
    # r7f (6, Codex codex_old_apk_lane_src_r7d_r2.md N2): "no snapshot" is not "nothing changed"
    def not_same(tag, spoil, want):
        m, p = case(tag, 'ok')
        m.pick_pinned('3 the pinned window picked')
        marked = bool(m.PICK_MARK)
        spoil(p)
        n_taps = len(taps(p))
        ok4 = m.input_check()
        os.environ['OA_FAKE_PULL_FAIL'] = '0'
        r = last(m)
        return marked and not ok4 and r[1] is False and want in r[2] and len(taps(p)) == n_taps, r[2][:200]

    def pull_fails(p):
        os.environ['OA_FAKE_PULL_FAIL'] = '1'

    def host_shrinks(p):
        t = open(p['host'], encoding='utf-8').read()
        open(p['host'], 'w', encoding='utf-8').write(t[:len(t) // 2])

    def host_replaced(p):
        t = open(p['host'], encoding='utf-8').read()
        open(p['host'], 'w', encoding='utf-8').write('X' * len(t) + '10-30 00:00:02.000 [native-video-host] other run\n')

    def marker_lost(p):
        open(p['app'], 'w', encoding='utf-8').write('2026-10-30 00:00:09.000 [scene] VIEWER\n')

    for tag, spoil, want, what in (('same-pullfail', pull_fails, 'no app snapshot', 'the app log pull fails'),
                                   ('same-hostshrink', host_shrinks, 'shrank', 'the host log is shorter than at the pick'),
                                   ('same-hostreplaced', host_replaced, 'does not continue', 'the host log was replaced'),
                                   ('same-markerlost', marker_lost, 'marker is gone', 'the pick marker is gone from the app log')):
        ok, d = not_same(tag, spoil, want)
        check(f'NEGATIVE 4 r7f(6): {what} after the pick -> "cannot tell" -> FAIL with NO tap', ok, d)
    old = os.path.join(RUNS, f'oa_avd_9cec590-{os.getpid()}.py')
    with open(old, 'wb') as f:
        f.write(subprocess.run(['git', '-C', WT, 'show', '9cec590:automation/old_apk_lane/oa_avd.py'], capture_output=True,
                               check=True).stdout)
    m, p = case('same-pullfail-9cec590', 'ok', avd=old)
    m.pick_pinned('3 the pinned window picked')
    os.environ['OA_FAKE_PULL_FAIL'] = '1'
    same_old, why_old = m.same_viewer()
    os.environ['OA_FAKE_PULL_FAIL'] = '0'
    check('r7f(6) contrast: 9cec590\'s same_viewer with the app log pull failing said "the same" (True) -- the hole',
          same_old is True, why_old[:160])
    m, p = case('same-unchanged', 'ok')
    m.pick_pinned('3 the pinned window picked')
    same, why = m.same_viewer()
    check('control r7f(6): two good reads, nothing changed since the pick -> the same viewer (True)', same is True, why[:160])
    m, p = case('mark-pullfail', 'ok')
    os.environ['OA_FAKE_PULL_FAIL'] = '1'
    marked, why = m.set_pick_mark(PINNED)
    os.environ['OA_FAKE_PULL_FAIL'] = '0'
    check('NEGATIVE r7f(6): the marker is NOT registered from a failed app log read', not marked and not m.PICK_MARK, why[:120])
    m, p = case('mark-hostempty', 'ok')
    open(p['host'], 'w').close()
    marked, why = m.set_pick_mark(PINNED)
    check('NEGATIVE r7f(6): the marker is NOT registered from an empty host log', not marked and not m.PICK_MARK, why[:120])
    # arm P driven from its start (auto_login NO_CREDENTIAL, manual connect) to 4, which FAILs: nothing after it
    rc, four, nr, i4, after = run_flow('flow-p-4fail', scene='login', arm='P')
    res = run_flow.res['results']
    steps_p = {r[0].split()[0]: r[1] for r in res}
    check('r7f(6): arm P from its start through 2 (P) / 2k / 2s / 3 / 3s / 3w, 4 FAILs -> no tap after 4 (no menu, no '
          '연결 해제), 5 6 7 NOT RUN', rc != 0 and four and four[-1][1] is False and i4 >= 0 and not after and
          all(c in nr for c in '567') and steps_p.get('2k') is True and steps_p.get('3w') is True,
          f'rc={rc} steps={steps_p} taps after 4={after} not run={nr}')
'''
rep('''
    print(f'\\n{checks} checks, {fails} failed\\nRESULT: {"PASS" if fails == 0 else "FAIL"}')''',
    new + '''
    print(f'\\n{checks} checks, {fails} failed\\nRESULT: {"PASS" if fails == 0 else "FAIL"}')''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
