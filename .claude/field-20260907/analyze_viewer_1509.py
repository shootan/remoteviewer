import re
V = 'D:/remote/remote/.claude/field-20260907/viewer_1509_1519.log'
H = 'D:/remote/remote/.claude/field-20260907/host_local_1509_1519.log'
vl = open(V, encoding='utf-8', errors='replace').read().splitlines()
hl = open(H, encoding='utf-8', errors='replace').read().splitlines()

def ts(line):
    m = re.search(r'09-07 (\d\d):(\d\d):(\d\d)(?:\.(\d\d\d))?', line)
    if not m: return None
    h, mi, s = int(m.group(1)), int(m.group(2)), int(m.group(3))
    ms = int(m.group(4)) if m.group(4) else 0
    return h*3600 + mi*60 + s + ms/1000.0

def kv(line):
    return {k: v for k, v in re.findall(r'(\w+)=([-\w.:]+)', line)}

def T(h, m, s): return h*3600 + m*60 + s

periods = [
    ('P0 session start 15:09:57-15:10:30', T(15,9,57), T(15,10,30)),
    ('P1 typing/static 15:10:30-15:14:20', T(15,10,30), T(15,14,20)),
    ('P2 video (approx) 15:14:20-15:15:12', T(15,14,20), T(15,15,12)),
    ('P3 static after video 15:15:12-15:15:50', T(15,15,12), T(15,15,50)),
    ('P4 UAC 15:15:50-15:16:20', T(15,15,50), T(15,16,20)),
    ('P5 after UAC 15:16:20-15:17:42', T(15,16,20), T(15,17,42)),
]

def pct(vals, p):
    if not vals: return None
    vals = sorted(vals)
    return vals[max(0, min(len(vals)-1, int(round((p/100.0)*(len(vals)-1)))))]
def fmt(vals):
    return 'n=0' if not vals else f'n={len(vals)} med={pct(vals,50)} p95={pct(vals,95)} max={max(vals)}'

print('== viewer 1-s stats lines (recvFrames=) per period: sums of recv/decoded/synthetic/skipped, avgLatency distribution, states ==')
for name, a, b in periods:
    recv = dec = syn = skip = 0; lat = []; states = {}; retries = set(); stale = set(); hold = set(); burst = set(); n = 0
    keyResyncMax = 0
    for ln in vl:
        if 'recvFrames=' not in ln: continue
        t = ts(ln)
        if t is None or t < a or t >= b: continue
        d = kv(ln); n += 1
        recv += int(d.get('recvFrames', 0)); dec += int(d.get('decodedFrames', 0)); syn += int(d.get('syntheticFrames', 0)); skip += int(d.get('skippedQueued', 0))
        if int(d.get('decodedFrames', 0)) - int(d.get('syntheticFrames', 0)) > 0: lat.append(int(d.get('avgLatencyUs', 0)))
        states[d.get('congestionState')] = states.get(d.get('congestionState'), 0) + 1
        retries.add(d.get('keyRetries')); stale.add(d.get('staleRefRecoveries')); hold.add(d.get('holdLatestDrops')); burst.add(d.get('burstDrops'))
    dur = b - a
    print(f'{name}: lines={n} recv={recv} decoded={dec} synthetic={syn} skipped={skip} -> recv/s={recv/dur:.1f} decoded/s={dec/dur:.1f} real/s={(dec-syn)/dur:.1f}')
    print(f'   avgLatencyUs(real frames, per-second avg) {fmt(lat)}; states={states}; keyRetries={sorted(retries)} staleRefRecoveries={sorted(stale)} holdLatestDrops={sorted(hold)} burstDrops={sorted(burst)}')

print('== udp-assembly per period: sums ==')
for name, a, b in periods:
    tot = {}
    pend = []
    for ln in vl:
        if 'udp-assembly' not in ln: continue
        t = ts(ln)
        if t is None or t < a or t >= b: continue
        d = kv(ln)
        for k in ('chunks','completed','dropped','malformed','reorder','keyReq','keyResync','fecRecovered','nackSent','nackChunks'):
            tot[k] = tot.get(k, 0) + int(d.get(k, 0))
        pend.append(int(d.get('pending', 0)))
        tot['nackOn'] = d.get('nackOn'); tot['nackExhausted'] = d.get('nackExhausted')
    print(f'{name}: {tot} pending max={max(pend) if pend else None}')

print('== present gap per period ([present] seq= frameGapUs=) ==')
for name, a, b in periods:
    gaps = []
    for ln in vl:
        if '[present] seq=' not in ln: continue
        t = ts(ln)
        if t is None or t < a or t >= b: continue
        d = kv(ln)
        try: gaps.append(int(d['frameGapUs']))
        except: pass
    print(f'{name}: presents {fmt(gaps)}')

print('== control RTT per period ([control] seq= rttUs=) ==')
for name, a, b in periods:
    r = []
    for ln in vl:
        if '[control] seq=' not in ln or 'rttUs=' not in ln: continue
        t = ts(ln)
        if t is None or t < a or t >= b: continue
        d = kv(ln)
        try: r.append(int(d['rttUs']))
        except: pass
    print(f'{name}: rttUs {fmt(r)}')

print('== viewer user-feedback (viewer-local fields only) per period ==')
for name, a, b in periods:
    rows = []
    for ln in vl:
        if '[user-feedback]' not in ln: continue
        t = ts(ln)
        if t is None or t < a or t >= b: continue
        rows.append(kv(ln))
    if not rows: print(f'{name}: n=0'); continue
    out = []
    for f in ('capGapUs','queueToPaintUs','queueToPresentUs','r2dUs','decUs','d2pUs','decodeToQueueUs','queueWaitUs','paintUs','c2eUs','encUs','e2sUs'):
        vals = []
        for d in rows:
            try: vals.append(int(d[f]))
            except: pass
        if vals: out.append(f'{f} med={pct(vals,50)} p95={pct(vals,95)}')
    print(f'{name}: n={len(rows)} ' + ' | '.join(out))

print('== event lines (keyframe-request / recovery retry / congestion / stale / catchup / liveness / video-nack / telemetry key) ==')
for ln in vl:
    if re.search(r'keyframe-request seq|recovery retry|\[congestion\]|stale-reference|catchup enter|catchup exit|\[liveness\]|video-nack|stage=assembly .*key=1|hello ack|directory chose|control connected|\[native-video-client\] done|session-dead|decode failed|decoder wedged|H264 decoder', ln):
        print(re.sub(r'^\d+:', '', ln)[:230])

print('== host 30s stats 15:09-15:19 (readback/d3dMt/udpTx) ==')
prev = None
for ln in hl:
    if 'encodedFrames=' not in ln: continue
    d = kv(ln); m = re.search(r'09-07 (\d\d:\d\d:\d\d)', ln)
    tx = int(d.get('udpTxFrames', 0)); by = int(d.get('udpTxBytes', 0))
    delta = f'dTx={tx-prev[0]} dBytes={by-prev[1]}' if prev else ''
    prev = (tx, by)
    print(m.group(1), 'unmapWaitAvg=%s' % d.get('captureUnmapWaitAvgUs'), 'd3dCallAvg=%s' % d.get('workerD3dCallAvgUs'), 'd3dMt=%s' % d.get('d3dMt'), 'udpTxFrames=%s' % tx, delta, 'held=%s' % d.get('senderHeldFrames'), 'keyReqTotal=%s' % d.get('keyReqTotal'))
