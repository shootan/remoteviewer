p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_avd.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)


# --- docstring (r7 scope)
rep("""  host's window-select applied=1 for that id, and select_ready. Any other card (the Desktop card, id 0) is a FAIL.""",
    """  host's window-select applied=1 for that id, and select_ready. Any other card (the Desktop card, id 0) is a FAIL.
  r7 (Codex codex_old_apk_lane_pinned_scope_r7.md): the pick is tied end to end -- the ONE non-zero target THIS run's host
  listed, the app's requested id and selection gen, the host's applied id and streamGen (= the app's select_ack
  streamGen), select_ready of the same gen, then a frame out after it. A listed id 0 or an id the host's own lines do
  not agree on is refused with NO tap. 4 is the tap's own events: a button down then up on the host after it, inside
  the window's picture -- not a count of lines. The seam is never relaxed and id 0 is never redirected to the window.
  Scope: S window 3 4 + park 5 / reuse 6; P window 3 4, P-5d, P-6r (5 6 NOT RUN), 7 with P.""")

# --- judge_pick: generation and first video
old_judge_start = s.index('def judge_pick(app_since, host_since, pinned):')
old_judge_end = s.index('def targets_ui(name):')
s = s[:old_judge_start] + '''def judge_pick(app_since, host_since, pinned):
    """r7: (ok, detail) for one pick, from what the app and the host logged after it. PASS only if ALL hold:
    - the app's FIRST select_request is the pinned id on the Windows tab (its gen G)
    - the host's window-select of that id says applied=1 (its streamGen S) and no window-select was refused
    - the app's select_ack is gen G, streamGen S (the same selection on both ends)
    - select_ready of the pinned id and gen G, and a video_debug after it with out > 0 (the first picture)
    A frame after a request for another target (id 0 = the desktop) is NOT a pass."""
    reqs = re.findall(r'\\[select_request\\] targetId=(-?\\d+) label=(.*?) tab=(\\S+) gen=(\\d+)', app_since)
    acks = re.findall(r'\\[select_ack\\] targetId=(-?\\d+) gen=(\\d+) streamGen=(\\d+)', app_since)
    m_ready = re.search(r'\\[select_ready\\] targetId=(-?\\d+) gen=(\\d+)[^\\n]* ms=(-?\\d+)', app_since)
    failed = re.findall(r'\\[select_failed\\][^\\n]*', app_since)
    applied = re.findall(r'window-select seq=\\d+ requestedId=(\\d+) applied=(\\d)(?: selectedId=\\d+ streamGen=(\\d+))?', host_since)
    first = reqs[0] if reqs else None
    gen = int(first[3]) if first else None
    host_ok = [int(sg) for i, a, sg in applied if a == '1' and int(i) == pinned and sg]
    stream_gen = host_ok[0] if host_ok else None
    ack_ok = any(int(t) == pinned and int(g) == gen and int(sg) == stream_gen for t, g, sg in acks) if stream_gen else False
    ready_ok = bool(m_ready) and int(m_ready.group(1)) == pinned and int(m_ready.group(2)) == gen
    after = app_since[m_ready.end():] if m_ready else ''
    outs = [int(o) for o in re.findall(r'\\[video_debug\\][^\\n]* out=(\\d+)', after)]
    video_ok = any(o > 0 for o in outs)
    ok = (first is not None and int(first[0]) == pinned and first[2] == 'WINDOWS' and stream_gen is not None
          and not any(a != '1' for _, a, _ in applied) and ack_ok and ready_ok and video_ok and not failed)
    detail = (f'pinned={pinned} request={"targetId=%s tab=%s gen=%s" % (first[0], first[2], first[3]) if first else "none"}'
              f' host window-select={[(i, a, sg) for i, a, sg in applied][:3]} ack={acks[:2]}'
              f' ready={m_ready.group(0)[:90] if m_ready else "none"} video out after ready={outs[:3]}'
              + (f' failed={failed[0][:120]}' if failed else ''))
    return ok, detail


def window_size(hl):
    """r7: the window's picture size as the host encodes it (its last [keyframe] size=WxH)."""
    m = re.findall(r'\\[keyframe\\][^\\n]* size=(\\d+)x(\\d+)', hl)
    return (int(m[-1][0]), int(m[-1][1])) if m else None


def judge_input(host_since, size):
    """r7 (4): the tap's own events, from the host's [input-trace] lines after it (kind 1 move, 2 button down, 3 button
    up, 4 wheel; the PinnedSelectTest seam logs every pointer event, injection none): PASS only if a button down is
    followed by a button up (a higher seq), both inside the window's picture (0 <= x < W, 0 <= y < H). Lines that are
    not this pair (moves alone, an up without its down, a key) do not count."""
    ev = [(int(q), int(k), int(x), int(y)) for q, k, x, y in
          re.findall(r'\\[input-trace\\] seq=(\\d+) kind=(\\d+) x=(-?\\d+) y=(-?\\d+)', host_since)]
    inside = (lambda x, y: size is not None and 0 <= x < size[0] and 0 <= y < size[1])
    downs = [e for e in ev if e[1] == 2 and inside(e[2], e[3])]
    pair = None
    for d in downs:
        ups = [e for e in ev if e[1] == 3 and e[0] > d[0] and inside(e[2], e[3])]
        if ups:
            pair = (d, ups[0])
            break
    detail = (f'window={size} events={len(ev)} kinds={[e[1] for e in ev][:8]} '
              + (f'down seq={pair[0][0]} at {pair[0][2]},{pair[0][3]} -> up seq={pair[1][0]} at {pair[1][2]},{pair[1][3]}'
                 if pair else 'no down->up pair inside the window'))
    return pair is not None, detail


''' + s[old_judge_end:]

# --- pinned_target: an id 0 / disagreement refuses (unchanged logic), already returns None -> no tap. Screenshot name.
rep("    digest = shot(tag.split()[0] + '_first_frame')\n    vd = re.findall(r'\\[video_debug\\][^\\n]* out=(\\d+)', since(n0))\n    verdict(f'{tag}: the pinned window picked",
    "    digest = shot(tag.split()[0] + '_window_first_frame')\n    vd = re.findall(r'\\[video_debug\\][^\\n]* out=(\\d+)', since(n0))\n    verdict(f'{tag}: the pinned window picked")

# --- 4 as a function, judged on the tap's own events
rep('''        # 4: one tap on the picture -> the host's log says it arrived (injection none: logged, not injected)
        n_in = len(re.findall(r'\\[input-trace\\][^\\n]*', host_log()))
        ka.adb('shell', 'input', 'tap', '1200', '540')
        step('  one tap on the picture at 1200,540')
        time.sleep(3)
        traces = re.findall(r'\\[input-trace\\][^\\n]*', host_log())[n_in:]
        verdict('4 one input reaches the host (its [input-trace]; not injected: input none)', len(traces) > 0,
                f'new={len(traces)} ' + ' | '.join(traces[:3])[:240])
''', '''        # 4: one tap on the window's picture -> its own down/up on the host (injection none: logged, not injected)
        input_check()
''')

rep('''class Invalid(Exception):''', '''def input_check():
    """r7 (4): one tap on the window's picture; judged by judge_input on the host's lines after it."""
    hl = host_log()
    h0 = len(hl)
    ka.adb('shell', 'input', 'tap', '1200', '540')
    step('  one tap on the window picture at 1200,540')
    time.sleep(3)
    ok, detail = judge_input(host_log()[h0:], window_size(hl))
    verdict('4 the tap on the window reaches the host as its own button down -> up inside the window '
            '([input-trace]; not injected: input none)', ok, detail[:300])
    return ok


class Invalid(Exception):''')

# the first picture: a video_debug after select_ready (0.2.32 logs it every 1-3 s) -- waited for, bounded
rep("""    wait_log(r'\\[select_(?:ready|failed)\\][^\\n]*', n0, 30)
    time.sleep(1.0)
""", """    wait_log(r'\\[select_(?:ready|failed)\\][^\\n]*', n0, 30)
    if '[select_ready]' in since(n0):
        wait_log(r'\\[select_ready\\][\\s\\S]*\\[video_debug\\]', n0, 8)
    time.sleep(1.0)
""")

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
