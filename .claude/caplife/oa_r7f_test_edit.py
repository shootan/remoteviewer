p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_pick_test.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''def case(tag, variant, listed_id=PINNED, input_mode='pair', ime='hidden', novideo=False, back='ime', scene=None,
         avd=None):''', '''def case(tag, variant, listed_id=PINNED, input_mode='pair', ime='hidden', novideo=False, back='ime', scene=None,
         avd=None, sh='ok', drop=None, frozen=False, pw_text=None):''')
rep('''                       'OA_FAKE_ADB_APK': apk})''', '''                       'OA_FAKE_ADB_APK': apk, 'OA_FAKE_SH': sh, 'OA_FAKE_DROP_SEC': drop or '',
                       'OA_FAKE_FROZEN': '1' if frozen else '0', 'OA_FAKE_PW_TEXT': pw_text or 'Password'})''')
rep('''    def run_flow(tag, avd=None):
        m, p = case(tag, 'ok', input_mode='none', scene='login', avd=avd)''', '''    def run_flow(tag, avd=None, **kw):
        m, p = case(tag, 'ok', input_mode=kw.pop('input_mode', 'none'), scene='login', avd=avd, **kw)''')
rep('''        nr = sorted({n for n, _ in res['not_run']})
        return rc, four, nr, i4, after''', '''        nr = sorted({n for n, _ in res['not_run']})
        run_flow.res = res
        run_flow.taps = all_taps
        return rc, four, nr, i4, after''')
rep('''    rc, four, nr, i4, after = run_flow('flow-4fail')
    check('r7e: arm S, 4 FAILs -> the arm ends there: no tap after 4 (no menu wake / menu), 5 6 7 NOT RUN',
          rc != 0 and four and four[-1][1] is False and i4 >= 0 and not after and all(c in nr for c in '567'),
          f'rc={rc} 4={[f[1] for f in four]} taps after 4={after} not run={nr}')''', '''    rc, four, nr, i4, after = run_flow('flow-4fail')
    check('r7e: arm S, 4 FAILs -> the arm ends there: no tap after 4 (no menu wake / menu), 5 6 7 NOT RUN',
          rc != 0 and four and four[-1][1] is False and i4 >= 0 and not after and all(c in nr for c in '567'),
          f'rc={rc} 4={[f[1] for f in four]} taps after 4={after} not run={nr}')
    steps_ok = {r[0].split()[0]: r[1] for r in run_flow.res['results']}
    check('r7f: the same flow went through 2s SH off (Settings), 3s SH in effect and 3w the 30 s watch (stub: 3 s) -- '
          'all PASS before 4', steps_ok.get('2s') is True and steps_ok.get('3s') is True and steps_ok.get('3w') is True,
          f'{steps_ok}')''')

new = r'''
    # r7f (Codex codex_old_apk_lane_src_r7d.md (3)): SH off through Settings, SH in effect, the 30 s watch, the
    # read-only record after a FAIL, arm P's keyboard check -- each with its refusal
    def sh_taps(paths):  # the fake's sharpening row spans y 1300-1400 (and the off-screen Off at y 2450-2550)
        return [t for t in taps(paths) if 1300 <= int(t[1]) <= 1400 or 2450 <= int(t[1]) <= 2550]

    m, p = case('sh-ok', 'ok')
    ok = m.sh_off('2s SH off')
    ui = os.path.join(p['out'], '2s_settings_ui.txt')
    check('control 2s r7f: Settings -> settingsSharpOffButton (on screen, enabled) -> sharpness_select level=off '
          'origin=settings, the button selected -> PASS; the settings screen kept as a UI dump',
          ok and last(m)[1] is True and sh_taps(p) == [['185', '1350']] and os.path.exists(ui) and
          'settingsSharpOffButton' in open(ui, encoding='utf-8').read(), last(m)[2][:200])
    m.pick_pinned('3 the pinned window picked')
    check('control 3s r7f: the picture: sharpen_path path=direct requested=off, no sharpen_start -> PASS',
          m.sh_in_effect('3s SH off') and last(m)[1] is True, last(m)[2][:200])
    for sh, what in (('norow', 'no sharpening row on the Settings screen'),
                     ('offscreen', "the Off button below the app's content box (not on screen)")):
        m, p = case(f'sh-{sh}', 'ok', sh=sh)
        try:
            m.sh_off('2s SH off')
            raised = ''
        except m.Invalid as e:
            raised = str(e)
        check(f'NEGATIVE 2s r7f: {what} -> the arm is NOT RUN (INVALID), the Off button never tapped',
              'NOT RUN' in raised and not sh_taps(p), f'sh taps={sh_taps(p)} {raised[:160]}')
    for sh, what in (('nolog', 'the tap logs no sharpness_select level=off'),
                     ('noselect', 'logged, but the requested level stays weak (the button not selected)')):
        m, p = case(f'sh-{sh}', 'ok', sh=sh)
        ok = m.sh_off('2s SH off')
        check(f'NEGATIVE 2s r7f: {what} -> FAIL', not ok and last(m)[1] is False, last(m)[2][:200])
    m, p = case('sh-fallback', 'ok')
    m.SH_MARK[:] = [m.cursor()]  # Settings never used: weak is still REQUESTED
    m.pick_pinned('3 the pinned window picked')
    ok = m.sh_in_effect('3s SH off')
    check('NEGATIVE 3s r7f: weak requested, effective=off by a fallback (path=direct requested=weak) -> FAIL '
          '(a fallback is not SH off)', not ok and 'requested=weak' in last(m)[2], last(m)[2][:200])
    m, p = case('sh-start', 'ok', sh='start')
    m.sh_off('2s SH off')
    m.pick_pinned('3 the pinned window picked')
    ok = m.sh_in_effect('3s SH off')
    check('NEGATIVE 3s r7f: the picture went through the sharpen path (sharpen_start) -> FAIL', not ok and
          'sharpen_start=1' in last(m)[2], last(m)[2][:200])

    m, p = case('watch-ok', 'ok')
    m.pick_pinned('3 the pinned window picked')
    n_taps, n_keys = len(taps(p)), len(keys(p))
    ok = m.observe_connection('3w the connection')
    check('control 3w r7f: the connection held for the whole watch, the picture went on, NO input during it -> PASS',
          ok and last(m)[1] is True and len(taps(p)) == n_taps and len(keys(p)) == n_keys, last(m)[2][:200])
    m, p = case('watch-drop', 'ok', drop='4.0')
    m.pick_pinned('3 the pinned window picked')
    n_taps = len(taps(p))
    os.environ['OA_OBSERVE_SEC'] = '10'
    t0 = time.time()
    ok = m.observe_connection('3w the connection')
    took = time.time() - t0
    os.environ['OA_OBSERVE_SEC'] = '3'
    rec = os.path.join(p['out'], 'fail1_ui.txt')
    rec_text = open(rec, encoding='utf-8').read() if os.path.exists(rec) else ''
    check('NEGATIVE 3w r7f: the connection breaks during the watch -> FAIL at once (not after the full watch), no input',
          not ok and last(m)[1] is False and took < 9 and "'HOSTS'" in last(m)[2] and len(taps(p)) == n_taps,
          f'{took:.1f} s {last(m)[2][:200]}')
    check('r7f: right after that FAIL a READ-ONLY record of the screen holds the error text (and the screenshot, logs)',
          '연결이 끊겼습니다' in rec_text and os.path.exists(os.path.join(p['out'], 'fail1_screen.png')) and
          os.path.exists(os.path.join(p['out'], 'fail1_app.log')), rec_text.strip()[:160])
    m, p = case('watch-frozen', 'ok', frozen=True)
    m.pick_pinned('3 the pinned window picked')
    ok = m.observe_connection('3w the connection')
    check('NEGATIVE 3w r7f: connected but the picture does not go on (video out unchanged) -> FAIL', not ok and
          last(m)[1] is False, last(m)[2][:160])
    m, p = case('watch-short', 'ok')
    m.pick_pinned('3 the pinned window picked')
    fake = os.environ.pop('OA_TEST_FAKE_ADB')
    try:
        m.observe_connection('3w the connection')
        raised = ''
    except m.Invalid as e:
        raised = str(e)
    finally:
        os.environ['OA_TEST_FAKE_ADB'] = fake
    check('NEGATIVE 3w r7f: a watch shorter than 30 s outside the stub test is refused (INVALID)', 'below 30' in raised,
          raised[:120])
    m, p = case('record-redact', 'ok', scene='login', pw_text='notarealpassword')
    m.ui_record('redact_ui')
    text = open(os.path.join(p['out'], 'redact_ui.txt'), encoding='utf-8').read()
    check('r7f: a record of the sign-in screen never writes the password field\'s text (the fixture password is absent)',
          'notarealpassword' not in text and '<not recorded: a password field>' in text, text.strip()[:160])
    m, p = case('p-kbd-shown', 'ok', ime='shown')
    ok = m.p_keyboard_clear('2k (P)')
    check('NEGATIVE 2k r7f (P): a keyboard up before the manual connect -> FAIL, nothing sent (no BACK, no tap)',
          not ok and not keys(p) and not taps(p), f'keys={keys(p)} taps={len(taps(p))}')
    m, p = case('p-kbd-hidden', 'ok', ime='hidden')
    check('control 2k r7f (P): no keyboard -> PASS', m.p_keyboard_clear('2k (P)') and not keys(p))
    src2 = open(os.path.join(E, 'oa_avd.py'), encoding='utf-8').read().replace('\r\n', '\n')
    check('r7f (source): both of arm P\'s connect taps come after its keyboard check',
          src2.count("    if not p_keyboard_clear('2k (P)'):  # r7f\n        return False\n    tap('connectButton')") == 1 and
          src2.count("    if not p_keyboard_clear('6k (P)'):  # r7f\n        return False\n    tap('connectButton')") == 1)
    os.environ['OA_OBSERVE_SEC'] = '12'
    rc, four, nr, i4, after = run_flow('flow-watchdrop', input_mode='pair', drop='5.0')
    os.environ['OA_OBSERVE_SEC'] = '3'
    res = run_flow.res['results']
    check('r7f: arm S driven from the install, the connection breaks in the 30 s watch -> the arm ends there: 3w FAIL, '
          'NO tap on the picture at all, 4 5 6 7 NOT RUN', rc != 0 and any(r[0].startswith('3w') and r[1] is False for r in res)
          and i4 < 0 and all(c in nr for c in '4567'),
          f'rc={rc} taps at the picture={i4 >= 0} not run={nr} 3w={[r[2][:120] for r in res if r[0].startswith("3w")]}')
    rc, four, nr, i4, after = run_flow('flow-shnorow', sh='norow')
    card = [t for t in run_flow.taps if 100 <= int(t[0]) <= 470 and 1000 <= int(t[1]) <= 1100]
    check('r7f: arm S driven from the install, the Settings control not usable -> INVALID (exit 2): nothing picked, '
          'nothing tapped after', rc == 2 and not card and i4 < 0, f'rc={rc} card taps={card}')

    print(f'\n{checks} checks, {fails} failed\nRESULT: {"PASS" if fails == 0 else "FAIL"}')'''
rep('''
    print(f'\\n{checks} checks, {fails} failed\\nRESULT: {"PASS" if fails == 0 else "FAIL"}')''', new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
