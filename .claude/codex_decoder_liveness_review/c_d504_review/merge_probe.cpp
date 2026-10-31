
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
 AndroidVideoDecoderSink s;s.SetSurface(nullptr,reinterpret_cast<jobject>(0x101));
 auto waitq=[&](size_t n){for(int i=0;i<1000&&queued()<n;i++)pause(2);return queued()>=n;};
 auto lose=[&](unsigned seq){auto x=f(seq,false);x.payload.resize((4u<<20)+1);s.OnEncodedH264Frame(std::move(x));};
 s.OnEncodedH264Frame(f(1,true));if(!waitq(1))return 2;
 lose(2);DecoderKeyRequest oldReq{};if(!s.TakeDecoderKeyRequest(&oldReq))return 2;
 s.OnEncodedH264Frame(f(3,true));if(!waitq(2))return 2;pause(10);
 lose(4);DecoderKeyRequest current{};if(!s.TakeDecoderKeyRequest(&current))return 2;
 auto merged=current;merged.Merge(oldReq);
 auto good=s.DecoderKeyRequestValidCauses(current),bad=s.DecoderKeyRequestValidCauses(merged);
 printf("SAME EPOCH late old publish: epoch=%llu currentEpisode=%llu oldEpisode=%llu mergedEpisode=%llu validNew=%u validMerged=%u\n",(unsigned long long)current.sinkEpoch,(unsigned long long)current.episodeId,(unsigned long long)oldReq.episodeId,(unsigned long long)merged.episodeId,good,bad);
 bool a=(good&kKeyCauseRecovery)!=0&&bad==0;
 s.SetSurface(nullptr,reinterpret_cast<jobject>(0x202));
 s.OnEncodedH264Frame(f(5,true));if(!waitq(3))return 2;pause(10);
 lose(6);DecoderKeyRequest newer{};if(!s.TakeDecoderKeyRequest(&newer))return 2;
 merged=newer;merged.Merge(oldReq);
 good=s.DecoderKeyRequestValidCauses(newer);bad=s.DecoderKeyRequestValidCauses(merged);
 printf("NEW EPOCH late old publish: currentEpoch=%llu oldEpoch=%llu mergedEpoch=%llu validNew=%u validMerged=%u\n",(unsigned long long)newer.sinkEpoch,(unsigned long long)oldReq.sinkEpoch,(unsigned long long)merged.sinkEpoch,good,bad);
 bool b=(good&kKeyCauseRecovery)!=0&&bad==0;
 s.SetSurface(nullptr,nullptr);return a&&b?0:1;
}
