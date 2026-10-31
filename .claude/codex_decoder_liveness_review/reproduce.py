from pathlib import Path
import subprocess, os, shutil, json

root = Path(__file__).parent / 'r3_7a79'
root.mkdir(exist_ok=True)
src = Path('C:/work/gnlink-wt/apk-decoder-liveness')
rev = '7a79b09df02017831515d342733452809b022fd2'
def freeze(rel, out):
    data = subprocess.check_output(['git','-C',str(src),'show',rev+':'+rel])
    (root/out).write_bytes(data)
base = 'apps/android_direct_client/app/src/main/cpp/'
for name in ['android_video_decoder.cpp','android_video_decoder.hpp','android_video_decoder_sh_seam.hpp']:
    freeze(base+name,name)
for name in ['fake_ndk.cpp','fake_ndk.hpp']:
    freeze('apps/native_poc/src/test_fake_ndk/'+name,name)
p = root/'fake_ndk.cpp'
s = p.read_text(encoding='utf-8')
needle = 'ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec*, int64_t) {'
assert s.count(needle)==1
s = s.replace(needle,needle+'\n  fake_ndk::hold_if(16); // REVIEW ONLY: pause after the product gate check, before its input dequeue')
p.write_text(s,encoding='utf-8')
(root/'candidate_probe.cpp').write_text(r'''
#include "android_video_decoder.hpp"
#include "fake_ndk.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
using remote60::android_direct::AndroidVideoDecoderSink;
using namespace remote60::native_poc;
UdpH264AssembledFrame f(uint64_t gen, uint8_t tag) {
  UdpH264AssembledFrame x; x.header.width=1280; x.header.height=720;
  x.header.flags=1; x.header.streamGeneration=gen; x.header.captureQpcUs=1000000+gen;
  x.payload={0,0,0,1,0x67,0x42,0,0x28,0xAB,0,0,0,1,0x68,0xCE,0x3C,0x80,0,0,0,1,0x65,tag,tag};
  return x;
}
template<class F> bool wait(F pred, int ms=2000) {
  auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
  while(std::chrono::steady_clock::now()<end) { if(pred()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  return pred();
}
int main() {
  AndroidVideoDecoderSink s;
  s.SetSurface(nullptr,reinterpret_cast<jobject>(0x101));
  s.PrepareForWindowSelection(42); s.BeginLocalSelectionFor(42);
  fake_ndk::barrier().which.store(16); fake_ndk::barrier().held.store(true);
  s.OnEncodedH264Frame(f(101,0xA1));
  bool held=wait([]{return fake_ndk::barrier().waiting.load()>0;});
  if(!held) { fake_ndk::barrier().held.store(false); std::puts("INVALID: no dequeue barrier"); return 2; }
  s.OnEncodedH264Frame(f(202,0xB2));
  std::puts("candidate 202 admitted while candidate 101's dequeue was held");
  fake_ndk::barrier().held.store(false);
  wait([]{std::lock_guard<std::mutex> l(fake_ndk::mu()); return fake_ndk::controls().queued.size()>=2;});
  bool leaked=false; size_t count=0;
  { std::lock_guard<std::mutex> l(fake_ndk::mu());
    for(const auto& q:fake_ndk::controls().queued) { ++count; auto tag=q.empty()?0:q.back();
      std::printf("queued tag=0x%02X\n",tag); if(tag==0xA1) leaked=true; }
  }
  std::printf("local bound=%llu count=%zu obsolete candidate queued=%d\n",(unsigned long long)s.LocalSelectionBoundFor(42),count,leaked);
  s.SetSurface(nullptr,nullptr);
  std::puts(leaked?"REPRODUCED: gate changed before old candidate was queued":"NOT REPRODUCED");
  return leaked?1:0;
}
''',encoding='utf-8')
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
r=subprocess.run([str(root/'candidate_probe.exe')],cwd=root,env=env,capture_output=True,timeout=15,
                 creationflags=subprocess.CREATE_NO_WINDOW)
(root/'candidate_probe.log').write_bytes(r.stdout+r.stderr)
print('probe rc',r.returncode)
print((r.stdout+r.stderr).decode(errors='replace')[-3500:])
