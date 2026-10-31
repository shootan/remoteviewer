from pathlib import Path
import subprocess, os, shutil, json

root = Path(__file__).parent / 'r5_ac587'
root.mkdir(exist_ok=True)
src = Path('C:/work/gnlink-wt/apk-decoder-liveness')
rev = 'ac587dde7ad87f35e00547e3992677a5554c9c3d'
def freeze(rel, out):
    data = subprocess.check_output(['git','-C',str(src),'show',rev+':'+rel])
    (root/out).write_bytes(data)
base = 'apps/android_direct_client/app/src/main/cpp/'
for name in ['android_video_decoder.cpp','android_video_decoder.hpp','android_video_decoder_sh_seam.hpp']:
    freeze(base+name,name)
for name in ['fake_ndk.cpp','fake_ndk.hpp']:
    freeze('apps/native_poc/src/test_fake_ndk/'+name,name)
for name in ['selection_ack_gate.hpp','decoder_pending_slot.hpp']:
    freeze('apps/native_poc/src/'+name,name)
p = root/'fake_ndk.cpp'
s = p.read_text(encoding='utf-8')
needle = 'ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec*, int64_t) {'
assert s.count(needle)==1
if 'fake_ndk::hold_if(fake_ndk::kHoldDequeueInput)' not in s:
    s = s.replace(needle,needle+'\n  fake_ndk::hold_if(16); // REVIEW ONLY: pause after the product gate check, before its input dequeue')
p.write_text(s,encoding='utf-8')
(root/'candidate_probe.cpp').write_text('\n#include "android_video_decoder.hpp"\n#include "fake_ndk.hpp"\n#include <chrono>\n#include <cstdio>\n#include <cstring>\n#include <thread>\nusing remote60::android_direct::AndroidVideoDecoderSink;\nusing namespace remote60::native_poc;\nUdpH264AssembledFrame f(uint64_t gen, uint8_t tag) {\n  UdpH264AssembledFrame x; x.header.width=1280; x.header.height=720;\n  x.header.flags=1; x.header.streamGeneration=gen; x.header.captureQpcUs=1000000+gen;\n  x.payload={0,0,0,1,0x67,0x42,0,0x28,0xAB,0,0,0,1,0x68,0xCE,0x3C,0x80,0,0,0,1,0x65,tag,tag};\n  return x;\n}\ntemplate<class F> bool wait(F pred, int ms=2000) {\n  auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);\n  while(std::chrono::steady_clock::now()<end) { if(pred()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }\n  return pred();\n}\nint main() {\n  AndroidVideoDecoderSink s;\n  s.SetSurface(nullptr,reinterpret_cast<jobject>(0x101));\n  s.PrepareForWindowSelection(42); s.BeginLocalSelectionFor(42);\n  fake_ndk::barrier().which.store(16); fake_ndk::barrier().held.store(true);\n  s.OnEncodedH264Frame(f(101,0xA1));\n  bool held=wait([]{return fake_ndk::barrier().waiting.load()>0;});\n  if(!held) { fake_ndk::barrier().held.store(false); std::puts("INVALID: no dequeue barrier"); return 2; }\n  s.OnEncodedH264Frame(f(202,0xB2));\n  std::puts("generation 202 passed RX Admit while generation 101 had a queue reservation");\n  fake_ndk::barrier().held.store(false);\n  wait([]{std::lock_guard<std::mutex> l(fake_ndk::mu()); return fake_ndk::controls().queued.size()>=2;});\n  bool leaked=false; size_t count=0;\n  { std::lock_guard<std::mutex> l(fake_ndk::mu());\n    for(const auto& q:fake_ndk::controls().queued) { ++count; auto tag=q.empty()?0:q.back();\n      std::printf("queued tag=0x%02X\\n",tag); if(tag==0xA1) leaked=true; }\n  }\n  std::printf("local bound=%llu count=%zu generation101_observed=%d\\n",(unsigned long long)s.LocalSelectionBoundFor(42),count,leaked);\n  s.SetSurface(nullptr,nullptr);\n  std::puts("raw generation101 observation above is not the r4 verdict; the contract verdict follows");\n  \n  const auto bound = s.LocalSelectionBoundFor(42);\n  bool candidateOk = bound == 101;\n  { std::lock_guard<std::mutex> l(fake_ndk::mu());\n    for(const auto& q:fake_ndk::controls().queued) if(!q.empty() && q.back()==0xB2) candidateOk=false;\n  }\n  std::printf("CONTRACT first queued generation binds: %s bound=%llu\\n",candidateOk?"PASS":"FAIL",(unsigned long long)bound);\n\n  // Actual owner change at the same input-dequeue barrier.\n  size_t before;\n  { std::lock_guard<std::mutex> l(fake_ndk::mu()); before=fake_ndk::controls().queued.size(); }\n  AndroidVideoDecoderSink own;\n  own.Bind(100); own.SetSurface(nullptr,reinterpret_cast<jobject>(0x303));\n  fake_ndk::barrier().held.store(true);\n  own.OnEncodedH264FrameAs(100,f(0,0xD4));\n  bool ownerHeld=wait([]{return fake_ndk::barrier().waiting.load()>0;});\n  own.Bind(200);\n  fake_ndk::barrier().held.store(false);\n  std::this_thread::sleep_for(std::chrono::milliseconds(100));\n  own.OnEncodedH264FrameAs(200,f(0,0xE5));\n  wait([&]{std::lock_guard<std::mutex> l(fake_ndk::mu()); return fake_ndk::controls().queued.size()>before;});\n  bool ownerOk=ownerHeld;\n  { std::lock_guard<std::mutex> l(fake_ndk::mu());\n    const auto& q=fake_ndk::controls().queued;\n    for(size_t k=before;k<q.size();++k) if(q[k].empty() || q[k].back()!=0xE5) ownerOk=false;\n    ownerOk=ownerOk && q.size()>before;\n  }\n  std::printf("CONTRACT owner change during dequeue: %s\\n",ownerOk?"PASS":"FAIL");\n  own.SetSurface(nullptr,nullptr);\n\n  AndroidVideoDecoderSink retry;\n  retry.SetSurface(nullptr,reinterpret_cast<jobject>(0x404));\n  retry.PrepareForWindowSelection(90);\n  ControlWindowSelectedMessage ack{}; ack.flags=1; ack.streamGeneration=303;\n  retry.OnWindowSelectionControlResultFor(ack,90);\n  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().createFails=true; }\n  retry.OnEncodedH264Frame(f(303,0xC3));\n  wait([&]{return retry.DebugStatus().find("codecFail=1 ")!=std::string::npos;});\n  for(int k=0;k<40;++k) {\n    auto delta=f(303,0x44); delta.header.flags=0; delta.payload={0,0,0,1,0x41,0x44};\n    retry.OnEncodedH264Frame(std::move(delta));\n    std::this_thread::sleep_for(std::chrono::milliseconds(50));\n  }\n  auto status=retry.DebugStatus();\n  std::printf("DELTA FAILURE SNAPSHOT %s\\n",status.c_str());\n  auto field=[&](const char* key) { auto at=status.find(key); return at==std::string::npos?0ull:std::stoull(status.substr(at+std::strlen(key))); };\n  bool retryOk=field("codecFail=")<=4;\n  std::printf("CONTRACT two seconds, one IDR plus 40 deltas: failures=%llu timerRetries=%llu %s\\n",\n    field("codecFail="),field("codecRetries="),retryOk?"PASS":"FAIL (deltas bypass retry cadence and budget)");\n\n  // IDRs may bypass the time slot, but all such attempts must consume the same finite budget.\n  for(int k=0;k<30;++k) {\n    retry.OnEncodedH264Frame(f(303,0xC3));\n    std::this_thread::sleep_for(std::chrono::milliseconds(10));\n  }\n  std::this_thread::sleep_for(std::chrono::milliseconds(30));\n  status=retry.DebugStatus();\n  bool terminalOk=status.find("codecState=failed_terminal")!=std::string::npos && field("codecFail=")==21;\n  std::printf("CONTRACT IDRs share budget: %s failures=%llu retry=%llu\\n",terminalOk?"PASS":"FAIL",field("codecFail="),field("codecRetries="));\n  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().createFails=false; }\n  auto failures=field("codecFail=");\n  for(int k=0;k<10;++k) {\n    retry.OnEncodedH264Frame(f(303,0xC3));\n    auto d=f(303,0x44); d.header.flags=0; d.payload={0,0,0,1,0x41,0x44};\n    retry.OnEncodedH264Frame(std::move(d));\n    std::this_thread::sleep_for(std::chrono::milliseconds(10));\n  }\n  status=retry.DebugStatus();\n  bool quietOk=status.find("codecState=failed_terminal")!=std::string::npos && field("codecFail=")==failures;\n  std::printf("CONTRACT terminal IDR/delta do not restart: %s\\n",quietOk?"PASS":"FAIL");\n  size_t oldQ;\n  { std::lock_guard<std::mutex> l(fake_ndk::mu()); oldQ=fake_ndk::controls().queued.size(); }\n  retry.SetSurface(nullptr,reinterpret_cast<jobject>(0x505));\n  retry.OnEncodedH264Frame(f(303,0xC3));\n  bool recovered=wait([&]{std::lock_guard<std::mutex> l(fake_ndk::mu()); return fake_ndk::controls().queued.size()>oldQ;});\n  std::printf("CONTRACT new surface resets terminal and queues IDR: %s\\n",recovered?"PASS":"FAIL");\n  retryOk=retryOk && terminalOk && quietOk && recovered;\n  retry.SetSurface(nullptr,nullptr);\n\n  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().createFails=false; }\n  return candidateOk && ownerOk && retryOk ? 0:1;\n\n}\n',encoding='utf-8')
ms = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
sdk = Path('C:/Program Files (x86)/Windows Kits/10')
env = os.environ.copy()
tmp = root/'tmp'; tmp.mkdir(exist_ok=True)
env['TMP']=env['TEMP']=str(tmp)
args=[str(ms/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++17','/EHsc','/utf-8','/MD','/DNOMINMAX',
      '/I'+str(root),'/I'+str(src/'apps/native_poc/src/test_fake_ndk'),'/I'+str(src/'apps/native_poc/src'),
      '/I'+str(ms/'include')]
for sub in ['ucrt','um','shared','winrt','cppwinrt']:
    args+=['/I'+str(sdk/'Include/10.0.26100.0'/sub)]
args+=['candidate_probe.cpp','android_video_decoder.cpp','fake_ndk.cpp','/Fe:candidate_probe.exe','/link',
       '/LIBPATH:'+str(ms/'lib/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/ucrt/x64'),
       '/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/um/x64'),'ws2_32.lib']
r=subprocess.run(args,cwd=root,env=env,capture_output=True)
(root/'build.log').write_bytes(r.stdout+r.stderr)
print('build rc',r.returncode)
if r.returncode:
    print((r.stdout+r.stderr).decode(errors='replace')[-2500:]); raise SystemExit(r.returncode)
r=subprocess.run([str(root/'candidate_probe.exe')],cwd=root,env=env,capture_output=True,timeout=20,
                 creationflags=subprocess.CREATE_NO_WINDOW)
(root/'candidate_probe.log').write_bytes(r.stdout+r.stderr)
print('probe rc',r.returncode)
print((r.stdout+r.stderr).decode(errors='replace')[-4500:])
