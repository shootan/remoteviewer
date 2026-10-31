
#include "android_video_decoder.hpp"
#include "fake_ndk.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
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
  std::puts("generation 202 passed RX Admit while generation 101 had a queue reservation");
  fake_ndk::barrier().held.store(false);
  wait([]{std::lock_guard<std::mutex> l(fake_ndk::mu()); return fake_ndk::controls().queued.size()>=2;});
  bool leaked=false; size_t count=0;
  { std::lock_guard<std::mutex> l(fake_ndk::mu());
    for(const auto& q:fake_ndk::controls().queued) { ++count; auto tag=q.empty()?0:q.back();
      std::printf("queued tag=0x%02X\n",tag); if(tag==0xA1) leaked=true; }
  }
  std::printf("local bound=%llu count=%zu generation101_observed=%d\n",(unsigned long long)s.LocalSelectionBoundFor(42),count,leaked);
  s.SetSurface(nullptr,nullptr);
  std::puts("raw generation101 observation above is not the r4 verdict; the contract verdict follows");
  
  const auto bound = s.LocalSelectionBoundFor(42);
  bool candidateOk = bound == 101;
  { std::lock_guard<std::mutex> l(fake_ndk::mu());
    for(const auto& q:fake_ndk::controls().queued) if(!q.empty() && q.back()==0xB2) candidateOk=false;
  }
  std::printf("CONTRACT first queued generation binds: %s bound=%llu\n",candidateOk?"PASS":"FAIL",(unsigned long long)bound);

  // Actual owner change at the same input-dequeue barrier.
  size_t before;
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); before=fake_ndk::controls().queued.size(); }
  AndroidVideoDecoderSink own;
  own.Bind(100); own.SetSurface(nullptr,reinterpret_cast<jobject>(0x303));
  fake_ndk::barrier().held.store(true);
  own.OnEncodedH264FrameAs(100,f(0,0xD4));
  bool ownerHeld=wait([]{return fake_ndk::barrier().waiting.load()>0;});
  own.Bind(200);
  fake_ndk::barrier().held.store(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  own.OnEncodedH264FrameAs(200,f(0,0xE5));
  wait([&]{std::lock_guard<std::mutex> l(fake_ndk::mu()); return fake_ndk::controls().queued.size()>before;});
  bool ownerOk=ownerHeld;
  { std::lock_guard<std::mutex> l(fake_ndk::mu());
    const auto& q=fake_ndk::controls().queued;
    for(size_t k=before;k<q.size();++k) if(q[k].empty() || q[k].back()!=0xE5) ownerOk=false;
    ownerOk=ownerOk && q.size()>before;
  }
  std::printf("CONTRACT owner change during dequeue: %s\n",ownerOk?"PASS":"FAIL");
  own.SetSurface(nullptr,nullptr);

  AndroidVideoDecoderSink retry;
  retry.SetSurface(nullptr,reinterpret_cast<jobject>(0x404));
  retry.PrepareForWindowSelection(90);
  ControlWindowSelectedMessage ack{}; ack.flags=1; ack.streamGeneration=303;
  retry.OnWindowSelectionControlResultFor(ack,90);
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().createFails=true; }
  retry.OnEncodedH264Frame(f(303,0xC3));
  wait([&]{return retry.DebugStatus().find("codecFail=1 ")!=std::string::npos;});
  for(int k=0;k<40;++k) {
    auto delta=f(303,0x44); delta.header.flags=0; delta.payload={0,0,0,1,0x41,0x44};
    retry.OnEncodedH264Frame(std::move(delta));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  auto status=retry.DebugStatus();
  std::printf("DELTA FAILURE SNAPSHOT %s\n",status.c_str());
  auto field=[&](const char* key) { auto at=status.find(key); return at==std::string::npos?0ull:std::stoull(status.substr(at+std::strlen(key))); };
  bool retryOk=field("codecFail=")<=4;
  std::printf("CONTRACT two seconds, one IDR plus 40 deltas: failures=%llu timerRetries=%llu %s\n",
    field("codecFail="),field("codecRetries="),retryOk?"PASS":"FAIL (deltas bypass retry cadence and budget)");
  retry.SetSurface(nullptr,nullptr);
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().createFails=false; }
  return candidateOk && ownerOk && retryOk ? 0:1;

}
