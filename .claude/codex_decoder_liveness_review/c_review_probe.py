from pathlib import Path
import subprocess, os

repo = Path('C:/work/gnlink-wt/apk-inbox-c')
rev = '5fe90a93985e2ac3cac922764d523de7d043920e'
root = Path(__file__).parent / 'c_5fe90_review'
root.mkdir(exist_ok=True)
for folder, names in [
    ('apps/android_direct_client/app/src/main/cpp/', ['android_video_decoder.cpp','android_video_decoder.hpp','android_video_decoder_sh_seam.hpp']),
    ('apps/native_poc/src/', ['decoder_chain_inbox.hpp','decoder_key_request.hpp','selection_ack_gate.hpp','decoder_pending_slot.hpp']),
    ('apps/native_poc/src/test_fake_ndk/', ['fake_ndk.cpp','fake_ndk.hpp'])]:
    for name in names:
        (root/name).write_bytes(subprocess.check_output(['git','-C',str(repo),'show',rev+':'+folder+name]))

code = r'''
#include "android_video_decoder.hpp"
#include "decoder_chain_inbox.hpp"
#include "fake_ndk.hpp"
#include <chrono>
#include <thread>
#include <cstdio>
#include <algorithm>
using namespace remote60::native_poc;
using remote60::android_direct::AndroidVideoDecoderSink;
struct Info { ChainAuInfo operator()(const ChainAuInfo& a) const {return a;} };
using Q=ChainInbox<ChainAuInfo,Info>;
void add(Q& q,unsigned seq,bool key) {q.Offer(ChainAuInfo{1,seq,key,20});}
UdpH264AssembledFrame f(unsigned seq,bool key) {
 UdpH264AssembledFrame x; x.header.seq=seq;x.header.streamGeneration=1;
 x.header.width=1280;x.header.height=720;x.header.flags=key?1:0;x.header.captureQpcUs=1000000+seq*33333;
 if(key) x.payload={0,0,0,1,0x67,0x42,0,0x28,0xAB,0,0,0,1,0x68,0xCE,0x3C,0x80,0,0,0,1,0x65};
 else x.payload={0,0,0,1,0x41};
 x.payload.push_back((uint8_t)seq);x.payload.push_back((uint8_t)seq);return x;
}
void pause(int n) {std::this_thread::sleep_for(std::chrono::milliseconds(n));}
size_t queued(){std::lock_guard<std::mutex> l(fake_ndk::mu());return fake_ndk::controls().queued.size();}
void available(bool b){std::lock_guard<std::mutex> l(fake_ndk::mu());fake_ndk::controls().inputAvailable=b;}
int main(){
 Q q(8,4096,{}); add(q,12,false);add(q,13,false);add(q,14,true);q.MarkLoss(11);
 ChainAuInfo out{};q.Pop(&out);
 printf("MARKLOSS loss11 queued12,13,IDR14: popped=%u key=%d gap=%d (expected skip to14)\n",out.seq,out.key,q.gapOpen());
 Q r(8,4096,{});add(r,100,true);r.Pop(&out);
 for(int i=0;i<30;i++)r.Offer(ChainAuInfo{1,99,false,20});
 auto rr=r.Offer(ChainAuInfo{1,99,false,20});
 printf("ORDER 31st old delta99 after100: admitted=%d resyncs=%llu\n",rr.admit==ChainAdmit::Admitted,(unsigned long long)r.orderResyncs());
 AndroidVideoDecoderSink s;s.SetSurface(nullptr,reinterpret_cast<jobject>(0x101));
 s.OnEncodedH264Frame(f(1,true));for(int i=0;i<1000&&queued()<1;i++)pause(2);
 if(queued()<1){puts("INVALID initial IDR not queued");return 2;}
 available(false);s.OnEncodedH264Frame(f(2,false));pause(100);
 s.OnEncodedH264Frame(f(3,true));pause(100);
 printf("PENDING before release: %s\n",s.DebugStatus().c_str());
 available(true);for(int i=0;i<1000&&queued()<2;i++)pause(2);
 pause(30);s.OnEncodedH264Frame(f(4,false));pause(100);
 printf("PENDING after IDR3 committed and delta4 offered: %s\n",s.DebugStatus().c_str());
 {std::lock_guard<std::mutex> l(fake_ndk::mu());printf("QUEUED tags:");for(const auto& p:fake_ndk::controls().queued)printf(" %u",p.back());puts("");}
 s.SetSurface(nullptr,nullptr);return 0;
}
'''
(root/'probe.cpp').write_text(code, encoding='utf-8')
ms=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
sdk=Path('C:/Program Files (x86)/Windows Kits/10')
tmp=root/'tmp';tmp.mkdir(exist_ok=True)
env=os.environ.copy();env['TMP']=env['TEMP']=str(tmp)
args=[str(ms/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++17','/EHsc','/utf-8','/MD','/DNOMINMAX','/I'+str(root),'/I'+str(repo/'apps/native_poc/src/test_fake_ndk'),'/I'+str(repo/'apps/native_poc/src'),'/I'+str(ms/'include')]
args += ['/I'+str(sdk/'Include/10.0.26100.0'/s) for s in ['ucrt','um','shared','winrt','cppwinrt']]
args += ['probe.cpp','android_video_decoder.cpp','fake_ndk.cpp','/Fe:probe.exe','/link','/LIBPATH:'+str(ms/'lib/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/ucrt/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/um/x64'),'ws2_32.lib']
r=subprocess.run(args,cwd=root,env=env,capture_output=True)
(root/'build.log').write_bytes(r.stdout+r.stderr)
print('build',r.returncode)
if r.returncode: print((r.stdout+r.stderr).decode(errors='replace')[-2200:]);raise SystemExit(r.returncode)
r=subprocess.run([str(root/'probe.exe')],cwd=root,env=env,capture_output=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
(root/'probe.log').write_bytes(r.stdout+r.stderr)
print('probe',r.returncode);print(r.stdout.decode(errors='replace'))
