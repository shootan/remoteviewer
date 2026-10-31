p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_pick_test.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
a = '''    old = os.path.join(RUNS, f'oa_avd_9cec590-{os.getpid()}.py')'''
assert s.count(a) == 1
s = s.replace(a, '''    # the 30 s watch (3w) uses the same check: a failed read during it is a FAIL, never "held"
    for tag, spoil, want, what in (('watch-pullfail', pull_fails, 'no app snapshot', 'the app log pull fails'),
                                   ('watch-hostshrink', host_shrinks, 'shrank', 'the host log shrinks')):
        m, p = case(tag, 'ok')
        m.pick_pinned('3 the pinned window picked')
        spoil(p)
        n_taps = len(taps(p))
        okw = m.observe_connection('3w the connection')
        os.environ['OA_FAKE_PULL_FAIL'] = '0'
        r = last(m)
        check(f'NEGATIVE 3w r7f(6): {what} during the watch -> FAIL ("cannot tell"), no input', not okw and r[1] is False
              and want in r[2] and len(taps(p)) == n_taps, r[2][:200])
''' + a)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
