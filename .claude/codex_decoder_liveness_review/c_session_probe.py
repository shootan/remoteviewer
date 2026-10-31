from pathlib import Path
import subprocess,os
root=Path(__file__).parent/'c_5fe90_review'
repo=Path('C:/work/gnlink-wt/apk-inbox-c')
rev='5fe90a93985e2ac3cac922764d523de7d043920e'
(root/'entry_sink_adapter.hpp').write_bytes(subprocess.check_output(['git','-C',str(repo),'show',rev+':apps/android_direct_client/app/src/main/cpp/entry_sink_adapter.hpp']))
def blob(name): return subprocess.check_output(['git','-C',str(repo),'show',rev+':apps/native_poc/src/'+name]).decode('utf-8')
for name in ['native_video_client_session.hpp','native_video_client_shared_core.cpp','native_video_client_tcp_control.cpp','udp_control_channel.cpp']:
    (root/name).write_text(blob(name),encoding='utf-8')
s=blob('native_video_client_session.cpp')
needle='if (action.kind == ControlOutboundActionKind::KeyframeRequest) {\n        {'
assert s.count(needle)==1
s=s.replace(needle,'if (action.kind == ControlOutboundActionKind::KeyframeRequest) {\n        RunTestKeyHook("review_before_ledger_take"); // REVIEW ONLY scheduling barrier; no product logic changed\n        {')
(root/'native_video_client_session.cpp').write_text(s,encoding='utf-8')
prefix=blob('android_decoder_inbox_session_test.cpp').split('int main() {')[0]
main=r'''
int main(){
 Rig r; bool up=r.Up(71); if(!up){r.Down();puts("INVALID startup");return 2;}
 r.host.ignoreKeyRequests=true;
 std::atomic<int> fired{0};std::atomic<int> pick{0};
 r.c.SetTestKeyRequestHook([&](const char* p){
  if(std::strcmp(p,"review_before_ledger_take")==0 && fired.fetch_add(1)==0){
   r.sink.Bind(72); // old entry is no longer owner
   pick.store(r.c.RequestWindowSelect(1001)?1:-1); // production path clears its descriptor
  }
 });
 auto start=Clock::now();r.Loss();std::this_thread::sleep_for(std::chrono::milliseconds(1800));
 auto req=r.host.RequestsSinceMs(start);
 printf("REVIEW old owner invalidated before ledger take: hook=%d newPick=%d staleDropped=%llu hostKeyRequests=%s\n",fired.load(),pick.load(),(unsigned long long)r.c.KeyRequestsDroppedStale(),list(req).c_str());
 r.Down();return fired.load()>0&&pick.load()==1?0:2;
}
'''
(root/'session_probe.cpp').write_text(prefix+main,encoding='utf-8')
ms=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
sdk=Path('C:/Program Files (x86)/Windows Kits/10');env=os.environ.copy();env['TMP']=env['TEMP']=str(root/'tmp')
args=[str(ms/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++17','/EHsc','/utf-8','/MD','/DNOMINMAX','/DREMOTE60_CLIENT_SESSION_TEST_HOOKS=1','/I'+str(root),'/I'+str(repo/'apps/native_poc/src/test_fake_ndk'),'/I'+str(repo/'apps/native_poc/src'),'/I'+str(repo/'apps/android_direct_client/app/src/main/cpp'),'/I'+str(repo/'libs/common/include'),'/I'+str(ms/'include')]
args[args.index('/MD')]='/MT'
args+=['/I'+str(sdk/'Include/10.0.26100.0'/s) for s in ['ucrt','um','shared','winrt','cppwinrt']]
args+=['session_probe.cpp','native_video_client_session.cpp','native_video_client_shared_core.cpp','native_video_client_tcp_control.cpp','udp_control_channel.cpp','android_video_decoder.cpp','fake_ndk.cpp','/Fe:session_probe.exe','/link','/LIBPATH:'+str(ms/'lib/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/ucrt/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/um/x64'),str(repo/'build/libs/common/Release/common.lib'),'ws2_32.lib','winhttp.lib','user32.lib','advapi32.lib','ole32.lib']
r=subprocess.run(args,cwd=root,env=env,capture_output=True)
(root/'session_build.log').write_bytes(r.stdout+r.stderr);print('build',r.returncode)
if r.returncode: print((r.stdout+r.stderr).decode(errors='replace')[-1800:]);raise SystemExit(r.returncode)
r=subprocess.run([str(root/'session_probe.exe')],cwd=root,env=env,capture_output=True,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
(root/'session_probe.log').write_bytes(r.stdout+r.stderr)
print('probe',r.returncode)
for l in r.stdout.decode(errors='replace').splitlines():
    if 'REVIEW' in l or 'INVALID' in l: print(l)
