from pathlib import Path
import subprocess,os,re,json,hashlib,sys
repo=Path('C:/work/gnlink-wt/apk-inbox-c');rev='cf3c9441aff2601e3da79bb394f1e1ff4759c970'
root=Path(__file__).parent/'c_cf3_review';root.mkdir(exist_ok=True)
def freeze(folder,name):
 b=subprocess.check_output(['git','-C',str(repo),'show',rev+':'+folder+name]);(root/name).write_bytes(b)
for n in ['android_video_decoder.cpp','android_video_decoder.hpp','android_video_decoder_sh_seam.hpp']:freeze('apps/android_direct_client/app/src/main/cpp/',n)
for n in ['decoder_chain_inbox.hpp','decoder_key_request.hpp','selection_ack_gate.hpp','decoder_pending_slot.hpp','native_video_client_session.hpp','android_decoder_inbox_prefix_test.cpp']:freeze('apps/native_poc/src/',n)
for n in ['fake_ndk.cpp','fake_ndk.hpp']:freeze('apps/native_poc/src/test_fake_ndk/',n)
ms=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');sdk=Path('C:/Program Files (x86)/Windows Kits/10')
tmp=root/'tmp';tmp.mkdir(exist_ok=True);env=os.environ.copy();env['TMP']=env['TEMP']=str(tmp)
args=[str(ms/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++17','/EHsc','/utf-8','/MD','/DNOMINMAX','/I'+str(root),'/I'+str(repo/'apps/native_poc/src/test_fake_ndk'),'/I'+str(repo/'apps/native_poc/src'),'/I'+str(ms/'include')]
args+=['/I'+str(sdk/'Include/10.0.26100.0'/s) for s in ['ucrt','um','shared','winrt','cppwinrt']]
def run(name,source,timeout=25):
 r=subprocess.run(args+[source,'android_video_decoder.cpp','fake_ndk.cpp','/Fe:'+name+'.exe','/link','/LIBPATH:'+str(ms/'lib/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/ucrt/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/um/x64'),'ws2_32.lib'],cwd=root,env=env,capture_output=True)
 (root/(name+'.build.log')).write_bytes(r.stdout+r.stderr);assert r.returncode==0,(r.stdout+r.stderr).decode(errors='replace')[-1800:]
 imp=subprocess.run([str(ms/'bin/Hostx64/x64/dumpbin.exe'),'/IMPORTS',str(root/(name+'.exe'))],capture_output=True)
 (root/(name+'.imports.log')).write_bytes(imp.stdout+imp.stderr)
 assert imp.returncode==0 and not re.search(rb'\b(?:SendInput|PostMessage[AW]?|SendMessage[AW]?|SetThreadExecutionState|SetForegroundWindow|SetClipboardData)\b',imp.stdout),'REFUSED: direct OS-effect import'
 r=subprocess.run([str(root/(name+'.exe'))],cwd=root,env=env,capture_output=True,timeout=timeout,creationflags=subprocess.CREATE_NO_WINDOW)
 (root/(name+'.log')).write_bytes(r.stdout+r.stderr)
 print(name,'rc',r.returncode,flush=True)
 for l in r.stdout.decode(errors='replace').splitlines():
  if l.startswith(('PASS','FAIL','OBSERVE','RESULT')) or 'checks' in l:print(l,flush=True)
 assert r.returncode==0
if '--edges-only' not in sys.argv:run('prefix_test','android_decoder_inbox_prefix_test.cpp')
base=Path(__file__).with_name('c_d504_review')/'probe.cpp'
prefix=base.read_text(encoding='utf-8').split('int main(){')[0]
main=r'''
void tags(size_t from){std::lock_guard<std::mutex> l(fake_ndk::mu());for(size_t i=from;i<fake_ndk::controls().queued.size();i++)printf(" %u",fake_ndk::controls().queued[i].back());puts("");}
bool waitq(size_t n){for(int i=0;i<1000&&queued()<n;i++)pause(2);return queued()>=n;}
int main(){
 {size_t start=queued();AndroidVideoDecoderSink s;s.SetSurface(nullptr,reinterpret_cast<jobject>(0x101));
  s.PrepareForWindowSelection(1);ControlWindowSelectedMessage ack{};ack.flags=1;ack.streamGeneration=1;s.OnWindowSelectionControlResultFor(ack,1);
  s.OnEncodedH264Frame(f(1,true));if(!waitq(start+1))return 2;
  s.OnEncodedH264Frame(f(2,false));if(!waitq(start+2))return 2;pause(900);
  size_t beforeNewDelta=queued();s.OnEncodedH264Frame(f(3,false));if(!waitq(beforeNewDelta+1))return 2;
  printf("OBSERVE no first output, committed newer delta before replay; codec tags:");tags(start);
  printf("OBSERVE replay status %s\n",s.DebugStatus().c_str());s.SetSurface(nullptr,nullptr);}
 {size_t start=queued();AndroidVideoDecoderSink s;s.SetPrefixPolicyForTest(true);s.SetTargetFps(30);s.SetSurface(nullptr,reinterpret_cast<jobject>(0x202));
  s.OnEncodedH264Frame(f(1,true));if(!waitq(start+1))return 2;
  available(false);s.OnEncodedH264Frame(f(2,true));pause(40);s.OnEncodedH264Frame(f(3,true));pause(650);
  printf("OBSERVE pending IDR2, newer IDR3 waiting after650ms: %s\n",s.DebugStatus().c_str());
  available(true);if(!waitq(start+3))return 2;
  printf("OBSERVE after input opens, codec tags:");tags(start);s.SetSurface(nullptr,nullptr);}
 return 0;}
'''
(root/'edge_probe.cpp').write_text(prefix+main,encoding='utf-8')
run('edge_probe','edge_probe.cpp')
names=['android_video_decoder.cpp','android_video_decoder.hpp','decoder_chain_inbox.hpp','prefix_test.exe','prefix_test.log','edge_probe.cpp','edge_probe.exe','edge_probe.log','prefix_test.imports.log','edge_probe.imports.log']
(root/'SHA256SUMS.json').write_text(json.dumps({n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in names},indent=2)+'\n',encoding='utf-8')
