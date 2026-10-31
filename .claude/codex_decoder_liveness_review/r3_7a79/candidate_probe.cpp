
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
