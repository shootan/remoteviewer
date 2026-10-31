p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''    def __init__(self, host_exe, out, static, input_log=None, layered_size=None, pulse_ms=200, host_args=(), host_env=None):''',
    '''    def __init__(self, host_exe, out, static, input_log=None, layered_size=None, pulse_ms=200, host_args=(), host_env=None,
                 scroll=False):''')
rep('''        self.pattern_log = open(os.path.join(out, 'pattern.log'), 'wb')''', '''        self.pattern_log = open(os.path.join(out, 'pattern.log'), 'wb')
        # apk-0232 E1: the pattern scrolls while this file exists (sh_pattern_window.ps1 -ScrollFile)
        self.scroll_file = os.path.join(os.path.abspath(out), 'scroll_on') if scroll else None''')
rep('''                                            '-PulseMs', str(pulse_ms)] if layered_size else []),''',
    '''                                            '-PulseMs', str(pulse_ms)] if layered_size else [])
                                        + (['-ScrollFile', self.scroll_file, '-ScrollPx', '4'] if scroll else []),''')

rep('''def scenario_ua2_desktop(fx, out):''', r'''def scenario_scroll_shimmer(fx, out):
    """apk-0232 E1 (Codex codex_apk0232_evidence_final.md): the phone's FINAL OUTPUT while the picture scrolls -- for a
    person to look at, not a score. The off-screen layered pattern (thin text, 1-px lines, checkerboard) scrolls up
    4 px per 33 ms tick while a file exists; every source picture carries its number (digits + 16 stripes, bottom right)
    and is logged (pattern.log "scroll frame=..."). The candidate APK as shipped, one connection, four settings in turn:
    끄기 / 약하게 0.20 / 강하게 0.60 (A, 기본) / 화면 크기 (실험) 0.60 (B, through the normal settings). Each: 4 s of
    adb screenrecord of the phone's display (shimmer_<name>.mp4) while it scrolls, one still after, the source frame
    range and what the app logged about its drawing path. Nothing is judged here beyond 'it was recorded'."""
    segs = [('off', 'off', None), ('A_weak_s20', 'weak', None), ('A_strong_s60', 'strong', None),
            ('B_display_s60', 'strong', 'display')]
    ok = connect_to_pattern(fx.port, level_before='off', window=True)
    check(ok, 'connected (끄기), the pattern window shown')
    time.sleep(4)
    summary = []
    for name, level, method in segs:
        if method == 'display':
            to_list()
            settings_method('settingsSharpMethodDisplayButton')
            check(pick_pattern_again(), name + ': back in the viewer with 화면 크기 (실험)')
            time.sleep(5)
        else:
            choose_level(level)
            time.sleep(4)
        adb('logcat', '-c', check_rc=False)
        before = open(os.path.join(out, 'pattern.log'), 'rb').read().decode(errors='replace')
        open(fx.scroll_file, 'w').close()
        time.sleep(0.5)  # the scroll is running before the recording starts
        dev = '/sdcard/shimmer_%s.mp4' % name
        t0 = time.time()
        adb('shell', 'screenrecord', '--time-limit', '4', dev, check_rc=False, timeout=30)
        t1 = time.time()
        os.remove(fx.scroll_file)  # the harness's own flag file, inside this run's output folder
        time.sleep(0.5)
        local = os.path.join(out, 'shimmer_%s.mp4' % name)
        adb('pull', dev, local, check_rc=False, timeout=60)
        adb('shell', 'rm', '-f', dev, check_rc=False)  # the AVD's own scratch file
        screencap(os.path.join(out, 'shimmer_%s_after.png' % name))
        after = open(os.path.join(out, 'pattern.log'), 'rb').read().decode(errors='replace')
        frames = [int(m) for m in re.findall(r'scroll frame=(\d+)', after[len(before):])]
        lines = log_lines()
        path = [l[l.index('[sharpen_'):][:120] for l in lines if '[sharpen_path]' in l or '[sharpen_display_pass]' in l]
        size = os.path.getsize(local) if os.path.exists(local) else 0
        rec = {'segment': name, 'level': level, 'method': method or 'default', 'mp4': os.path.basename(local), 'bytes': size,
               'recordSeconds': round(t1 - t0, 2), 'sourceFrames': [frames[0], frames[-1]] if frames else None,
               'sourceFrameCount': len(frames), 'sourceFps': round(len(frames) / max(0.1, t1 - t0 + 1.0), 1),
               'appPathLog': path[-2:]}
        summary.append(rec)
        print('      %s' % json.dumps(rec, ensure_ascii=False), flush=True)
        check(size > 20000 and frames, name + ': recorded while the source scrolled (mp4 + its source frame range)',
              '%d bytes, frames %s' % (size, rec['sourceFrames']))
    to_list()
    settings_method('settingsSharpMethodDefaultButton')  # leave the app's choice as it was (기본)
    with open(os.path.join(out, 'shimmer_summary.json'), 'w', encoding='utf-8') as fh:
        json.dump(summary, fh, ensure_ascii=False, indent=1)


def scenario_ua2_desktop(fx, out):''')

rep('''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b', 'ua2_desktop', 'prelogin_ui', 'prelogin_ui_control', 'prelogin_tripwire') else None,''',
    '''if args.scenario in ('compare_ab', 'latency_ab', 'touch_ab', 'ua2', 'ua2_old', 'method_ui', 'layout_b', 'ua2_desktop', 'prelogin_ui', 'prelogin_ui_control', 'prelogin_tripwire', 'scroll_shimmer') else None,''')
rep('''                                                        input_log=input_log,''', '''                                                        input_log=input_log,
                                                        scroll=args.scenario == 'scroll_shimmer',''')
rep('''        elif args.scenario == 'ua2_desktop':
            scenario_ua2_desktop(fx, args.out)''', '''        elif args.scenario == 'ua2_desktop':
            scenario_ua2_desktop(fx, args.out)
        elif args.scenario == 'scroll_shimmer':
            scenario_scroll_shimmer(fx, args.out)''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

q = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_pattern_window.ps1'
t = open(q, 'rb').read()
t2 = t.replace(b'(white = 1, black = 0, least significant on the left)', b'(white = 1, grey = 0, least significant on the left)')
assert t2 != t
open(q, 'wb').write(t2)
print('ok')
