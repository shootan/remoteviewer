"""keepalive-multi r1: from a fixture host's own log -- its connections, and every parked interval (stream-state
active=0 after the stream had been active, until active=1 or the session's end): after a 2 s tail, encoded frames
and video datagrams sent (must be 0), control bytes/s (the cost of keeping it)."""
import re, sys
from datetime import datetime

def t(s): return datetime.strptime('2026-' + s, '%Y-%m-%d %H:%M:%S.%f').timestamp()

for path in sys.argv[1:]:
    stats, events = [], []
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'(\d\d-\d\d \d\d:\d\d:\d\d\.\d+) ', line)
        if not m: continue
        ts = t(m.group(1))
        if 'encodedFrames=' in line and 'udpTxVideoDatagrams=' in line:
            g = lambda k: int(re.search(k + r'=(\d+)', line).group(1))
            stats.append((ts, g('encodedFrames'), g('udpTxVideoDatagrams'), g('ctlTxBytes'), g('ctlTxDatagrams')))
        elif '[control] stream-state' in line:
            events.append((ts, 'active' if 'active=1' in line else 'paused'))
        elif 'client connected' in line or 'control session ended' in line:
            events.append((ts, 'connected' if 'connected' in line else 'ended'))
    print(f'== {path}')
    print('  connections:', sum(1 for _, e in events if e == 'connected'), ' sessions ended:', [datetime.fromtimestamp(x).strftime('%H:%M:%S') for x, e in events if e == 'ended'])
    end = stats[-1][0] if stats else 0
    seen_active = False
    ok = True
    for i, (ts, e) in enumerate(events):
        if e == 'active': seen_active = True
        if e != 'paused' or not seen_active: continue
        nxt = next((x for x, ee in events[i + 1:] if ee in ('active', 'ended')), end)
        win = [s for s in stats if ts + 2.0 <= s[0] <= nxt]
        if len(win) < 2:
            print(f'  park {datetime.fromtimestamp(ts):%H:%M:%S}..{datetime.fromtimestamp(nxt):%H:%M:%S}: too short to measure'); continue
        enc = sum(s[1] for s in win[1:])
        vid = win[-1][2] - win[0][2]
        dur = win[-1][0] - win[0][0]
        cb = (win[-1][3] - win[0][3]) / dur
        cd = (win[-1][4] - win[0][4]) / dur
        ok = ok and enc == 0 and vid == 0
        print(f'  park {datetime.fromtimestamp(ts):%H:%M:%S}..{datetime.fromtimestamp(nxt):%H:%M:%S} steady {dur:.0f}s: '
              f'encoded={enc} videoDatagrams={vid} control={cb:.0f} B/s {cd:.2f} dgram/s')
    print('  STEADY PARK: no encode, no video sent' if ok else '  STEADY PARK: VIDEO SENT')
