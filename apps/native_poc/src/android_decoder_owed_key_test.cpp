// t-970r4zgo r5 N1: the APK's decoder sink -- android_video_decoder.cpp itself, compiled against a
// stand-in NDK (test_fake_ndk/) -- and the IDR a selection is owed.
//
// An answered selection whose own IDR came before the answer (and was dropped) is owed an IDR of
// its generation. Passing the selection gate is not paying that: the decoder can still drop the
// frame for want of a surface or a codec (create / configure / start), or hold it for want of an
// input buffer and lose it to a codec reset. The duty ends only when such an IDR is queued into
// the codec. A surface arriving re-arms an owed duty, so the session asks afresh.
//
// What the stand-in cannot show: MediaCodec's own behaviour (it never decodes or outputs), a real
// Surface's lifetime on a device.

#include <cstdio>
#include <string>
#include <vector>

#include "android_video_decoder.hpp"
#include "fake_ndk.hpp"
#include "poc_protocol.hpp"

using remote60::android_direct::AndroidVideoDecoderSink;
using namespace remote60::native_poc;

namespace {

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}

// An Annex-B access unit: SPS + PPS + one slice (IDR or not). `tag` marks it in the codec's input.
UdpH264AssembledFrame frame(uint64_t gen, bool key, uint8_t tag) {
  UdpH264AssembledFrame f;
  f.header.width = 1920;
  f.header.height = 1080;
  f.header.flags = key ? 0x1u : 0u;
  f.header.streamGeneration = gen;
  static uint64_t capture = 1000000;
  capture += 16667;
  f.header.captureQpcUs = capture;
  const uint8_t sc[] = {0, 0, 0, 1};
  auto put = [&](std::initializer_list<uint8_t> nal) {
    f.payload.insert(f.payload.end(), sc, sc + 4);
    f.payload.insert(f.payload.end(), nal.begin(), nal.end());
  };
  if (key) {
    put({0x67, 0x42, 0x00, 0x28, 0xAB});  // SPS
    put({0x68, 0xCE, 0x3C, 0x80});        // PPS
    put({0x65, 0x88, tag, tag});          // IDR slice
  } else {
    put({0x41, 0x9A, tag, tag});          // non-IDR slice
  }
  return f;
}

size_t queued_count() {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  return fake_ndk::controls().queued.size();
}

bool last_queued_has(uint8_t tag) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  const auto& q = fake_ndk::controls().queued;
  if (q.empty()) return false;
  const auto& p = q.back();
  return p.size() >= 2 && p[p.size() - 1] == tag && p[p.size() - 2] == tag;
}

template <class F>
void with_controls(F f) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  f(fake_ndk::controls());
}

ControlWindowSelectedMessage answer(uint64_t gen) {
  ControlWindowSelectedMessage m{};
  m.flags = 0x1u;
  m.streamGeneration = gen;
  return m;
}

// The sink's state for one selection whose IDR came before its answer: owed.
void owe(AndroidVideoDecoderSink& sink, uint64_t selection, uint64_t gen) {
  sink.PrepareForWindowSelection(selection);
  sink.OnEncodedH264Frame(frame(gen, true, 0x10));  // its IDR, before the answer: dropped
  sink.OnWindowSelectionControlResultFor(answer(gen), selection);
}

int surfaceToken = 1;  // any non-null pointer is a "surface"
jobject surface() { return &surfaceToken; }

}  // namespace

int main() {
  {
    std::puts("\n--- 1. the owed IDR arrives while there is no surface yet ---");
    AndroidVideoDecoderSink sink;
    owe(sink, 5, 40);
    const uint64_t t1 = sink.KeyframeOwedFor();
    check("answered after its IDR was dropped: owed", t1 != 0);
    const size_t q0 = queued_count();
    sink.OnEncodedH264Frame(frame(40, true, 0x21));  // the IDR asked for -- but no surface
    check("the IDR passed the gate but no codec could take it: nothing queued", queued_count() == q0);
    check("STILL OWED (the duty is not paid by passing the gate)", sink.KeyframeOwedFor() != 0,
          "token=" + std::to_string(sink.KeyframeOwedFor()));
    JNIEnv* env = nullptr;
    sink.SetSurface(env, surface());
    const uint64_t t2 = sink.KeyframeOwedFor();
    check("the surface arrives: still owed, re-armed (a new token -- the session asks afresh)", t2 != 0 && t2 != t1,
          std::to_string(t1) + " -> " + std::to_string(t2));
    sink.OnEncodedH264Frame(frame(40, false, 0x22));  // a delta of that generation
    check("a delta of that generation does not pay it", sink.KeyframeOwedFor() != 0);
    sink.OnEncodedH264Frame(frame(40, true, 0x23));  // the IDR asked for afresh
    check("the next IDR of that generation is QUEUED into the codec", last_queued_has(0x23));
    check("and only that pays it: nothing owed", sink.KeyframeOwedFor() == 0);
    sink.SetSurface(env, nullptr);
  }
  {
    std::puts("\n--- 2. a surface, but the codec cannot be created / configured / started ---");
    const char* what[] = {"created", "configured", "started"};
    for (int k = 0; k < 3; ++k) {
      AndroidVideoDecoderSink sink;
      JNIEnv* env = nullptr;
      sink.SetSurface(env, surface());
      owe(sink, 6, 41);
      with_controls([&](fake_ndk::Controls& c) {
        c.createFails = k == 0;
        c.configureFails = k == 1;
        c.startFails = k == 2;
      });
      const size_t q0 = queued_count();
      sink.OnEncodedH264Frame(frame(41, true, 0x31));
      check(std::string("codec cannot be ") + what[k] + ": the IDR is not queued, still owed",
            queued_count() == q0 && sink.KeyframeOwedFor() != 0);
      with_controls([](fake_ndk::Controls& c) { c.createFails = c.configureFails = c.startFails = false; });
      sink.OnEncodedH264Frame(frame(41, true, 0x32));
      check("the codec comes up: the next IDR is queued and pays it", last_queued_has(0x32) && sink.KeyframeOwedFor() == 0);
      sink.SetSurface(env, nullptr);
    }
  }
  {
    std::puts("\n--- 3. no free input buffer: the IDR is held, then lost to a codec reset ---");
    AndroidVideoDecoderSink sink;
    JNIEnv* env = nullptr;
    sink.SetSurface(env, surface());
    owe(sink, 7, 42);
    with_controls([](fake_ndk::Controls& c) { c.inputAvailable = false; });
    const size_t q0 = queued_count();
    sink.OnEncodedH264Frame(frame(42, true, 0x41));
    check("no input buffer: held, not queued, still owed", queued_count() == q0 && sink.KeyframeOwedFor() != 0);
    const uint64_t tHeld = sink.KeyframeOwedFor();
    sink.OnVideoDiscontinuity();  // the codec is reset; the held IDR goes with it
    check("lost with the codec: still owed", sink.KeyframeOwedFor() != 0 && sink.KeyframeOwedFor() == tHeld);
    with_controls([](fake_ndk::Controls& c) { c.inputAvailable = true; });
    sink.OnEncodedH264Frame(frame(42, true, 0x42));
    check("an IDR queued afterwards pays it", last_queued_has(0x42) && sink.KeyframeOwedFor() == 0);
    sink.SetSurface(env, nullptr);
  }
  {
    std::puts("\n--- 4. held, then queued once a buffer frees up ---");
    AndroidVideoDecoderSink sink;
    JNIEnv* env = nullptr;
    sink.SetSurface(env, surface());
    owe(sink, 8, 43);
    with_controls([](fake_ndk::Controls& c) { c.inputAvailable = false; });
    sink.OnEncodedH264Frame(frame(43, true, 0x51));
    check("held: still owed", sink.KeyframeOwedFor() != 0);
    with_controls([](fake_ndk::Controls& c) { c.inputAvailable = true; });
    sink.OnEncodedH264Frame(frame(43, false, 0x52));  // the next frame pumps the held one first
    bool heldQueued = false;
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      for (const auto& p : fake_ndk::controls().queued) {
        if (p.size() >= 2 && p[p.size() - 1] == 0x51) heldQueued = true;
      }
    }
    check("the held IDR is queued when a buffer frees up, and that pays it", heldQueued && sink.KeyframeOwedFor() == 0);
    sink.SetSurface(env, nullptr);
  }
  {
    std::puts("\n--- 5. what does not pay it, and what ends it ---");
    AndroidVideoDecoderSink sink;
    JNIEnv* env = nullptr;
    sink.SetSurface(env, surface());
    owe(sink, 9, 44);
    sink.OnEncodedH264Frame(frame(45, true, 0x61));  // another generation: not admitted
    check("an IDR of another generation does not pay it", sink.KeyframeOwedFor() != 0);
    sink.AbortWindowSelection();
    check("the switch abandoned: nothing owed", sink.KeyframeOwedFor() == 0);
    owe(sink, 10, 46);
    check("a new selection owes its own", sink.KeyframeOwedFor() != 0);
    sink.PrepareForWindowSelection(11);
    check("a newer pick: the old duty is gone", sink.KeyframeOwedFor() == 0);
    sink.SetSurface(env, nullptr);
  }

  {
    std::puts("\n--- 6. the answer came first (nothing owed), then its IDR finds no surface ---");
    AndroidVideoDecoderSink sink;
    sink.PrepareForWindowSelection(12);
    sink.OnWindowSelectionControlResultFor(answer(47), 12);  // nothing of gen 47 dropped before it
    check("answered first: nothing owed", sink.KeyframeOwedFor() == 0);
    sink.OnEncodedH264Frame(frame(47, true, 0x71));  // its IDR -- no surface yet
    const uint64_t t1 = sink.KeyframeOwedFor();
    check("its IDR dropped for want of a surface: now owed", t1 != 0);
    JNIEnv* env = nullptr;
    sink.SetSurface(env, surface());
    check("the surface arrives: re-armed", sink.KeyframeOwedFor() != 0 && sink.KeyframeOwedFor() != t1);
    sink.OnEncodedH264Frame(frame(47, true, 0x72));
    check("the IDR asked for is queued and pays it", last_queued_has(0x72) && sink.KeyframeOwedFor() == 0);
    sink.SetSurface(env, nullptr);
  }
  {
    std::puts("\n--- 7. nothing owed, its IDR held for a buffer, then lost to a codec reset ---");
    AndroidVideoDecoderSink sink;
    JNIEnv* env = nullptr;
    sink.SetSurface(env, surface());
    sink.PrepareForWindowSelection(13);
    sink.OnWindowSelectionControlResultFor(answer(48), 13);
    with_controls([](fake_ndk::Controls& c) { c.inputAvailable = false; });
    sink.OnEncodedH264Frame(frame(48, true, 0x81));  // codec created, no buffer: held
    check("held, nothing owed yet", sink.KeyframeOwedFor() == 0);
    sink.OnVideoDiscontinuity();
    check("the held IDR lost with the codec: now owed", sink.KeyframeOwedFor() != 0);
    with_controls([](fake_ndk::Controls& c) { c.inputAvailable = true; });
    sink.OnEncodedH264Frame(frame(48, true, 0x82));
    check("an IDR queued afterwards pays it", last_queued_has(0x82) && sink.KeyframeOwedFor() == 0);
    sink.SetSurface(env, nullptr);
  }

  if (gFailures == 0) {
    std::printf("\nRESULT: ALL PASS  (%d checks, 0 failed)\n", gChecks);
    return 0;
  }
  std::printf("\nRESULT: %d FAILED  (%d checks)\n", gFailures, gChecks);
  return 1;
}
