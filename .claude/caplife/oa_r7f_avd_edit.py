L = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b):
    assert s.count(a) == 1, (a[:100], s.count(a))
    return s.replace(a, b)


# ---- oa_adb.dump: the selected / password flags too (read from the same dump)
p = L + r'\oa_adb.py'
s, crlf = load(p)
s = rep(s, """                    'clickable': n.get('clickable') == 'true',
""", """                    'clickable': n.get('clickable') == 'true',
                    'selected': n.get('selected') == 'true',  # r7f: a segment button's chosen state (View.isSelected)
                    'password': n.get('password') == 'true',  # r7f: its text is never recorded
""")
save(p, s, crlf)

# ---- oa_avd.py
p = L + r'\oa_avd.py'
s, crlf = load(p)

s = rep(s, """  Scope: S window 3 4 + park 5 / reuse 6; P window 3 4, P-5d, P-6r (5 6 NOT RUN), 7 with P.
""", """  Scope: S window 3 4 + park 5 / reuse 6; P window 3 4, P-5d, P-6r (5 6 NOT RUN), 7 with P.
  r7f (Codex codex_old_apk_lane_src_r7d.md (3)): both arms with SH REQUESTED off through 0.2.32's own Settings
  (listSettingsButton -> settingsSharpOffButton, b554e76 MainActivity.kt:531/5308) -- set after install + pm clear, seen
  in the UI dump and the app's own sharpness_select level=off origin=settings; at the picture sharpen_path path=direct
  requested=off and no sharpen_start (a weak request that merely fell back to effective=off is NOT this). A Settings
  control that cannot be used safely: the arm is NOT RUN (INVALID), never prefs / a hidden handler. After the first
  frame, 30 s with no input at all watching the viewer and the connection (3w); a break ends the arm there. Any FAIL is
  followed at once by a READ-ONLY record of the screen (ui dump with no password text, screenshot, logs). A failed step
  ends the arm: no further input (4, 5, 6a, 3w).
""")

s = rep(s, """def verdict(name, ok, detail=''):
    results.append((name, bool(ok), detail))
    step(('PASS  ' if ok else 'FAIL  ') + name + ('  ' + detail if detail else ''))
""", """def verdict(name, ok, detail=''):
    results.append((name, bool(ok), detail))
    step(('PASS  ' if ok else 'FAIL  ') + name + ('  ' + detail if detail else ''))
    if not ok:
        failure_record(name)


FAIL_RECORDS = []


def ui_record(name, nodes=None):
    \"\"\"r7f: the screen as uiautomator shows it, kept as <OUT>/<name>.txt -- READ ONLY (a dump, no input). A password
    field's text is never written (its flag or its id), and the fixture password is masked wherever it would appear.\"\"\"
    nodes = ka.dump() if nodes is None else nodes
    lines = []
    for n in nodes:
        if not (n['id'] or n['text']):
            continue
        text = n['text'][:80]
        if n.get('password') or 'password' in n['id'].lower():
            text = '<not recorded: a password field>'
        elif PW and PW in text:
            text = text.replace(PW, '<masked>')
        lines.append(f"{n['id'] or '-':<30} en={int(n['enabled'])} sel={int(n.get('selected', False))} {n['bounds']} {text!r}")
    open(os.path.join(OUT, name + '.txt'), 'w', encoding='utf-8').write('\\n'.join(lines) + '\\n')
    return lines


def failure_record(name):
    \"\"\"r7f: right after a FAIL -- what the screen says (ui dump, screenshot) and both logs as they are; READ ONLY.\"\"\"
    tag = f'fail{len(FAIL_RECORDS) + 1}'
    FAIL_RECORDS.append(tag)
    try:
        lines = ui_record(tag + '_ui')
        shot(tag + '_screen')
        open(os.path.join(OUT, tag + '_app.log'), 'w', encoding='utf-8').write(applog())
        try:
            open(os.path.join(OUT, tag + '_host.log'), 'w', encoding='utf-8').write(host_log())
        except Exception as e:  # noqa: BLE001 -- the record is best effort; it never changes a verdict
            step(f'  {tag}: no host log ({e})')
        shown = [l for l in lines if re.search(r"(?i)status|error|message|dialog|android:id/(message|alertTitle)", l)]
        step(f'  {tag} (after "{name[:60]}"): ui {tag}_ui.txt -- ' + (' | '.join(x.strip()[:120] for x in shown[:4]) or
                                                                      f'{len(lines)} elements, no status / error text'))
    except Exception as e:  # noqa: BLE001
        step(f'  {tag}: no record ({type(e).__name__}: {e})')
""")

s = rep(s, """def input_check():""", """SH_MARK = []  # r7f: the app log cursor right before Settings' 끄기 was tapped


def screen_box(nodes):
    boxes = [n['bounds'] for n in nodes if n['id'] == 'android:id/content']
    return boxes[0] if boxes else None


def sh_off(tag):
    \"\"\"r7f: SH REQUESTED off through 0.2.32's public Settings (on the targets screen: listSettingsButton, then the
    sharpening row's settingsSharpOffButton -> selectSharpness(OFF, "settings"), b554e76 MainActivity.kt:531, :5308).
    The control must be ON SCREEN (inside the app's content), enabled -- else the arm is NOT RUN (INVALID: no
    prefs edit, no hidden handler, no scrolling guess). Judged: the app's own `[sharpness_select] level=off ...
    origin=settings` and the button shown selected afterwards.\"\"\"
    t = tag.split()[0]
    tab = ka.wait('listSettingsButton', 20)
    if not tab or not tab['enabled']:
        raise Invalid(f'{tag}: the Settings tab is not shown enabled ({tab}) -- SH cannot be set through the UI')
    tap('listSettingsButton')
    ka.wait('settingsSharpOffButton', 10)
    nodes = ka.dump()
    ui_record(t + '_settings_ui', nodes)
    box = screen_box(nodes)
    offs = [n for n in nodes if n['id'] == 'settingsSharpOffButton' and n['bounds'][2] > n['bounds'][0]
            and n['bounds'][3] > n['bounds'][1]]
    inside = bool(offs) and box is not None and all(box[i] <= offs[0]['bounds'][i] for i in (0, 1)) and \\
        all(offs[0]['bounds'][i] <= box[i] for i in (2, 3))
    if len(offs) != 1 or not offs[0]['enabled'] or not inside:
        raise Invalid(f'{tag}: settingsSharpOffButton not usable on screen (found {len(offs)}, '
                      f'{offs[0]["bounds"] if offs else "-"} enabled={offs[0]["enabled"] if offs else "-"} screen={box}) '
                      f'-- SH not set; the arm is NOT RUN (no prefs edit, no other way)')
    n0 = cursor()
    SH_MARK[:] = [n0]
    x1, y1, x2, y2 = offs[0]['bounds']
    ka.adb('shell', 'input', 'tap', str((x1 + x2) // 2), str((y1 + y2) // 2))
    step(f'tapped settingsSharpOffButton at {(x1 + x2) // 2},{(y1 + y2) // 2} text={offs[0]["text"]!r}')
    m = wait_log(r'\\[sharpness_select\\][^\\n]*', n0, 10)
    after = ka.dump()
    ui_record(t + '_settings_ui_after', after)
    sel = [n for n in after if n['id'] == 'settingsSharpOffButton']
    line = m.group(0) if m else ''
    ok = bool(m) and ' level=off ' in line + ' ' and 'origin=settings' in line and bool(sel) and sel[0]['selected']
    verdict(f'{tag}: SH requested=off through Settings (settingsSharpOffButton; the app\\'s own sharpness_select level=off '
            'origin=settings; the button selected)', ok,
            f'{line[:160] or "no sharpness_select line"} | selected={sel[0]["selected"] if sel else "not shown"} '
            f'screen={box} button={offs[0]["bounds"]}')
    return ok


def sh_in_effect(tag):
    \"\"\"r7f: the picture runs with SH requested off -- since the Settings tap: a `[sharpen_path] path=direct
    requested=off` line, no sharpen path, no `[sharpen_start]`, no fallback of another request. effective=off alone (a
    weak request that fell back) is NOT this.\"\"\"
    log = since(SH_MARK[0]) if SH_MARK else ''
    paths = re.findall(r'\\[sharpen_path\\][^\\n]*', log)
    starts = re.findall(r'\\[sharpen_start\\][^\\n]*', log)
    falls = re.findall(r'\\[sharpen_fallback\\][^\\n]*', log)
    direct = [x for x in paths if 'path=direct' in x and 'requested=off' in x]
    other = [x for x in paths if x not in direct]
    ok = bool(SH_MARK) and bool(direct) and not other and not starts and not falls
    verdict(f'{tag}: SH off in effect for the picture (sharpen_path path=direct requested=off; sharpen_start 0)', ok,
            f'direct={[x[:90] for x in direct[:1]]} other={[x[:90] for x in other[:2]]} sharpen_start={len(starts)} '
            f'fallback={[x[:90] for x in falls[:1]]}')
    return ok


OBSERVE_SEC = 30


def observe_connection(tag):
    \"\"\"r7f: after the first frame, OBSERVE_SEC (30) s with NO input: the same viewer and connection throughout (no
    other scene, no non-connected status, no new pick, the host's session not ended, no new client) and the picture
    going on (video_debug out grows). A break ends the watch at once. Shorter only for the stub test.\"\"\"
    sec = float(os.environ.get('OA_OBSERVE_SEC', OBSERVE_SEC))
    if sec < OBSERVE_SEC and not os.environ.get('OA_TEST_FAKE_ADB'):
        raise Invalid(f'OA_OBSERVE_SEC={sec:g} is below {OBSERVE_SEC} s: for the stub test only')
    outs0 = [int(o) for o in re.findall(r'\\[video_debug\\][^\\n]* out=(\\d+)', since(PICK_MARK[0]))] if PICK_MARK else []
    out0 = outs0[-1] if outs0 else 0
    t0 = time.time()
    same, why = same_viewer()
    while same and time.time() - t0 < sec:
        time.sleep(min(2.0, max(0.1, sec / 15)))
        same, why = same_viewer()
    held = time.time() - t0
    outs = [int(o) for o in re.findall(r'\\[video_debug\\][^\\n]* out=(\\d+)', since(PICK_MARK[0]))] if PICK_MARK else []
    out1 = outs[-1] if outs else 0
    ok = same and held >= sec and out1 > out0
    verdict(f'{tag}: {sec:g} s with no input after the first frame -- the same viewer and connection held, the picture '
            'went on', ok, f'held {held:.1f} s; video out {out0} -> {out1}; {why}')
    return ok


def p_keyboard_clear(tag):
    \"\"\"r7f: arm P -- before its manual connect the keyboard must not be up; it is never put away there (no BACK / Enter
    on the connect form or in the viewer): a keyboard up or unknown is a FAIL, nothing is tapped.\"\"\"
    st, why = ime_state()
    ok = st == 'hidden'
    verdict(f'{tag}: no soft keyboard up before the manual connect (none put away)', ok, f'keyboard {st} {why}')
    return ok


def input_check():""")

# P: keyboard checks before both manual connects
s = rep(s, """    tap('loginManualButton')
    if not ka.wait('connectButton', 20):
        verdict('2 (P) the manual connect form is shown', False)
        return False
    tap('connectButton')""", """    tap('loginManualButton')
    if not ka.wait('connectButton', 20):
        verdict('2 (P) the manual connect form is shown', False)
        return False
    if not p_keyboard_clear('2k (P)'):  # r7f
        return False
    tap('connectButton')""")
s = rep(s, """    if not ka.wait('connectButton', 10):
        verdict('6r (P) the connect form is there to connect again', False)
        return False
    tap('connectButton')""", """    if not ka.wait('connectButton', 10):
        verdict('6r (P) the connect form is there to connect again', False)
        return False
    if not p_keyboard_clear('6k (P)'):  # r7f
        return False
    tap('connectButton')""")

# main: SH off after the targets (both arms), the SH check and the 30 s watch after 3; 5 / 6a FAIL end the arm
s = rep(s, """        # 3
        if not pick_pinned('3 the pinned window picked'):
            return 1
""", """        # r7f: SH requested off through Settings, on the targets screen, before anything is picked
        if not sh_off('2s SH off'):
            return 1

        # 3
        if not pick_pinned('3 the pinned window picked'):
            return 1
        if not sh_in_effect('3s SH off'):
            return 1
        # r7f: 30 s, no input, the connection watched; a break ends the arm here (no 4, nothing after)
        if not observe_connection('3w the connection'):
            return 1
""")
s = rep(s, """            verdict('5 PC 목록: 0.2.32\\'s park (park_begin, park_confirmed) -- the PC listed', listed and '[park_begin]' in log
                    and '[park_confirmed]' in log, ' | '.join(re.findall(r'\\[park_[a-z_]+\\][^\\n]*', log))[:300] +
                    f' | host sessions ended since: {ended}')
            shot('5_list')
            n0 = cursor()""", """            ok5 = listed and '[park_begin]' in log and '[park_confirmed]' in log
            verdict('5 PC 목록: 0.2.32\\'s park (park_begin, park_confirmed) -- the PC listed', ok5,
                    ' | '.join(re.findall(r'\\[park_[a-z_]+\\][^\\n]*', log))[:300] + f' | host sessions ended since: {ended}')
            shot('5_list')
            if not ok5:  # r7f: no further input after a failed step
                return 1
            n0 = cursor()""")
s = rep(s, """            verdict('6a the parked PC again: 0.2.32 reuses the parked session (park_reuse)', m is not None,
                    m.group(0)[:200] if m else ' | '.join(re.findall(r'\\[(?:park_[a-z_]+|directory_[a-z_]+)\\][^\\n]*', since(n0)))[:240])
            pick_pinned('6b the pinned window again after the park')""", """            verdict('6a the parked PC again: 0.2.32 reuses the parked session (park_reuse)', m is not None,
                    m.group(0)[:200] if m else ' | '.join(re.findall(r'\\[(?:park_[a-z_]+|directory_[a-z_]+)\\][^\\n]*', since(n0)))[:240])
            if m is None:  # r7f
                return 1
            pick_pinned('6b the pinned window again after the park')""")
# the exception path: a read-only ui record beside the screenshot
s = rep(s, """        try:
            shot('exception')
        except Exception as e:
            step(f'  no screenshot at the exception ({e})')""", """        try:
            shot('exception')
            ui_record('exception_ui')  # r7f: read only
        except Exception as e:
            step(f'  no screenshot / ui record at the exception ({e})')""")
save(p, s, crlf)
print('ok')
