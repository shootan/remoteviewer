"""keepalive-multi r2 V1: each fixture host's own connection timeline (client connected / control session ended /
park stream-state) against the AVD run's marks -- who connected when, so 'B's late answer adopted nothing', 'X never
saw a client', 'A ended at D's commit' are read from the hosts, not the app.
  python -I km2_hosts.py <run_root>"""
import json
import os
import re
import sys
from datetime import datetime

root = sys.argv[1]
res = json.load(open(os.path.join(root, 'avd', 'result.json')))
marks = res.get('marks', {})


def ts(s):
    return datetime.strptime('2026-' + s, '%Y-%m-%d %H:%M:%S.%f').timestamp()


def hm(t):
    return datetime.fromtimestamp(t).strftime('%H:%M:%S')


for m, t in sorted(marks.items(), key=lambda kv: kv[1]):
    print(f'mark {hm(t)} {m}')
for h in ('a', 'b', 'c', 'd', 'x', 'old'):
    p = os.path.join(root, f'host_{h}', 'directoryhost_host.log')
    if not os.path.exists(p):
        print(f'== host_{h}: no log')
        continue
    ev = []
    for line in open(p, encoding='utf-8', errors='replace'):
        m = re.match(r'(\d\d-\d\d \d\d:\d\d:\d\d\.\d+) ', line)
        if not m:
            continue
        t = ts(m.group(1))
        if 'client connected' in line:
            ev.append((t, 'connected'))
        elif 'control session ended' in line:
            ev.append((t, 'ended ' + (re.search(r'reason=(\S+)', line).group(1) if 'reason=' in line else '')))
        elif 'peer replaced' in line or 'replacing' in line.lower():
            ev.append((t, 'replaced: ' + line.strip()[24:110]))
    print(f'== host_{h}: ' + ', '.join(f'{hm(t)} {e}' for t, e in ev))
