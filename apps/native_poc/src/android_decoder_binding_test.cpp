// keepalive-multi r1 (contract 4): one decoder, one Surface, a PC connection on it at a time. Each PC's controller talks
// to the decoder through its own EntrySinkAdapter with the binding token it was put on screen with. The APK's decoder
// sink itself (android_video_decoder.cpp) against the stand-in NDK.
//
// The counterexamples are the late callbacks of a PC that is no longer on screen (parked, or being closed) arriving
// after another PC was bound: a stream reset, a discontinuity, a frame, a selection answer, an episode start, a stats
// drain, an unbind. None may touch the bound PC's codec, selection or numbers. Negative control: 0.2.30 gave every
// controller the decoder itself (no token) -- the same late reset stops the other PC's picture.

#include <cstdio>
#include <string>
#include <vector>

#include "android_video_decoder.hpp"
#include "entry_sink_adapter.hpp"
#include "fake_ndk.hpp"
#include "poc_protocol.hpp"

using remote60::android_direct::AndroidVideoDecoderSink;
using remote60::android_direct::EntrySinkAdapter;
using namespace remote60::native_poc;

namespace {
int gFailures = 0, gChecks = 0;
void check(const std::string& name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}

UdpH264AssembledFrame frame(uint64_t gen, bool key, uint8_t tag) {
  UdpH264AssembledFrame f;
  f.header.width = 1280;
  f.header.height = 720;
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
    put({0x67, 0x42, 0x00, 0x28, 0xAB});
    put({0x68, 0xCE, 0x3C, 0x80});
    put({0x65, 0x88, tag, tag});
  } else {
    put({0x41, 0x9A, tag, tag});
  }
  return f;
}
size_t queued() {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  return fake_ndk::controls().queued.size();
}
bool last_queued(uint8_t tag) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  const auto& q = fake_ndk::controls().queued;
  return !q.empty() && q.back().size() >= 2 && q.back()[q.back().size() - 1] == tag;
}
int surfaceToken = 1;
jobject surface() { return &surfaceToken; }
ControlWindowSelectedMessage answer(uint64_t gen) {
  ControlWindowSelectedMessage m{};
  m.flags = 0x1u;
  m.streamGeneration = gen;
  return m;
}
}  // namespace

int main() {
  JNIEnv* env = nullptr;
  std::puts("--- [1] A on screen; B (parked / closing) calls in late: nothing of A changes");
  {
    AndroidVideoDecoderSink dec;
    dec.SetSurface(env, surface());
    EntrySinkAdapter a(&dec), b(&dec);
    const uint64_t tokA = 101, tokB = 100;  // B was on screen before (token 100), now A (101)
    b.SetToken(tokB);
    dec.Bind(tokB);
    b.OnEncodedH264Frame(frame(5, true, 0x10));
    // the switch: B leaves (its adapter keeps its stale token -- the late callbacks below carry it), A is bound
    dec.Bind(tokA);
    a.SetToken(tokA);
    a.OnEncodedH264Frame(frame(7, true, 0x20));  // A's IDR
    a.OnEncodedH264Frame(frame(7, false, 0x21));
    check("A's IDR and delta are in the codec", last_queued(0x21));
    const size_t q0 = queued();
    b.OnVideoStreamReset();       // B's controller tearing down (Disconnect -> OnVideoStreamReset)
    b.OnVideoDiscontinuity();
    b.OnEncodedH264Frame(frame(5, false, 0x11));  // a frame of B still in flight
    b.OnDecodeEpisodeStart();
    b.OnWindowSelectionControlResultFor(answer(5), 1);
    const bool bUnbound = dec.Unbind(tokB);       // and B "releasing" the decoder
    check("B's late frame is not queued", queued() == q0);
    check("B's late unbind is refused: A stays bound", !bUnbound && dec.Binding() == tokA);
    a.OnEncodedH264Frame(frame(7, false, 0x22));
    check("A's next DELTA still goes in -- B's late reset did not reset A's codec", last_queued(0x22));
    ClientDecodeStats st{};
    check("B's stats drain gets nothing of A's decode", !b.DrainDecodeStats(&st) && b.DecodeGeneration() == 0);
    check("B's key request / owed key / selection tag are empty", !b.ConsumeDecoderKeyframeRequest() && b.KeyframeOwedFor() == 0 && b.CurrentSelectionTag() == 0);
    dec.SetSurface(env, nullptr);
  }

  std::puts("--- [2] A selection in progress; B's late selection answer does not answer it");
  {
    AndroidVideoDecoderSink dec;
    dec.SetSurface(env, surface());
    EntrySinkAdapter a(&dec), b(&dec);
    dec.Bind(201);
    a.SetToken(201);
    b.SetToken(200);  // stale
    dec.PrepareForWindowSelectionAs(201, 9);
    const uint64_t tagBefore = a.CurrentSelectionTag();
    b.OnWindowSelectionControlResult(answer(30));  // a late answer to B's old selection
    a.OnEncodedH264Frame(frame(30, true, 0x31));    // a frame of that (B's) stream generation
    check("A's selection is still waiting for ITS answer (B's did not arm generation 30)",
          a.CurrentSelectionTag() == tagBefore && tagBefore != 0);
    check("a prepare with B's token is refused", !dec.PrepareForWindowSelectionAs(200, 10));
    dec.SetSurface(env, nullptr);
  }

  std::puts("--- [3] B -> A -> B: the same PC with a new binding; its OLD binding's callbacks are stale too");
  {
    AndroidVideoDecoderSink dec;
    dec.SetSurface(env, surface());
    EntrySinkAdapter b(&dec);
    dec.Bind(300);
    b.SetToken(300);
    b.OnEncodedH264Frame(frame(3, true, 0x41));
    dec.Bind(301);  // A
    dec.Bind(302);  // B again, a new binding
    const uint64_t oldTok = 300;
    b.SetToken(302);
    b.OnEncodedH264Frame(frame(4, true, 0x42));
    const size_t q0 = queued();
    dec.OnEncodedH264FrameAs(oldTok, frame(3, false, 0x43));  // a frame queued under B's first binding
    dec.OnVideoStreamResetAs(oldTok);
    b.OnEncodedH264Frame(frame(4, false, 0x44));
    check("the old binding's frame is dropped and its reset ignored; the new binding's delta goes in",
          queued() == q0 + 1 && last_queued(0x44));
    dec.SetSurface(env, nullptr);
  }

  std::puts("--- [4] NEGATIVE (0.2.30): every controller had the decoder itself -- B's late reset stops A");
  {
    AndroidVideoDecoderSink dec;
    dec.SetSurface(env, surface());
    dec.OnEncodedH264Frame(frame(7, true, 0x50));  // A, through the decoder directly
    dec.OnVideoStreamReset();                       // B's late teardown, through the same decoder
    const size_t q0 = queued();
    dec.OnEncodedH264Frame(frame(7, false, 0x51));  // A's next delta
    check("NEGATIVE: after B's late reset, A's delta no longer reaches a codec (A's picture stops)", queued() == q0);
    dec.SetSurface(env, nullptr);
  }

  if (gFailures == 0) {
    std::printf("\nandroid_decoder_binding_test: %d passed, 0 failed\nRESULT: ALL PASS\n", gChecks);
    return 0;
  }
  std::printf("\nandroid_decoder_binding_test: %d passed, %d failed\nRESULT: FAIL\n", gChecks - gFailures, gFailures);
  return 1;
}
