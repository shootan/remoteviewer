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


def case(tag, variant, listed_id=PINNED, input_mode='pair'):
    """A fresh run folder, the fake's state, and oa_avd loaded anew against it. -> (module, paths)"""
    run_id = f'{tag}-{time.strftime("%Y%m%d%H%M%S")}-{os.getpid()}'
    root = os.path.join(RUNS, run_id)
    os.makedirs(os.path.join(root, 'a'))  # new: refused if it exists
    with open(os.path.join(root, 'fixture_account.txt'), 'w', encoding='ascii') as f:
        f.write('oatest notarealpassword\n')
    host_log = os.path.join(root, 'a', 'host.log')
    with open(host_log, 'w', encoding='utf-8') as f:
        f.write(CAPTURE + f'10-30 00:00:00.000 [native-video-host][control] window-list seq=1 count=1 selectedId={listed_id}\n')
    with open(os.path.join(root, 'a', 'directoryhost_ready.txt'), 'w', encoding='ascii') as f:
        f.write(f'ready run={run_id}-a harness_pid=1 host_pid=2 host_log={host_log}\n')
    applog = os.path.join(root, 'app.log')
    with open(applog, 'w', encoding='utf-8') as f:
        f.write('2026-10-30 00:00:00.000 [targets_ready] origin=connect ms=1 items=1\n')
    st = os.path.join(root, 'ui_state.json')
    json.dump({'tab': 'desktop'}, open(st, 'w', encoding='utf-8'))
    calls = os.path.join(root, 'adb_calls.txt')
    open(calls, 'w').close()
    os.environ.update({'OA_FAKE_ADB_LOG': calls, 'OA_FAKE_UI_STATE': st, 'OA_FAKE_APPLOG': applog,
                       'OA_FAKE_HOSTLOG': host_log, 'OA_FAKE_UI_VARIANT': variant, 'OA_FAKE_INPUT': input_mode})
    sys.argv = ['oa_avd.py', root, 'S', os.path.join(root, 'none.apk'), os.path.join(root, 'avd')]
    spec = importlib.util.spec_from_file_location(f'oa_avd_{tag.replace("-", "_")}', os.path.join(E, 'oa_avd.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m, {'calls': calls, 'host': host_log, 'app': applog, 'out': os.path.join(root, 'avd')}


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
    os.environ.update({'OA_TEST_FAKE_ADB': os.path.join(E, 'stubs', 'fake_ui_adb.cmd'), 'KA_SERIAL': 'emulator-5554'})

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

    print(f'\n{checks} checks, {fails} failed\nRESULT: {"PASS" if fails == 0 else "FAIL"}')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
