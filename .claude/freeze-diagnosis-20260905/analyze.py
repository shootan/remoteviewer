from pathlib import Path
import re, collections, statistics, json
base = Path(__file__).parent
def read(name):
    return [(i,line) for i,line in enumerate((base/name).read_text(encoding='utf-8').splitlines(),1)]
def fields(line):
    return dict(re.findall(r'\b([A-Za-z][A-Za-z0-9_]*)=([^\s]+)',line))
def select(line, keys):
    d=fields(line)
    return ' '.join(k+'='+d[k] for k in keys.split() if k in d)
host=read('host-host.log.txt')
viewer=read('viewer-viewer.log.txt')
print('HOST 30s samples during latest session')
for i,line in host:
    if '09-05 15:44' <= line[:11] <= '09-05 15:53' and 'encodedFrames=' in line:
        print(i,line[:18],select(line,'encodedFrames sentFrames captureReadbackSamples captureUnmapWaitAvgUs captureUnmapWaitMaxUs captureD3DWaitAvgUs captureCopyMapAvgUs captureUnmapAvgUs oldestGpuPendingPeakUs mbps bitrateTarget fpsTarget senderQueueDrops senderHeldFrames keyReqTotal stampClampCount stampClampMaxUs'))
print('VIEWER minute summary latest session')
groups=collections.defaultdict(list)
for i,line in viewer:
    if '09-05 15:44' <= line[:11] <= '09-05 15:53' and 'recvFrames=' in line:
        groups[line[:11]].append(fields(line))
for minute,rows in groups.items():
    print(minute,'samples',len(rows),' '.join(k+'='+str(round(statistics.mean(float(d[k]) for d in rows if k in d),1)) for k in ['recvFrames','decodedFrames','avgLatencyUs','mbps','skippedQueued']), 'last', ' '.join(k+'='+rows[-1].get(k,'?') for k in ['congestionTransitions','congestionRecoveryCount','staleRefRecoveries','burstDrops']))
print('VIEWER significant event examples latest session')
counts=collections.Counter()
for i,line in viewer:
    if not ('09-05 15:44' <= line[:11] <= '09-05 15:53'): continue
    for category,pattern in [('congestion','congestion transition'),('catchup','catchup'),('assembly','assembly-discontinuity'),('keyrequest','request keyframe'),('waiting','waiting keyframe'),('burst','burst-drain')]:
        if pattern in line and 'recvFrames=' not in line:
            counts[category]+=1
            if counts[category] <= 28: print(category,i,line[:500])
print('counts',dict(counts))
print('TOP present gaps latest session')
gaps=[]
for i,line in viewer:
    if '09-05 15:44' <= line[:11] <= '09-05 15:53' and '[present]' in line:
        d=fields(line); gap=int(d.get('frameGapUs',0))
        if gap>500000: gaps.append((gap,i,line))
for gap,i,line in sorted(gaps,reverse=True)[:18]: print(i,line)
