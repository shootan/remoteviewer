p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_avd.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')

reps = [
    # docstring: what 3 / 6b / 6r pick
    ("""listDevicesButton, targetCardTitle, viewerMenuButton / viewerMenuHostsButton, loginManualButton / connectButton) and its""",
     """listWindowsButton / listDevicesButton, targetCardTitle, viewerMenuButton / viewerMenuHostsButton, loginManualButton /
connectButton) and its"""),
    ("""         (directory), 3 the desktop picked -> first frame (time, screenshot sha256), 4 one input reaches the host, 5 PC""",
     """         (directory), 3 the PINNED window picked -> first frame (time, screenshot sha256), 4 one input reaches the host, 5 PC"""),
    ("""         and 6r connect again (a new session) -> the desktop -> a picture; 7 as S.""",
     """         and 6r connect again (a new session) -> the pinned window -> a picture; 7 as S.
  r7 (verify-0164 S/P runs, both FAIL at 3): the host is the PinnedSelectTest seam -- it serves ONE window and refuses
  every other target by design (host_control_session.cpp:1699: windowId 0 = the desktop -> pinned_test_seam_refused). So
  3 / 6b / 6r pick that window: its id from the host's own log (capture target hwnd == window-list selectedId), the
  Windows tab, the one card whose title is that window's; judged on the app's select_request (that id, tab=WINDOWS), the
  host's window-select applied=1 for that id, and select_ready. Any other card (the Desktop card, id 0) is a FAIL."""),
]
for a, b in reps:
    assert s.count(a) == 1, a
    s = s.replace(a, b)

old_start = s.index('def pick_desktop(tag):')
old_end = s.index('class Invalid(Exception):')
new = '''def pinned_target(hl):
    """r7: the window the PinnedSelectTest host serves, from ITS log: (id, title, detail). The capture target's hwnd and
    the last window-list's selectedId must be the same non-zero id and the list must hold exactly that one window --
    else (None, None, why): nothing is tapped."""
    caps = re.findall(r'capture-window target hwnd=0x([0-9a-fA-F]+) pid=\\d+ process=\\S+ title=(.*?) clientOnly=', hl)
    lists = re.findall(r'\\[control\\] window-list seq=\\d+ count=(\\d+) selectedId=(\\d+)', hl)
    if not caps or not lists:
        return None, None, f'host log: capture target lines={len(caps)} window-list lines={len(lists)}'
    hwnd, title = int(caps[-1][0], 16), caps[-1][1].strip()
    count, sel = int(lists[-1][0]), int(lists[-1][1])
    detail = f'capture hwnd=0x{hwnd:x} ({hwnd}) window-list count={count} selectedId={sel} title={title!r}'
    if sel == 0 or sel != hwnd or count != 1 or not title:
        return None, None, 'not one agreed pinned window: ' + detail
    return sel, title, detail


def card_shows(text, title):
    """r7: a targetCardTitle shows the window's title, possibly cut with an ellipsis (0.2.33 on this AVD showed
    'remote60 mobile abr\u2026'): what it shows, without the ellipsis, is a non-trivial start of the title."""
    shown = text.rstrip().rstrip('\u2026').rstrip('.').rstrip()
    return len(shown) >= 8 and title.startswith(shown)


def judge_pick(app_since, host_since, pinned):
    """r7: (ok, detail) for one pick, from what the app and the host logged after it. PASS only if the app's FIRST
    select_request is the pinned id on the Windows tab, the host applied a window-select of that id (applied=1) and
    refused none, and the app logged select_ready for that id -- a frame after a request for another target (id 0 =
    the desktop) is NOT a pass."""
    reqs = re.findall(r'\\[select_request\\] targetId=(-?\\d+) label=(.*?) tab=(\\S+)', app_since)
    ready = re.findall(r'\\[select_ready\\] targetId=(-?\\d+)[^\\n]* ms=(-?\\d+)', app_since)
    failed = re.findall(r'\\[select_failed\\][^\\n]*', app_since)
    applied = re.findall(r'window-select seq=\\d+ requestedId=(\\d+) applied=(\\d)[^\\n]*', host_since)
    first = reqs[0] if reqs else None
    ok = (first is not None and int(first[0]) == pinned and first[2] == 'WINDOWS'
          and any(int(i) == pinned and a == '1' for i, a in applied) and not any(a != '1' for _, a in applied)
          and any(int(i) == pinned for i, _ in ready) and not failed)
    detail = (f'pinned={pinned} request={"targetId=%s tab=%s label=%r" % (first[0], first[2], first[1]) if first else "none"}'
              f' host window-select={applied[:3]} ready={[(i, ms) for i, ms in ready][:2]}'
              + (f' failed={failed[0][:120]}' if failed else ''))
    return ok, detail


def targets_ui(name):
    """r7: the targets screen as uiautomator shows it (ids, text, enabled, bounds), kept as <OUT>/<name>.txt -- the
    selectors' evidence from the real 0.2.32 UI."""
    nodes = ka.dump()
    lines = [f"{n['id'] or '-':<28} en={int(n['enabled'])} {n['bounds']} {n['text'][:60]!r}" for n in nodes
             if n['id'] or n['text']]
    open(os.path.join(OUT, name + '.txt'), 'w', encoding='utf-8').write('\\n'.join(lines) + '\\n')
    step(f'  ui {name}.txt: ' + ' | '.join(l for l in lines if l.startswith(('list', 'targetCardTitle')))[:300])
    return nodes


def pick_pinned(tag):
    """r7: the targets screen: the Windows tab, the pinned window's card -> select_ready (0.2.32's own 'the first
    picture shown'), judged by judge_pick."""
    n0 = cursor()
    hl0 = len(host_log())
    if not ka.wait('listWindowsButton', 30):
        verdict(f'{tag}: the targets screen is shown', False)
        return None
    pinned, title, why = None, None, ''
    end = time.time() + 15
    while time.time() < end:
        pinned, title, why = pinned_target(host_log())
        if pinned:
            break
        time.sleep(0.5)
    step(f'  {tag.split()[0]}: pinned window from the host log: {why}')
    if not pinned:
        verdict(f'{tag}: the host names one pinned window', False, why)
        return None
    tap('listWindowsButton')
    cards = []
    end = time.time() + 30
    while time.time() < end:
        nodes = ka.dump()
        cards = [n for n in nodes if n['id'] == 'targetCardTitle' and n['bounds'][2] > n['bounds'][0]]
        if any(card_shows(n['text'], title) for n in cards):
            break
        time.sleep(0.5)
    nodes = targets_ui(tag.split()[0] + '_targets_ui')
    cards = [n for n in nodes if n['id'] == 'targetCardTitle' and n['bounds'][2] > n['bounds'][0]]
    mine = [n for n in cards if card_shows(n['text'], title)]
    if len(mine) != 1 or not mine[0]['enabled']:
        verdict(f'{tag}: the pinned window\\'s card is listed on the Windows tab (exactly one, enabled)', False,
                f'cards={[n["text"] for n in cards]} pinned title={title!r}')
        return None
    x1, y1, x2, y2 = mine[0]['bounds']
    ka.adb('shell', 'input', 'tap', str((x1 + x2) // 2), str((y1 + y2) // 2))
    step(f'tapped the pinned card at {(x1 + x2) // 2},{(y1 + y2) // 2} text={mine[0]["text"]!r}')
    t0 = time.time()
    wait_log(r'\\[select_(?:ready|failed)\\][^\\n]*', n0, 30)
    time.sleep(1.0)
    ok, detail = judge_pick(since(n0), host_log()[hl0:], pinned)
    if not ok:
        verdict(f'{tag}: the pinned window picked (id {pinned}, Windows tab) -> first frame', False, detail[:400])
        return None
    time.sleep(0.5)
    digest = shot(tag.split()[0] + '_first_frame')
    vd = re.findall(r'\\[video_debug\\][^\\n]* out=(\\d+)', since(n0))
    verdict(f'{tag}: the pinned window picked (id {pinned}, Windows tab) -> first frame', True,
            f'{detail[:300]} (wall {time.time() - t0:.1f} s) screenshot={digest[:16]} video_debug out={vd[-1] if vd else "?"}')
    return digest


'''
s = s[:old_start] + new + s[old_end:]

for a, b in (
    ("if not pick_desktop('3 the desktop picked'):", "if not pick_pinned('3 the pinned window picked'):"),
    ("pick_desktop('6b the desktop again after the park')", "pick_pinned('6b the pinned window again after the park')"),
    ("pick_desktop('6r (P) the desktop again')", "pick_pinned('6r (P) the pinned window again')"),
    ("and 6r connect again (a new session) -> the desktop -> a picture", "and 6r connect again (a new session) -> the pinned window -> a picture"),
):
    if a in s:
        assert s.count(a) == 1, a
        s = s.replace(a, b)
assert 'pick_desktop' not in s
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
