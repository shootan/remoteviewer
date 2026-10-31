import re, statistics, sys
L = 'D:/remote/remote/.claude/field-20260907/host_local_1413_1436.log'
lines = open(L, encoding='utf-8', errors='replace').read().splitlines()

def ts(line):
    m = re.search(r'09-07 (\d\d):(\d\d):(\d\d)\.(\d\d\d)', line)
    if not m: return None
    h, mi, s, ms = map(int, m.groups())
    return h*3600 + mi*60 + s + ms/1000.0

def kv(line):
    return {k: v for k, v in re.findall(r'(\w+)=([-\w.:]+)', line)}

periods = [
    ('A static+typing 14:15:00-14:24:30', 14*3600+15*60, 14*3600+24*60+30),
    ('B video 14:25:00-14:26:10', 14*3600+25*60, 14*3600+26*60+10),
    ('B2 transitions 14:26:10-14:27:20', 14*3600+26*60+10, 14*3600+27*60+20),
    ('C UAC 14:27:20-14:28:40', 14*3600+27*60+20, 14*3600+28*60+40),
    ('C2 post-UAC 14:28:40-14:30:30', 14*3600+28*60+40, 14*3600+30*60+30),
    ('D reconnects 14:30:30-14:33:40', 14*3600+30*60+30, 14*3600+33*60+40),
]

def pct(vals, p):
    if not vals: return None
    vals = sorted(vals)
    k = max(0, min(len(vals)-1, int(round((p/100.0)*(len(vals)-1)))))
    return vals[k]

def fmt(vals):
    if not vals: return 'n=0'
    return f'n={len(vals)} med={pct(vals,50)} p95={pct(vals,95)} max={max(vals)}'

print('== wire seq per period (host send path) ==')
for name, a, b in periods:
    ints, qw, sd, keys, bytes_, frames = [], [], [], 0, [], 0
    for ln in lines:
        if 'wire seq=' not in ln: continue
        t = ts(ln)
        if t is None or t < a or t >= b: continue
        d = kv(ln)
        frames += 1
        if d.get('key') == '1': keys += 1
        for key, lst in (('wireIntUs', ints), ('queueWaitUs', qw), ('sendDurUs', sd), ('bytes', bytes_)):
            try: lst.append(int(d[key]))
            except (KeyError, ValueError): pass
    print(f'{name}: frames={frames} keys={keys} bytes med={pct(bytes_,50)} p95={pct(bytes_,95)}')
    print(f'   wireIntUs {fmt(ints)}')
    print(f'   queueWaitUs {fmt(qw)}  sendDurUs {fmt(sd)}')

print('== user-feedback per period (host capture->publish attribution) ==')
fields = ['pipeUs', 'captureToQueueUs', 'captureUnmapWaitUs', 'captureUnmapUs', 'workerCtxWaitUs', 'workerD3dCallUs', 'encUs', 'queueWaitUs', 'sendDurUs', 'bottleneckStageUs']
for name, a, b in periods:
    rows = []
    names = {}
    for ln in lines:
        if 'user-feedback' not in ln: continue
        t = ts(ln)
        if t is None or t < a or t >= b: continue
        d = kv(ln)
        rows.append(d)
        names[d.get('bottleneckStageName','?')] = names.get(d.get('bottleneckStageName','?'), 0) + 1
    print(f'{name}: n={len(rows)} bottleneck={names}')
    for f in fields:
        vals = []
        for d in rows:
            try: vals.append(int(d[f]))
            except (KeyError, ValueError): pass
        if vals: print(f'   {f}: {fmt(vals)}')

print('== 30s stats: readback / d3d-mt series ==')
for ln in lines:
    if 'encodedFrames=' not in ln: continue
    d = kv(ln)
    m = re.search(r'09-07 (\d\d:\d\d:\d\d)', ln)
    print(m.group(1), 'enc=%s' % d.get('encodedFrames'), 'unmapWaitAvg=%s' % d.get('captureUnmapWaitAvgUs'),
          'unmapWaitMax=%s' % d.get('captureUnmapWaitMaxUs'), 'gpuPendPeak=%s' % d.get('oldestGpuPendingPeakUs'),
          'ctxWaitAvg=%s' % d.get('workerCtxWaitAvgUs'), 'd3dCallAvg=%s' % d.get('workerD3dCallAvgUs'),
          'd3dCallMax=%s' % d.get('workerD3dCallMaxUs'), 'd3dMt=%s' % d.get('d3dMt'), 'clamp=%s' % d.get('stampClampCount'),
          'nackReq=%s' % d.get('nackRequests'), 'nackRetx=%s' % d.get('nackRetransmitChunks'), 'nackMiss=%s' % d.get('nackMisses'),
          'nackSupp=%s' % d.get('nackSuppressed'), 'senderDrops=%s' % d.get('senderQueueDrops'), 'held=%s' % d.get('senderHeldFrames'),
          'kf=%s' % d.get('keyframes'), 'abr=%s/%s' % (d.get('abrModSec'), d.get('abrSevSec')))

print('== any nack fields present at all? ==')
for ln in lines:
    if 'nack' in ln.lower():
        print(re.findall(r'nack\w*=\S+', ln, flags=re.I)[:8]); break
else:
    print('no nack fields in host log window')
