p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep('''class Fixture:
    def __init__(self, host_exe, out, static, input_log=None):
        self.out = out
        self.port = free_udp_port()
        self.control_port = free_tcp_port()  # manual connect uses a TCP control link (loopback only)
        self.pattern = subprocess.Popen(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                         os.path.join(HERE, 'sh_pattern_window.ps1'), '-Seconds', '1800']
                                        + (['-Static'] if static else [])
                                        + (['-InputLog', input_log] if input_log else []))''',
    '''class Fixture:
    def __init__(self, host_exe, out, static, input_log=None, layered_size=None):
        self.out = out
        self.port = free_udp_port()
        self.control_port = free_tcp_port()  # manual connect uses a TCP control link (loopback only)
        # sh-strength r2: layered_size=(w, h) -> the pattern is a LAYERED window outside every monitor (it covers
        # nothing on this PC's screen); the APK must pick it as a window (WGC).
        self.pattern_log = open(os.path.join(out, 'pattern.log'), 'wb')
        self.pattern = subprocess.Popen(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                         os.path.join(HERE, 'sh_pattern_window.ps1'), '-Seconds', '1800']
                                        + (['-Static'] if static else [])
                                        + (['-InputLog', input_log] if input_log else [])
                                        + (['-Layered', '-Width', str(layered_size[0]), '-Height', str(layered_size[1])]
                                           if layered_size else []),
                                        stdout=self.pattern_log, stderr=subprocess.STDOUT)''')

rep('''def scenario_race(fx, out):''', '''CMP_SCALE = 4.0 / 3.0  # a 1440x810 pattern window shown 1920x1080 on the AVD's 1080-high screen
CMP_REGIONS = {'A_text': (60, 40, 900, 400), 'B_lowcontrast': (960, 40, 1430, 400), 'C_lines': (60, 440, 440, 700),
               'D_flat': (490, 450, 590, 690), 'E_checker': (1040, 440, 1400, 700)}


def cmp_metrics(img):
    """metrics() for the x1.33 picture: the same pattern regions, scaled onto the screen."""
    x0 = video_origin(img)
    if x0 is None:
        return None
    m = {'x0': x0}
    for k, (l, t, r, b) in CMP_REGIONS.items():
        reg = img[int(t * CMP_SCALE):int(b * CMP_SCALE), x0 + int(l * CMP_SCALE):x0 + int(r * CMP_SCALE), :]
        m[k + '_edge'] = round(edge_energy(reg), 3)
        g = reg.mean(axis=2)
        m[k + '_clip'] = round(float(((g <= 2) | (g >= 253)).mean()), 4)
    top = img[0, x0 + 5:x0 + VIDEO_W - 5, :].mean(axis=0)
    bottom = img[VIDEO_H - 1, x0 + 5:x0 + VIDEO_W - 5, :].mean(axis=0)
    right = img[5:VIDEO_H - 5, x0 + VIDEO_W - 1, :].mean(axis=0)
    m['border_top_rgb'] = [round(float(v)) for v in top]
    m['border_bottom_rgb'] = [round(float(v)) for v in bottom]
    m['border_right_rgb'] = [round(float(v)) for v in right]
    return m


def scenario_compare_ab(fx, out):
    """sh-strength r2 (Codex §4/§4B): the REAL APK path on the AVD, the same static picture (an off-screen layered
    1440x810 pattern window, captured as a window) shown x1.33 -- A (source-size pass, the compositor enlarges) at
    off/0.20/0.35/0.60 and B (display-size pass, one draw) at 0.20/0.35/0.60, plus B's double-transform negative
    control. Each configuration is its own launch (B is a launch extra of the test build)."""
    configs = [
        ('A_off', 'off', ()), ('A_s20', 'weak', ()), ('A_s35', 'strong', ('--ef', 'shTestStrength', '0.35')),
        ('A_s60', 'strong', ()),
        ('B_s20', 'weak', ('--ez', 'shTestDisplayPass', 'true')),
        ('B_s35', 'strong', ('--ez', 'shTestDisplayPass', 'true', '--ef', 'shTestStrength', '0.35')),
        ('B_s60', 'strong', ('--ez', 'shTestDisplayPass', 'true')),
        ('Bdouble_s35', 'strong', ('--ez', 'shTestDisplayPassDouble', 'true', '--ef', 'shTestStrength', '0.35')),
    ]
    shots = {}
    imgs = {}
    for name, level, extras in configs:
        ok = connect_to_pattern(fx.port, extras, level_before=level, window=True)
        check(ok, name + ': the APK connects and shows the pattern window (real UI, window pick)')
        time.sleep(6)
        img = screencap(os.path.join(out, 'shot_%s.png' % name))
        imgs[name] = img
        shots[name] = cmp_metrics(img)
        lines = log_lines()
        place = [l[l.index('sharpen_display_pass'):] for l in lines if 'sharpen_display_pass' in l]
        sizes = [l[l.index('sharpen_'):][:160] for l in lines if 'sharpen_path' in l or 'sh_output' in l][-2:]
        print('      %s: %s | %s | %s' % (name, json.dumps(shots[name]), place[-1:] , sizes), flush=True)
        if name.startswith('B'):
            check(bool(place), name + ': the app ran the display-size pass (placement logged)', str(place[-1:]))
    good = [k for k in shots if not k.startswith('Bdouble')]
    for k in good:
        check(shots[k] is not None and borders_ok(shots[k]), k + ': the picture is found with its four borders where the x1.33 picture\\'s edges are',
              str(shots[k] and (shots[k]['border_top_rgb'], shots[k]['border_bottom_rgb'], shots[k]['border_right_rgb'])))
    if all(shots[k] for k in good):
        check(len({shots[k]['x0'] for k in good}) == 1, 'A and B place the picture at the same screen x (no double placement)',
              str({k: shots[k]['x0'] for k in good}))
        # the same picture, pixel for pixel in placement: flat colour identical, and B s vs A s differ only at edges
        for st in ('s20', 's35', 's60'):
            a, b = imgs['A_' + st].astype(np.float64), imgs['B_' + st].astype(np.float64)
            x0 = shots['A_' + st]['x0']
            l, t, r, bb = CMP_REGIONS['D_flat']
            fa = a[int(t * CMP_SCALE):int(bb * CMP_SCALE), x0 + int(l * CMP_SCALE):x0 + int(r * CMP_SCALE)]
            fb = b[int(t * CMP_SCALE):int(bb * CMP_SCALE), x0 + int(l * CMP_SCALE):x0 + int(r * CMP_SCALE)]
            check(np.abs(fa.mean(axis=(0, 1)) - fb.mean(axis=(0, 1))).max() < 4,
                  'A vs B %s: flat colour the same (colour / geometry of the two paths agree)' % st,
                  '%s vs %s' % (fa.mean(axis=(0, 1)).round(1), fb.mean(axis=(0, 1)).round(1)))
        for path in ('A', 'B'):
            seq = [('A_off' if path == 'A' else None), path + '_s20', path + '_s35', path + '_s60']
            seq = [k for k in seq if k]
            vals = [shots[k]['A_text_edge'] for k in seq]
            check(all(x < y for x, y in zip(vals, vals[1:])), path + ': text edge energy rises with the strength', str(dict(zip(seq, vals))))
    d = shots.get('Bdouble_s35')
    caught = d is None or not borders_ok(d) or (shots.get('B_s35') and d['x0'] != shots['B_s35']['x0'])
    check(caught, 'NEGATIVE CONTROL: B with the TextureView placement applied as well (double transform) is caught',
          str(d and {'x0': d['x0'], 'borders': (d['border_top_rgb'], d['border_bottom_rgb'])}))
    with open(os.path.join(out, 'compare_ab.json'), 'w') as fh:
        json.dump(shots, fh, indent=1)


def scenario_race(fx, out):''')

rep('''    fx = None if args.scenario in ('shader', 'strength_ab') else Fixture(args.host_exe, args.out, static=args.scenario == 'race',''',
    '''    fx = None if args.scenario in ('shader', 'strength_ab') else Fixture(args.host_exe, args.out, static=args.scenario == 'race',
                                                                      layered_size=(1440, 810) if args.scenario == 'compare_ab' else None,''')
rep('''        elif args.scenario == 'strength_ab':''', '''        elif args.scenario == 'compare_ab':
            scenario_compare_ab(fx, args.out)
        elif args.scenario == 'strength_ab':''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
