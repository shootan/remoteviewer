p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_avd.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


# the pick leaves its mark: where the app's and the host's logs were when the pinned window was on screen
rep('''    verdict(f'{tag}: the pinned window picked (id {pinned}, Windows tab) -> first frame', True,''',
    '''    PICK_MARK[:] = [cursor(), len(host_log()), pinned]  # r8: what "the same viewer, the same connection" is measured from
    verdict(f'{tag}: the pinned window picked (id {pinned}, Windows tab) -> first frame', True,''')

rep('''def viewer_tap_point():''', '''PICK_MARK = []  # [app log cursor, host log length, pinned id] at the last successful pick


def same_viewer():
    """r8 (Codex codex_old_apk_lane_ime_status_r8.md): still the viewer and the connection of the last pick -- since the
    pick the app logged no scene other than VIEWER, no `[status]` that is not connected, no new select_request; the
    host logged no `udp control session ended` / new `client connected`. -> (ok, detail)."""
    if not PICK_MARK:
        return False, 'no pick to compare with'
    app = since(PICK_MARK[0])
    hl = host_log()[PICK_MARK[1]:]
    scenes = re.findall(r'\\[scene\\] (\\S+)', app)
    status = [st for st in re.findall(r'\\[status\\] ([^\\n]*)', app) if not st.startswith('connected')]
    picks = re.findall(r'\\[select_request\\][^\\n]*', app)
    ended = re.findall(r'udp control session ended[^\\n]*', hl)
    connected = re.findall(r'client connected[^\\n]*', hl)
    ok = all(sc == 'VIEWER' for sc in scenes) and not status and not picks and not ended and not connected
    return ok, (f'since the pick: scenes={scenes[:3]} status={[x[:30] for x in status[:2]]} picks={len(picks)} '
                f'host ended={[x[:60] for x in ended[:1]]} host new clients={len(connected)}')


def viewer_tap_point():''')

rep('''    st, why = ime_state()
    if st != 'hidden':
        return None, f'soft keyboard {st} {why}'
    views =''', '''    same, swhy = same_viewer()
    if not same:
        return None, f'not the same viewer / connection: {swhy}'
    st, why = ime_state()
    if st != 'hidden':
        return None, f'soft keyboard {st} {why}'
    views =''')

# 4 failing ends the arm there: no menu wake tap, no next case
rep('''        # 4: one tap on the window's picture -> its own down/up on the host (injection none: logged, not injected)
        input_check()
''', '''        # 4: one tap on the window's picture -> its own down/up on the host (injection none: logged, not injected)
        # r8: a FAIL here ends the arm -- no menu wake tap, no 5 / 6 / P-5d (they are NOT RUN)
        if not input_check():
            return 1
''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
