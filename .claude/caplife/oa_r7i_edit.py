L = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b):
    assert s.count(a) == 1, (a[:100], s.count(a))
    return s.replace(a, b)


# ---- oa_avd.ime_state: on screen = IMMS mInputShown or the IME window's own visibility
p = L + r'\oa_avd.py'
s, crlf = load(p)
s = rep(s, '''def ime_state():
    """r7d: the soft keyboard, from `dumpsys input_method` -- ('shown' | 'hidden' | 'unknown', the flags read). Any of
    mInputShown / mWindowVisible / mDecorViewVisible / mIsInputViewShown true = shown; mInputShown false and none true =
    hidden; anything else = unknown (judged as not safe: no tap)."""
    out = adb('shell', 'dumpsys', 'input_method')
    flags = re.findall(r'(mInputShown|mWindowVisible|mDecorViewVisible|mIsInputViewShown|isInputViewShown)=(true|false)', out)
    trues = sorted({k for k, v in flags if v == 'true'})
    if trues:
        return 'shown', trues
    if any(k == 'mInputShown' for k, _ in flags):
        return 'hidden', []
    return 'unknown', []
''', '''IME_READS = []  # r7i: every keyboard read's flag lines (only those lines -- nothing else of the dump is kept)
SCREEN_FLAGS = ('mInputShown', 'mWindowVisible', 'mDecorViewVisible')


def ime_state():
    """The soft keyboard ON SCREEN, from `dumpsys input_method` -> ('shown' | 'hidden' | 'unknown', detail list).
    r7i (verifier oa-s-20261030072336: after BACK the screenshot showed no keyboard, the app logged ImeTracker onHidden
    for HIDE_SOFT_INPUT_BY_BACK_KEY, yet mIsInputViewShown stayed true and r7d called it shown): what is on screen is
    the system's mInputShown (InputMethodManagerService: the IME is being shown) and the IME window's own visibility
    (InputMethodService mWindowVisible / mDecorViewVisible -- both cleared by hideWindow()). mIsInputViewShown is the
    IME's internal "the input view would be shown" evaluation (updateInputViewShown / onEvaluateInputViewShown), NOT
    cleared when the window hides -- recorded, not judged.
      shown    any of mInputShown / mWindowVisible / mDecorViewVisible true
      hidden   all three read and all three false (mIsInputViewShown true alone is noted as IME-internal only)
      unknown  anything else -- a flag missing, nothing read: not safe, no tap"""
    out = adb('shell', 'dumpsys', 'input_method')
    lines = [l.strip() for l in out.splitlines()
             if re.search(r'(mInputShown|mWindowVisible|mDecorViewVisible|mIsInputViewShown|isInputViewShown)=', l)]
    IME_READS.append(f'{time.strftime("%H:%M:%S")} ' + ' | '.join(lines)[:600])
    try:
        open(os.path.join(OUT, 'ime_reads.txt'), 'w', encoding='utf-8').write('\\n'.join(IME_READS) + '\\n')
    except OSError:
        pass
    flags = re.findall(r'(mInputShown|mWindowVisible|mDecorViewVisible|mIsInputViewShown|isInputViewShown)=(true|false)', out)
    seen = {}
    for k, v in flags:
        seen.setdefault(k, set()).add(v)
    on_screen = sorted(k for k in SCREEN_FLAGS if 'true' in seen.get(k, set()))
    if on_screen:
        return 'shown', on_screen
    if all(seen.get(k) == {'false'} for k in SCREEN_FLAGS):
        internal = sorted(k for k in ('mIsInputViewShown', 'isInputViewShown') if 'true' in seen.get(k, set()))
        return 'hidden', ([f'{k}=true (IME-internal only; mInputShown / mWindowVisible / mDecorViewVisible false)'
                           for k in internal])
    return 'unknown', [f'missing / unclear: {[k for k in SCREEN_FLAGS if seen.get(k) != {"false"}]}']
''')
save(p, s, crlf)

# ---- the fake: the r7i pattern, and a partial read
p = L + r'\stubs\fake_ui_adb.py'
s, crlf = load(p)
s = rep(s, '''elif cmd.startswith('shell dumpsys input_method'):
    st = ime()
    if st == 'shown':
        print('  mInputShown=true mVisibleBound=true\\n  mWindowVisible=true mDecorViewVisible=true')
    elif st == 'hidden':
        print('  mInputShown=false mVisibleBound=false\\n  mWindowVisible=false mDecorViewVisible=false')
    else:
        print('  (no state lines this build prints)')''', '''elif cmd.startswith('shell dumpsys input_method'):
    st = ime()
    if st == 'shown':
        print('  mInputShown=true mVisibleBound=true\\n  mWindowVisible=true mDecorViewVisible=true')
    elif st == 'hidden':
        print('  mInputShown=false mVisibleBound=false\\n  mWindowVisible=false mDecorViewVisible=false')
    elif st == 'internal':  # r7i: the verifier's run after BACK -- off screen, the IME's own flag still true
        print('  mInputShown=false mVisibleBound=false\\n  mWindowVisible=false mDecorViewVisible=false\\n'
              '  mIsInputViewShown=true')
    elif st == 'windowonly':  # r7i: the system says hidden but the IME window is still visible
        print('  mInputShown=false\\n  mWindowVisible=true mDecorViewVisible=true')
    elif st == 'partial':  # r7i: only the system flag read -- the window's own state missing
        print('  mInputShown=false')
    else:
        print('  (no state lines this build prints)')''')
s = rep(s, '''    if ime() == 'shown' and os.environ.get('OA_FAKE_BACK', 'ime') == 'ime':
        state['ime'] = 'hidden\'''', '''    if ime() == 'shown' and os.environ.get('OA_FAKE_BACK', 'ime') in ('ime', 'internal'):
        state['ime'] = 'hidden' if os.environ.get('OA_FAKE_BACK', 'ime') == 'ime' else 'internal\'''')
save(p, s, crlf)

# ---- the tests
p = L + r'\oa_pick_test.py'
s, crlf = load(p)
anchor = '''
    print(f'\\n{checks} checks, {fails} failed\\nRESULT: {"PASS" if fails == 0 else "FAIL"}')'''
new = r'''
    # r7i (verifier oa-s-20261030072336 1b: after BACK only mIsInputViewShown was true, the keyboard gone on screen)
    for ime, want, what in (('internal', 'hidden', "the run's pattern: mInputShown / mWindowVisible / mDecorViewVisible false, "
                             'mIsInputViewShown true'), ('shown', 'shown', 'all on'),
                            ('windowonly', 'shown', 'mInputShown false but the IME window visible'),
                            ('partial', 'unknown', 'only mInputShown read (the window state missing)'),
                            ('unknown', 'unknown', 'no flags at all')):
        m, p = case(f'ime-{ime}', 'ok', ime=ime, scene='hosts')
        st, why = m.ime_state()
        check(f'r7i ime_state: {what} -> {want}', st == want and (ime != 'internal' or 'IME-internal only' in ' '.join(why)),
              f'{st} {why}')
    m, p = case('hosts-ime-hide-internal', 'ok', ime='shown', back='internal', scene='hosts')
    ok = m.hide_ime_on_hosts('1b the sign-in keyboard')
    rec = open(os.path.join(p['out'], 'ime_reads.txt'), encoding='utf-8').read()
    check('r7i 1b: BACK puts the keyboard away and only mIsInputViewShown stays true (the run) -> PASS, the flag noted; '
          'the flag lines kept (ime_reads.txt)', ok and last(m)[1] is True and 'IME-internal only' in last(m)[2] and
          'mIsInputViewShown=true' in rec and keys(p) == ['shell input keyevent 4'], last(m)[2][:200])
    old = os.path.join(RUNS, f'oa_avd_ff1c72a-{os.getpid()}.py')
    with open(old, 'wb') as f:
        f.write(subprocess.run(['git', '-C', WT, 'show', 'ff1c72a:automation/old_apk_lane/oa_avd.py'], capture_output=True,
                               check=True).stdout)
    m, p = case('hosts-ime-hide-internal-ff1c72a', 'ok', ime='shown', back='internal', scene='hosts', avd=old)
    ok = m.hide_ime_on_hosts('1b the sign-in keyboard')
    check('r7i contrast: ff1c72a on the same pattern -> 1b FAIL (the run\'s failure reproduced)', not ok and
          last(m)[1] is False, last(m)[2][:160])
    m, p = case('input-ime-partial', 'ok', ime='partial')
    m.pick_pinned('3 the pinned window picked')
    n_taps = len(taps(p))
    ok4 = m.input_check()
    check('NEGATIVE 4 r7i: the keyboard state only half read -> unknown -> FAIL with NO tap', not ok4 and
          len(taps(p)) == n_taps and 'unknown' in last(m)[2], last(m)[2][:160])
    m, p = case('input-ime-internal', 'ok', ime='internal')
    m.pick_pinned('3 the pinned window picked')
    ok4 = m.input_check()
    check('control 4 r7i: the run\'s internal-only pattern counts as no keyboard -> the tap goes, PASS', ok4, last(m)[2][:160])
'''
s = rep(s, anchor, new + anchor)
save(p, s, crlf)
print('ok')
