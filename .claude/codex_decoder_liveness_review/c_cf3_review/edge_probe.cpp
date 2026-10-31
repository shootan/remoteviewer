
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
