from pathlib import Path
import re,datetime,hashlib,json,collections
r=Path('C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/runs/c3-shoff-v-20261030073538-28856')
out=Path(__file__).parent/'c3_evidence';out.mkdir(exist_ok=True)
files=['slot.txt','fixture_result.txt','avd/steps.txt','avd/result.json','avd/app_final.log','avd/logcat.txt','a/directoryhost_host.log','dir/server.stdout.txt']
hashes={n:hashlib.sha256((r/n).read_bytes()).hexdigest() for n in files}
steps=(r/'avd/steps.txt').read_text(encoding='utf-8-sig')
b=int(re.search(r'SEGMENT watch begin epoch_ms=(\d+)',steps)[1]);e=int(re.search(r'SEGMENT watch end epoch_ms=(\d+)',steps)[1])
rows=[]
for line in (r/'avd/app_final.log').read_text(encoding='utf-8-sig').splitlines():
 if '[video_debug]' not in line:continue
 t=int(datetime.datetime.strptime(line[:23],'%Y-%m-%d %H:%M:%S.%f').replace(tzinfo=datetime.timezone.utc).timestamp()*1000)
 if b<=t<=e:rows.append((t,dict(re.findall(r' (\w+)=([^\s]+)',line))))
f,l=rows[0][1],rows[-1][1]
delta=lambda k:int(l[k])-int(f[k])
want=lambda d:dict((k,int(v)) for k,v in re.findall(r'(\w+):(\d+)',d['keyWant']))
wf,wl=want(f),want(l)
want_delta={k:wl[k]-wf[k] for k in wf}
losses=[]
for line in (r/'avd/logcat.txt').read_text(encoding='utf-8-sig',errors='replace').splitlines():
 m=re.search(r'decoder inbox: chain loss at seq=(\d+) lost=(\d+) \(total (\d+)\).*episode=(\d+)',line)
 if m and int(f['inboxDrops'])<int(m[3])<=int(l['inboxDrops']):losses.append(tuple(map(int,m.groups())))
bulk=[x for x in losses if x[1]>1]
assert len(bulk)==len(set(bulk))==92 and all(x[1]==9 for x in bulk)
overflow_events=want_delta['ovf']-delta('gapDrops')
overflow_lost=delta('inboxDrops')-delta('gapDrops')-delta('chainTrim')
tail_events=overflow_events-len(bulk);tail_lost=overflow_lost-sum(x[1] for x in bulk)
assert (overflow_events,overflow_lost,tail_events,tail_lost)==(94,830,2,2)
assert delta('inboxRefused')==0
host=(r/'a/directoryhost_host.log').read_text(encoding='utf-8-sig')
keyreq=[];metrics=[]
for line in host.splitlines():
 if not re.match(r'\d\d-\d\d ',line):continue
 t=int(datetime.datetime.strptime('2026-'+line[:18],'%Y-%m-%d %H:%M:%S.%f').replace(tzinfo=datetime.timezone(datetime.timedelta(hours=9))).timestamp()*1000)
 if not b<=t<e:continue
 if 'keyframe-request seq=' in line:keyreq.append(int(re.search(r'keyframe-request seq=(\d+)',line)[1]))
 if 'encodedFrames=' in line:metrics.append(dict(re.findall(r'(\w+)=([^\s]+)',line)))
result={'run':str(r),'hashes':hashes,'segment_ms':e-b,'video_sample_span_ms':rows[-1][0]-rows[0][0],
 'video_rows':len(rows),'out_delta':delta('out'),'in_delta':delta('in'),'decoder_out_per_sec':delta('out')/((rows[-1][0]-rows[0][0])/1000),
 'out_monotonic':all(int(y[1]['out'])>=int(x[1]['out']) for x,y in zip(rows,rows[1:])),
 'host_key_requests':len(keyreq),'host_unique_key_requests':len(set(keyreq)),'keyWant_delta':want_delta,
 'drop_reconciliation':{'total':delta('inboxDrops'),'gap_subset':delta('gapDrops'),'markloss_trim_subset':delta('chainTrim'),
 'overflow_subset':overflow_lost,'overflow_events':overflow_events,'bulk_events':len(bulk),'bulk_lost':sum(x[1] for x in bulk),'tail_events':tail_events,'tail_lost':tail_lost},
 'host_config_sets':{k:sorted(set(x.get(k,'?') for x in metrics)) for k in ['fpsTarget','bitrateTarget','abrProfile','abrOverride','encoderResets']},
 'note':'Host encodedFrames/sentFrames are interval counters, not a monotonic cumulative series; no end-minus-start rate inferred.'}
(out/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:result[k] for k in ['decoder_out_per_sec','out_monotonic','host_unique_key_requests','drop_reconciliation','host_config_sets']},indent=2))
