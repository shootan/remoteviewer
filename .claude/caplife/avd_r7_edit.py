E = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/'


def edit(name, reps):
    s = open(E + name, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:70], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(E + name, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


edit('ke_avd.py', [
    ('''def main():
    step(f'ke run apk_off={APK_OFF} apk_on={APK_ON}')''',
     '''def env_note(tag):
    """decoder-liveness r1 (2): the emulator's state at this point, recorded only -- uptime, load, this boot's id."""
    up = adb('shell', 'cat', '/proc/uptime').split()
    la = adb('shell', 'cat', '/proc/loadavg').strip()
    boot = adb('shell', 'cat', '/proc/sys/kernel/random/boot_id').strip()
    step(f'  env[{tag}]: uptime_s={up[0] if up else "?"} loadavg="{la}" boot_id={boot}')


def e2_shoff():
    """decoder-liveness r1 (Codex codex_apk_decoder_liveness_design_r1.md (2)): E2 once with the REAL sharpening
    pipeline off -- Settings' OFF (SharpnessLevel.OFF: the decoder writes straight to the TextureView), not strength 0,
    not AUTO_PREPARE. The effective path is judged from the app's own log. Nothing after E2 runs."""
    env_note('before E2')
    pkg = adb('shell', 'dumpsys', 'package', PKG)
    step('  app: ' + ' '.join(re.findall(r'versionName=\\S+|versionCode=\\d+', pkg)[:2]))
    step('  emulator: ' + adb('shell', 'getprop', 'ro.build.fingerprint').strip() +
         ' qemu=' + adb('shell', 'getprop', 'ro.kernel.qemu').strip())
    mark('E2')
    n_sh = cursor()
    tap('card:' + H['a'])
    if not ka_avd.wait(TARGET, 30):
        verdict('E2-SHOFF A: the targets are shown', False)
        return
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
    select('E2 A (ordinary connect, sharpening pipeline OFF)')
    log = since(n_sh)
    paths = re.findall(r'\\[sharpen_(?:path|start|fallback|direct_skipped)\\][^\\n]*', log)
    verdict('E2-SHOFF the viewer used the direct path only (requested=off, no sharpen_start)',
            any('path=direct requested=off' in p for p in paths) and not any(p.startswith('[sharpen_start]') for p in paths),
            ' | '.join(paths)[:300])
    env_note('after E2 select')
    n0, ok = to_list('E2')
    st = wait_card(H['a'], '대기 중', 40)
    log = since(n0)
    shot('E2_list_A_ready')
    verdict('E2 PC 목록: the session ends, the FIRST transition (a new lease, its directory requests logged)',
            ok and '[list_return]' in log and 'decision=FIRST_TRANSITION' in log and '[standby_first_transition]' in log
            and re.search(r'\\[standby_capability\\] host=\\S+ inc=\\d+ ok=true', log) is not None,
            ' | '.join(re.findall(r'\\[standby_capability\\][^\\n]*', log))[:200])
    verdict('E2 A shows 대기 중 (a confirmed entry on a live lease)', st is not None and st[0] == '대기 중', str(st))
    env_note('end')
    for case in ('E3', 'E4', 'E5', 'E6', 'E7', 'E7b', 'E7c', 'E7e', 'E7f', 'E7g', 'E7h', 'E8', 'E9', 'E10', 'E11', 'E12',
                 'E13', 'E13b'):
        skip_case(case, 'plan e2-shoff: E1 and E2 only (decoder-liveness r1 (2))')


def main():
    step(f'ke run apk_off={APK_OFF} apk_on={APK_ON}')'''),
    ('''        # E2 -- A ordinary, then the first transition
        mark('E2')''',
     '''        plan = os.environ.get('KE_PLAN', 'full')
        step(f'plan: {plan}')
        if plan == 'e2-shoff':
            e2_shoff()
            open(os.path.join(OUT, 'app_final.log'), 'w', encoding='utf-8').write(applog())
            return 0 if results and all(ok for _, ok, _ in results) else 1
        if plan != 'full':
            raise RuntimeError(f'unknown KE_PLAN: {plan}')

        # E2 -- A ordinary, then the first transition
        mark('E2')'''),
])
