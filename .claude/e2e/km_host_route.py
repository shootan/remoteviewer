"""keepalive-multi r1 §5: after a switch, input and settings go to the PC on screen only. Reads km_avd's windows
(result.json timing, epoch s) and both fixture hosts' logs ([input-trace] = a pointer event the host received,
runtime-config-applied = a quality request it applied).  python -I km_host_route.py <run_root>"""
import json, os, re, sys
from datetime import datetime
root = sys.argv[1]
tm = json.load(open(os.path.join(root, 'avd', 'result.json')))['timing']
def events(h):
    out = []
    for line in open(os.path.join(root, f'host_{h}', 'directoryhost_host.log'), encoding='utf-8', errors='replace'):
        m = re.match(r'(\d\d-\d\d \d\d:\d\d:\d\d\.\d+) ', line)
        if not m: continue
        ts = datetime.strptime('2026-' + m.group(1), '%Y-%m-%d %H:%M:%S.%f').timestamp()
        if '[input-trace]' in line: out.append((ts, 'input'))
        elif 'runtime-config-applied' in line: out.append((ts, 'config ' + re.search(r'bitrate=(\d+)', line).group(1)))
    return out
ev = {h: events(h) for h in 'ab'}
ok = True
for win, on, off in (('route_A', 'a', 'b'), ('route_B', 'b', 'a')):
    s, e = tm[win + '_start'], tm[win + '_end'] + 1.0
    got = {h: [x for t, x in ev[h] if s <= t <= e] for h in 'ab'}
    want_on = any(x == 'input' for x in got[on]) and (win != 'route_A' or 'config 6000000' in got[on])
    clean_off = not got[off]
    ok = ok and want_on and clean_off
    print(f'{win}: on-screen host_{on} {len([x for x in got[on] if x == "input"])} inputs {[x for x in got[on] if x != "input"]}'
          f' | parked host_{off} {got[off] or "nothing"}  -> {"PASS" if want_on and clean_off else "FAIL"}')
print('ROUTE:', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
