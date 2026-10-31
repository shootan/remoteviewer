p = r'D:\remote\remote\.claude\caplife\ke_r26_summary.py'
s = open(p, encoding='utf-8').read()
reps = [
    ("""    for name, pat in (('rx dg age ms', r' dg=(\\d+)ms'), ('rx ctl age ms', r' ctl=(\\d+)ms'), ('rx dmax ms', r'dmax=(\\d+)ms'),
                      ('wk pass ms', r'pass=(\\d+)ms \\|'), ('ch retx', r' retx=(\\d+)'), ('ch nack', r' nack=(\\d+)'),
                      ('ch pend', r' pend=(\\d+)'), ('ch txq', r' txq=(\\d+)'), ('head attempts', r'head=\\d+/(\\d+)'),
                      ('sink codecCall ms', r'codecCall=(\\d+)ms'), ('sink inbox', r'inbox=(\\d+)')):""",
     """    # r1 label correction (Codex codex_apk_decoder_inbox_decision_r1.md): retx = selective FRAGMENT retransmits only
    # (Tick's timeout resend of the head is not in it -- head attempts > 1 shows those); pend = rxPending (receive side);
    # wk pass = age of the worker's OUTER-loop snapshot (not a stall length); codecCall = the age of a call in progress
    # at a 500 ms sample (not a latency distribution); rx dmax = an undivided elapsed (lock wait + run + scheduling).
    for name, pat in (('rx dg age ms', r' dg=(\\d+)ms'), ('rx ctl age ms', r' ctl=(\\d+)ms'),
                      ('rx dmax ms (undivided elapsed)', r'dmax=(\\d+)ms'),
                      ('wk outer-pass snapshot age ms (not a stall)', r'pass=(\\d+)ms \\|'),
                      ('ch fragment retransmits (NOT head timeout resends)', r' retx=(\\d+)'), ('ch nack', r' nack=(\\d+)'),
                      ('ch rxPending', r' pend=(\\d+)'), ('ch txq', r' txq=(\\d+)'),
                      ('ch head attempts (timeout resends of the head)', r'head=\\d+/(\\d+)'),
                      ('sink codecCall in-progress age at a sample ms', r'codecCall=(\\d+)ms'), ('sink inbox', r'inbox=(\\d+)')):"""),
    ("""    print(f'  relay-trace: {len(snaps)} snapshots, sendErr>0 in {len(errs)}; connections={list(ages)}')""",
     """    print(f'  relay-trace: {len(snaps)} snapshots, socket send-callback errors>0 in {len(errs)} (not delivery / ACK); '
          f'connections={list(ages)}')"""),
    ("""    print(f'  host a: keyframe-request total={kf_all} in the watch={kf_win} by reason={reasons}; peer-lost lines={len(lost)}')""",
     """    print(f'  host a: keyframe-request lines total={kf_all} (whole run: startup + watch + list) in the 60 s watch={kf_win} '
          f'by wire reason={reasons}; peer-lost lines={len(lost)}')
    print('    (the app\\'s kq counts Request(...).queued on SOME paths only -- not sends, not Host receipts)')"""),
]
for a, b in reps:
    assert s.count(a) == 1, a[:60]
    s = s.replace(a, b)
open(p, 'w', encoding='utf-8').write(s)
print('ok')
