from pathlib import Path
import subprocess,os,re,json,hashlib,sys
wt=Path('C:/work/gnlink-wt/old-apk-lane');oldwt=Path('C:/work/gnlink-wt/old-ref-163');rev='7f6c7fe'
root=Path(__file__).parent/'oldref_r1';root.mkdir(exist_ok=True)
def blob(repo,ref,name):return subprocess.check_output(['git','-C',str(repo),'show',ref+':apps/native_poc/src/'+name])
for n in ['udp_control_channel.cpp','udp_control_channel.hpp','udp_control_close_diag_test.cpp']:
 b=blob(wt,rev,n);assert b==blob(oldwt,'ff98f19',n);(root/n).write_bytes(b)
(root/'host_log.hpp').write_bytes(blob(oldwt,'ff98f19','host_log.hpp'))

ms=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');sdk=Path('C:/Program Files (x86)/Windows Kits/10')
tmp=root/'tmp';tmp.mkdir(exist_ok=True);env=os.environ.copy();env['TMP']=env['TEMP']=str(tmp)
args=[str(ms/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++17','/EHsc','/utf-8','/MD','/DNOMINMAX','/I'+str(root),'/I'+str(wt/'apps/native_poc/src'),'/I'+str(wt/'libs/common/include'),'/I'+str(ms/'include')]
args+=['/I'+str(sdk/'Include/10.0.26100.0'/s) for s in ['ucrt','um','shared','winrt','cppwinrt']]
libs=['/link','/LIBPATH:'+str(ms/'lib/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/ucrt/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/um/x64'),'ws2_32.lib','user32.lib']
def buildrun(name,src,defs=[]):
 cmd=args+defs+src+['/Fe:'+name+'.exe']+libs
 r=subprocess.run(cmd,cwd=root,env=env,capture_output=True);(root/(name+'.build.log')).write_bytes(r.stdout+r.stderr)
 assert r.returncode==0,(r.stdout+r.stderr).decode(errors='replace')[-1600:]
 if name=='power_spy':
  imports=subprocess.run([str(ms/'bin/Hostx64/x64/dumpbin.exe'),'/IMPORTS',str(root/(name+'.exe'))],capture_output=True)
  (root/'power_spy.imports.log').write_bytes(imports.stdout+imports.stderr)
  assert imports.returncode==0
  assert not re.search(rb'\b(?:SendInput|PostMessage[AW]?|SendMessage[AW]?|SetThreadExecutionState)\b',imports.stdout), 'REFUSED: real OS-effect import remains'
 r=subprocess.run([str(root/(name+'.exe'))],cwd=root,env=env,capture_output=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
 (root/(name+'.log')).write_bytes(r.stdout+r.stderr);print(name,'rc',r.returncode,flush=True);print(r.stdout.decode(errors='replace')[-2800:],flush=True);assert r.returncode==0
if '--power-only' not in sys.argv:
 buildrun('diag_unit',['udp_control_close_diag_test.cpp','udp_control_channel.cpp'],['/DREMOTE60_CONTROL_CLOSE_DIAG=1'])
prefix=(root/'udp_control_close_diag_test.cpp').read_text(encoding='utf-8').split('int main() {')[0]
probe=r'''
int main(){Pair p;auto m=message(37);p.a.Send(m.data(),m.size());
 for(int i=0;i<200&&!p.a.IsClosed();++i){p.pump(true);std::this_thread::sleep_for(std::chrono::milliseconds(3));p.a.Tick();p.b.Tick();}
 auto before=p.a.CloseDiagLine();std::this_thread::sleep_for(std::chrono::milliseconds(5));p.a.Tick();auto after=p.a.CloseDiagLine();
 printf("FIRST %s\nLATER %s\n",before.c_str(),after.c_str());return 0;}
'''
(root/'repeat_tick.cpp').write_text(prefix+probe,encoding='utf-8')
if '--power-only' not in sys.argv:
 buildrun('repeat_tick',['repeat_tick.cpp','udp_control_channel.cpp'],['/DREMOTE60_CONTROL_CLOSE_DIAG=1'])
power=r'''
#include <windows.h>
#include <cstdio>
static int execs=0,inputs=0,posts=0;
static EXECUTION_STATE WINAPI powerSpy(EXECUTION_STATE x){++execs;return x;}
static UINT WINAPI inputSpy(UINT n,LPINPUT,int){inputs+=n;return n;}
static BOOL WINAPI postSpy(HWND,UINT,WPARAM,LPARAM){++posts;return TRUE;}
#undef PostMessage
#define PostMessage postSpy
#define PostMessageW postSpy
#define PostMessageA postSpy
#define SendInput inputSpy
#define SetThreadExecutionState powerSpy
#include "host_log.hpp"
int main(){
 {remote60::native_poc::HostPowerKeepalive p(false);p.SetStreaming(true,true);p.SetStreaming(false);}
 printf("OFF exec=%d input=%d post=%d\n",execs,inputs,posts);bool off=execs==0&&inputs==0&&posts==0;
 {remote60::native_poc::HostPowerKeepalive p(true);p.SetStreaming(true,true);p.SetStreaming(true,true);p.SetStreaming(false);}
 printf("ON RECORD ONLY exec=%d input=%d post=%d\n",execs,inputs,posts);return off&&execs==4&&inputs==2&&posts==1?0:1;}
'''
(root/'power_spy.cpp').write_text(power,encoding='utf-8')
buildrun('power_spy',['power_spy.cpp'])
guard=r'''
#include "udp_control_channel.hpp"
#include <type_traits>
template<class T,class=void>struct Has:std::false_type{};
template<class T>struct Has<T,std::void_t<decltype(&T::CloseDiagLine)>>:std::true_type{};
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
static_assert(Has<remote60::native_poc::UdpControlChannel>::value);
#else
static_assert(!Has<remote60::native_poc::UdpControlChannel>::value);
#endif
'''
(root/'guard.cpp').write_text(guard,encoding='utf-8')
for mode in ['production','diag']:
 r=subprocess.run(args+(['/DREMOTE60_CONTROL_CLOSE_DIAG=1'] if mode=='diag' else [])+['/c','guard.cpp','/Fo:guard_'+mode+'.obj'],cwd=root,env=env,capture_output=True)
 (root/('guard_'+mode+'.log')).write_bytes(r.stdout+r.stderr);assert r.returncode==0;print('guard',mode,'PASS')
