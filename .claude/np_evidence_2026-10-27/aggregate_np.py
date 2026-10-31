# np-design r1 measurement: join server journal (per connect id) with PC viewer / APK race results.
# Inputs (this directory): journal_since_1022.txt, viewer_attempts.txt, apk_punch.txt
# Read-only analysis; prints tables to stdout.
import re, sys, collections, datetime as dt

HOSTS = {'f6e8e54a': 'homePC(.76)', '1c8ebffe': 'homePC2(.16)', 'df15b73b': 'officePC'}

def grp(ip):
    if ip.startswith('211.218.222.'): return 'office'
    if ip.startswith('175.207.45.') or ip.startswith('192.168.0.'): return 'home'
    return '.'.join(ip.split('.')[:2]) + '.x'

rec = collections.OrderedDict()
hb = collections.defaultdict(list)   # host -> [(ts, wire, public, aim, via)]
for line in open('journal_since_1022.txt', encoding='utf-8', errors='replace'):
    m = re.match(r'(\S+)\+09:00 \S+ node\[\d+\]: (.*)', line)
    if not m: continue
    ts = dt.datetime.fromisoformat(m.group(1)); body = m.group(2)
    h = re.match(r'\[host-hb\] host=(\w+) wire=(\S+) public=(\S+) aim=(\S+) via=(\w+)', body)
    if h:
        hb[h.group(1)].append((ts,) + h.groups()[1:]); continue
    c = re.match(r'\[(relay|capability|wake)\] connect=(\w+) (.*)', body)
    if not c: continue
    kind, cid, rest = c.groups()
    r = rec.setdefault(cid, {'id': cid, 'ts': ts})
    if kind == 'capability' and rest.startswith('minted'):
        r['host'] = re.search(r'host=(\w+)', rest).group(1)
    elif kind == 'relay' and rest.startswith('eligible'):
        e = re.search(r'client=(\S+):(\d+) wire=\S+ host=(.*)@(\S+):(\d+)', rest)
        r['client'], r['cport'], r['hname'], r['hip'], r['hport'] = e.groups()
    elif kind == 'relay' and rest.startswith('bound'):
        r['bound'] = 1
    elif kind == 'relay' and rest.startswith('active'):
        r['active'] = 1
    elif kind == 'wake' and rest.startswith('tx host='):
        r['wake'] = rest.split()[0][8:]

# PC viewer results by connect id
viewer = {}
for line in open('viewer_attempts.txt', encoding='utf-8', errors='replace'):
    m = re.search(r'connect=(\w+) punch answered=(\d) kind=(\w+) ms=(\d+)', line)
    if m: viewer[m.group(1)] = (m.group(3), int(m.group(4)))
# viewer 'directory chose ip:port' follows its punch line; relay port 29190 = relay (config snapshot)
chose = {}
last = None
for line in open('viewer_attempts.txt', encoding='utf-8', errors='replace'):
    m = re.search(r'connect=(\w+) punch answered', line)
    if m: last = m.group(1); continue
    m = re.search(r'directory chose (\S+):(\d+)', line)
    if m and last: chose[last] = m.group(2); last = None

# APK: directory_connect (host name, time) -> directory_chosen kind
apk = []
pend = None
for line in open('apk_punch.txt', encoding='utf-8', errors='replace'):
    m = re.match(r'(\S+ \S+) \[directory_connect\] host=(.*?) id=(\w{8})', line)
    if m: pend = (dt.datetime.fromisoformat(m.group(1)), m.group(3)); continue
    m = re.match(r'(\S+ \S+) \[directory_chosen\] (\S+):(\d+)\|(\w+)', line)
    if m and pend:
        t = dt.datetime.fromisoformat(m.group(1))
        apk.append((pend[0], pend[1], m.group(4), (t - pend[0]).total_seconds() * 1000)); pend = None

def apk_match(r):
    for t, h, kind, ms in apk:
        if h == r.get('host') and abs((t - r['ts']).total_seconds()) <= 3: return kind, ms
    return None

def port_preserved(host, ts):
    xs = [x for x in hb.get(host, []) if x[0] <= ts]
    if not xs: return '?'
    _, wire, pub, aim, via = xs[-1]
    return 'pp' if wire.split(':')[1] == aim.split(':')[1] else 'rw(%s->%s)' % (aim.split(':')[1], wire.split(':')[1])

rows = []
for r in rec.values():
    if 'client' not in r: continue
    out, src = None, None
    if r['id'] in viewer:
        out = 'relay' if chose.get(r['id']) == '29190' else 'direct'
        src = 'viewer'; ms = viewer[r['id']][1]
    else:
        a = apk_match(r)
        if a: out = 'relay' if a[0] == 'relay' else 'direct'; src = 'apk'; ms = a[1]
    rows.append(dict(r, out=out, src=src, ms=ms if out else None,
                     pp=port_preserved(r.get('host', ''), r['ts'])))

print('# per-connect (relay-eligible, since 2026-10-22 01:28:45 KST)')
print('ts | id | host | adv port | host NAT port | client grp | client port | bound | viewer/apk outcome | ms')
for x in rows:
    print(' | '.join(str(v) for v in [x['ts'].strftime('%m-%d %H:%M:%S'), x['id'], HOSTS.get(x.get('host'), x.get('host')),
          x['hport'], x['pp'], grp(x['client']), x['cport'], 'B' if x.get('bound') else '-',
          (x['src'] + ':' + x['out']) if x['out'] else 'n/a', x['ms'] if x['ms'] is not None else '']))

print('\n# summary host x client group x host-NAT-port (bound = relay chosen per server)')
s = collections.Counter(); b = collections.Counter()
for x in rows:
    k = (HOSTS.get(x.get('host'), x.get('host')), grp(x['client']), 'pp' if x['pp'] == 'pp' else 'rw')
    s[k] += 1; b[k] += 1 if x.get('bound') else 0
for k in sorted(s): print(k, 'connects=%d bound=%d' % (s[k], b[k]))

print('\n# labelled outcomes (viewer/apk) ms')
o = collections.defaultdict(list)
for x in rows:
    if x['out']: o[(HOSTS.get(x.get('host')), grp(x['client']), x['pp'] == 'pp', x['src'], x['out'])].append(round(x['ms']))
for k in sorted(o, key=str): print(k, len(o[k]), sorted(o[k]))

print('\n# host advertised-port history (changes only)')
for h, xs in hb.items():
    prev = None
    for ts, wire, pub, aim, via in xs:
        key = (wire, aim, via)
        if key != prev: print(HOSTS.get(h, h), ts.strftime('%m-%d %H:%M'), 'wire=' + wire, 'aim=' + aim, via); prev = key
