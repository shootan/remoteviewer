from pathlib import Path
import subprocess, hashlib, json, re
root=Path('D:/remote/remote')
out=root/'.claude/audit-20260912-direct'
paths=subprocess.check_output(['rg','--files','apps/native_poc','apps/directory','libs'],cwd=root,text=True).splitlines()
paths=sorted(p.replace('\\','/') for p in paths if Path(p).suffix in ('.cpp','.hpp','.h','.js','.html') or Path(p).name in ('CMakeLists.txt','package.json'))
patterns={
 'thread':r'std::thread|CreateThread|\.detach\(|\.join\(',
 'wait':r'WaitForSingleObject|ReadFile\(|WriteFile\(|recv_all\(|send_all\(|\.wait\(|\.wait_for\(',
 'sync':r'lock_guard|unique_lock|atomic<|PostMessage|SendMessage',
 'recovery':r'watchdog|restart|reconnect|reinitial|retry|starvation|device.?lost',
 'failure':r'return false|catch\s*\(|FAILED\(|TerminateProcess|Flow::Return',
}
rows=[]
for name in paths:
 data=(root/name).read_bytes(); text=data.decode('utf-8-sig',errors='replace')
 snap=out/'source'/name; snap.parent.mkdir(parents=True,exist_ok=True); snap.write_bytes(data)
 hits={k:[i for i,l in enumerate(text.splitlines(),1) if re.search(v,l,re.I)] for k,v in patterns.items()}
 rows.append(dict(path=name,sha256=hashlib.sha256(data).hexdigest(),lines=len(text.splitlines()),hits=hits))
manifest={'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(), 'files':rows}
(out/'source-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'files':len(rows),'lines':sum(r['lines'] for r in rows),'thread_files':sum(bool(r['hits']['thread']) for r in rows),'head':manifest['head']}))
for name in ('nas-host-local.log','nas-host-other.log','nas-viewer.log','nas-client.log'):
 p=out/name; lines=p.read_text(encoding='utf-8',errors='replace').splitlines()
 pats={'dxgi_wedge':'dxgi-worker-wedge','main_hang':'main-loop hang','starvation':'encoder-output-starvation','dead_close':'session-dead action=close','recv_stall':'recv-thread stalled','encode_fail':'encode failed count='}
 counts={k:sum(v in l for l in lines) for k,v in pats.items()}
 print(name,len(lines),hashlib.sha256(p.read_bytes()).hexdigest(),counts)
 events=[]
 for i,l in enumerate(lines):
  if 'dxgi-worker-wedge' in l:
   before=next((x[:18] for x in reversed(lines[:i]) if re.match(r'\d\d-\d\d \d\d:',x)), '?')
   after=next((x[:18] for x in lines[i+1:] if re.match(r'\d\d-\d\d \d\d:',x)), '?')
   events.append({'line':i+1,'preceding_timestamp':before,'following_timestamp':after,'message':l})
 (out/(name+'.events.json')).write_text(json.dumps(events,ensure_ascii=False,indent=2),encoding='utf-8')
 if events: print(json.dumps(events[-8:],ensure_ascii=False))
