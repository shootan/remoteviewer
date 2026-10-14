// mobile-abr r1: what the APK's decoder sink reports to the session as "decoded" -- android_video_decoder.cpp
// itself, compiled against the stand-in NDK (test_fake_ndk/), whose codec now hands out the outputs this
// test queues (Codex contract section 3: decodedFps = valid decoded pictures where the decoder gives them
// out, not their later release to the display; format change / config / end-of-stream are no picture;
// a replayed picture is not progress; no surface or a selection between generations is "not valid").
//
// r3 (Codex candidate review R1): section 9 drives the sink's own bootstrap replay (PumpCodecLocked ->
// TryQueueFrameLocked of the held selection IDR), and the stand-in codec answers every queued input with
// an output carrying the PTS the sink queued it with -- a replayed input gets a NEW PTS from the playout
// clock, so only where it was queued can tell it apart. Ordinary inputs, synthetic AUs and a new codec's
// output after a loss reset still count.
//
// What the stand-in cannot show: MediaCodec's own timing and buffer behaviour, a device's Surface.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

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

UdpH264AssembledFrame frame(uint64_t gen, bool key, uint8_t tag) {
  UdpH264AssembledFrame f;
  f.header.width = 1920;
  f.header.height = 1080;
  f.header.flags = key ? 0x1u : 0u;
  f.header.streamGeneration = gen;
  static uint64_t capture = 1000000;
  capture += 33333;
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

template <class F>
void with_controls(F f) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  f(fake_ndk::controls());
}
void picture(int64_t pts, uint32_t flags = 0, int32_t size = 1) {
  with_controls([&](fake_ndk::Controls& c) {
    fake_ndk::Controls::Output o;
    o.presentationTimeUs = pts;
    o.flags = flags;
    o.size = size;
    c.outputs.push_back(o);
  });
}
void format_change(int32_t w, int32_t h) {
  with_controls([&](fake_ndk::Controls& c) {
    fake_ndk::Controls::Output o;
    o.formatChange = true;
    o.width = w;
    o.height = h;
    c.outputs.push_back(o);
  });
}
int released() {
  int r = 0;
  with_controls([&](fake_ndk::Controls& c) { r = c.released; });
  return r;
}
bool outputs_drained() {
  bool e = false;
  with_controls([&](fake_ndk::Controls& c) { e = c.outputs.empty(); });
  return e;
}
size_t queued_inputs() {
  size_t n = 0;
  with_controls([&](fake_ndk::Controls& c) { n = c.queuedPts.size(); });
  return n;
}
// The codec "decodes" queued inputs [from, to): one output each, with the PTS it was queued with.
void answer_queued(size_t from, size_t to) {
  with_controls([&](fake_ndk::Controls& c) {
    for (size_t i = from; i < to && i < c.queuedPts.size(); ++i) {
      fake_ndk::Controls::Output o;
      o.presentationTimeUs = c.queuedPts[i];
      c.outputs.push_back(o);
    }
  });
}
int64_t steady_us() {
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
// The sink's output pump runs every 3 ms; give it time to take what was queued.
void settle(int ms = 60) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int surfaceToken = 1;
jobject surface() { return &surfaceToken; }

struct Drained {
  bool valid = false;
  ClientDecodeStats s;
};
Drained drain(AndroidVideoDecoderSink& sink) {
  Drained d;
  d.valid = sink.DrainDecodeStats(&d.s);
  return d;
}

}  // namespace

int main() {
  JNIEnv* env = nullptr;
  AndroidVideoDecoderSink sink;
  {
    std::puts("\n--- 0. before a surface ---");
    const Drained d = drain(sink);
    check("no surface: the decode state is not valid (the session reports no decode field)", !d.valid);
  }
  sink.SetSurface(env, surface());
  sink.OnEncodedH264Frame(frame(1, true, 0x01));  // the IDR: the codec is created and started
  (void)drain(sink);                               // a fresh window
  {
    std::puts("\n--- 1. valid pictures, past due (released at once) ---");
    picture(1000);
    picture(2000);
    picture(3000);
    settle();
    const Drained d = drain(sink);
    check("three decoded pictures count three", d.valid && d.s.decodedFrames == 3,
          "valid=" + std::to_string(d.valid) + " decoded=" + std::to_string(d.s.decodedFrames));
    check("...and a second drain right after counts none", drain(sink).s.decodedFrames == 0);
  }
  {
    std::puts("\n--- 2. a picture held for its display slot ---");
    const int r0 = released();
    picture(steady_us() + 300000);  // due in 300 ms: held in the codec until then
    settle();
    const Drained d = drain(sink);
    check("counted when the decoder gave it out, before its release", d.s.decodedFrames == 1 && released() == r0,
          "decoded=" + std::to_string(d.s.decodedFrames) + " releasedSince=" + std::to_string(released() - r0));
    settle(400);
    check("released at its slot", released() == r0 + 1);
    check("...and not counted again then", drain(sink).s.decodedFrames == 0);
  }
  {
    std::puts("\n--- 3. buffers that carry no picture ---");
    picture(4000, 2);     // codec config
    picture(5000, 4, 0);  // end of stream, empty
    settle();
    check("a codec-config buffer and an EMPTY end-of-stream buffer are not decoded pictures",
          drain(sink).s.decodedFrames == 0);
    picture(5500, 4, 1);  // end of stream carrying the last picture
    settle();
    check("r3: an end-of-stream buffer that carries a picture counts it (only EOS-only is left out)",
          drain(sink).s.decodedFrames == 1);
  }
  {
    std::puts("\n--- 4. an output format change ---");
    format_change(1280, 720);
    picture(6000);
    settle();
    const Drained d = drain(sink);
    check("the format change is no picture; the size is the decoder's output format",
          d.s.decodedFrames == 1 && d.s.width == 1280 && d.s.height == 720,
          std::to_string(d.s.decodedFrames) + " " + std::to_string(d.s.width) + "x" + std::to_string(d.s.height));
  }
  {
    std::puts("\n--- 5. two outputs of one PTS that are not a replay ---");
    picture(7000);
    picture(7000);
    settle();
    // r3 R1: a replay is known by where it was queued (section 9), not by a repeated PTS -- a real replay
    // gets a new PTS, and two ordinary outputs are two pictures.
    check("r3: two ordinary outputs count two, whatever their PTS", drain(sink).s.decodedFrames == 2);
  }
  {
    std::puts("\n--- 6. the surface goes and comes back ---");
    picture(8000);
    settle();
    sink.SetSurface(env, nullptr);
    const Drained gone = drain(sink);
    check("without a surface the window is not valid (no \"0 fps\" for a decoder that cannot run)", !gone.valid);
    sink.SetSurface(env, surface());
    sink.OnEncodedH264Frame(frame(1, true, 0x02));
    picture(9000);
    settle();
    const Drained across = drain(sink);
    check("r3: the window the surface came back in spans a generation change -- withheld, not mixed",
          !across.valid);
    picture(9500);
    settle();
    const Drained back = drain(sink);
    check("the next window is valid and counts only its own picture",
          back.valid && back.s.decodedFrames == 1, "decoded=" + std::to_string(back.s.decodedFrames));
    // r3 R2: the surface goes and comes back BETWEEN two drains -- the window spans both states.
    picture(9600);
    settle();
    sink.SetSurface(env, nullptr);
    sink.SetSurface(env, surface());
    sink.OnEncodedH264Frame(frame(1, true, 0x05));
    picture(9700);
    settle();
    const Drained roundTrip = drain(sink);
    check("r3: a surface round trip between two drains -- that window is withheld, valid on both ends or not",
          !roundTrip.valid);
    picture(9800);
    settle();
    check("...and the window after it counts its own picture", drain(sink).s.decodedFrames == 1);
  }
  {
    std::puts("\n--- 7. a selection between generations ---");
    sink.PrepareForWindowSelection(7);
    const Drained wait = drain(sink);
    check("a selection waiting for its answer: not valid", !wait.valid);
    ControlWindowSelectedMessage ack{};
    ack.flags = 0x1u;
    ack.streamGeneration = 70;
    sink.OnWindowSelectionControlResultFor(ack, 7);
    check("answered, but its first IDR not yet in: still not valid", !drain(sink).valid);
    sink.OnEncodedH264Frame(frame(70, true, 0x03));
    picture(10000);
    settle();
    check("r3: the window its first picture arrived in spans the generation change -- withheld",
          !drain(sink).valid);
    picture(10500);
    settle();
    const Drained ready = drain(sink);
    check("then valid, counting from there", ready.valid && ready.s.decodedFrames == 1,
          "valid=" + std::to_string(ready.valid) + " decoded=" + std::to_string(ready.s.decodedFrames));
  }
  {
    std::puts("\n--- 8. a codec reset for a loss ---");
    sink.OnVideoDiscontinuity();
    const Drained after = drain(sink);
    check("a loss reset stays valid (frames that do not decode now are a real stall) and counts 0",
          after.valid && after.s.decodedFrames == 0);
    sink.OnEncodedH264Frame(frame(70, true, 0x04));
    picture(11000);
    picture(12000);
    settle();
    check("after the IDR it counts on, no negative or burst", drain(sink).s.decodedFrames == 2);
  }
  {
    std::puts("\n--- 9. the bootstrap replay (r3 R1): a re-queued IDR is not progress ---");
    sink.PrepareForWindowSelection(9);
    ControlWindowSelectedMessage ack{};
    ack.flags = 0x1u;
    ack.streamGeneration = 90;
    sink.OnWindowSelectionControlResultFor(ack, 9);
    const size_t q0 = queued_inputs();
    sink.OnEncodedH264Frame(frame(90, true, 0x09));  // the selection's IDR: queued, no output yet
    // No output for it: the sink re-queues the same IDR every 250 ms, at most twice (kBootstrapReplay*).
    for (int i = 0; i < 4; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      (void)sink.ReadySelectionGeneration();  // a sink call that pumps the codec, as the UI's poll does
    }
    const size_t q1 = queued_inputs();
    std::vector<int64_t> pts;
    with_controls([&](fake_ndk::Controls& c) { pts.assign(c.queuedPts.begin() + q0, c.queuedPts.end()); });
    std::string ptsList;
    for (int64_t p : pts) ptsList += std::to_string(p) + " ";
    check("the IDR went in once and was re-queued twice, each time with a new PTS",
          q1 - q0 == 3 && pts.size() == 3 && pts[0] != pts[1] && pts[1] != pts[2], ptsList);
    (void)drain(sink);
    // The codec answers the original input first: that picture makes the selection ready (a generation
    // change, so its window is withheld). Then it answers the two replayed inputs.
    answer_queued(q0, q0 + 1);
    settle(400);
    (void)drain(sink);
    answer_queued(q0 + 1, q1);
    settle(700);
    const Drained d = drain(sink);
    check("THE REPLAYED INPUTS' OUTPUTS DO NOT COUNT AS PROGRESS: 0 pictures in the window they came out in",
          d.valid && d.s.decodedFrames == 0,
          "valid=" + std::to_string(d.valid) + " decoded=" + std::to_string(d.s.decodedFrames));
    // Ordinary inputs afterwards, a synthetic (static-refresh) AU among them: each counts.
    const size_t q2 = queued_inputs();
    sink.OnEncodedH264Frame(frame(90, false, 0x0A));
    UdpH264AssembledFrame synth = frame(90, false, 0x0B);
    synth.header.flags |= kEncodedFrameFlagSynthetic;
    sink.OnEncodedH264Frame(std::move(synth));
    const size_t q3 = queued_inputs();
    answer_queued(q2, q3);
    settle(700);
    const Drained n = drain(sink);
    check("two ordinary inputs (one a synthetic AU) count two", n.s.decodedFrames == 2 && q3 - q2 == 2,
          "decoded=" + std::to_string(n.s.decodedFrames) + " queued=" + std::to_string(q3 - q2));
    // A loss reset: the next IDR goes to a new codec, and its output counts.
    sink.OnVideoDiscontinuity();
    const size_t q4 = queued_inputs();
    sink.OnEncodedH264Frame(frame(90, true, 0x0C));
    const size_t q5 = queued_inputs();
    answer_queued(q4, q5);
    settle(700);
    check("after a loss reset the new codec's picture counts", drain(sink).s.decodedFrames == 1 && q5 - q4 == 1);
  }
  check("every queued output was taken by the sink", outputs_drained());
  sink.SetSurface(env, nullptr);
  std::printf("\nandroid_decoder_decode_stats_test: %d passed, %d failed\nRESULT: %s\n", gChecks - gFailures, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
