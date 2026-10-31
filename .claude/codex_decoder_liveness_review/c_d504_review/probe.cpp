
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
 printf("ORDER 31st old delta99 after100: admitted=%d (expected zero)\n",rr.admit==ChainAdmit::Admitted);
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
