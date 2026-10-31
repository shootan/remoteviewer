p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_filter_avd.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''        if name.endswith('_noring'):
            check(c['note'] == 'no_output' and c.get('heldNoSource', 0) >= 1 and c.get('drawn', 0) == 0,
                  name + ': B without a source size draws NOTHING (held, no output-size fallback)',
                  'note=%s held=%s drawn=%s' % (c['note'], c.get('heldNoSource'), c.get('drawn')))
            continue''', '''        if name.endswith('_noring'):
            check(c['note'] == 'no_output' and c.get('heldNoSource', 0) >= 1 and c.get('drawn', 0) == 0,
                  name + ': B without a source size draws NOTHING (held, no output-size fallback)',
                  'note=%s held=%s drawn=%s' % (c['note'], c.get('heldNoSource'), c.get('drawn')))
            continue
        if c.get('flow') == 'layout_fail':
            # sh-strength r4 (Codex codex_sh_strength_r3.md N1 a): never video into a buffer of the wrong size --
            # bounded cleared swaps, then the renderer fails (the policy's way back to direct)
            check(c['note'].startswith('failed:layout') and c.get('drawn', -1) == 0 and 2 <= c.get('clearSwaps', 0) <= 120,
                  name + ': the output never the view\\'s size -> NO video frame drawn, bounded cleared swaps, then failed (-> direct)',
                  'note=%s drawn=%s clearSwaps=%s' % (c['note'][:90], c.get('drawn'), c.get('clearSwaps')))
            continue
        if c.get('flow') == 'pan_redraw':
            pan_case = c
            continue''')

# after the generic loop: the pan case against the same frame's B output, moved
rep('''    # ---- the comparisons (same input frame)''', '''    # ---- r4 (N1 b): a pan with no new decoder frame -> the latched frame redrawn once at the new place
    pc = locals().get('pan_case')
    if pc is not None:
        name = pc['name']
        raw = adb('exec-out', 'run-as', PKG, 'cat', 'files/sh_fixture_%s.rgba' % name, binary=True, timeout=60)
        w, h = pc['outW'], pc['outH']
        base = got.get('textB133_s35')
        ok = len(raw) == w * h * 4 and pc['note'] == 'ok' and base is not None
        check(pc['note'] == 'ok' and pc.get('redrawsAfterPan') == 1 and pc.get('drawn') == 1 and pc.get('drawnAtPan') == 1,
              name + ': no new frame, a moved placement (size check lying 3x) -> exactly ONE redraw, no new frame drawn',
              'note=%s redraws=%s drawn=%s clearSwaps=%s' % (pc['note'][:60], pc.get('redrawsAfterPan'), pc.get('drawn'), pc.get('clearSwaps')))
        check(pc.get('clearSwaps', 0) >= 1 and pc.get('lastTs') == pc.get('firstTs') and pc.get('firstTs', 0) > 0,
              name + ': the redraw keeps the frame\\'s timestamp (after the cleared swaps)',
              'first=%s last=%s' % (pc.get('firstTs'), pc.get('lastTs')))
        if ok:
            img = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 4)[:, :, :3].astype(np.float64)
            dx = int(round(w / 4.0))
            moved = np.abs(img[:, :w - dx - 2] - base[:, dx:w - 2])
            right = img[:, w - dx + 2:]
            check(moved.max() <= 6 and right.max() <= 8,
                  name + ': the redraw shows the picture at the NEW place (shifted by %d px, black beyond)' % dx,
                  'moved max=%.1f right max=%.1f' % (moved.max(), right.max()))
        else:
            check(False, name + ': the redraw read back', 'bytes=%d base=%s' % (len(raw), base is not None))
    else:
        check(False, 'r4: the pan redraw case ran')

    # ---- the comparisons (same input frame)''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
