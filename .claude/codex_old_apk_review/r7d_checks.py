from pathlib import Path
import ast,subprocess,re,json,hashlib
repo=Path('C:/work/gnlink-wt/old-apk-lane')
rev='c468c27'
root=Path(__file__).parent/'r7d_c468';root.mkdir(exist_ok=True)
raw=subprocess.check_output(['git','-C',str(repo),'show',rev+':automation/old_apk_lane/oa_avd.py'])
(root/'oa_avd.py').write_bytes(raw)
t=ast.parse(raw.decode('utf-8-sig'))
func={n.name:n for n in t.body if isinstance(n,ast.FunctionDef)}
state={'text':''}
ns={'re':re,'adb':lambda *a:state['text']}
exec(compile(ast.Module(body=[func['ime_state']],type_ignores=[]),str(root/'oa_avd.py'),'exec'),ns)
checks=[]
for text,want in [('mInputShown=false mWindowVisible=false','hidden'),('mInputShown=true','shown'),('mInputShown=false mDecorViewVisible=true','shown'),('mInputShown=false isInputViewShown=true','shown'),('no flags','unknown'),('mWindowVisible=false','unknown')]:
 state['text']=text;got=ns['ime_state']()[0];checks.append({'name':text,'got':got,'expected':want,'pass':got==want})
main=func['main'];body=None;index=None
for n in ast.walk(main):
 if not hasattr(n,'body') or not isinstance(n.body,list):continue
 for i,x in enumerate(n.body):
  if isinstance(x,ast.Expr) and isinstance(x.value,ast.Call) and isinstance(x.value.func,ast.Name) and x.value.func.id=='input_check':body=n.body;index=i
assert body is not None
# Execute the actual frozen main statements from pick through the first following scenario, with every effect stubbed.
f=ast.FunctionDef(name='review_flow',args=ast.arguments(posonlyargs=[],args=[],kwonlyargs=[],kw_defaults=[],defaults=[]),body=body[index-1:index+2],decorator_list=[])
tree=ast.fix_missing_locations(ast.Module(body=[f],type_ignores=[]))
class ReachedMenu(Exception):pass
calls=[]
def menu(*a):calls.append('menu');raise ReachedMenu()
scope={'pick_pinned':lambda *a:True,'input_check':lambda:False,'ARM':'S','cursor':lambda:0,'host_log':lambda:'','menu':menu}
exec(compile(tree,'frozen-main-slice','exec'),scope)
try:scope['review_flow']()
except ReachedMenu:pass
result={'source':subprocess.check_output(['git','-C',str(repo),'rev-parse',rev]).decode().strip(),'blob_sha256':hashlib.sha256(raw).hexdigest(),'ime_checks':checks,'actual_main_continued_to_menu_after_input_false':calls==['menu'],'scope':'pure frozen function/main-slice with effect stubs; no adb, no child, no real Host'}
(root/'checks.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('IME parser checks',sum(x['pass'] for x in checks),'/',len(checks))
print('PRODUCT HARNESS BUG: input_check=False -> scenario5 menu called:',result['actual_main_continued_to_menu_after_input_false'])
