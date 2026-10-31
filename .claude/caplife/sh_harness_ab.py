p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep('''            if pattern == 'alt':
                v[y, x] = (40 + 2 * x + (x % 2) * 50, 40 + 3 * y + (y % 2) * 50, 170 - x - y + ((x + y) % 2) * 30)''',
    '''            if pattern == 'alt':
                v[y, x] = (40 + 2 * x + (x % 2) * 50, 40 + 3 * y + (y % 2) * 50, 170 - x - y + ((x + y) % 2) * 30)
            elif pattern == 'text':
                if 8 <= x <= 39 and 4 <= y <= 35:
                    g = 228 if (x // 2) % 2 == 0 else 28
                elif y == 20:
                    g = 228
                elif x >= 44 and 4 <= y <= 35:
                    g = 120 if (x + y) % 2 == 0 else 136
                else:
                    g = 28
                v[y, x] = (g, g, (g * 3 + 255) // 4)''')

rep('''def scenario_race(fx, out):''', '''def shader_model(src, s, out_w, out_h):
    """sh-strength r2: what the production shader computes, on the CPU -- each output pixel centre mapped onto the
    visible picture (the B placement covers the whole output; out == visible size is A), the five taps one SOURCE
    pixel apart, each clamped half a pixel inside the picture, bilinear with clamp-to-edge, ONE final clamp.
    At out == visible size this is kernel()."""
    h, w = src.shape[:2]
    uu = (np.arange(out_w) + 0.5) / out_w
    vv = (np.arange(out_h) + 0.5) / out_h
    U, V = np.meshgrid(uu, vv)

    def samp(a, b):
        a = np.clip(a, 0.5 / w, 1 - 0.5 / w)
        b = np.clip(b, 0.5 / h, 1 - 0.5 / h)
        x = a * w - 0.5
        y = b * h - 0.5
        x0 = np.clip(np.floor(x).astype(int), 0, w - 1)
        y0 = np.clip(np.floor(y).astype(int), 0, h - 1)
        x1 = np.clip(x0 + 1, 0, w - 1)
        y1 = np.clip(y0 + 1, 0, h - 1)
        fx = (x - np.floor(x))[..., None]
        fy = (y - np.floor(y))[..., None]
        top = src[y0, x0] * (1 - fx) + src[y0, x1] * fx
        bot = src[y1, x0] * (1 - fx) + src[y1, x1] * fx
        return top * (1 - fy) + bot * fy

    c = samp(U, V)
    n = samp(U + 1.0 / w, V) + samp(U - 1.0 / w, V) + samp(U, V + 1.0 / h) + samp(U, V - 1.0 / h)
    return np.clip((1 + 4 * s) * c - s * n, 0, 255)


def upscale_bilinear(img, out_w, out_h):
    """A's last step stood in on the CPU: the compositor's enlargement of the source-size output (a model --
    the real compositor's filter is the device's; the AVD compare captures the real one)."""
    return shader_model(img, 0.0, out_w, out_h)


def picture_stats(img, region):
    """Edge energy (mean |horizontal gradient|), and the clipped share (0 or 255 in any channel), over `region`."""
    y0, y1, x0, x1 = region
    a = img[y0:y1, x0:x1]
    g = np.abs(np.diff(a.mean(axis=2), axis=1)).mean()
    clip = float(((a <= 0.5) | (a >= 254.5)).any(axis=2).mean())
    return round(float(g), 3), round(clip, 4)


def scenario_strength_ab(fx, out):
    """sh-strength r2 (Codex §4/§4B): one input frame through the production renderer -- A at 0/0.20/0.35/0.60 and B
    (display-size, one pass) at x1.33/x2 -- read back and checked against shader_model; then the strength effect
    (A s vs A 0) and the position effect (A s enlarged afterwards vs B s at the same size) on the same frame."""
    adb('shell', 'am', 'force-stop', PKG, check_rc=False)
    adb('shell', 'run-as', PKG, 'sh', '-c', 'rm -f files/sh_fixture*', check_rc=False)
    adb('logcat', '-c', check_rc=False)
    adb('shell', 'am', 'start', '-n', ACT, '--es', 'shTestFixture', 'strengthab')
    done = None
    for _ in range(120):
        lines = log_lines('sh_fixture_done')
        if lines:
            done = lines[-1]
            break
        time.sleep(1)
    if not check(done is not None, 'the fixture ran (sh_fixture_done)'):
        return
    summary = json.loads(done[done.index('{'):])
    vis = fixture_visible('text')
    got = {}
    results = {}
    for c in summary.get('cases', []):
        name = c['name']
        if name.endswith('_noring'):
            check(c['note'] == 'no_output' and c.get('heldNoSource', 0) >= 1 and c.get('drawn', 0) == 0,
                  name + ': B without a source size draws NOTHING (held, no output-size fallback)',
                  'note=%s held=%s drawn=%s' % (c['note'], c.get('heldNoSource'), c.get('drawn')))
            continue
        raw = adb('exec-out', 'run-as', PKG, 'cat', 'files/sh_fixture_%s.rgba' % name, binary=True, timeout=60)
        w, h = c['outW'], c['outH']
        ok = len(raw) == w * h * 4 and c['note'] == 'ok'
        check(ok, name + ': output read back (%dx%d)' % (w, h), 'note=%s bytes=%d src=%s' % (c['note'], len(raw), c.get('src')))
        if not ok:
            continue
        img = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 4)[:, :, :3].astype(np.float64)
        got[name] = img
        exp = shader_model(vis, c['strength'], w, h)
        err = np.abs(img - exp)
        results[name] = {'size': [w, h], 'max_err': float(err.max()), 'mean_err': round(float(err.mean()), 3)}
        if c.get('control'):
            check(err.max() > 6, name + ': NEGATIVE CONTROL -- output-pixel taps are caught', 'max=%.1f' % err.max())
            continue
        check(err.max() <= 6, name + ': matches the shader model on the same frame (max |d| <= 6)',
              'max=%.1f mean=%.3f' % (err.max(), err.mean()))
    # ---- the comparisons (same input frame)
    bars = lambda w, h: (int(4 * h / 40), int(36 * h / 40), int(8 * w / 64), int(40 * w / 64))
    low = lambda w, h: (int(4 * h / 40), int(36 * h / 40), int(44 * w / 64), w)
    table = {}
    for name, img in got.items():
        h, w = img.shape[:2]
        e_bar, c_bar = picture_stats(img, bars(w, h))
        e_low, _ = picture_stats(img, low(w, h))
        table[name] = {'bars_edge': e_bar, 'bars_clipped': c_bar, 'lowcontrast_edge': e_low}
    a0 = got.get('textA_s00')
    for st in ('20', '35', '60'):
        a = got.get('textA_s' + st)
        if a is not None and a0 is not None:
            d = np.abs(a - a0)
            table['textA_s' + st]['vs_A0_mean'] = round(float(d.mean()), 3)
            table['textA_s' + st]['vs_A0_p99'] = round(float(np.percentile(d, 99)), 2)
    if got.get('textA_s20') is not None and got.get('textA_s35') is not None:
        d = np.abs(got['textA_s35'] - got['textA_s20'])
        check(d.max() > 6, 'A: 0.35 and 0.20 are DIFFERENT final pixels on the same frame (the strength reaches the output)',
              'mean=%.3f max=%.1f' % (d.mean(), d.max()))
    if got.get('textA_s60') is not None and got.get('textA_s35') is not None:
        d = np.abs(got['textA_s60'] - got['textA_s35'])
        check(d.max() > 6, 'A: 0.60 differs from 0.35 on the same frame', 'mean=%.3f max=%.1f' % (d.mean(), d.max()))
    for scale in ('B133', 'B2'):
        for st in ('00', '20', '35', '60'):
            b = got.get('text%s_s%s' % (scale, st))
            a = got.get('textA_s' + st)
            if b is None or a is None:
                continue
            h, w = b.shape[:2]
            a_up = upscale_bilinear(a, w, h)
            d = np.abs(b - a_up)
            e_aup, c_aup = picture_stats(a_up, bars(w, h))
            table['text%s_s%s' % (scale, st)].update({
                'vs_Aup_mean': round(float(d.mean()), 3), 'vs_Aup_p99': round(float(np.percentile(d, 99)), 2),
                'vs_Aup_max': round(float(d.max()), 1), 'Aup_bars_edge': e_aup, 'Aup_bars_clipped': c_aup})
    print('      ' + json.dumps(table), flush=True)
    with open(os.path.join(out, 'strength_ab.json'), 'w') as fh:
        json.dump({'checks': results, 'table': table}, fh, indent=1)
    for name, img in got.items():
        Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).resize(
            (img.shape[1] * 4, img.shape[0] * 4), Image.NEAREST).save(os.path.join(out, name + '_x4.png'))


def scenario_race(fx, out):''')

rep('''    fx = None if args.scenario == 'shader' else Fixture(''', '''    fx = None if args.scenario in ('shader', 'strength_ab') else Fixture(''')
rep('''        elif args.scenario == 'shader':''', '''        elif args.scenario == 'strength_ab':
            scenario_strength_ab(fx, args.out)
        elif args.scenario == 'shader':''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
