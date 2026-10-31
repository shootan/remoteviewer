from pathlib import Path
import subprocess, os, importlib.util, json, hashlib

base=Path(__file__).parent/'r3_494'
base.mkdir(exist_ok=True)
src=Path('C:/work/gnlink-wt/old-apk-lane')
rev='494d05b55c995b4dd687deccf618169d18df1df0'
manifest=Path('D:/remote/remote/.claude/oa_lane_r3_SHA256SUMS.txt')
checked=[]
for line in manifest.read_text(encoding='utf-8-sig').splitlines():
    if not line or line.startswith('#'): continue
    canon,raw,rel=line.split(maxsplit=2)
    blob=subprocess.check_output(['git','-C',str(src),'show',rev+':'+rel])
    actual=(src/rel).read_bytes()
    assert hashlib.sha256(blob).hexdigest()==canon,(rel,'blob')
    assert hashlib.sha256(actual).hexdigest()==raw,(rel,'raw')
    target=base/rel
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(blob)
    checked.append(rel)
print('snapshot',len(checked),'blob/raw matches',flush=True)
tmp=base/'tmp'; tmp.mkdir(exist_ok=True)
os.environ['TMP']=os.environ['TEMP']=str(tmp)
e=base/'automation/old_apk_lane'
spec=importlib.util.spec_from_file_location('pmclear',e/'oa_pmclear_test.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
results=[]
for arm,mode in [('P','rc1'),('P','failed'),('P','empty'),('P','timeout'),('P','installbad'),('S','rc1')]:
    r=m.run_case('codex-'+arm.lower()+'-'+mode,arm,mode)
    ok=r['rc']==2 and not r['started'] and r['taps']==0
    results.append({'arm':arm,'mode':mode,'ok':ok,'rc':r['rc'],'started':r['started'],'taps':r['taps'],'clears':r['clears']})
    print(results[-1],flush=True)
r=m.run_case('codex-control','P','ok',wait_for_start=True)
results.append({'arm':'P','mode':'ok','ok':r['started'],'started':r['started']})
print(results[-1],flush=True)
(base/'pmclear_review.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
assert all(x['ok'] for x in results)
ms=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
sdk=Path('C:/Program Files (x86)/Windows Kits/10')
cpp=base/'apps/native_poc/src'
args=[str(ms/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++17','/EHsc','/utf-8','/MD',
      '/I'+str(ms/'include')]
for p in ['ucrt','um','shared']: args+=['/I'+str(sdk/'Include/10.0.26100.0'/p)]
args+=['ka_standby_holder_verdict_test.cpp','/Fe:verdict.exe','/link','/LIBPATH:'+str(ms/'lib/x64'),
       '/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/ucrt/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/um/x64')]
p=subprocess.run(args,cwd=cpp,capture_output=True)
(base/'build.log').write_bytes(p.stdout+p.stderr)
assert p.returncode==0,(p.stdout+p.stderr)[-1500:]
p=subprocess.run([str(cpp/'verdict.exe')],cwd=cpp,capture_output=True,timeout=10,
                 creationflags=subprocess.CREATE_NO_WINDOW)
(base/'verdict.log').write_bytes(p.stdout+p.stderr)
print(p.stdout.decode(errors='replace')[-250:],flush=True)
assert p.returncode==0
print('RESULT PASS (fake adb 7, holder verdict 7; real device/Host/Viewer/adb/GPU 0)',flush=True)
