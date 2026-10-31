"""ke-r26: the two runs' traces on one time axis (UTC). app_final.log and the relay trace are UTC; the host log and
steps.txt are local (KST = UTC+9). Windows: E2 select (first frame) -> +60 s watch, and the whole run.
  python -I ke_r26_summary.py <run folder> ..."""
import datetime as dt
import re
import sys

KST = dt.timedelta(hours=9)


def utc(s, fmt):
    return dt.datetime.strptime(s, fmt)


for root in sys.argv[1:]:
    print('==', root.split('\\')[-1])
    steps = open(root + r'\avd\steps.txt', encoding='utf-8', errors='replace').read()
    day = '2026-10-29'
    m1 = re.search(r'^(\d\d:\d\d:\d\d) PASS  E2 A \(ordinary connect[^\n]*first frame\s+(\d+) ms', steps, re.M)
    w0 = utc(f'{day} {m1.group(1)}', '%Y-%m-%d %H:%M:%S') - KST
    w1 = w0 + dt.timedelta(seconds=61)
    print(f'  first frame at {m1.group(1)} KST ({m1.group(2)} ms after the pick); watch {w0:%H:%M:%S}-{w1:%H:%M:%S} UTC')
    for l in re.findall(r'^[^\n]*E2-SH[^\n]*$', steps, re.M)[:3]:
        print('  ' + l[:240])
    # app
    app = open(root + r'\avd\app_final.log', encoding='utf-8', errors='replace').read().splitlines()
    rows = []
    for l in app:
        m = re.match(r'(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) \[liveness_trace\] handle=[1-9]', l)
        if m:
            t = utc(m.group(1)[:23], '%Y-%m-%d %H:%M:%S.%f')
            rows.append((t, l))
    win = [l for t, l in rows if w0 <= t <= w1]

    def mx(pat, lines):
        v = [int(x) for l in lines for x in re.findall(pat, l)]
        return max(v) if v else None
    print(f'  app liveness_trace (session): {len(rows)} lines, {len(win)} in the watch')
    # r1 label correction (Codex codex_apk_decoder_inbox_decision_r1.md): retx = selective FRAGMENT retransmits only
    # (Tick's timeout resend of the head is not in it -- head attempts > 1 shows those); pend = rxPending (receive side);
    # wk pass = age of the worker's OUTER-loop snapshot (not a stall length); codecCall = the age of a call in progress
    # at a 500 ms sample (not a latency distribution); rx dmax = an undivided elapsed (lock wait + run + scheduling).
    for name, pat in (('rx dg age ms', r' dg=(\d+)ms'), ('rx ctl age ms', r' ctl=(\d+)ms'),
                      ('rx dmax ms (undivided elapsed)', r'dmax=(\d+)ms'),
                      ('wk outer-pass snapshot age ms (not a stall)', r'pass=(\d+)ms \|'),
                      ('ch fragment retransmits (NOT head timeout resends)', r' retx=(\d+)'), ('ch nack', r' nack=(\d+)'),
                      ('ch rxPending', r' pend=(\d+)'), ('ch txq', r' txq=(\d+)'),
                      ('ch head attempts (timeout resends of the head)', r'head=\d+/(\d+)'),
                      ('sink codecCall in-progress age at a sample ms', r'codecCall=(\d+)ms'), ('sink inbox', r'inbox=(\d+)')):
        print(f'    max {name}: watch={mx(pat, win)} session={mx(pat, [l for _, l in rows])}')
    closed = [l for _, l in rows if 'closed=1' in l]
    print(f'    closed=1 lines: {len(closed)}' + (f' first {closed[0][:120]}' if closed else ''))
    ends = [l for l in app if re.search(r'\[(?:keep_end|peer_lost|session_lost|connect_failed)\]', l)]
    print(f'  app end / lost lines: {len(ends)}' + ('' if not ends else ' first: ' + ends[0][:160]))
    # host a
    host = open(root + r'\a\directoryhost_host.log', encoding='utf-8', errors='replace').read().splitlines()
    kf_all, kf_win, lost, ended = 0, 0, [], []
    for l in host:
        m = re.match(r'(\d\d-\d\d \d\d:\d\d:\d\d\.\d+) ', l)
        t = utc('2026-' + m.group(1), '%Y-%m-%d %H:%M:%S.%f') - KST if m else None
        if 'keyframe-request seq=' in l:
            kf_all += 1
            kf_win += 1 if t and w0 <= t <= w1 else 0
        if re.search(r'peer.?lost', l, re.I):
            lost.append(l)
        if 'udp control session ended' in l:
            ended.append(l)
    reasons = {}
    for l in host:
        m = re.search(r'keyframe-request seq=\d+ reason=(\d+)', l)
        if m:
            reasons[m.group(1)] = reasons.get(m.group(1), 0) + 1
    print(f'  host a: keyframe-request lines total={kf_all} (whole run: startup + watch + list) in the 60 s watch={kf_win} '
          f'by wire reason={reasons}; peer-lost lines={len(lost)}')
    print('    (the app\'s kq counts Request(...).queued on SOME paths only -- not sends, not Host receipts)')
    for l in ended:
        print('    ' + l[:160])
    # relay
    srv = open(root + r'\dir\server.stdout.txt', encoding='utf-8', errors='replace').read().splitlines()
    snaps = [l for l in srv if l.startswith('[relay-trace]') and ' snap ' in l]
    errs = [l for l in snaps if re.search(r'sendErr c2h=[1-9]|h2c=[1-9]', l)]
    ages = {}
    for l in snaps:
        c = re.search(r'connect=(\w+)', l).group(1)
        st = re.search(r'state=(\w+)', l).group(1)
        a = [int(x) for x in re.findall(r'(?:c2hRx|h2cRx)=(\d+)', l)]
        ages.setdefault(c, []).append((st, a))
    print(f'  relay-trace: {len(snaps)} snapshots, socket send-callback errors>0 in {len(errs)} (not delivery / ACK); '
          f'connections={list(ages)}')
    other = [l for l in srv if l.startswith('[relay-trace]') and ' snap ' not in l]
    for l in other[:4]:
        print('    ' + l[:200])
print()
