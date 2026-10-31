from pathlib import Path
import ast,subprocess,re,json,hashlib,difflib

repo=Path('C:/work/gnlink-wt/old-apk-lane');rev='b11fc0d40b49e086ed3df9bcbfcaa467fd7318a2'
root=Path(__file__).parent/'r7g_b11';root.mkdir(exist_ok=True)
raw=subprocess.check_output(['git','-C',str(repo),'show',rev+':automation/old_apk_lane/oa_avd.py'])
source=raw.decode('utf-8')
(root/'oa_avd_original.py').write_bytes(raw)
old=r"""    outs0 = [int(o) for o in re.findall(r'\[video_debug\][^\n]* out=(\d+)', since(PICK_MARK[0]))] if PICK_MARK else []
    out0 = outs0[-1] if outs0 else 0
    t0 = time.time()
"""
new=r'''    ok0, before = applog_snapshot()
    outs0 = [int(o) for o in re.findall(r'\[video_debug\][^\n]* out=(\d+)', before)] if ok0 else []
    if not ok0 or not PICK_MARK or PICK_MARK[0] not in before or not outs0:
        verdict(f'{tag}: a readable current-pick video counter exists before the watch', False,
                'no current snapshot / pick marker / video out baseline; nothing judged as progressing')
        return False
    out0 = outs0[-1]
    watch_marker = [l for l in before.splitlines() if l.strip()][-1]
    t0 = time.monotonic()
'''
assert source.count(old)==1,repr(old)
fixed=source.replace(old,new)
fixed=fixed.replace('while same and time.time() - t0 < sec:', 'while same and time.monotonic() - t0 < sec:')
old_end=r'''    held = time.time() - t0
    outs = [int(o) for o in re.findall(r'\[video_debug\][^\n]* out=(\d+)', since(PICK_MARK[0]))] if PICK_MARK else []
    out1 = outs[-1] if outs else 0
    ok = same and held >= sec and out1 > out0
'''
new_end=r'''    held = time.monotonic() - t0
    ok1, after = applog_snapshot()
    marker_at = after.rfind(watch_marker) if ok1 else -1
    tail = after[marker_at + len(watch_marker):] if marker_at >= 0 else ''
    outs = [int(o) for o in re.findall(r'\[video_debug\][^\n]* out=(\d+)', tail)]
    out1 = outs[-1] if outs else out0
    values = [out0] + outs
    comparable = all(b >= a for a, b in zip(values, values[1:]))
    ok = same and held >= sec and ok1 and marker_at >= 0 and bool(outs) and comparable and out1 > out0
'''
assert fixed.count(old_end)==1
fixed=fixed.replace(old_end,new_end)
old_p="""            if p_disconnect():
                shot('5d_connect_form')
                if p_reconnect(control_port):
                    pick_pinned('6r (P) the pinned window again')
"""
new_p="""            if not p_disconnect():
                return 1
            shot('5d_connect_form')
            if not p_reconnect(control_port):
                return 1
            if not pick_pinned('6r (P) the pinned window again'):
                return 1
"""
assert fixed.count(old_p)==1
fixed=fixed.replace(old_p,new_p)
old_s="""            pick_pinned('6b the pinned window again after the park')
"""
new_s="""            if not pick_pinned('6b the pinned window again after the park'):
                return 1
"""
assert fixed.count(old_s)==1
fixed=fixed.replace(old_s,new_s)
(root/'oa_avd_fixed.py').write_text(fixed,encoding='utf-8',newline='\n')
patch=''.join(difflib.unified_diff(source.splitlines(True),fixed.splitlines(True),fromfile='a/automation/old_apk_lane/oa_avd.py',tofile='b/automation/old_apk_lane/oa_avd.py'))
(root/'watch_baseline_approved.patch').write_text(patch,encoding='utf-8',newline='\n')

class Time:
 def __init__(self):self.now=0
 def time(self):return self.now
 def monotonic(self):return self.now
 def sleep(self,t):self.now+=t

def run(code,kind):
 tree=ast.parse(code);f=next(x for x in tree.body if isinstance(x,ast.FunctionDef) and x.name=='observe_connection')
 clock=Time();calls=[];snapcalls=[]
 mark='2026-10-30 00:00:00.000 [pick_mark] active'
 baseline='2026-10-30 00:00:00.000 [video_debug] in=57 out=55\n'+mark+'\n'
 if kind=='no_baseline':baseline=mark+'\n'
 def tail():
  if clock.now==0:return ''
  if kind=='growing':return '2026-10-30 00:00:30.000 [video_debug] in=59 out=57\n'
  if kind=='reset_then_growing':return '2026-10-30 00:00:01.000 [video_debug] in=2 out=1\n2026-10-30 00:00:30.000 [video_debug] in=59 out=57\n'
  return '2026-10-30 00:00:30.000 [video_debug] in=57 out=55\n'
 def snapshot():
  snapcalls.append(clock.now)
  if clock.now>0 and kind=='final_read_fail':return False,'read failed'
  if clock.now>0 and kind=='marker_lost':return True,tail()
  return True,baseline+tail()
 ns={'os':type('OS',(),{'environ':{}})(),'time':clock,'re':re,'OBSERVE_SEC':30,'PICK_MARK':[mark,100,1,'hash'],
     'applog_snapshot':snapshot,'since':lambda c:tail(),'same_viewer':lambda:(kind!='disconnected','test snapshot'),
     'verdict':lambda *a:calls.append(a),'Invalid':RuntimeError}
 exec(compile(ast.Module(body=[f],type_ignores=[]),'frozen-function','exec'),ns)
 result=ns['observe_connection']('3w')
 return {'result':result,'verdict':calls[-1] if calls else None,'snapshot_calls':snapcalls,'elapsed':clock.now}

rows=[]
for kind in ['frozen_nonzero','growing','reset_then_growing','no_baseline','final_read_fail','marker_lost','disconnected']:
 a=run(source,kind);b=run(fixed,kind);want=kind=='growing'
 rows.append({'case':kind,'original':a,'fixed':b,'fixed_expected':want,'fixed_pass':b['result']==want})
 print(kind,'original',a['result'],'fixed',b['result'],'expected',want)
assert rows[0]['original']['result'] is True
assert all(r['fixed_pass'] for r in rows)
branch_rows=[]
def run_p(code,fail):
 tree=ast.parse(code);main=next(x for x in tree.body if isinstance(x,ast.FunctionDef) and x.name=='main')
 arm=next(x for x in ast.walk(main) if isinstance(x,ast.If) and ast.unparse(x.test)=="ARM == 'S'" and any(isinstance(y,ast.If) and 'p_disconnect' in ast.unparse(y.test) for y in x.orelse))
 fn=ast.FunctionDef(name='p_flow',args=ast.arguments(posonlyargs=[],args=[],kwonlyargs=[],kw_defaults=[],defaults=[]),body=arm.orelse+[ast.Expr(value=ast.Call(func=ast.Name(id='next7',ctx=ast.Load()),args=[],keywords=[])),ast.Return(value=ast.Constant(value=0))],decorator_list=[])
 calls=[];ns={'skip_case':lambda *a:None,'p_disconnect':lambda:fail!='disconnect','p_reconnect':lambda *a:fail!='reconnect','pick_pinned':lambda *a:fail!='pick','control_port':18460,'shot':lambda *a:None,'next7':lambda:calls.append('7')}
 exec(compile(ast.fix_missing_locations(ast.Module(body=[fn],type_ignores=[])),'actual-P-branch','exec'),ns)
 return {'return':ns['p_flow'](),'next7':calls}
for kind in ['disconnect','reconnect','pick']:
 a=run_p(source,kind);b=run_p(fixed,kind)
 assert a['next7']==['7'] and b['return']==1 and not b['next7']
 branch_rows.append({'case':kind,'original':a,'fixed':b});print('P failure',kind,'original next7',a['next7'],'fixed next7',b['next7'])
snapshot_rows=[]
tree=ast.parse(source);fs={x.name:x for x in tree.body if isinstance(x,ast.FunctionDef)}
for kind in ['unchanged','app_fail','host_fail','host_short','host_replaced','marker_lost','scene_changed']:
 before='2026-10-30 [video_debug] out=55\npick marker\n';host='owned host prefix\n'
 state={'a':(True,before),'h':(True,host)}
 ns={'re':re,'hashlib':hashlib,'PICK_MARK':[],'applog_snapshot':lambda:state['a'],'host_snapshot':lambda:state['h']}
 exec(compile(ast.Module(body=[fs['set_pick_mark'],fs['same_viewer']],type_ignores=[]),'fixed-snapshot-functions','exec'),ns)
 assert ns['set_pick_mark'](1)[0]
 if kind=='app_fail':state['a']=(False,'pull failed')
 if kind=='host_fail':state['h']=(False,'read failed')
 if kind=='host_short':state['h']=(True,'')
 if kind=='host_replaced':state['h']=(True,'x'*len(host))
 if kind=='marker_lost':state['a']=(True,'another current log')
 if kind=='scene_changed':state['a']=(True,before+'[scene] HOSTS\n')
 got,why=ns['same_viewer']();assert got==(kind=='unchanged')
 snapshot_rows.append({'case':kind,'result':got,'detail':why})
print('snapshot guards',len(snapshot_rows),'PASS')
hashes={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ['oa_avd_original.py','oa_avd_fixed.py','watch_baseline_approved.patch']}
(root/'checks.json').write_text(json.dumps({'rev':rev,'scope':'real frozen functions/P branch with synthetic cumulative logs and virtual clock; no adb/Host/Viewer/child','hashes':hashes,'rows':rows,'p_failures':branch_rows,'snapshot_guards':snapshot_rows},indent=2)+'\n',encoding='utf-8')
print(json.dumps(hashes,indent=2))
