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
#include "client_telemetry_window.hpp"
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

// One drain, and the pictures it adds since the last valid one -- r6: the sink gives its measurement
// span (cumulative), so this takes the difference the session's window takes (client_telemetry_window.hpp);
// before r6 the sink gave the count since the last drain itself.
struct Drained {
  bool valid = false;
  ClientDecodeStats s;
  uint64_t frames = 0;
};
Drained drain(AndroidVideoDecoderSink& sink) {
  Drained d;
  d.valid = sink.DrainDecodeStats(&d.s);
#ifdef REMOTE60_DECODE_SPAN
  static uint64_t lastId = 0, lastFrames = 0;
  if (d.valid) {
    d.frames = d.s.spanId == lastId && d.s.spanFrames >= lastFrames ? d.s.spanFrames - lastFrames : d.s.spanFrames;
    lastId = d.s.spanId;
    lastFrames = d.s.spanFrames;
  }
#else
  d.frames = d.s.decodedFrames;
#endif
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
    check("three decoded pictures count three", d.valid && d.frames == 3,
          "valid=" + std::to_string(d.valid) + " decoded=" + std::to_string(d.frames));
    check("...and a second drain right after counts none", drain(sink).frames == 0);
  }
  {
    std::puts("\n--- 2. a picture held for its display slot ---");
    const int r0 = released();
    picture(steady_us() + 300000);  // due in 300 ms: held in the codec until then
    settle();
    const Drained d = drain(sink);
    check("counted when the decoder gave it out, before its release", d.frames == 1 && released() == r0,
          "decoded=" + std::to_string(d.frames) + " releasedSince=" + std::to_string(released() - r0));
    settle(400);
    check("released at its slot", released() == r0 + 1);
    check("...and not counted again then", drain(sink).frames == 0);
  }
  {
    std::puts("\n--- 3. buffers that carry no picture ---");
    picture(4000, 2);     // codec config
    picture(5000, 4, 0);  // end of stream, empty
    settle();
    check("a codec-config buffer and an EMPTY end-of-stream buffer are not decoded pictures",
          drain(sink).frames == 0);
    picture(5500, 4, 1);  // end of stream carrying the last picture
    settle();
    check("r3: an end-of-stream buffer that carries a picture counts it (only EOS-only is left out)",
          drain(sink).frames == 1);
  }
  {
    std::puts("\n--- 4. an output format change ---");
    format_change(1280, 720);
    picture(6000);
    settle();
    const Drained d = drain(sink);
    check("the format change is no picture; the size is the decoder's output format",
          d.frames == 1 && d.s.width == 1280 && d.s.height == 720,
          std::to_string(d.frames) + " " + std::to_string(d.s.width) + "x" + std::to_string(d.s.height));
  }
  {
    std::puts("\n--- 5. two outputs of one PTS that are not a replay ---");
    picture(7000);
    picture(7000);
    settle();
    // r3 R1: a replay is known by where it was queued (section 9), not by a repeated PTS -- a real replay
    // gets a new PTS, and two ordinary outputs are two pictures.
    check("r3: two ordinary outputs count two, whatever their PTS", drain(sink).frames == 2);
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
    check("r5: the window the surface came back in counts only the picture after the boundary (not mixed, not thrown away)",
          across.valid && across.frames == 1,
          "valid=" + std::to_string(across.valid) + " decoded=" + std::to_string(across.frames));
    picture(9500);
    settle();
    const Drained back = drain(sink);
    check("the next window is valid and counts only its own picture",
          back.valid && back.frames == 1, "decoded=" + std::to_string(back.frames));
    // r3 R2: the surface goes and comes back BETWEEN two drains -- the window spans both states.
    picture(9600);
    settle();
    sink.SetSurface(env, nullptr);
    sink.SetSurface(env, surface());
    sink.OnEncodedH264Frame(frame(1, true, 0x05));
    picture(9700);
    settle();
    const Drained roundTrip = drain(sink);
    check("r3/r5: a surface round trip between two drains -- the window counts only what came after it",
          roundTrip.valid && roundTrip.frames == 1,
          "valid=" + std::to_string(roundTrip.valid) + " decoded=" + std::to_string(roundTrip.frames));
    picture(9800);
    settle();
    check("...and the window after it counts its own picture", drain(sink).frames == 1);
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
    const Drained firstWin = drain(sink);
    check("r5: the window its first picture arrived in counts it (the answer's boundary, not thrown away)",
          firstWin.valid && firstWin.frames == 1,
          "valid=" + std::to_string(firstWin.valid) + " decoded=" + std::to_string(firstWin.frames));
    picture(10500);
    settle();
    const Drained ready = drain(sink);
    check("then valid, counting from there", ready.valid && ready.frames == 1,
          "valid=" + std::to_string(ready.valid) + " decoded=" + std::to_string(ready.frames));
  }
  {
    std::puts("\n--- 8. a codec reset for a loss ---");
    sink.OnVideoDiscontinuity();
    const Drained after = drain(sink);
    check("a loss reset stays valid (frames that do not decode now are a real stall) and counts 0",
          after.valid && after.frames == 0);
    sink.OnEncodedH264Frame(frame(70, true, 0x04));
    picture(11000);
    picture(12000);
    settle();
    check("after the IDR it counts on, no negative or burst", drain(sink).frames == 2);
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
          d.valid && d.frames == 0,
          "valid=" + std::to_string(d.valid) + " decoded=" + std::to_string(d.frames));
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
    check("two ordinary inputs (one a synthetic AU) count two", n.frames == 2 && q3 - q2 == 2,
          "decoded=" + std::to_string(n.frames) + " queued=" + std::to_string(q3 - q2));
    // A loss reset: the next IDR goes to a new codec, and its output counts.
    sink.OnVideoDiscontinuity();
    const size_t q4 = queued_inputs();
    sink.OnEncodedH264Frame(frame(90, true, 0x0C));
    const size_t q5 = queued_inputs();
    answer_queued(q4, q5);
    settle(700);
    check("after a loss reset the new codec's picture counts", drain(sink).frames == 1 && q5 - q4 == 1);
  }
  {
    // r4 F1 (verifier, AVD 3/3): the app abandons a switch (nativeAbortVideoSwitch -> AbortWindowSelection):
    // the codec and its csd are dropped, the gate is cleared -- the state is "valid" again at once, but no
    // picture can come out until the next IDR. Those windows were reported as decoded 0 while the host
    // sent a full cadence -> a severe_client demotion the 0.2.158 APK never caused.
    std::puts("\n--- 10. r4 F1: an abandoned switch, before the next IDR's picture ---");
    sink.PrepareForWindowSelection(11);
    sink.AbortWindowSelection();
    (void)drain(sink);  // the window the abort happened in (a generation change)
    for (int i = 0; i < 10; ++i) sink.OnEncodedH264Frame(frame(90, false, static_cast<uint8_t>(0x20 + i)));  // deltas: no codec can take them
    settle(300);
    const Drained wait1 = drain(sink);
    for (int i = 0; i < 10; ++i) sink.OnEncodedH264Frame(frame(90, false, static_cast<uint8_t>(0x30 + i)));
    settle(300);
    const Drained wait2 = drain(sink);
    check("F1: the windows waiting for the next IDR after an abandoned switch are NOT reported as a valid 0",
          !(wait1.valid && wait1.frames == 0) && !(wait2.valid && wait2.frames == 0),
          "window1 valid=" + std::to_string(wait1.valid) + " decoded=" + std::to_string(wait1.frames) +
              " | window2 valid=" + std::to_string(wait2.valid) + " decoded=" + std::to_string(wait2.frames));
    const size_t q6 = queued_inputs();
    sink.OnEncodedH264Frame(frame(90, true, 0x40));  // the next IDR: a codec again
    const size_t q7 = queued_inputs();
    answer_queued(q6, q7);
    settle(400);
    (void)drain(sink);
    sink.OnEncodedH264Frame(frame(90, false, 0x41));
    const size_t q8 = queued_inputs();
    answer_queued(q7, q8);
    settle(400);
    const Drained after = drain(sink);
    check("F1: once the next IDR's picture is out, windows count again", after.valid && after.frames == 1,
          "valid=" + std::to_string(after.valid) + " decoded=" + std::to_string(after.frames));
  }
  {
    // r5 (Codex F1 contract) -- the lifecycle of an active transition's startup grace, on the real sink.
    using clk = std::chrono::steady_clock;
    std::puts("\n--- 11. r5: a selection whose answer never comes -- held back 2 s, then a real 0 ---");
    sink.PrepareForWindowSelection(21);  // a picture was counted in section 10: a grace may start
    const auto t0 = clk::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const Drained early = drain(sink);
    check("r5: inside the transition's grace the count is not measured yet", !early.valid);
    std::this_thread::sleep_for(std::chrono::milliseconds(1900));  // past t0 + 2 s, nothing decoded
    const Drained late = drain(sink);
    check("r5: AFTER THE GRACE A SELECTION STILL WITHOUT A PICTURE IS A VALID 0 (not unmeasured for ever)",
          late.valid && late.frames == 0,
          "valid=" + std::to_string(late.valid) + " decoded=" + std::to_string(late.frames) + " at +" +
              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count()) + " ms");
    sink.AbortWindowSelection();

    std::puts("\n--- 12. r5: prepare -> abort -> prepare again does not stretch the grace ---");
    // A picture first, so the next transition may start a grace.
    {
      const size_t qa = queued_inputs();
      sink.OnEncodedH264Frame(frame(90, true, 0x50));
      const size_t qb = queued_inputs();
      answer_queued(qa, qb);
      settle(300);
      (void)drain(sink);
    }
    sink.PrepareForWindowSelection(22);
    const auto t1 = clk::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    sink.AbortWindowSelection();
    sink.AbortWindowSelection();  // the same abort again: idempotent
    sink.PrepareForWindowSelection(23);  // retry: still the same grace
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));  // t1 + 2.3 s
    const Drained retry = drain(sink);
    check("r5: THE RETRY DID NOT STRETCH IT -- at the first transition's deadline the 0 is valid",
          retry.valid && retry.frames == 0,
          "valid=" + std::to_string(retry.valid) + " decoded=" + std::to_string(retry.frames) + " at +" +
              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t1).count()) + " ms");
    sink.AbortWindowSelection();

    std::puts("\n--- 13. r5: the first picture ends the grace at once, and the window counts it ---");
    {
      const size_t qa = queued_inputs();
      sink.OnEncodedH264Frame(frame(90, true, 0x51));
      const size_t qb = queued_inputs();
      answer_queued(qa, qb);
      settle(300);
      (void)drain(sink);
    }
    sink.PrepareForWindowSelection(24);
    ControlWindowSelectedMessage ack{};
    ack.flags = 0x1u;
    ack.streamGeneration = 240;
    sink.OnWindowSelectionControlResultFor(ack, 24);
    (void)drain(sink);  // the window the selection began in
    const size_t q0 = queued_inputs();
    sink.OnEncodedH264Frame(frame(240, true, 0x52));
    sink.OnEncodedH264Frame(frame(240, false, 0x53));
    const size_t q1 = queued_inputs();
    answer_queued(q0, q1);
    settle(500);
    const Drained first = drain(sink);
    check("r5: the window the selection's first pictures came out in is reported with them (not thrown away)",
          first.valid && first.frames == 2,
          "valid=" + std::to_string(first.valid) + " decoded=" + std::to_string(first.frames)
#ifdef REMOTE60_DECODE_SPAN
              + " spanUs=" + std::to_string(first.s.spanUs)
#elif defined(REMOTE60_TELEMETRY_PAUSED)
              + " measuredUs=" + std::to_string(first.s.measuredUs)
#endif
    );

    std::puts("\n--- 14. r5: a loss reset in normal streaming gets no grace ---");
    sink.OnVideoDiscontinuity();
    sink.OnVideoDiscontinuity();
    sink.OnVideoDiscontinuity();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const Drained loss = drain(sink);
    check("r5: frames that do not decode after a loss are a valid 0 at once (no grace, however many resets)",
          loss.valid && loss.frames == 0,
          "valid=" + std::to_string(loss.valid) + " decoded=" + std::to_string(loss.frames));

    std::puts("\n--- 15. r5: the surface recreated ---");
    {
      const size_t qa = queued_inputs();
      sink.OnEncodedH264Frame(frame(240, true, 0x54));
      const size_t qb = queued_inputs();
      answer_queued(qa, qb);
      settle(300);
      (void)drain(sink);
    }
    sink.SetSurface(env, nullptr);
    check("r5: no surface: not measured", !drain(sink).valid);
    sink.SetSurface(env, surface());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check("r5: the surface back starts a grace: not measured until its first picture", !drain(sink).valid);
    const size_t qc = queued_inputs();
    sink.OnEncodedH264Frame(frame(240, true, 0x55));
    const size_t qd = queued_inputs();
    answer_queued(qc, qd);
    settle(300);
    const Drained back2 = drain(sink);
    check("r5: ...and its first picture counts at once", back2.valid && back2.frames == 1,
          "valid=" + std::to_string(back2.valid) + " decoded=" + std::to_string(back2.frames));
  }
  {
    // r6 (Codex S1, codex_mobile_abr_e599123.md): the rate the session's window gives the host, from this
    // sink, on the real clock -- the session's due pass as native_video_client_session.cpp runs it.
    std::puts("\n--- 16. r6 S1-a: the first picture after a startup grace, just before the report tick ---");
    ClientTelemetryWindow tel;
    tel.Reset(static_cast<uint64_t>(steady_us()), 0, 0);
    struct Report {
      bool made = false;
      uint32_t fpsX100 = 0;
      int64_t atUs = 0;
      int held = 0;
    };
    // One due pass now, then (r6) the passes Hold asks for, 10 ms apart as the control loop runs.
    auto report_now = [&]() {
      Report r;
      for (int i = 0; i < 100; ++i) {
        const uint64_t now = static_cast<uint64_t>(steady_us());
        if (i > 0 && !tel.Due(now)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        ClientDecodeStats d{};
        const bool v = sink.DrainDecodeStats(&d);
#ifdef REMOTE60_DECODE_SPAN
        if (tel.Hold(now, v ? &d : nullptr, false)) {
          ++r.held;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
#endif
        r.made = tel.Close(now, 0, 0, v ? &d : nullptr, nullptr);
        r.fpsX100 = tel.Pending().message.decodedFpsX100;
        r.atUs = static_cast<int64_t>(now);
        return r;
      }
      return r;
    };
    // 30 fps from the decoder, starting `delayMs` from now, on its own thread (the codec keeps producing
    // while the session's pass waits).
    auto feeder = [&](int delayMs, int count, int64_t ptsBase) {
      return std::thread([=] {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        for (int i = 0; i < count; ++i) {
          picture(ptsBase + i);
          std::this_thread::sleep_for(std::chrono::microseconds(33333));
        }
      });
    };
    auto fps = [](const Report& r) { return std::to_string(r.fpsX100 / 100.0).substr(0, 5) + " fps"; };
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      (void)report_now();  // the window's base: what came before is not this check's
      std::thread healthy = feeder(0, 30, 30000);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      const Report h = report_now();
      healthy.join();
      check("r6: healthy 30 fps before it is reported as ~30 fps", h.made && h.fpsX100 >= 2500 && h.fpsX100 <= 3500, fps(h));
    }
    sink.OnDecodeEpisodeStart();  // the stream resumed: a startup grace (the codec has no picture yet)
    const int64_t g0 = steady_us();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const Report inGrace = report_now();
    check("r6: inside the grace the window is not reported", !inGrace.made);
    // The first picture 15 ms before the next tick (g0 + 1.3 s), then 30 fps.
    std::this_thread::sleep_for(std::chrono::microseconds(g0 + 1285000 - steady_us()));
    const int64_t firstUs = steady_us();
    std::thread after = feeder(0, 20, 31000);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    const Report r16 = report_now();
    after.join();
    check("r6 S1-a: THE REPORT AFTER THE GRACE'S FIRST PICTURE IS ~30 fps -- the grace is not in its denominator",
          r16.made && r16.fpsX100 >= 2000 && r16.fpsX100 <= 4500,
          fps(r16) + ", held passes " + std::to_string(r16.held) + ", " +
              std::to_string((r16.atUs - firstUs) / 1000) + " ms after the first picture");
    check("r6: ...and it comes within ~0.25 s of the first picture, not a second later",
          r16.made && r16.atUs - firstUs < 450000, std::to_string((r16.atUs - firstUs) / 1000) + " ms");

    std::puts("\n--- 17. r6 S1-b: a generation that begins just before the tick, its first picture 20 ms later ---");
    sink.PrepareForWindowSelection(31);  // pictures were counted in 16: a grace
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    const Report expired = report_now();
    check("r6 (kept): after the grace, no picture is a valid 0", expired.made && expired.fpsX100 == 0, fps(expired));
    ControlWindowSelectedMessage ack{};
    ack.flags = 0x1u;
    ack.streamGeneration = 310;
    sink.OnWindowSelectionControlResultFor(ack, 31);  // the answer: a new generation
    const int64_t ackUs = steady_us();
    sink.OnEncodedH264Frame(frame(310, true, 0x60));  // its IDR: a codec again
    std::thread answered = feeder(20, 20, 32000);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const Report r17 = report_now();
    answered.join();
    check("r6 S1-b: THE REPORT JUST AFTER A GENERATION CHANGE IS NOT A 0 OVER A FEW ms -- ~30 fps once measured",
          r17.made && r17.fpsX100 >= 2000 && r17.fpsX100 <= 4500,
          fps(r17) + ", held passes " + std::to_string(r17.held) + ", at +" + std::to_string((r17.atUs - ackUs) / 1000) +
              " ms from the answer");
    settle(100);
    (void)drain(sink);
  }
  check("every queued output was taken by the sink", outputs_drained());
  sink.SetSurface(env, nullptr);
  std::printf("\nandroid_decoder_decode_stats_test: %d passed, %d failed\nRESULT: %s\n", gChecks - gFailures, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
