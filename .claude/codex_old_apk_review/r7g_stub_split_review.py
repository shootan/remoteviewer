from pathlib import Path
import ast,subprocess,re,os,json,hashlib,difflib,types

wt=Path('C:/work/gnlink-wt/old-apk-lane');rev='b11fc0d40b49e086ed3df9bcbfcaa467fd7318a2'
root=Path(__file__).parent/'r7g_b11'
raw=subprocess.check_output(['git','-C',str(wt),'show',rev+':automation/old_apk_lane/oa_pick_test.py'])
old=raw.decode('utf-8')
a=old.index('    # the 30 s watch (3w) uses the same check:')
b=old.index("    old = os.path.join(RUNS,",a)
replacement='''    # The watch has a strict baseline read now. Test that path separately from a failure in its loop.
    m, p = case('watch-baseline-pullfail', 'ok')
    m.pick_pinned('3 the pinned window picked')
    n_taps = len(taps(p))
    os.environ['OA_FAKE_PULL_FAIL'] = '1'
    try:
        okw = m.observe_connection('3w the connection')
    finally:
        os.environ['OA_FAKE_PULL_FAIL'] = '0'
    r = last(m)
    check('NEGATIVE 3w: the baseline app snapshot fails -> FAIL before the watch, no input', not okw and r[1] is False
          and 'no current snapshot / pick marker / video out baseline' in r[2] and len(taps(p)) == n_taps, r[2][:200])

    for tag, spoil, want, what in (('watch-pullfail', pull_fails, 'no app snapshot', 'the app log pull fails'),
                                   ('watch-hostshrink', host_shrinks, 'shrank', 'the host log shrinks')):
        m, p = case(tag, 'ok')
        m.pick_pinned('3 the pinned window picked')
        read_snapshot = m.applog_snapshot
        reads, injected = [], []

        def snapshot_after_initial_gate():
            # Read 1 is the video-counter baseline; read 2 is same_viewer's initial gate.
            # Read 3 is its check AFTER the first loop sleep. Inject only there, once.
            if len(reads) == 2:
                spoil(p)
                injected.append(True)
            result = read_snapshot()
            reads.append(result[0])
            return result

        m.applog_snapshot = snapshot_after_initial_gate
        n_taps = len(taps(p))
        try:
            okw = m.observe_connection('3w the connection')
        finally:
            m.applog_snapshot = read_snapshot
            os.environ['OA_FAKE_PULL_FAIL'] = '0'
        r = last(m)
        check(f'NEGATIVE 3w: {what} AFTER a good baseline and initial gate -> FAIL in the loop, no input', not okw
              and r[1] is False and want in r[2] and reads[:2] == [True, True] and len(injected) == 1
              and len(taps(p)) == n_taps, f'reads={reads} injected={len(injected)} {r[2][:160]}')
'''
fixed=old[:a]+replacement+old[b:]
(root/'oa_pick_test_fixed.py').write_text(fixed,encoding='utf-8',newline='\n')
patch=''.join(difflib.unified_diff(old.splitlines(True),fixed.splitlines(True),fromfile='a/automation/old_apk_lane/oa_pick_test.py',tofile='b/automation/old_apk_lane/oa_pick_test.py'))
(root/'stub_watch_split_approved.patch').write_text(patch,encoding='utf-8',newline='\n')
ast.parse(fixed)

# Execute the exact new test statements with the real patched watch/snapshot/marker functions.
# Only adb, host-log availability, the clock, and verdict output are effect stubs. No process or device is started.
avd=ast.parse((root/'oa_avd_fixed.py').read_text(encoding='utf-8'))
functions={x.name:x for x in avd.body if isinstance(x,ast.FunctionDef)}
needed=['applog_snapshot','set_pick_mark','same_viewer','observe_connection']
made=[]
class Time:
 def __init__(self):self.n=0
 def time(self):return self.n
 def monotonic(self):return self.n
 def sleep(self,t):self.n+=t
def case(tag,*unused):
 out=root/('split-'+tag);out.mkdir(exist_ok=True)
 host=out/'host.log';host.write_text('owned host prefix\n',encoding='utf-8')
 m=types.ModuleType(tag);clock=Time();verdicts=[]
 class KA:
  LOG_PATH='fixture-app-log'
  def adb(self,*args):
   if os.environ.get('OA_FAKE_PULL_FAIL')=='1':raise RuntimeError('fixture pull failed')
   p=Path(args[-1]);assert p.parent.resolve()==out.resolve()
   text='2026-10-30 00:00:00.000 [video_debug] in=57 out=55\npick marker\n'
   if clock.n>0:text+='2026-10-30 00:00:01.000 [video_debug] in=58 out=56\n'
   p.write_text(text,encoding='utf-8')
 m.__dict__.update(os=os,re=re,hashlib=hashlib,ka=KA(),OUT=str(out),time=clock,OBSERVE_SEC=30,PICK_MARK=[],
                   host_snapshot=lambda:(True,host.read_text(encoding='utf-8')),verdict=lambda *args:verdicts.append(args),Invalid=RuntimeError)
 exec(compile(ast.Module(body=[functions[n] for n in needed],type_ignores=[]),'real-watch-functions','exec'),m.__dict__)
 assert m.set_pick_mark(1)[0]
 m.pick_pinned=lambda *a:True;m.review_verdicts=verdicts
 made.append((tag,clock,verdicts))
 return m,{'host':str(host),'out':str(out)}
def pull_fails(p):os.environ['OA_FAKE_PULL_FAIL']='1'
def host_shrinks(p):
 h=Path(p['host']);s=h.read_text();h.write_text(s[:len(s)//2])
results=[]
def check(name,ok,detail):
 results.append({'name':name,'pass':ok,'detail':detail});print(('PASS ' if ok else 'FAIL ')+name)
tree=ast.parse('def run():\n'+replacement)
env={'case':case,'taps':lambda p:[],'os':os,'last':lambda m:m.review_verdicts[-1],'check':check,'pull_fails':pull_fails,'host_shrinks':host_shrinks}
saved=os.environ.get('OA_FAKE_PULL_FAIL');os.environ['OA_FAKE_PULL_FAIL']='0'
try:
 exec(compile(tree,'exact-approved-test-block','exec'),env);env['run']()
finally:
 if saved is None:os.environ.pop('OA_FAKE_PULL_FAIL',None)
 else:os.environ['OA_FAKE_PULL_FAIL']=saved
assert len(results)==3 and all(x['pass'] for x in results)
assert made[0][1].n==0 and all(x[1].n>0 for x in made[1:])
hashes={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ['oa_pick_test_fixed.py','stub_watch_split_approved.patch']}
(root/'stub_split_checks.json').write_text(json.dumps({'scope':'exact new test block and real watch/snapshot functions; no process/adb/device','hashes':hashes,'results':results,'virtual_elapsed':{tag:t.n for tag,t,_ in made}},indent=2)+'\n',encoding='utf-8')
print(json.dumps(hashes,indent=2))
