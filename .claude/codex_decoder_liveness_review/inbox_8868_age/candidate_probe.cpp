
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
  AndroidVideoDecoderSink sink;
  sink.SetSurface(nullptr,reinterpret_cast<jobject>(0x101));
  sink.PrepareForWindowSelection(90);
  ControlWindowSelectedMessage ack{}; ack.flags=1; ack.streamGeneration=303;
  sink.OnWindowSelectionControlResultFor(ack,90);
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().inputAvailable=false; }
  auto t0=std::chrono::steady_clock::now();
  sink.OnEncodedH264Frame(f(303,0xA1));
  bool held=wait([&]{return sink.DebugStatus().find("pending=1 ")!=std::string::npos;});
  if(!held) { std::puts("INVALID no held frame"); return 2; }
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  auto delta=f(303,0xB2); delta.header.flags=0; delta.payload={0,0,0,1,0x41,0xB2};
  sink.OnEncodedH264Frame(std::move(delta));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  { std::lock_guard<std::mutex> l(fake_ndk::mu()); fake_ndk::controls().inputAvailable=true; }
  bool queued=wait([]{std::lock_guard<std::mutex> l(fake_ndk::mu());return !fake_ndk::controls().queued.empty();});
  auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count();
  std::printf("held=%d queued=%d actual IDR age at least 280ms, observed elapsed=%lldms\n",held,queued,(long long)ms);
  std::printf("%s\n",sink.LivenessTraceLine().c_str());
  sink.SetSurface(nullptr,nullptr);
  return queued?0:2;
}
