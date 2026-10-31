"""old-APK lane r7 (verify-0164 S/P runs: 3 tapped the Desktop card, id 0, which the PinnedSelectTest host refuses by
design; Codex codex_old_apk_lane_pinned_scope_r7.md): oa_avd.py's pick_pinned / judge_pick and input_check /
judge_input against a FAKE adb that plays 0.2.32's targets screen and the two logs (stubs/fake_ui_adb.cmd: no device,
no adb server, no emulator). The pinned window's card and the tap's own down->up PASS; every wrong one FAILs:
  3  the r6 path itself (the Desktop card, id 0 -> refused), judged by judge_pick; a Windows card sent as id 0
     (wrongid); id 0 applied with a frame after it (zeroready); the app's ack naming another streamGen (genmismatch);
     no picture after select_ready (novideo); no pinned card listed (nopinned: no card tap); the host listing id 0, or
     its capture target and window-list disagreeing (NO tap at all)
  4  no events, an up alone, a down/up outside the window, a pair logged before the tap, moves only, a down without
     an up, a down and an up at two positions (the tap is the offset: only the host's lines after it count)
  the Windows tab: its node must be visible AND enabled (recorded on the 3 line)
Each case runs in its own new folder under <worktree>/.claude/oa_runs (nothing outside it is written).
  python -I oa_pick_test.py"""
import importlib.util
import json
import os
import subprocess
import sys
import time

E = os.path.dirname(os.path.abspath(__file__))
WT = os.path.normpath(os.path.join(E, '..', '..'))
RUNS = os.path.join(WT, '.claude', 'oa_runs')
PINNED = 80809746
CAPTURE = ('10-30 00:00:00.000 [native-video-host] capture-window target hwnd=0x4d10f12 pid=43948 '
           'process=remote60_mobile_abr_session_e2e_test.exe title=remote60 mobile abr target pid 43948 92abc8fe '
           'clientOnly=0\n'
           '10-30 00:00:00.000 [native-video-host][keyframe] seq=1 bytes=1350 reasons=initial size=480x270 bitrate=1500000\n')
checks = fails = 0


def check(what, ok, detail=''):
    global checks, fails
    checks += 1
    fails += 0 if ok else 1
    print(f'{"PASS" if ok else "FAIL"}  {what}' + (f'  {detail}' if detail else ''), flush=True)


def case(tag, variant, listed_id=PINNED, input_mode='pair', ime='hidden', novideo=False, back='ime', scene=None,
         avd=None, sh='ok', drop=None, frozen=False, pw_text=None, arm='S'):
    """A fresh run folder, the fake's state, and oa_avd loaded anew against it. -> (module, paths)"""
    run_id = f'{tag}-{time.strftime("%Y%m%d%H%M%S")}-{os.getpid()}'
    root = os.path.join(RUNS, run_id)
    os.makedirs(os.path.join(root, 'a'))  # new: refused if it exists
    with open(os.path.join(root, 'fixture_account.txt'), 'w', encoding='ascii') as f:
        f.write('oatest notarealpassword\n')
    host_log = os.path.join(root, 'a', 'host.log')
    with open(host_log, 'w', encoding='utf-8') as f:
        f.write(CAPTURE + f'10-30 00:00:00.000 [native-video-host][control] window-list seq=1 count=1 selectedId={listed_id}\n'
                '10-30 00:00:00.000 [native-video-host] waiting client bindPort=50000\n')
    with open(os.path.join(root, 'a', 'directoryhost_ready.txt'), 'w', encoding='ascii') as f:
        f.write(f'ready run={run_id}-a harness_pid=1 host_pid=2 host_log={host_log}\n')
    # r7e: what oa_avd.main reads before it starts (a fixture, an APK the fake hands back as the installed one)
    with open(os.path.join(root, 'fixture_ready.txt'), 'w', encoding='ascii') as f:
        f.write(f'ready run={run_id} hosts=a standby=none controlPort={18460 if arm == "P" else 0} '
                f'relay={"127.0.0.1" if arm == "P" else "10.0.2.2"}\n')
    apk = os.path.join(root, 'none.apk')
    with open(apk, 'wb') as f:
        f.write(b'PK fake apk for ' + run_id.encode())
    applog = os.path.join(root, 'app.log')
    with open(applog, 'w', encoding='utf-8') as f:
        f.write('2026-10-30 00:00:00.000 [targets_ready] origin=connect ms=1 items=1\n')
    st = os.path.join(root, 'ui_state.json')
    json.dump({'tab': 'desktop', **({'scene': scene} if scene else {})}, open(st, 'w', encoding='utf-8'))
    calls = os.path.join(root, 'adb_calls.txt')
    open(calls, 'w').close()
    os.environ.update({'OA_FAKE_ADB_LOG': calls, 'OA_FAKE_UI_STATE': st, 'OA_FAKE_APPLOG': applog,
                       'OA_FAKE_HOSTLOG': host_log, 'OA_FAKE_UI_VARIANT': variant, 'OA_FAKE_INPUT': input_mode,
                       'OA_FAKE_IME': ime, 'OA_FAKE_NOVIDEO': '1' if novideo else '0', 'OA_FAKE_BACK': back,
                       'OA_FAKE_ADB_APK': apk, 'OA_FAKE_SH': sh, 'OA_FAKE_DROP_SEC': drop or '',
                       'OA_FAKE_FROZEN': '1' if frozen else '0', 'OA_FAKE_PW_TEXT': pw_text or 'Password',
                       'OA_FAKE_PULL_FAIL': '0'})
    sys.argv = ['oa_avd.py', root, arm, os.path.join(root, 'none.apk'), os.path.join(root, 'avd')]
    spec = importlib.util.spec_from_file_location(f'oa_avd_{tag.replace("-", "_")}', avd or os.path.join(E, 'oa_avd.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m, {'calls': calls, 'host': host_log, 'app': applog, 'out': os.path.join(root, 'avd'), 'root': root}


def taps(paths):
    return [l.split()[3:5] for l in open(paths['calls'], encoding='utf-8') if l.startswith('shell input tap')]


def card_taps(paths):
    """The `input tap`s that landed on the card (the fake's card spans x 100-470, y 1000-1100)."""
    return [t for t in taps(paths) if 100 <= int(t[0]) <= 470 and 1000 <= int(t[1]) <= 1100]


def last(m):
    return m.results[-1] if m.results else ('', None, '')


def main():
    os.makedirs(RUNS, exist_ok=True)
    print('oa_pick_test (old-APK lane r7): pick_pinned / judge_pick / input_check against a fake 0.2.32')
    os.environ.pop('KE_ADB_SHA256', None)
    os.environ.update({'OA_TEST_FAKE_ADB': os.path.join(E, 'stubs', 'fake_ui_adb.cmd'), 'KA_SERIAL': 'emulator-5554',
                       'OA_OBSERVE_SEC': '3'})  # r7f: the 30 s watch, 3 s against the fake

    # the control: the pinned card -> 3 PASS, then the tap's own down->up -> 4 PASS (the gates are not dead ends)
    m, p = case('pick-ok', 'ok')
    digest = m.pick_pinned('3 the pinned window picked')
    r = last(m)
    check('control 3: the Windows tab, the pinned card -> PASS (id, gen, streamGen, ready, picture after it)',
          digest is not None and r[1] is True and len(card_taps(p)) == 1, r[2][:240])
    check('the screenshot is named for the window', os.path.exists(os.path.join(p['out'], '3_window_first_frame.png')))
    ui = os.path.join(p['out'], '3_targets_ui.txt')
    check('the targets screen is kept as a UI dump (ids, text, bounds) for the record',
          os.path.exists(ui) and 'listWindowsButton' in open(ui, encoding='utf-8').read(), ui)
    ok4 = m.input_check()
    check('control 4: the tap -> its own button down -> up inside the window -> PASS', ok4 and last(m)[1] is True,
          last(m)[2][:200])

    # NEGATIVE 3: the r6 path -- the Desktop tab, its first card (id 0); the same judge FAILs it
    m, p = case('pick-r6', 'ok')
    h0, n0 = len(open(p['host'], encoding='utf-8').read()), m.cursor()
    m.tap('listDevicesButton')
    m.tap('targetCardTitle')
    ok, detail = m.judge_pick(m.since(n0), open(p['host'], encoding='utf-8').read()[h0:], PINNED)
    check('NEGATIVE r6 path: the Desktop card (id 0) tapped -> judge_pick FAILs it', not ok and 'targetId=0' in detail
          and 'DESKTOP' in detail, detail[:220])

    for variant, what in (('wrongid', 'a Windows card whose request goes out as id 0 (the host refuses)'),
                          ('zeroready', 'a request for id 0 that the host applies, with a frame after it'),
                          ('genmismatch', "the app's select_ack names another streamGen than the host applied"),
                          ('novideo', 'no picture (video_debug out > 0) after select_ready')):
        m, p = case(f'pick-{variant}', variant)
        digest = m.pick_pinned('3 the pinned window picked')
        r = last(m)
        check(f'NEGATIVE {variant}: {what} -> FAIL', digest is None and r[1] is False, r[2][:220])

    m, p = case('pick-nopinned', 'nopinned')
    digest = m.pick_pinned('3 the pinned window picked')
    r = last(m)
    check('NEGATIVE nopinned: no pinned card on the Windows tab -> FAIL, no card tapped',
          digest is None and r[1] is False and not card_taps(p), f'card taps={card_taps(p)} {r[2][:160]}')

    for tag, listed, what in (('pick-disagree', PINNED + 1, 'capture hwnd != window-list selectedId'),
                              ('pick-listzero', 0, 'the host lists id 0')):
        m, p = case(tag, 'ok', listed_id=listed)
        digest = m.pick_pinned('3 the pinned window picked')
        r = last(m)
        check(f'NEGATIVE {tag[5:]}: {what} -> FAIL with NO tap at all', digest is None and r[1] is False and not taps(p),
              f'taps={len(taps(p))} {r[2][:160]}')

    for mode, what in (('none', 'no events'), ('uponly', 'an up without its down'),
                       ('outside', 'a down/up outside the window'),
                       ('pretap', 'a down/up logged BEFORE the tap (at the pick), nothing after it'),
                       ('moveonly', 'moves only'), ('downonly', 'a down without an up'),
                       ('moved', 'a down and an up at two positions')):
        m, p = case(f'input-{mode}', 'ok', input_mode=mode)
        m.pick_pinned('3 the pinned window picked')
        ok4 = m.input_check()
        r = last(m)
        # pretap is a contrast only if the host log really holds a down/up from before the tap
        before = mode != 'pretap' or ('[input-trace] seq=1 kind=2' in open(p['host'], encoding='utf-8').read())
        check(f'NEGATIVE 4 {mode}: {what} -> FAIL', not ok4 and r[1] is False and r[0].startswith('4 ') and before,
              r[2][:200] + ('' if mode != 'pretap' else f' | pair logged before the tap: {before}'))

    # r7d (verifier oa-s-20261030025800: the sign-in keyboard stayed up over the viewer; 4's fixed 1200,540 went into it)
    def keys(paths):
        return [l.strip() for l in open(paths['calls'], encoding='utf-8') if l.startswith('shell input keyevent')]

    for ime, novideo, what in (('shown', False, 'a soft keyboard is up'), ('unknown', False, 'the keyboard state cannot be read'),
                               ('hidden', True, 'no video view on screen')):
        m, p = case(f'input-ime-{ime}{"-novideo" if novideo else ""}', 'ok', ime=ime, novideo=novideo)
        m.pick_pinned('3 the pinned window picked')
        n_taps = len(taps(p))
        ok4 = m.input_check()
        r = last(m)
        check(f'NEGATIVE 4 r7d: {what} -> FAIL with NO tap on the picture', not ok4 and r[1] is False and
              'NO tap' in r[2] and len(taps(p)) == n_taps, f'taps after the pick={len(taps(p)) - n_taps} {r[2][:160]}')
    m, p = case('input-video-centre', 'ok')
    m.pick_pinned('3 the pinned window picked')
    n_taps = len(taps(p))
    ok4 = m.input_check()
    t4 = taps(p)[n_taps:]
    check('control 4 r7d: keyboard hidden, the ONE tap lands at the video view centre (from the UI dump) -> PASS',
          ok4 and t4 == [['540', '1268']], f'taps={t4}')
    m, p = case('menu-ime-shown', 'ok', ime='shown')
    m.pick_pinned('3 the pinned window picked')
    n_taps = len(taps(p))
    try:
        m.menu('viewerMenuHostsButton')
        raised = False
    except RuntimeError:
        raised = True
    check('NEGATIVE menu r7d: a keyboard up -> no wake tap on the picture (the menu is not reached; nothing typed)',
          raised and len(taps(p)) == n_taps and not keys(p), f'taps={len(taps(p)) - n_taps} keys={keys(p)}')

    # r8: the tap only in the SAME viewer and connection as the pick
    for what, app_line, host_line in (('the app left the viewer (scene HOSTS, status error)',
                                       '2026-10-30 00:00:01.000 [scene] HOSTS\n2026-10-30 00:00:01.001 [status] error\n', ''),
                                      ('the host ended the session',
                                       '', '10-30 00:00:01.000 [native-video-host][control] udp control session ended epoch=1 resumed=0 reason=none\n')):
        m, p = case('input-not-same', 'ok')
        m.pick_pinned('3 the pinned window picked')
        if app_line:
            open(p['app'], 'a', encoding='utf-8').write(app_line)
        if host_line:
            open(p['host'], 'a', encoding='utf-8').write(host_line)
        n_taps = len(taps(p))
        ok4 = m.input_check()
        r = last(m)
        check(f'NEGATIVE 4 r8: {what} after the pick -> FAIL with NO tap', not ok4 and r[1] is False and
              'not the same viewer' in r[2] and len(taps(p)) == n_taps, r[2][:200])
    src = open(os.path.join(E, 'oa_avd.py'), encoding='utf-8').read()
    check('r8 (source): a 4 FAIL ends the arm before the menu / 5 / 6 / P-5d',
          'if not input_check():\n            return 1' in src.replace('\r\n', '\n'))
    # r7e: arm S driven from the install to 4 (the fake's login -> PC list -> pick); 4 FAILs (no input events). The
    # arm must end there: no tap of any kind after 4's, and 5 / 6 / 7 NOT RUN in result.json. The contrast: the same
    # flow against c468c27's oa_avd.py (it threw 4's answer away and went on into the menu).
    def run_flow(tag, avd=None, **kw):
        m, p = case(tag, 'ok', input_mode=kw.pop('input_mode', 'none'), scene=kw.pop('scene', 'login'), avd=avd, **kw)
        try:
            rc = m.main()
        except RuntimeError as e:  # c468c27 went on into the menu and raised there (its result.json is still written)
            rc = f'raised {e}'
        res = json.load(open(os.path.join(p['out'], 'result.json'), encoding='utf-8'))
        all_taps = taps(p)
        # 4's tap: the FIRST at the video view's centre (a later wake tap of the menu lands there too)
        i4 = min([i for i, t in enumerate(all_taps) if t == ['540', '1268']], default=-1)
        after = all_taps[i4 + 1:] if i4 >= 0 else all_taps
        four = [r for r in res['results'] if r[0].startswith('4 ')]
        nr = sorted({n for n, _ in res['not_run']})
        run_flow.res = res
        run_flow.taps = all_taps
        return rc, four, nr, i4, after
    rc, four, nr, i4, after = run_flow('flow-4fail')
    check('r7e: arm S, 4 FAILs -> the arm ends there: no tap after 4 (no menu wake / menu), 5 6 7 NOT RUN',
          rc != 0 and four and four[-1][1] is False and i4 >= 0 and not after and all(c in nr for c in '567'),
          f'rc={rc} 4={[f[1] for f in four]} taps after 4={after} not run={nr}')
    steps_ok = {r[0].split()[0]: r[1] for r in run_flow.res['results']}
    check('r7f: the same flow went through 2s SH off (Settings), 3s SH in effect and 3w the 30 s watch (stub: 3 s) -- '
          'all PASS before 4', steps_ok.get('2s') is True and steps_ok.get('3s') is True and steps_ok.get('3w') is True,
          f'{steps_ok}')
    old = os.path.join(RUNS, f'oa_avd_c468c27-{os.getpid()}.py')
    with open(old, 'wb') as f:
        f.write(subprocess.run(['git', '-C', WT, 'show', 'c468c27:automation/old_apk_lane/oa_avd.py'], capture_output=True,
                               check=True).stdout)
    rc, four, nr, i4, after = run_flow('flow-4fail-c468c27', avd=old)
    check('r7e contrast: c468c27 (4 answer thrown away) on the same flow went on tapping after 4 -- the check sees it',
          i4 >= 0 and len(after) > 0, f'taps after 4={len(after)} first={after[:2]} not run={nr}')
    m, p = case('hosts-ime-hide', 'ok', ime='shown', scene='hosts')
    ok = m.hide_ime_on_hosts('1b the sign-in keyboard')
    check('control 1b r7d: on the PC list a keyboard up is put away with BACK (it takes it); the list stays, no dialog',
          ok and last(m)[1] is True and keys(p) == ['shell input keyevent 4'], last(m)[2][:160])
    m, p = case('hosts-ime-leak', 'ok', ime='shown', back='leak', scene='hosts')
    ok = m.hide_ime_on_hosts('1b the sign-in keyboard')
    check('NEGATIVE 1b r7d: BACK reaches the app instead (the exit question appears) -> FAIL', not ok and
          last(m)[1] is False, last(m)[2][:160])
    m, p = case('hosts-ime-unknown', 'ok', ime='unknown', scene='hosts')
    ok = m.hide_ime_on_hosts('1b the sign-in keyboard')
    check('NEGATIVE 1b r7d: the keyboard state unreadable -> FAIL, no key sent', not ok and not keys(p), f'keys={keys(p)}')
    m, p = case('hosts-ime-hidden', 'ok', ime='hidden', scene='hosts')
    check('1b r7d: no keyboard up -> nothing sent, goes on', m.hide_ime_on_hosts('1b the sign-in keyboard') and not keys(p))

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
    # The watch has a strict baseline read now. Test that path separately from a failure in its loop.
    m, p = case('watch-baseline-pullfail', 'ok')
    m.pick_pinned('3 the pinned window picked')
    n_taps = len(taps(p))
    os.environ['OA_FAKE_PULL_FAIL'] = '1'
    try:
        okw = m.observe_connection('3w the connection')
    finally:
        os.environ['OA_FAKE_PULL_FAIL'] = '0'
    r = last(m)
    check('NEGATIVE 3w: the baseline app snapshot fails -> FAIL before the watch, no input', not okw and r[1] is False
          and 'no current snapshot / pick marker / video out baseline' in r[2] and len(taps(p)) == n_taps, r[2][:200])

    for tag, spoil, want, what in (('watch-pullfail', pull_fails, 'no app snapshot', 'the app log pull fails'),
                                   ('watch-hostshrink', host_shrinks, 'shrank', 'the host log shrinks')):
        m, p = case(tag, 'ok')
        m.pick_pinned('3 the pinned window picked')
        read_snapshot = m.applog_snapshot
        reads, injected = [], []

        def snapshot_after_initial_gate():
            # Read 1 is the video-counter baseline; read 2 is same_viewer's initial gate.
            # Read 3 is its check AFTER the first loop sleep. Inject only there, once.
            if len(reads) == 2:
                spoil(p)
                injected.append(True)
            result = read_snapshot()
            reads.append(result[0])
            return result

        m.applog_snapshot = snapshot_after_initial_gate
        n_taps = len(taps(p))
        try:
            okw = m.observe_connection('3w the connection')
        finally:
            m.applog_snapshot = read_snapshot
            os.environ['OA_FAKE_PULL_FAIL'] = '0'
        r = last(m)
        check(f'NEGATIVE 3w: {what} AFTER a good baseline and initial gate -> FAIL in the loop, no input', not okw
              and r[1] is False and want in r[2] and reads[:2] == [True, True] and len(injected) == 1
              and len(taps(p)) == n_taps, f'reads={reads} injected={len(injected)} {r[2][:160]}')
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

    print(f'\n{checks} checks, {fails} failed\nRESULT: {"PASS" if fails == 0 else "FAIL"}')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
