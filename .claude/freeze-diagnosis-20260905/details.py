from pathlib import Path
import re, collections, statistics
b=Path(__file__).parent
rows=(b/'viewer-viewer.log.txt').read_text(encoding='utf-8').splitlines()
last=None; counts=collections.Counter(); others=[]; requests=[]
for i,s in enumerate(rows,1):
    if not ('09-05 15:44'<=s[:11]<='09-05 15:53'): continue
    if 'stage=assembly' in s: last=(i,s)
    if '[control] keyframe-request seq=' in s and 'reason=2' in s:
        counts['reason2']+=1; requests.append((i,s))
        if last and 'key=1' in last[1] and 'droppedPrev=1' in last[1] and i-last[0]<=3:
            counts['adjacent_completed_key_with_gap']+=1
        else: others.append((i,s,last))
print('LATEST REQUEST CORRELATION',dict(counts),'unmatched',others[:4])
print('KEY REQUEST NEIGHBORHOOD sample')
for i,s in requests[:2]:
    for j in range(i-3,i+3): print(j+1,rows[j][:500])
print('LOSS vs KEY REQUEST summaries (15:45 dense motion)')
for i,s in enumerate(rows,1):
    if '09-05 15:45:45'<=s[:14]<='09-05 15:46:06' and 'udp-assembly' in s: print(i,s)
host=(b/'host-host.log.txt').read_text(encoding='utf-8').splitlines()
print('HOST START HEAD')
for i,s in enumerate(host,1):
    if s.startswith('09-05 15:26:13'): print(i,s[:550])
print('HOST sample per-frame stage times')
ks='seq key captureToQueueUs captureUnmapWaitUs captureD3DWaitUs captureCopyMapUs captureUnmapUs captureMemcpyUs encUs scaleUs queueToEncodeUs preEncodePrepUs auCaptureUs encodeInputUs captureToAuSkewUs'.split()
for i,s in enumerate(host,1):
    if '09-05 15:48:20'<=s[:14]<='09-05 15:48:35' and '[user-feedback]' in s:
        d=dict(re.findall(r'\b(\w+)=([^\s]+)',s))
        print(i,s[:18],{k:d[k] for k in ks if k in d})
