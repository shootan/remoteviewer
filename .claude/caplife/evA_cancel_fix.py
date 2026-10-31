p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''        adb('logcat', '-c', check_rc=False)
        # the tap and the cancel in one shell call: the cancel lands before the first IDR is shown
        adb('shell', 'input tap %d %d; input keyevent KEYCODE_BACK' % (cx, cy), check_rc=False)
        time.sleep(1)
        screencap(os.path.join(out, 'prelogin_2_cancelled.png'))
        early = find(dump(), rid='targetCardTitle') is not None
        time.sleep(6)
        late = find(dump(), rid='targetCardTitle') is not None and find(dump(), rid='viewerMenuButton') is None
        lines = log_lines()
        aborted = any('selection aborted' in l for l in lines)
        completed = [l for l in lines if 'window_selected' in l and 'logon screen' in l]
        check(early and late and aborted, 'cancel before the picture: the list stays, no late frame opens the viewer',
              'early=%s late=%s aborted=%s completed=%s' % (early, late, aborted, completed[-1:]))''',
    '''        adb('logcat', '-c', check_rc=False)
        # the host holds every frame while this file exists (test build: GNLINK_STREAM_TEST_PAUSE_PUBLISH_FILE,
        # bounded 15 s) -- the pick waits for its first IDR, the user cancels in that wait, then the frames go on
        pause = os.path.join(os.path.abspath(out), 'pause_publish')
        open(pause, 'w').close()
        try:
            tap_node(card)
            waiting = wait_for(rid='viewerMenuButton', timeout=10)  # the viewer, no picture yet
            screencap(os.path.join(out, 'prelogin_2_waiting.png'))
            adb('shell', 'input', 'keyevent', 'KEYCODE_BACK', check_rc=False)
            time.sleep(2)
            early = find(dump(), rid='targetCardTitle') is not None
            screencap(os.path.join(out, 'prelogin_2_cancelled.png'))
        finally:
            os.remove(pause)  # our own file, inside this run's output folder
        time.sleep(6)  # the held frames are published now: none of them may open the viewer again
        late = find(dump(), rid='targetCardTitle') is not None and find(dump(), rid='viewerMenuButton') is None
        lines = log_lines()
        aborted = any('selection aborted' in l for l in lines)
        completed = [l for l in lines if 'local selection' in l and 'selected' in l and 'logon screen' in l]
        check(waiting and early and late and aborted,
              'cancel while the pick waits for its first frame: the list stays, the late frames open nothing',
              'waiting=%s early=%s late=%s aborted=%s' % (waiting, early, late, aborted))''')
rep('''                                                        host_env=({'GNLINK_STREAM_TEST_PRELOGIN_ROLE': '1'}
                                                                  if args.scenario == 'prelogin_ui' else None))''',
    '''                                                        host_env=({'GNLINK_STREAM_TEST_PRELOGIN_ROLE': '1',
                                                                   'GNLINK_STREAM_TEST_PAUSE_PUBLISH_FILE':
                                                                       os.path.join(os.path.abspath(args.out), 'pause_publish')}
                                                                  if args.scenario == 'prelogin_ui' else None))''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
