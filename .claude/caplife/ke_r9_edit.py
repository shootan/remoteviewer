p = r'C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\ke_avd.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)


rep('''def e2_shoff():
    """decoder-liveness r1 (Codex codex_apk_decoder_liveness_design_r1.md (2)): E2 once with the REAL sharpening
    pipeline off -- Settings' OFF (SharpnessLevel.OFF: the decoder writes straight to the TextureView), not strength 0,
    not AUTO_PREPARE. The effective path is judged from the app's own log. Nothing after E2 runs."""
    env_note('before E2')''', '''def e2_watch(sh_off):
    """decoder-liveness r1 (Codex codex_apk_decoder_liveness_design_r1.md (2)): E2 once with the REAL sharpening
    pipeline off -- Settings' OFF (SharpnessLevel.OFF: the decoder writes straight to the TextureView), not strength 0,
    not AUTO_PREPARE. The effective path is judged from the app's own log. Nothing after E2 runs.
    AVD r9 (Codex codex_apk_decoder_liveness_src_r5.md): the same E2 + 60 s watch with the DEFAULT sharpening
    (sh_off=False, plan e2-sh) -- Settings untouched; what was requested, what was effective and any fallback are
    recorded from the app's own log, and a run that went direct is said to be NOT an SH-on render check."""
    tag = 'E2-SHOFF' if sh_off else 'E2-SH'
    env_note('before E2')''')
rep('''    if ka_avd.wait('listSettingsButton', 10):
        tap('listSettingsButton')
        for _ in range(10):
            if ka_avd.wait('settingsSharpOffButton', 1):
                break
            ka_avd.adb('shell', 'input', 'swipe', '540', '1800', '540', '900', '300', check=False)
        if ka_avd.wait('settingsSharpOffButton', 2):
            tap('settingsSharpOffButton')
            time.sleep(1.5)
    sel = re.findall(r'\\[sharpness_select\\][^\\n]*', since(n_sh))
    shot('E2_shoff_settings')
    verdict('E2-SHOFF the sharpening pipeline is set OFF in Settings (level=off, effective=off)',
            any('level=off' in s and 'effective=off' in s for s in sel), ' | '.join(sel)[:200])
    first_ms = select('E2 A (ordinary connect, sharpening pipeline OFF)')
    log = since(n_sh)
    paths = re.findall(r'\\[sharpen_(?:path|start|fallback|direct_skipped)\\][^\\n]*', log)
    verdict('E2-SHOFF the viewer used the direct path only (requested=off, no sharpen_start)',
            any('path=direct requested=off' in p for p in paths) and not any(p.startswith('[sharpen_start]') for p in paths),
            ' | '.join(paths)[:300])''', '''    if sh_off:
        if ka_avd.wait('listSettingsButton', 10):
            tap('listSettingsButton')
            for _ in range(10):
                if ka_avd.wait('settingsSharpOffButton', 1):
                    break
                ka_avd.adb('shell', 'input', 'swipe', '540', '1800', '540', '900', '300', check=False)
            if ka_avd.wait('settingsSharpOffButton', 2):
                tap('settingsSharpOffButton')
                time.sleep(1.5)
        sel = re.findall(r'\\[sharpness_select\\][^\\n]*', since(n_sh))
        shot('E2_shoff_settings')
        verdict('E2-SHOFF the sharpening pipeline is set OFF in Settings (level=off, effective=off)',
                any('level=off' in s and 'effective=off' in s for s in sel), ' | '.join(sel)[:200])
    else:
        step('  E2-SH: Settings untouched -- the default sharpening of a cleared app')
    first_ms = select('E2 A (ordinary connect, sharpening pipeline ' + ('OFF)' if sh_off else 'DEFAULT)'))
    log = since(n_sh)
    paths = re.findall(r'\\[sharpen_(?:path|start|fallback|direct_skipped|producer_stuck)\\][^\\n]*', log)
    if sh_off:
        verdict('E2-SHOFF the viewer used the direct path only (requested=off, no sharpen_start)',
                any('path=direct requested=off' in p for p in paths) and not any(p.startswith('[sharpen_start]') for p in paths),
                ' | '.join(paths)[:300])
    else:
        # AVD r9: requested / effective / fallback, from the app's own lines -- recorded, and a run that went direct is
        # never counted as an SH-on render check
        requested = re.findall(r'requested=(\\S+)', ' '.join(paths))
        fallback = [p for p in paths if p.startswith('[sharpen_fallback]') or p.startswith('[sharpen_producer_stuck]')]
        started = any(p.startswith('[sharpen_start]') for p in paths)
        effective = 'sharpen' if started and not fallback else 'direct'
        step(f'  E2-SH requested={requested[-1] if requested else "?"} effective={effective} '
             f'fallback={" | ".join(fallback)[:200] if fallback else "none"}')
        if effective != 'sharpen':
            step('  E2-SH NOTE: this run went DIRECT -- it is NOT an SH-on render check')
        verdict('E2-SH the effective sharpening path is known from the app log (requested / effective / fallback)',
                bool(paths) and bool(requested), f'effective={effective} | ' + ' | '.join(paths)[:260])''')
rep('''        verdict('E2-SHOFF the session stayed up 60 s after its first frame (no keep_end, still the viewer)',
                first_end is None,''', '''        verdict(f'{tag} the session stayed up 60 s after its first frame (no keep_end, still the viewer)',
                first_end is None,''')
rep('''        skip_case(case, 'plan e2-shoff: E1 and E2 only (decoder-liveness r1 (2))')''',
    '''        skip_case(case, f'plan {"e2-shoff" if sh_off else "e2-sh"}: E1 and E2 only (decoder-liveness r1 (2) / r5)')''')
rep('''        if plan == 'e2-shoff':
            e2_shoff()''', '''        # AVD r9 (Codex src r5): e2-sh -- the default sharpening -- next to e2-shoff; each stops after E2's watch.
        # `full` is never used for these runs (it goes on to E3 and beyond).
        if plan in ('e2-shoff', 'e2-sh'):
            e2_watch(sh_off=plan == 'e2-shoff')''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok crlf=%s' % crlf)
