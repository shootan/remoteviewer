pp = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_pattern_window.ps1'
raw = open(pp, 'rb').read()
bom = raw.startswith(b'\xef\xbb\xbf')
s = raw.decode('utf-8-sig')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
o = "[switch]$Static, [switch]$Bottom, [switch]$Layered)"
assert s.count(o) == 1
s = s.replace(o, "[switch]$Static, [switch]$Bottom, [switch]$Layered, [int]$PulseMs = 200)")
o = "    $pulse.Interval = 200"
assert s.count(o) == 1
s = s.replace(o, "    $pulse.Interval = [Math]::Max(16, $PulseMs)  # -PulseMs 33: ~30 changes/s (the cost runs)")
if crlf:
    s = s.replace('\n', '\r\n')
open(pp, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.encode('utf-8'))

p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep('''    def __init__(self, host_exe, out, static, input_log=None, layered_size=None):''',
    '''    def __init__(self, host_exe, out, static, input_log=None, layered_size=None, pulse_ms=200):''')
rep('''                                        + (['-Layered', '-Width', str(layered_size[0]), '-Height', str(layered_size[1])]
                                           if layered_size else []),''',
    '''                                        + (['-Layered', '-Width', str(layered_size[0]), '-Height', str(layered_size[1]),
                                            '-PulseMs', str(pulse_ms)] if layered_size else []),''')
rep('''def scenario_race(fx, out):''', '''def scenario_latency_ab(fx, out, seconds):
    """sh-strength r2 cost (Codex §4B: 1 pass with more output pixels is not free): the same changing input (the
    off-screen layered pattern, a block flipped every 33 ms) shown x1.33 -- A off (direct), A 0.60, B 0.60, each its
    own launch, `seconds` long. Read from the app's own 5 s lines: handoff->latch window p95, GL draw wall time,
    frames drawn / skipped."""
    configs = [('A_off', 'off', ()), ('A_s60', 'strong', ()), ('B_s60', 'strong', ('--ez', 'shTestDisplayPass', 'true'))]
    res = {}
    for name, level, extras in configs:
        ok = connect_to_pattern(fx.port, extras, level_before=level, window=True)
        check(ok, name + ': connected and viewing the pattern window')
        adb('logcat', '-c', check_rc=False)
        time.sleep(seconds)
        lines = [l[l.index('sh_latency '):] for l in log_lines() if 'sh_latency ' in l]
        draws, p95s, drawn, skipped = [], [], 0, 0
        for l in lines:
            f = dict(kv.split('=', 1) for kv in l.replace('|', ' ').split() if '=' in kv)
            if 'drawWallUs' in f and f.get('glDrawn', '0') != '0':
                draws.append(int(f['drawWallUs']))
            if 'p95us' in f:
                try:
                    p95s.append(int(f['p95us']))
                except ValueError:
                    pass
            drawn += int(f.get('glDrawn', '0'))
            skipped += int(f.get('glSkipped', '0'))
        res[name] = {'lines': len(lines), 'drawWallUs': draws, 'segment_p95us_last': p95s[-1] if p95s else None,
                     'glDrawn': drawn, 'glSkipped': skipped, 'sample': lines[-1][:400] if lines else ''}
        print('      %s: %s' % (name, json.dumps(res[name])), flush=True)
        check(len(lines) >= 3, name + ': the app reported its 5 s latency lines', str(len(lines)))
    with open(os.path.join(out, 'latency_ab.json'), 'w') as fh:
        json.dump(res, fh, indent=1)


def scenario_race(fx, out):''')
rep('''                                                                      layered_size=(1440, 810) if args.scenario == 'compare_ab' else None,''',
    '''                                                                      layered_size=(1440, 810) if args.scenario in ('compare_ab', 'latency_ab') else None,
                                                                      pulse_ms=33 if args.scenario == 'latency_ab' else 200,''')
rep('''        elif args.scenario == 'compare_ab':''', '''        elif args.scenario == 'latency_ab':
            scenario_latency_ab(fx, args.out, args.seconds)
        elif args.scenario == 'compare_ab':''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
