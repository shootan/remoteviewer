from pathlib import Path
import ast,subprocess,re,time,os,json,hashlib,difflib
wt=Path('D:/remote/remote/.claude/worktrees/verify-0164');rev='98d71efca956f07d24009c8baa5416f2e5424a29'
root=Path(__file__).parent/'r7i_98';root.mkdir(exist_ok=True)
path='automation/old_apk_lane/oa_avd.py'
raw=subprocess.check_output(['git','-C',str(wt),'show',rev+':'+path]);old=raw.decode('utf-8')
fixed=old.replace('on_screen = sorted(k for k in SCREEN_FLAGS if', "on_screen = sorted(k for k in SCREEN_FLAGS + ('isInputViewShown',) if")
fixed=fixed.replace("for k in ('mIsInputViewShown', 'isInputViewShown') if", "for k in ('mIsInputViewShown',) if")
assert fixed!=old
fixed=fixed.replace('mIsInputViewShown is the\n', 'The public isInputViewShown() reports decor visibility and is conservative shown evidence.\n    mIsInputViewShown is the\n')
(root/'oa_avd_fixed.py').write_text(fixed,encoding='utf-8',newline='\n')
patch=''.join(difflib.unified_diff(old.splitlines(True),fixed.splitlines(True),fromfile='a/'+path,tofile='b/'+path))
(root/'ime_public_flag_approved.patch').write_text(patch,encoding='utf-8',newline='\n')
cases=[('all hidden','mInputShown=false mWindowVisible=false mDecorViewVisible=false','hidden'),
 ('internal only','mInputShown=false mWindowVisible=false mDecorViewVisible=false mIsInputViewShown=true','hidden'),
 ('public true contradiction','mInputShown=false mWindowVisible=false mDecorViewVisible=false isInputViewShown=true','shown'),
 ('system true','mInputShown=true mWindowVisible=false mDecorViewVisible=false','shown'),
 ('window true','mInputShown=false mWindowVisible=true mDecorViewVisible=false','shown'),
 ('decor true','mInputShown=false mWindowVisible=false mDecorViewVisible=true','shown'),
 ('only system false','mInputShown=false','unknown'),('missing system','mWindowVisible=false mDecorViewVisible=false','unknown'),
 ('missing window','mInputShown=false mDecorViewVisible=false','unknown'),('missing decor','mInputShown=false mWindowVisible=false','unknown'),
 ('no flags','','unknown'),('conflict','mInputShown=false mInputShown=true mWindowVisible=false mDecorViewVisible=false','shown')]
result=[]
for mode,source in [('original',old),('fixed',fixed)]:
 tree=ast.parse(source);func=next(x for x in tree.body if isinstance(x,ast.FunctionDef) and x.name=='ime_state')
 state={'output':''};ns={'re':re,'time':time,'os':os,'OUT':str(root),'IME_READS':[],'SCREEN_FLAGS':('mInputShown','mWindowVisible','mDecorViewVisible'),'adb':lambda *a:state['output']}
 exec(compile(ast.Module(body=[func],type_ignores=[]),'pinned-ime-function','exec'),ns)
 for name,text,want in cases:
  state['output']=text;got,why=ns['ime_state']();result.append({'mode':mode,'case':name,'got':got,'expected':want,'pass':got==want})
  if mode=='fixed':assert got==want,(name,got,want)
assert any(x['mode']=='original' and x['case']=='public true contradiction' and not x['pass'] for x in result)
hashes={path:hashlib.sha256(fixed.encode()).hexdigest()}
for name in ['oa_pick_test.py','stubs/fake_ui_adb.py']:
 b=subprocess.check_output(['git','-C',str(wt),'show',rev+':automation/old_apk_lane/'+name]);hashes['automation/old_apk_lane/'+name]=hashlib.sha256(b).hexdigest()
patchsha=hashlib.sha256((root/'ime_public_flag_approved.patch').read_bytes()).hexdigest()
(root/'checks.json').write_text(json.dumps({'rev':rev,'scope':'actual parser AST, synthetic dumpsys strings, no process/adb/device','files':hashes,'patch_sha256':patchsha,'checks':result},indent=2)+'\n',encoding='utf-8')
print('fixed parser checks',sum(x['pass'] for x in result if x['mode']=='fixed'),'/12')
print('original public true -> hidden; fixed -> shown')
print(json.dumps({'files':hashes,'patch_sha256':patchsha},indent=2))
