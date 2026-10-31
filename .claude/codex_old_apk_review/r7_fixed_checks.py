from pathlib import Path
import ast, subprocess, hashlib, json, re

root=Path('C:/work/gnlink-wt/old-apk-lane')
verify=Path('D:/remote/remote/.claude/worktrees/verify-0164/automation/old_apk_lane')
rev='548d19911d85c503f4b650d8bfd5ac1e0afada19'
out=Path(__file__).parent/'r7_548'
out.mkdir(exist_ok=True)
rows=[l.split(maxsplit=1) for l in Path('D:/remote/remote/.claude/oa_lane_r7c_SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines() if l and not l.startswith('#')]
norm=lambda b:b.removeprefix(bytes.fromhex('efbbbf')).replace(b'\r\n',b'\n')
sha=lambda b:hashlib.sha256(b).hexdigest()
meta=[]
for expected,p in rows:
    b=subprocess.check_output(['git','-C',str(root),'show',rev+':automation/old_apk_lane/'+p])
    v=(verify/p).read_bytes()
    assert norm(b)==norm(v),p
    assert expected in {sha(b),sha(v),sha(norm(b)),sha(norm(b).replace(b'\n',b'\r\n'))},p
    f=out/'automation/old_apk_lane'/p
    f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b)
    meta.append({'path':p,'blob_sha256':sha(b),'verify_raw_sha256':sha(v),'normalized_equal':True})
(out/'review_hashes.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
p=out/'automation/old_apk_lane/oa_avd.py'
tree=ast.parse(p.read_text(encoding='utf-8-sig'))
names={'pinned_target','card_shows','judge_pick','window_size','judge_input'}
ns={'re':re}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[]),str(p),'exec'),ns)
pid=80809746
app=f'[select_request] targetId={pid} label=fixture tab=WINDOWS gen=7\n[select_ack] targetId={pid} gen=7 streamGen=42\n[select_ready] targetId={pid} gen=7 lastOutUs=8 ms=100\n[video_debug] in=1 out=1\n'
host=f'window-select seq=9 requestedId={pid} applied=1 selectedId={pid} streamGen=42\n'
checks=[]
def check(name,ok):
    checks.append({'name':name,'ok':bool(ok)})
    print(('PASS ' if ok else 'FAIL ')+name)
check('selection complete current id/gen/stream/video',ns['judge_pick'](app,host,pid)[0])
check('id0 request rejected',not ns['judge_pick'](app.replace(f'targetId={pid} label','targetId=0 label'),host,pid)[0])
check('wrong ACK stream rejected',not ns['judge_pick'](app.replace('streamGen=42','streamGen=41'),host,pid)[0])
check('wrong ready gen rejected',not ns['judge_pick'](app.replace(f'[select_ready] targetId={pid} gen=7',f'[select_ready] targetId={pid} gen=8'),host,pid)[0])
check('no output after ready rejected',not ns['judge_pick'](app.replace('out=1','out=0'),host,pid)[0])
mk=lambda seq,k,x,y:f'[input-trace] seq={seq} kind={k} x={x} y={y}\n'
j=ns['judge_input']
check('down/up same point inside accepted',j(mk(1,2,100,100)+mk(2,3,100,100),(480,270))[0])
check('modifier releases alone rejected',not j(mk(1,6,0,0),(480,270))[0])
check('up alone rejected',not j(mk(1,3,100,100),(480,270))[0])
check('down alone rejected',not j(mk(1,2,100,100),(480,270))[0])
check('moved pair rejected',not j(mk(1,2,100,100)+mk(2,3,120,100),(480,270))[0])
check('first invalid pair cannot hide behind later valid pair',not j(mk(1,2,900,900)+mk(2,3,900,900)+mk(3,2,100,100)+mk(4,3,100,100),(480,270))[0])
check('unknown picture size rejected',not j(mk(1,2,100,100)+mk(2,3,100,100),None)[0])
(out/'checks.json').write_text(json.dumps(checks,indent=2),encoding='utf-8')
assert all(x['ok'] for x in checks)
print(f'{len(checks)} checks, 0 failed; snapshot {len(meta)} files pinned to {rev}')
