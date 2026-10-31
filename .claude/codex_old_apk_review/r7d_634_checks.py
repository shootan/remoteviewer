from pathlib import Path
import ast,subprocess,re,json,hashlib
repo=Path('C:/work/gnlink-wt/old-apk-lane');rev='6345512'
root=Path(__file__).parent/'r7d_634';root.mkdir(exist_ok=True)
raw=subprocess.check_output(['git','-C',str(repo),'show',rev+':automation/old_apk_lane/oa_avd.py'])
(root/'oa_avd.py').write_bytes(raw)
t=ast.parse(raw.decode('utf-8-sig'));func={n.name:n for n in t.body if isinstance(n,ast.FunctionDef)}
body=None;index=None
for n in ast.walk(func['main']):
 if not hasattr(n,'body') or not isinstance(n.body,list):continue
 for i,x in enumerate(n.body):
  if isinstance(x,ast.If) and isinstance(x.test,ast.UnaryOp) and isinstance(x.test.operand,ast.Call) and isinstance(x.test.operand.func,ast.Name) and x.test.operand.func.id=='input_check':body=n.body;index=i
assert body is not None
f=ast.FunctionDef(name='review_flow',args=ast.arguments(posonlyargs=[],args=[],kwonlyargs=[],kw_defaults=[],defaults=[]),body=body[index-1:index+2],decorator_list=[])
tree=ast.fix_missing_locations(ast.Module(body=[f],type_ignores=[]));results=[]
for arm in ['S','P']:
 calls=[]
 scope={'pick_pinned':lambda *a:True,'input_check':lambda:False,'ARM':arm,'menu':lambda *a:calls.append('menu')}
 exec(compile(tree,'frozen-main-slice','exec'),scope)
 rc=scope['review_flow']();results.append({'case':arm+' input False','return':rc,'menu':calls,'pass':rc==1 and not calls})
state={'app':'','host':'x'*100}
ns={'re':re,'PICK_MARK':['baseline line',100,123],'since':lambda c:state['app'],'host_log':lambda:state['host']}
exec(compile(ast.Module(body=[func['same_viewer']],type_ignores=[]),str(root/'oa_avd.py'),'exec'),ns)
for name,app,host,want in [('no new events','', 'x'*100,True),('scene changed','[scene] HOSTS\n','x'*100,False),('host ended','','x'*100+'udp control session ended',False),('host log unavailable/shortened','','',False),('app pull unavailable','','x'*100,False)]:
 state.update(app=app,host=host);got,detail=ns['same_viewer']()
 results.append({'case':name,'got':got,'expected':want,'pass':got==want,'detail':detail})
record={'rev':subprocess.check_output(['git','-C',str(repo),'rev-parse',rev]).decode().strip(),'blob_sha256':hashlib.sha256(raw).hexdigest(),'scope':'fixed real functions/main statements, effect stubs only; unavailable helpers return empty as product harness does','results':results}
(root/'checks.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
for r in results:print(('PASS ' if r['pass'] else 'BUG ')+r['case']+' '+str(r.get('got',r.get('return'))))
