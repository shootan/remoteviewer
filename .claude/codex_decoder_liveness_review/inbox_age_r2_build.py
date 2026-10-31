from pathlib import Path
import subprocess, os, shutil, json

root = Path(__file__).parent / 'inbox_79b_age'
root.mkdir(exist_ok=True)
src = Path('C:/work/gnlink-wt/apk-decoder-liveness')
rev = '79b5c25744252f20a482fe6d6ddf51e38c99f85e'
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
(root/'candidate_probe.cpp').write_text('\n#include "android_video_decoder.hpp"\n#include "fake_ndk.hpp"\n#include <chrono>\n#include <cstdio>\n#include <cstring>\n#include <thread>\nusing remote60::android_direct::AndroidVideoDecoderSink;\nusing namespace remote60::native_poc;\nUdpH264AssembledFrame f(uint64_t gen, uint8_t tag) {\n  UdpH264AssembledFrame x; x.header.width=1280; x.header.height=720;\n  x.header.flags=1; x.header.streamGeneration=gen; x.header.captureQpcUs=1000000+gen;\n  x.payload={0,0,0,1,0x67,0x42,0,0x28,0xAB,0,0,0,1,0x68,0xCE,0x3C,0x80,0,0,0,1,0x65,tag,tag};\n  return x;\n}\ntemplate<class F> bool wait(F pred, int ms=2000) {\n  auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);\n  while(std::chrono::steady_clock::now()<end) { if(pred()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }\n  return pred();\n}\n\nint main() {\n  AndroidVideoDecoderSink sink;\n  sink.SetSurface(nullptr,reinterpret_cast<jobject>(0x101));\n  sink.PrepareForWindowSelection(90);\n  ControlWindowSelectedMessage ack{}; ack.flags=1; ack.streamGeneration=303;\n  sink.OnWindowSelectionControlResultFor(ack,90);\n  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().inputAvailable=false; }\n  auto t0=std::chrono::steady_clock::now();\n  auto key=f(303,0xA1); key.header.seq=1; sink.OnEncodedH264Frame(std::move(key));\n  bool held=wait([&]{return sink.DebugStatus().find("pending=1 ")!=std::string::npos;});\n  if(!held) { std::puts("INVALID no held frame"); return 2; }\n  std::this_thread::sleep_for(std::chrono::milliseconds(250));\n  auto delta=f(303,0xB2); delta.header.seq=2; delta.header.flags=0; delta.payload={0,0,0,1,0x41,0xB2};\n  sink.OnEncodedH264Frame(std::move(delta));\n  std::this_thread::sleep_for(std::chrono::milliseconds(30));\n  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().inputAvailable=true; }\n  bool queued=wait([]{std::lock_guard<std::mutex> l(fake_ndk::mu());return !fake_ndk::controls().queued.empty();});\n  auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count();\n  std::printf("held=%d queued=%d actual IDR age at least 280ms, observed elapsed=%lldms\\n",held,queued,(long long)ms);\n  std::printf("%s\\n",sink.LivenessTraceLine().c_str());\n  sink.SetSurface(nullptr,nullptr);\n  return queued?0:2;\n}\n',encoding='utf-8')
ms = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
sdk = Path('C:/Program Files (x86)/Windows Kits/10')
env = os.environ.copy()
tmp = root/'tmp'; tmp.mkdir(exist_ok=True)
env['TMP']=env['TEMP']=str(tmp)
args=[str(ms/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++17','/EHsc','/utf-8','/MD','/DNOMINMAX','/DGNLINK_LIVENESS_TRACE=1',
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
