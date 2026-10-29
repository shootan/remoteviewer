// apk-decoder-inbox r1 (Codex codex_apk_decoder_inbox_decision_r1.md (3), contrasts 1-5): the APK's decoder sink
// (android_video_decoder.cpp, the PRODUCT source) against the stand-in NDK, driven as the session drives it -- after
// each frame the receive thread asks ConsumeDecoderKeyframeRequest (a `want`; the session then hands it to its limiter).
// The criteria are those of the design (caplife/design_apk_decoder_inbox_r1.md): one recovery want per gap episode, a
// usable IDR + suffix already in the inbox used without a want, bounded frames / bytes / wants under a sustained stall,
// no stale want across a new pick. AT ac587dd THEY ARE THE BASELINE: the cases the policy has not been written for yet
// FAIL, and their counts are the numbers to compare a prototype against. Tag: pure-logic + threads, no device.
//
// A gap is made deterministically: the codec's input dequeue is held (fake_ndk kHoldDequeueInput), so the worker stops
// taking frames while the receive side keeps handing them over (it never waits: each call is timed).
//
//   remote60_android_decoder_inbox_test

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "android_video_decoder.hpp"
#include "fake_ndk.hpp"
#include "poc_protocol.hpp"

using remote60::android_direct::AndroidVideoDecoderSink;
using namespace remote60::native_poc;
using Clock = std::chrono::steady_clock;

namespace {
int gFailures = 0, gChecks = 0;
void check(const std::string& name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}
constexpr int64_t kCallBoundUs = 50000;
int64_t us_since(Clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
}
uint32_t gSeq = 0;
UdpH264AssembledFrame frame(uint64_t gen, bool key, uint8_t tag, size_t pad = 0) {
  UdpH264AssembledFrame f;
  f.header.width = 1280;
  f.header.height = 720;
  f.header.flags = key ? 0x1u : 0u;
  f.header.streamGeneration = gen;
  f.header.seq = ++gSeq;
  static std::atomic<uint64_t> capture{1000000};
  f.header.captureQpcUs = capture.fetch_add(33333);
  const uint8_t sc[] = {0, 0, 0, 1};
  auto put = [&](std::initializer_list<uint8_t> nal) {
    f.payload.insert(f.payload.end(), sc, sc + 4);
    f.payload.insert(f.payload.end(), nal.begin(), nal.end());
  };
  if (key) {
    put({0x67, 0x42, 0x00, 0x28, 0xAB});
    put({0x68, 0xCE, 0x3C, 0x80});
    f.payload.insert(f.payload.end(), sc, sc + 4);
    f.payload.push_back(0x65);
  } else {
    f.payload.insert(f.payload.end(), sc, sc + 4);
    f.payload.push_back(0x41);
  }
  f.payload.insert(f.payload.end(), pad, 0x55);
  f.payload.push_back(tag);
  f.payload.push_back(tag);
  return f;
}
template <class F>
bool wait_until(F&& f, int ms) {
  const auto end = Clock::now() + std::chrono::milliseconds(ms);
  while (Clock::now() < end) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return f();
}
void hold(uint32_t which) {
  fake_ndk::barrier().which.store(which);
  fake_ndk::barrier().held.store(true);
}
void let_go() { fake_ndk::barrier().held.store(false); }
bool held_now() { return fake_ndk::barrier().waiting.load() > 0; }
size_t queued() {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  return fake_ndk::controls().queued.size();
}
std::vector<uint8_t> tags_from(size_t from) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  std::vector<uint8_t> out;
  const auto& q = fake_ndk::controls().queued;
  for (size_t i = from; i < q.size(); ++i) out.push_back(q[i].empty() ? 0 : q[i].back());
  return out;
}
uint64_t field(const std::string& status, const char* key) {
  const std::string k = std::string(" ") + key + "=";
  const size_t at = status.find(k);
  return at == std::string::npos ? ~0ull : std::strtoull(status.c_str() + at + k.size(), nullptr, 10);
}
ControlWindowSelectedMessage answer(uint64_t gen) {
  ControlWindowSelectedMessage m{};
  m.flags = 0x1u;
  m.streamGeneration = gen;
  return m;
}
JNIEnv* env = nullptr;
int surfaceA = 1;

// The session's receive thread: hand the frame over, then ask for the decoder's want (as VideoReceiveMain does).
struct Rx {
  AndroidVideoDecoderSink& sink;
  int wants = 0;
  int64_t maxCallUs = 0;
  void send(UdpH264AssembledFrame&& f) {
    const auto t0 = Clock::now();
    sink.OnEncodedH264Frame(std::move(f));
    if (sink.ConsumeDecoderKeyframeRequest()) ++wants;
    maxCallUs = std::max(maxCallUs, us_since(t0));
  }
};

// An answered selection with its codec up and its first IDR in (startup done): returns the generation.
uint64_t start(AndroidVideoDecoderSink& sink, Rx& rx, uint64_t sel, uint64_t gen) {
  sink.SetSurface(env, &surfaceA);
  sink.PrepareForWindowSelection(sel);
  sink.OnWindowSelectionControlResultFor(answer(gen), sel);
  const size_t q0 = queued();
  rx.send(frame(gen, true, 0x80));
  wait_until([&] { return queued() > q0; }, 2000);
  for (int i = 0; i < 3; ++i) {
    rx.send(frame(gen, false, 0x01));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 1000);
  rx.wants = 0;  // startup's own wants (if any) are not the cases'
  return gen;
}
}  // namespace

int main() {
  std::printf("android_decoder_inbox_test (apk-decoder-inbox r1: the design's criteria; at ac587dd the BASELINE)\n");

  std::puts("\n--- [1] steady lossless input after startup: no recovery want");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 11, 101);
    for (int i = 0; i < 120; ++i) {  // 120 frames, an IDR every 30, the codec takes each at once
      rx.send(frame(g, (i % 30) == 29, (i % 30) == 29 ? 0x81 : 0x02));
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 1000);
    const std::string st = sink.DebugStatus();
    check("[1] steady: 0 wants, 0 inbox drops", rx.wants == 0 && field(st, "inboxDrops") == 0,
          "wants=" + std::to_string(rx.wants) + " inboxDrops=" + std::to_string(field(st, "inboxDrops")));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [2] one gap (the worker held while 20 deltas arrive), then deltas, then the next IDR");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 12, 102);
    hold(fake_ndk::kHoldDequeueInput);
    rx.send(frame(g, false, 0x03));  // the worker takes it and stops in the dequeue
    wait_until(held_now, 2000);
    for (int i = 0; i < 20; ++i) rx.send(frame(g, false, 0x04));  // 8 fit; the rest overflow
    let_go();
    for (int i = 0; i < 30; ++i) {
      rx.send(frame(g, false, 0x05));
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    const size_t q0 = queued();
    rx.send(frame(g, true, 0x82));  // the IDR the recovery waited for
    wait_until([&] { return !tags_from(q0).empty(); }, 2000);
    for (int i = 0; i < 10; ++i) rx.send(frame(g, false, 0x06));
    const std::string st = sink.DebugStatus();
    const auto after = tags_from(q0);
    check("[2] one gap episode -> exactly ONE recovery want (the deltas of the same gap add none)", rx.wants == 1,
          "wants=" + std::to_string(rx.wants) + " inboxDrops=" + std::to_string(field(st, "inboxDrops")) +
              " gapDrops=" + std::to_string(field(st, "gapDrops")));
    check("[2] no delta of the broken chain reaches the codec; the next IDR does, and decoding resumes",
          !after.empty() && after.front() == 0x82, "queued after=" + std::to_string(after.size()));
    check("[2] RX never waited", rx.maxCallUs < kCallBoundUs, std::to_string(rx.maxCallUs / 1000) + " ms");
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [3] the inbox already holds a usable IDR + its suffix when it overflows: used, no want");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 13, 103);
    hold(fake_ndk::kHoldDequeueInput);
    rx.send(frame(g, false, 0x07));
    wait_until(held_now, 2000);
    for (int i = 0; i < 6; ++i) rx.send(frame(g, false, 0x08));  // 6 deltas of the old chain
    rx.send(frame(g, true, 0x83));                                // an IDR (the host's periodic one)
    for (int i = 0; i < 3; ++i) rx.send(frame(g, false, 0x09));  // its suffix -> 10 > 8: overflow
    const size_t q0 = queued();
    let_go();
    wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 2000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto after = tags_from(q0);
    int suffix = 0;
    for (uint8_t t : after) suffix += t == 0x09 ? 1 : 0;
    check("[3] recovery from the queued IDR + suffix with NO want", rx.wants == 0, "wants=" + std::to_string(rx.wants));
    check("[3] the IDR then its 3 deltas reach the codec; no old-chain delta after the loss",
          std::find(after.begin(), after.end(), 0x83) != after.end() && suffix == 3,
          "queued after=" + std::to_string(after.size()) + " suffix=" + std::to_string(suffix));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [4] a sustained stall (3 s) while 90 frames arrive (an IDR every 30): bounded");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 14, 104);
    hold(fake_ndk::kHoldDequeueInput);
    rx.send(frame(g, false, 0x0A));
    wait_until(held_now, 2000);
    uint64_t maxInbox = 0, maxBytes = 0;
    const auto t0 = Clock::now();
    for (int i = 0; i < 90; ++i) {
      rx.send(frame(g, (i % 30) == 29, (i % 30) == 29 ? 0x84 : 0x0B, (i % 30) == 29 ? 600000 : 30000));
      const std::string st = sink.DebugStatus();
      maxInbox = std::max(maxInbox, field(st, "inbox"));
      maxBytes = std::max(maxBytes, field(st, "inboxBytes"));
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    const int64_t stallMs = us_since(t0) / 1000;
    let_go();
    wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 2000);
    check("[4] frames / bytes bounded (<= 8, <= 4 MiB) and RX never waited", maxInbox <= 8 && maxBytes <= (4u << 20) &&
          rx.maxCallUs < kCallBoundUs, "inbox max=" + std::to_string(maxInbox) + " bytes max=" + std::to_string(maxBytes) +
          " rx max=" + std::to_string(rx.maxCallUs / 1000) + "ms");
    check("[4] wants bounded: at most 1 per episode + 3 retries over the stall", rx.wants <= 4,
          "wants=" + std::to_string(rx.wants) + " over " + std::to_string(stallMs) + " ms");
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [5] a new pick while a gap episode is open: no stale want for the new selection");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 15, 105);
    hold(fake_ndk::kHoldDequeueInput);
    rx.send(frame(g, false, 0x0C));
    wait_until(held_now, 2000);
    for (int i = 0; i < 9; ++i) sink.OnEncodedH264Frame(frame(g, false, 0x0D));  // overflow: an episode opens
    sink.PrepareForWindowSelection(16);                                         // the user picks another window
    const bool stale = sink.ConsumeDecoderKeyframeRequest();
    let_go();
    check("[5] after the new pick the old episode's want is gone", !stale, std::string("stale want=") + (stale ? "1" : "0"));
    sink.SetSurface(env, nullptr);
  }

  std::printf("\nandroid_decoder_inbox_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
