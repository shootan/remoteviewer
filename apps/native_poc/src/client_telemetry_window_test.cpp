// mobile-abr r1: the shared client session's report window (client_telemetry_window.hpp) against the
// product ClientControlScheduler, on a hand-made clock (Codex contract section 5, "결정적 실제 규칙 시험").
//
// [1] the loss counterexample. The scheduler hands out Ping / list / select / input before Metrics.
//     118420f built the report in a local on the pass its stats were drained; a pass that went to
//     something else dropped it. That rule is reproduced here ONLY as the negative control, next to
//     the product helper: with the sample tick on the Ping deadline, a list request queued on the
//     next pass, or input arriving continuously, the local loses windows; the pending snapshot sends
//     every one. (The old session itself, unmodified, is what mobile_abr_session_e2e_test runs at
//     118420f.)
// [2] present / decode separation: present samples 0-1 with decode at 30, decode at 30 with the view
//     latching nothing, received 30 / decoded 0, received 3 / decoded 3 -- none hides as 30 fps and none
//     drops the report.
// [3] windows of 0.5 / 1 / 2 s and a late tick: rates over the real elapsed; a long gap reports one
//     window, not a backlog.
// [4] counter reset (a cumulative count that went backwards) -> 0, no negative delta, no burst.
// [5] decode validity: no decode fields before the sink gives a valid count in this connection (r5: a valid
//     count, even 0 -- the sink's grace decides when); a not-valid window after it is withheld (not
//     reported as 0); Reset (a new connection) drops pending and seen.
// [6] two windows (two controllers) are independent.
// [12] r6 (Codex S1): the chain sink -> window -> scheduler -> host snapshot -> host ABR around a startup
//     grace's first picture and a generation change just before a report close, over report phases.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "client_decode_snapshot.hpp"
#include "client_telemetry_window.hpp"
#include "host_abr.hpp"
#include "host_client_metrics.hpp"
#include "native_video_client_shared_core.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

// The product scheduler with its request states, as ClientSessionController owns them.
struct Control {
  ClientControlScheduler scheduler;
  WindowPanelStateModel windowPanel;
  StreamStateControl streamState;
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframes{120000, 300000, 3};
  RuntimeTuneState runtimeTune{300000, 30000000, 250000, 1, 240};
  ClientInputQueue inputQueue;
  uint64_t metricsSent = 0, pings = 0, lists = 0, inputs = 0;
  std::vector<uint64_t> sentStamps;
  explicit Control(uint64_t nowUs) { scheduler.Reset(kClientControlIntervalMsDefault, nowUs); }
  // One pass of the control loop: what NextAction returns, "executed" at once.
  void Pass(uint64_t nowUs, const ClientControlMetricsSnapshot& metrics) {
    ControlOutboundAction a{};
    if (!scheduler.NextAction(nowUs, metrics, &windowPanel, &streamState, &captureMode, &keyframes, &runtimeTune,
                              &inputQueue, &a)) {
      return;
    }
    switch (a.kind) {
      case ControlOutboundActionKind::Ping:
        ++pings;
        scheduler.OnPingCompleted(nowUs);
        break;
      case ControlOutboundActionKind::Metrics:
        ++metricsSent;
        sentStamps.push_back(a.metrics.clientSendQpcUs);
        break;
      case ControlOutboundActionKind::WindowListRequest:
        ++lists;
        break;
      case ControlOutboundActionKind::InputEvent:
        ++inputs;
        break;
      default:
        break;
    }
  }
  void QueueInput(uint64_t nowUs) {
    QueuedControlInputMessage m{};
    m.type = MessageType::ControlInputEvent;
    m.inputEvent.kind = 1;
    m.generatedUs = nowUs;
    inputQueue.Enqueue(m);
  }
};

// A window's decode drain: n pictures over us. r6: the sink gives its measurement span -- each call here is
// a new one covering exactly this window (how the sink starts and continues spans is [12] and
// android_decoder_decode_stats_test); before r6 it gave the count since the last drain.
ClientDecodeStats decoded(uint32_t n, uint64_t us = 1'000'000, uint32_t w = 1920, uint32_t h = 1080) {
  ClientDecodeStats d;
#ifdef REMOTE60_DECODE_SPAN
  static uint64_t id = 1000;
  d.spanId = ++id;
  d.spanFrames = n;
  d.spanUs = us;
#else
  (void)us;
  d.decodedFrames = n;
#endif
  d.width = w;
  d.height = h;
  return d;
}

// 118420f's rule, kept here only as the negative control: drained into a local on the tick pass, sent
// only if that same pass picks Metrics.
ClientControlMetricsSnapshot old_local(uint64_t nowUs, uint64_t* lastUs, bool presentAvailable) {
  ClientControlMetricsSnapshot m{};
  if (nowUs >= *lastUs + 1000000ULL && presentAvailable) {
    m.message.presentSampleCount = 30;
    m.message.presentFpsX100 = 3000;
    m.updatedQpcUs = nowUs;
    *lastUs = nowUs;
  }
  return m;
}

enum class Load { PingAligned, ListOnTick, InputStream };

// Runs 60 s of 10 ms passes. Returns metrics actually sent; `windows` = windows that were measured.
uint64_t run(Load load, bool pending, uint64_t* windows) {
  const uint64_t t0 = 10'000'000;
  Control c(t0);
  ClientTelemetryWindow w;
  w.Reset(t0, 0, 0);
  uint64_t oldLast = t0;  // 118420f: lastPresentReportUs (function static)
  uint64_t frames = 0;
  *windows = 0;
  for (uint64_t now = t0; now < t0 + 60'000'000; now += 10'000) {
    frames += (now % 33'333) < 10'000 ? 1 : 0;
    // Ping deadline on every tick: the scheduler's interval is 500 ms, its next ping is due exactly at
    // the passes the window closes on (the first ping at t0, then each completed at a tick).
    if (load == Load::ListOnTick && (now - t0) % 1'000'000 == 0) c.windowPanel.RequestList("tick");
    if (load == Load::InputStream) c.QueueInput(now);  // a drag: one event every pass
    if (pending) {
      if (w.Due(now)) {
        const ClientDecodeStats d = decoded(30);
        if (w.Close(now, frames, frames * 4000, &d, nullptr)) ++*windows;
      }
      c.Pass(now, w.Pending());
    } else {
      const bool tick = now >= oldLast + 1000000ULL;
      if (tick) ++*windows;
      c.Pass(now, old_local(now, &oldLast, true));
    }
  }
  return c.metricsSent;
}

// ------------------------------------------------------------------------------------------------------
// [12] r6 (Codex S1, codex_mobile_abr_e599123.md): the chain, on a hand-made 1 ms clock --
//   the sink's decode bookkeeping -> ClientTelemetryWindow (with Hold after r6) -> the product
//   ClientControlScheduler (one action per 10 ms pass, as the session runs it) -> the host's
//   ClientMetricsSnapshot (stamped on arrival, fresh for 3 s) -> RateControlState::DecideAbrProfile on the
//   host's own 1 s tick (full cadence sent all along, latency / tail 0 as the APK reports them).
namespace chain {

constexpr uint64_t kMs = 1000ULL;
constexpr uint64_t kSec = 1000000ULL;
constexpr uint64_t kFrameUs = 33333ULL;

// The sink's decode bookkeeping in the order android_video_decoder.cpp runs it: a startup grace
// (StartGraceLocked: explicit episode always, a transition only if a picture was counted since the last
// one), a generation change (PublishDecodeStateLocked), a counted picture (DrainOutputLocked) and the
// control thread's drain (DrainDecodeStats). After r6 the span pieces are the product's own
// (client_decode_snapshot.hpp). e599123's sink keeps its drain inline, so for that build the drain is
// transcribed here, line for line -- what the real sink gives in the same phases is
// android_decoder_decode_stats_test sections 16 / 17.
struct ModelSink {
  uint64_t total = 0, generation = 1;
  bool graceActive = false, graceEligible = true;
  uint64_t deadline = 0;
#ifdef REMOTE60_DECODE_SPAN
  ClientDecodeSpanStart span;
#else
  uint64_t genStartTotal = 0, genStartUs = 0, graceBase = 0, drainBase = 0, drainGeneration = 0;
#endif
  void Grace(uint64_t t, bool explicitEpisode) {
    if (!explicitEpisode && !graceEligible) return;
    graceActive = true;
    graceEligible = false;
    deadline = t + 2 * kSec;
#ifdef REMOTE60_DECODE_SPAN
    span.OnGraceStart(total, deadline);
#else
    graceBase = total;
#endif
  }
  void Generation(uint64_t t) {
    ++generation;
#ifdef REMOTE60_DECODE_SPAN
    span.OnGeneration(t, total, graceActive, deadline);
#else
    genStartTotal = total;
    genStartUs = t;
#endif
  }
  void Picture(uint64_t t) {
#ifdef REMOTE60_DECODE_SPAN
    if (graceActive) span.OnGraceFirstPicture(t, total);
#else
    (void)t;
#endif
    ++total;
    graceActive = false;
    graceEligible = true;
  }
  bool Drain(uint64_t t, ClientDecodeStats* out) {
    out->width = 1920;
    out->height = 1080;
    out->generation = generation;
#ifdef REMOTE60_DECODE_SPAN
    ClientDecodeState s;
    s.valid = true;
    s.decodedTotal = total;
    s.generation = generation;
    s.spanId = span.id();
    s.spanStartUs = span.startUs();
    s.spanStartTotal = span.startTotal();
    ClientDecodeSpan sp;
    if (!ReadDecodeSpan(s, t, &sp)) return false;
    out->spanId = sp.id;
    out->spanFrames = sp.frames;
    out->spanUs = sp.us;
    return true;
#else
    uint64_t delta = total >= drainBase ? total - drainBase : 0;
    uint64_t measuredUs = 0;
    if (generation != drainGeneration) {
      delta = total >= genStartTotal ? total - genStartTotal : 0;
      measuredUs = t > genStartUs ? t - genStartUs : 1;
    }
    drainBase = total;
    drainGeneration = generation;
    if (graceActive && total == graceBase && t < deadline) return false;
    out->decodedFrames = static_cast<uint32_t>(delta);
    out->measuredUs = measuredUs;
    return true;
#endif
  }
};

enum class Ev { GraceTransition, Generation, PicturesStop, PicturesFrom };
struct Event {
  uint64_t atUs;
  Ev kind;
};
struct Scenario {
  std::string name;
  uint64_t lengthUs = 20 * kSec;
  uint64_t phaseUs = 995 * kMs;  // where in the host's second the client's windows close (10 ms grid)
  bool qualityFirst = false;
  std::vector<Event> events;     // sorted
  uint64_t preemptAfterUs = 0;   // a window-list request on the first close at / after this (once)
  uint64_t watchFromUs = 0;      // reports sent from here are the ones "after the transition"
};
struct Report {
  uint64_t sentUs;
  uint32_t fpsX100;
};
struct Result {
  int firstDemoteUs = -1;
  std::string reason, evidence;
  std::vector<Report> reports;
  uint32_t minFpsAfter = 0xFFFFFFFFu;
  uint64_t held = 0;
  uint64_t lowReportTicks = 0;  // host ticks that read a fresh report under the collapse floor
  uint32_t maxStaleActive = 0;  // P7: the longest run of stale seconds while the host sends (stale_active at 2, QF 3)
};

Result Run(const Scenario& sc) {
  Result r;
  ModelSink sink;
  const uint64_t resetUs = kSec + sc.phaseUs;  // the connection's window starts here
  ClientTelemetryWindow tel;
  Control c(resetUs - 250 * kMs);  // the control link came up a little before the first window
  ClientMetricsSnapshot hostMetrics;
  RateControlState rate;
  rate.abrEnabled = true;
  rate.abrQualityFirst = sc.qualityFirst;
  rate.abrHasMidProfile = true;
  rate.abrHasLowProfile = true;
  rate.abrHasLowerResolution = true;
  rate.abrHighBitrate = 6000000;
  rate.abrMidBitrate = 4500000;
  rate.abrLowBitrate = 3000000;
  bool picturesOn = true;
  uint64_t nextPictureUs = 500 * kMs;
  bool preemptDone = sc.preemptAfterUs == 0;
  size_t next = 0;
  const uint32_t collapseX100 = 30u * (sc.qualityFirst ? 18u : 12u);
  for (uint64_t t = 0; t <= sc.lengthUs; t += kMs) {
    while (next < sc.events.size() && sc.events[next].atUs <= t) {
      const Event& e = sc.events[next++];
      switch (e.kind) {
        case Ev::GraceTransition:
          sink.Grace(t, false);
          break;
        case Ev::Generation:
          sink.Generation(t);
          break;
        case Ev::PicturesStop:
          picturesOn = false;
          break;
        case Ev::PicturesFrom:
          picturesOn = true;
          nextPictureUs = t;
          break;
      }
    }
    if (picturesOn && t >= nextPictureUs) {
      sink.Picture(t);
      nextPictureUs += kFrameUs;
    }
    // the client's control pass, every 10 ms
    if (t >= resetUs - 250 * kMs && (t % (10 * kMs)) == (sc.phaseUs % (10 * kMs))) {
      if (t == resetUs) {
        ClientDecodeStats prime{};
        (void)sink.Drain(t, &prime);  // WorkerMain: what the sink counted before is dropped
        tel.Reset(t, 0, 0);
        sink.Grace(t, true);  // OnDecodeEpisodeStart
      }
      if (t > resetUs && tel.Due(t)) {
        ClientDecodeStats d{};
        const bool v = sink.Drain(t, &d);
        bool held = false;
#ifdef REMOTE60_DECODE_SPAN
        held = tel.Hold(t, v ? &d : nullptr, false);
#endif
        if (!held) {
          (void)tel.Close(t, 0, 0, v ? &d : nullptr, nullptr);
          if (!preemptDone && t >= sc.preemptAfterUs) {
            c.windowPanel.RequestList("the panel refreshes its list on this pass");
            preemptDone = true;
          }
        }
      }
      if (t >= resetUs) tel.OnDecodeGeneration(sink.generation);
      ControlOutboundAction a{};
      if (c.scheduler.NextAction(t, t >= resetUs ? tel.Pending(t) : ClientControlMetricsSnapshot{}, &c.windowPanel,
                                 &c.streamState, &c.captureMode, &c.keyframes, &c.runtimeTune, &c.inputQueue, &a)) {
        if (a.kind == ControlOutboundActionKind::Ping) {
          c.scheduler.OnPingCompleted(t);
        } else if (a.kind == ControlOutboundActionKind::Metrics) {
          ViewerMetrics v;
          v.width = a.metrics.width;
          v.height = a.metrics.height;
          v.recvFpsX100 = a.metrics.recvFpsX100;
          v.decodedFpsX100 = a.metrics.decodedFpsX100;
          v.updatedUs = t;  // the host stamps a report with its arrival (loopback)
          hostMetrics.Publish(v);
          r.reports.push_back({t, a.metrics.decodedFpsX100});
          if (t >= sc.watchFromUs) r.minFpsAfter = std::min(r.minFpsAfter, a.metrics.decodedFpsX100);
        }
      }
    }
    // the host's 1 s tick
    if (t > 0 && t % kSec == 0) {
      const ViewerMetrics viewer = hostMetrics.Snapshot();
      const bool fresh = viewer.updatedUs > 0 && t >= viewer.updatedUs && (t - viewer.updatedUs) <= 3 * kSec;
      AbrInputs in;
      in.metricsFresh = fresh;
      in.clDecodedFpsX100 = fresh ? viewer.decodedFpsX100 : 0;
      in.cb2eAvgUs = 5000;
      in.sentFrames = 30;
      in.activeFps = 30;
      in.startUs = 0;
      in.clientEverReported = viewer.firstUs != 0;
      in.clientDecodeReported = viewer.decodeReported;
      if (fresh && viewer.decodeReported && viewer.decodedFpsX100 < collapseX100) ++r.lowReportTicks;
      const AbrDecision dec = rate.DecideAbrProfile(in, t);
      r.maxStaleActive = std::max(r.maxStaleActive, rate.abrStaleActiveSeconds);
      if (dec.targetProfile != rate.abrProfile) {
        if (r.firstDemoteUs < 0) {
          r.firstDemoteUs = static_cast<int>(t / kMs);
          r.reason = dec.reason;
          r.evidence = abr_evidence_names(dec.evidence);
        }
        rate.CommitAbrProfile(dec.targetProfile, false, t);
      }
    }
  }
#ifdef REMOTE60_DECODE_SPAN
  r.held = tel.held();
#endif
  return r;
}

std::string Describe(const Result& r) {
  std::string s = r.firstDemoteUs < 0 ? "no demotion"
                                      : "DEMOTED at " + std::to_string(r.firstDemoteUs / 1000.0).substr(0, 6) + " s " +
                                            r.reason + "(" + r.evidence + ")";
  s += ", min fps after the transition " +
       (r.minFpsAfter == 0xFFFFFFFFu ? std::string("-") : std::to_string(r.minFpsAfter / 100.0).substr(0, 5));
  s += ", host ticks on a fresh low report " + std::to_string(r.lowReportTicks);
  s += ", held passes " + std::to_string(r.held);
  s += ", max stale s " + std::to_string(r.maxStaleActive);
  return s;
}

// The report windows close on this grid until something holds one.
uint64_t FirstClose(uint64_t phaseUs, uint64_t atLeastUs) {
  const uint64_t resetUs = kSec + phaseUs;
  return resetUs + ((atLeastUs - resetUs + kSec - 1) / kSec) * kSec;
}

// S1-a: an active switch abandoned at T (a transition grace + a generation change, the codec reset: no
// picture), its first picture `before` the close G, then 30 fps -- the grace still running at that picture.
Scenario GraceFirstPicture(uint64_t phaseUs, uint64_t beforeUs, bool qualityFirst = false) {
  Scenario s;
  const uint64_t T = 8300 * kMs;
  const uint64_t G = FirstClose(phaseUs, T + kSec + beforeUs);
  const uint64_t F = G - beforeUs;
  s.name = "S1-a phase " + std::to_string(phaseUs / kMs) + " ms, first picture " + std::to_string(beforeUs / kMs) +
           " ms before the close at " + std::to_string(G / 1000.0 / 1000.0).substr(0, 6) + " s";
  s.phaseUs = phaseUs;
  s.qualityFirst = qualityFirst;
  s.events = {{T, Ev::GraceTransition}, {T, Ev::Generation}, {T, Ev::PicturesStop}, {F, Ev::PicturesFrom}};
  s.preemptAfterUs = G + kMs;  // the next report goes one pass late (a list refresh on its pass)
  s.watchFromUs = T;
  return s;
}
// ...the first picture `afterUs` past that close instead (still in the grace).
Scenario GraceFirstPictureAfter(uint64_t phaseUs, uint64_t afterUs) {
  Scenario s;
  const uint64_t T = 8300 * kMs;
  const uint64_t G = FirstClose(phaseUs, T + kSec);
  const uint64_t F = G + afterUs;
  s.name = "S1-a phase " + std::to_string(phaseUs / kMs) + " ms, first picture " + std::to_string(afterUs / kMs) +
           " ms after a close";
  s.phaseUs = phaseUs;
  s.events = {{T, Ev::GraceTransition}, {T, Ev::Generation}, {T, Ev::PicturesStop}, {F, Ev::PicturesFrom}};
  s.preemptAfterUs = G + kSec + kMs;
  s.watchFromUs = T;
  if (F >= T + 2 * kSec) s.name += " (past the deadline)";
  return s;
}
// S1-b: a selection prepared at T whose answer (a generation change) comes after the grace expired,
// `beforeUs` before the close G, its first picture `firstUs` after the answer.
Scenario AnswerJustBeforeClose(uint64_t phaseUs, uint64_t beforeUs, uint64_t firstUs) {
  Scenario s;
  const uint64_t T = 8300 * kMs;
  const uint64_t G = FirstClose(phaseUs, T + 2 * kSec + beforeUs + kMs);
  const uint64_t A = G - beforeUs;
  s.name = "S1-b phase " + std::to_string(phaseUs / kMs) + " ms, answer " + std::to_string(beforeUs / kMs) +
           " ms before the close, first picture +" + std::to_string(firstUs / kMs) + " ms";
  s.phaseUs = phaseUs;
  s.events = {{T, Ev::GraceTransition}, {T, Ev::Generation}, {T, Ev::PicturesStop}, {A, Ev::Generation},
              {A + firstUs, Ev::PicturesFrom}};
  s.preemptAfterUs = G + kMs;
  s.watchFromUs = A;
  return s;
}

}  // namespace chain

void TestChain() {
  using namespace chain;
  std::printf("[12] r6 S1: sink -> window -> product scheduler -> host snapshot (3 s fresh) -> host ABR\n");
  {
    // Codex's order: the low report at 9.995 s, host ticks at 10 and 11 s, the next report at 11.005 s.
    const Result r = Run(GraceFirstPicture(995 * kMs, 10 * kMs));
    std::printf("    Codex order (first picture 9.985 s, close 9.995 s, next report 11.005 s): %s\n", Describe(r).c_str());
    check("[12] S1-a Codex order: NO high_to_mid_severe FROM ONE PICTURE AFTER A GRACE (one low report, two host ticks)",
          r.firstDemoteUs < 0 && r.minFpsAfter >= 2000, Describe(r));
  }
  {
    std::string bad;
    int runs = 0;
    for (uint64_t phase : {5 * kMs, 255 * kMs, 305 * kMs, 505 * kMs, 755 * kMs, 805 * kMs, 985 * kMs, 995 * kMs}) {
      for (uint64_t before : {5 * kMs, 10 * kMs, 30 * kMs, 60 * kMs, 100 * kMs}) {
        const Scenario s = GraceFirstPicture(phase, before);
        const Result r = Run(s);
        ++runs;
        if (r.firstDemoteUs >= 0 || r.minFpsAfter < 2000 || r.maxStaleActive >= 2) bad += "\n        " + s.name + ": " + Describe(r);
      }
    }
    check("[12] S1-a sweep (report phase x first picture before the close, " + std::to_string(runs) +
              " runs): no demotion, every report after the switch >= 20 fps, no stale_active from the hold",
          bad.empty(), bad);
  }
  {
    std::string bad;
    int runs = 0;
    for (uint64_t phase : {5 * kMs, 505 * kMs, 995 * kMs}) {
      for (uint64_t after : {5 * kMs, 10 * kMs, 30 * kMs, 300 * kMs}) {
        const Scenario s = GraceFirstPictureAfter(phase, after);
        const Result r = Run(s);
        ++runs;
        if (r.firstDemoteUs >= 0 || r.minFpsAfter < 2000 || r.maxStaleActive >= 2) bad += "\n        " + s.name + ": " + Describe(r);
      }
    }
    check("[12] S1-a first picture just after a close (" + std::to_string(runs) + " runs): no demotion, >= 20 fps",
          bad.empty(), bad);
  }
  {
    const Result r = Run(AnswerJustBeforeClose(995 * kMs, 5 * kMs, 20 * kMs));
    std::printf("    S1-b answer 10.990 s, close 10.995 s, first picture 11.010 s: %s\n", Describe(r).c_str());
    check("[12] S1-b: NO high_to_mid_severe FROM A 0 OVER THE FIRST ms OF A GENERATION", r.firstDemoteUs < 0 &&
          r.minFpsAfter >= 2000, Describe(r));
  }
  {
    std::string bad;
    int runs = 0;
    for (uint64_t phase : {5 * kMs, 255 * kMs, 505 * kMs, 755 * kMs, 995 * kMs}) {
      for (uint64_t before : {5 * kMs, 10 * kMs, 30 * kMs}) {
        for (uint64_t first : {10 * kMs, 20 * kMs, 30 * kMs}) {
          const Scenario s = AnswerJustBeforeClose(phase, before, first);
          const Result r = Run(s);
          ++runs;
          if (r.firstDemoteUs >= 0 || r.minFpsAfter < 2000 || r.maxStaleActive >= 2) bad += "\n        " + s.name + ": " + Describe(r);
        }
      }
    }
    check("[12] S1-b sweep (phase x answer before the close x first picture, " + std::to_string(runs) +
              " runs): no demotion, >= 20 fps",
          bad.empty(), bad);
  }
  {
    const Result r = Run(GraceFirstPicture(995 * kMs, 10 * kMs, /*qualityFirst=*/true));
    std::printf("    quality-first, Codex order: %s\n", Describe(r).c_str());
    check("[12] quality-first (3 ticks) keeps its margin: no demotion", r.firstDemoteUs < 0, Describe(r));
  }
  // What has to stay.
  {
    Scenario s;
    s.name = "fault B";
    s.events = {{8300 * kMs, Ev::GraceTransition}, {8300 * kMs, Ev::Generation}, {8300 * kMs, Ev::PicturesStop}};
    s.watchFromUs = 8300 * kMs;
    const Result r = Run(s);
    std::printf("    fault B (abandoned switch at 8.3 s, no picture ever): %s\n", Describe(r).c_str());
    check("[12] kept: no picture after the grace (deadline 10.3 s) is a valid 0 and demotes (severe_client)",
          r.firstDemoteUs > 10300 && r.firstDemoteUs <= 13500 && r.reason == "high_to_mid_severe" &&
              r.evidence.find("severe_client") != std::string::npos,
          Describe(r));
  }
  {
    Scenario s;
    s.name = "loss";
    s.events = {{8300 * kMs, Ev::PicturesStop}};
    s.watchFromUs = 8300 * kMs;
    const Result r = Run(s);
    std::printf("    normal loss at 8.3 s (no grace): %s\n", Describe(r).c_str());
    check("[12] kept: a stall in normal streaming is a valid 0 at once (no grace) and demotes",
          r.firstDemoteUs > 8300 && r.firstDemoteUs <= 11500 && r.reason == "high_to_mid_severe", Describe(r));
  }
  {
    Scenario s;
    s.name = "steady";
    s.lengthUs = 60 * kSec;
    s.watchFromUs = 3 * kSec;
    const Result r = Run(s);
    size_t perSecondMax = 0;
    for (uint64_t sec = 3; sec < 60; ++sec) {
      size_t n = 0;
      for (const Report& rep : r.reports) n += (rep.sentUs > (sec - 1) * kSec && rep.sentUs <= sec * kSec) ? 1 : 0;
      perSecondMax = std::max(perSecondMax, n);
    }
    std::printf("    steady 60 s: %s, %zu reports\n", Describe(r).c_str(), r.reports.size());
    check("[12] kept: steady streaming is reported once a second at ~30 fps, nothing held",
          r.firstDemoteUs < 0 && r.held == 0 && r.reports.size() >= 57 && perSecondMax <= 2 && r.minFpsAfter >= 2900,
          Describe(r) + ", " + std::to_string(r.reports.size()) + " reports, max/s " + std::to_string(perSecondMax));
  }
}

}  // namespace

int main() {
  std::printf("[1] measured snapshots and what the product scheduler actually sends (60 s, 10 ms passes)\n");
  for (Load load : {Load::PingAligned, Load::ListOnTick, Load::InputStream}) {
    const char* name = load == Load::PingAligned ? "ping-aligned" : load == Load::ListOnTick ? "list-on-tick" : "input-stream";
    uint64_t oldWindows = 0, newWindows = 0;
    const uint64_t oldSent = run(load, false, &oldWindows);
    const uint64_t newSent = run(load, true, &newWindows);
    std::printf("    %-13s 118420f local: %llu of %llu windows sent | pending: %llu of %llu\n", name,
                (unsigned long long)oldSent, (unsigned long long)oldWindows, (unsigned long long)newSent,
                (unsigned long long)newWindows);
    check(std::string("[1] ") + name + ": the pending snapshot sends every measured window",
          newWindows >= 58 && newSent == newWindows,
          std::to_string(newSent) + " of " + std::to_string(newWindows));
    // The negative control has to fail, or this check cannot tell the two apart.
    check(std::string("[1] ") + name + ": NEGATIVE CONTROL -- the 118420f local loses windows",
          oldSent < oldWindows, std::to_string(oldSent) + " of " + std::to_string(oldWindows));
  }

  std::printf("[2] present / decode separation (one 1 s window each)\n");
  struct Case {
    const char* name;
    uint32_t recv, dec;
    bool present;
    uint32_t presentSamples, displayed;
  };
  const Case cases[] = {
      {"decode 30, present 0-1 samples", 30, 30, false, 0, 0},
      {"decode 30, view latched 0", 30, 30, true, 29, 0},
      {"recv 30, decode 0 (decoder stall)", 30, 0, false, 0, 0},
      {"recv 3, decode 3 (thin)", 3, 3, false, 0, 0},
      {"recv 0, decode 0 (video stopped, control alive)", 0, 0, false, 0, 0},
  };
  for (const Case& k : cases) {
    ClientTelemetryWindow w;
    w.Reset(0, 0, 0);
    const ClientDecodeStats seed = decoded(30);
    w.Close(1'000'000, 30, 30 * 4000, &seed, nullptr);  // the decoder has produced output this connection
    const ClientDecodeStats d = decoded(k.dec);
    ClientPresentationStats p{};
    p.sampleCount = k.presentSamples;
    p.displayedCount = k.displayed;
    p.fpsX100 = k.presentSamples * 100;
    const bool made = w.Close(2'000'000, 30 + k.recv, (30 + k.recv) * 4000, &d, k.present ? &p : nullptr);
    const auto& m = w.Pending().message;
    check(std::string("[2] ") + k.name + ": reported, with decode " + std::to_string(k.dec) + " and recv " +
              std::to_string(k.recv) + " as measured",
          made && w.Pending().updatedQpcUs == 2'000'000 && m.decodedFpsX100 == k.dec * 100 &&
              m.recvFpsX100 == k.recv * 100 && m.presentDisplayedCount == k.displayed,
          "decodedFpsX100=" + std::to_string(m.decodedFpsX100) + " recvFpsX100=" + std::to_string(m.recvFpsX100) +
              " presentSamples=" + std::to_string(m.presentSampleCount));
  }

  std::printf("[3] real elapsed windows\n");
  for (uint64_t lenUs : {500'000ULL, 1'000'000ULL, 2'000'000ULL, 37'000'000ULL}) {
    ClientTelemetryWindow w;
    w.Reset(0, 0, 0);
    const uint64_t n = 30 * lenUs / 1'000'000;
    const ClientDecodeStats d = decoded(static_cast<uint32_t>(n), lenUs);
    w.Close(lenUs, n, n * 4000, &d, nullptr);
    const auto& m = w.Pending().message;
    check("[3] a " + std::to_string(lenUs / 1000) + " ms window of 30 fps reports 30 fps, 0.96 Mbps",
          m.decodedFpsX100 == 3000 && m.recvFpsX100 == 3000 && m.recvMbpsX1000 == 960,
          "dec=" + std::to_string(m.decodedFpsX100) + " recv=" + std::to_string(m.recvFpsX100) +
              " mbpsX1000=" + std::to_string(m.recvMbpsX1000));
  }
  {
    // A late tick (control busy 5 s): one window, then the next starts at that close -- no backlog.
    ClientTelemetryWindow w;
    w.Reset(0, 0, 0);
    const ClientDecodeStats d1 = decoded(150, 5'000'000);
    w.Close(5'000'000, 150, 600'000, &d1, nullptr);
    check("[3] a 5 s late close is one report of 30 fps", w.reports() == 1 && w.Pending().message.decodedFpsX100 == 3000);
    check("[3] ...and the next window is due 1 s after it, not at once", !w.Due(5'500'000) && w.Due(6'000'000));
  }

  std::printf("[4] counter reset\n");
  {
    ClientTelemetryWindow w;
    w.Reset(0, 1000, 4'000'000);
    const ClientDecodeStats d = decoded(30);
    w.Close(1'000'000, 1030, 4'120'000, &d, nullptr);
    w.Close(2'000'000, 5, 20'000, &d, nullptr);  // the cumulative counts went backwards
    const auto& m = w.Pending().message;
    check("[4] a counter that went backwards reports 0 recv, no negative delta / burst",
          m.recvFpsX100 == 0 && m.recvMbpsX1000 == 0, "recv=" + std::to_string(m.recvFpsX100));
    w.Close(3'000'000, 35, 140'000, &d, nullptr);
    check("[4] ...and the next window counts from the new base", w.Pending().message.recvFpsX100 == 3000);
  }

  std::printf("[5] decode validity\n");
  {
    ClientTelemetryWindow w;
    w.Reset(0, 0, 0);
    const bool made0 = w.Close(1'000'000, 10, 40'000, nullptr, nullptr);
    check("[5] before the sink gives a valid count: no report",
          !made0 && !w.decode_seen() && w.Pending().updatedQpcUs == 0);
    ClientPresentationStats p{};
    p.sampleCount = 5;
    const bool madeP = w.Close(2'000'000, 20, 80'000, nullptr, &p);
    check("[5] ...a present-only report then carries no decode field (as before)",
          madeP && w.Pending().message.decodedFpsX100 == 0 && w.Pending().message.width == 0 &&
              w.Pending().message.recvFpsX100 == 0);
    const ClientDecodeStats d = decoded(28);
    w.Close(3'000'000, 50, 200'000, &d, nullptr);
    check("[5] the first valid count turns decode fields on", w.decode_seen() && w.Pending().message.decodedFpsX100 == 2800);
    const bool madeU = w.Close(4'000'000, 80, 320'000, nullptr, &p);  // not valid (no surface / selection wait)
    check("[5] a not-valid window after it is withheld -- not reported as 0, and the unsent one before it is dropped",
          !madeU && w.withheld() == 1 && w.Pending().updatedQpcUs == 0,
          "pending updatedQpcUs=" + std::to_string(w.Pending().updatedQpcUs));
    w.Reset(9'000'000, 80, 320'000);
    check("[5] a new connection drops pending and seen", w.Pending().updatedQpcUs == 0 && !w.decode_seen());
  }

  std::printf("[6] two windows\n");
  {
    ClientTelemetryWindow a, b;
    a.Reset(0, 0, 0);
    b.Reset(0, 500, 2'000'000);
    const ClientDecodeStats d = decoded(30);
    a.Close(1'000'000, 30, 120'000, &d, nullptr);
    b.Close(1'000'000, 503, 2'012'000, &d, nullptr);
    check("[6] two connections' windows do not share counts",
          a.Pending().message.recvFpsX100 == 3000 && b.Pending().message.recvFpsX100 == 300);
  }

  std::printf("[7] r3 R2: an unsent snapshot does not outlive a not-valid window (product scheduler)\n");
  {
    const uint64_t t0 = 50'000'000;
    Control c(t0);
    ClientTelemetryWindow w;
    w.Reset(t0, 0, 0);
    c.Pass(t0, w.Pending());            // the first ping goes out at once
    c.Pass(t0 + 500'000, w.Pending());  // and the next at +500 ms
    // Window A, healthy, is measured; the pass it is measured on goes to a list request instead.
    const ClientDecodeStats a = decoded(30);
    w.Close(t0 + 1'000'000, 30, 120'000, &a, nullptr);
    c.windowPanel.RequestList("A's pass");
    c.Pass(t0 + 1'000'000, w.Pending());
    check("[7] A was measured but not sent on its own pass", c.metricsSent == 0 && w.Pending().updatedQpcUs != 0,
          "sent=" + std::to_string(c.metricsSent) + " lists=" + std::to_string(c.lists));
    // Before the next pass that would send it, the decoder's state stops being meaningful (the surface goes,
    // a selection starts): window B closes not-valid.
    w.Close(t0 + 1'010'000, 30, 120'000, nullptr, nullptr);
    c.Pass(t0 + 1'020'000, w.Pending());
    c.Pass(t0 + 1'030'000, w.Pending());
    check("[7] A IS NOT SENT AFTER THE NOT-VALID WINDOW (the host would take it as fresh on arrival)",
          c.metricsSent == 0, std::to_string(c.metricsSent) + " metrics sent");
    // ...while an ordinary Ping preemption still keeps a healthy window: C is measured on a ping pass, goes next.
    const ClientDecodeStats cc = decoded(30);
    w.Close(t0 + 2'030'000, 60, 240'000, &cc, nullptr);
    c.scheduler.OnPingCompleted(t0 + 1'530'000);  // the ping is due exactly on C's pass
    c.Pass(t0 + 2'030'000, w.Pending());
    const uint64_t pingsBefore = c.pings;
    c.Pass(t0 + 2'040'000, w.Pending());
    check("[7] a healthy window preempted by a Ping is still sent on the next pass",
          c.metricsSent == 1 && pingsBefore >= 3, std::to_string(c.metricsSent) + " metrics sent, " +
                                                     std::to_string(c.pings) + " pings");
  }

  std::printf("[8] r3 R2: generation and age\n");
  {
    ClientTelemetryWindow w;
    w.Reset(0, 0, 0);
    ClientDecodeStats d = decoded(30);
    d.generation = 4;
    w.Close(1'000'000, 30, 120'000, &d, nullptr);
    w.OnDecodeGeneration(4);
    check("[8] the same generation keeps the unsent snapshot", w.Pending(1'010'000).updatedQpcUs == 1'000'000);
    w.OnDecodeGeneration(5);  // the surface went (or a selection started) before it was sent
    check("[8] a generation change on any pass drops it", w.Pending(1'020'000).updatedQpcUs == 0 && w.dropped() == 1);
    d.generation = 5;
    w.Close(2'000'000, 60, 240'000, &d, nullptr);
    check("[8] a snapshot is handed out within 1.5 s of its measurement",
          w.Pending(3'400'000).updatedQpcUs == 2'000'000);
    check("[8] ...and not after (the host would stamp it fresh on arrival)", w.Pending(3'600'000).updatedQpcUs == 0);
    w.Reset(9'000'000, 60, 240'000);
    check("[8] a new connection hands out nothing old", w.Pending(9'000'001).updatedQpcUs == 0);
  }

  std::printf("[9] r5: a decoder that never produces anything still reaches the host as 0\n");
  {
    ClientTelemetryWindow w;
    w.Reset(0, 0, 0);
    const ClientDecodeStats zero = decoded(0);  // the sink calls its count valid (its grace is over)
    const bool made = w.Close(1'000'000, 30, 120'000, &zero, nullptr);
    check("[9] A VALID 0 IS REPORTED even before any positive decode in this connection",
          made && w.Pending().updatedQpcUs == 1'000'000 && w.Pending().message.decodedFpsX100 == 0 &&
              w.Pending().message.recvFpsX100 == 3000,
          "made=" + std::to_string(made) + " recvFpsX100=" + std::to_string(w.Pending().message.recvFpsX100));
  }
#ifdef REMOTE60_TELEMETRY_PAUSED
  std::printf("[10] r5: a paused stream is not measured\n");
  {
    ClientTelemetryWindow w;
    w.Reset(0, 0, 0);
    const ClientDecodeStats d = decoded(30);
    w.Close(1'000'000, 30, 120'000, &d, nullptr);
    const ClientDecodeStats zero = decoded(0);
    const bool made = w.Close(2'000'000, 30, 120'000, &zero, nullptr, /*paused=*/true);
    check("[10] paused: no report (no 0 fps), and the unsent one is dropped", !made && w.Pending().updatedQpcUs == 0);
    const bool resumed = w.Close(3'000'000, 60, 240'000, &d, nullptr, false);
    check("[10] resumed: reported again", resumed && w.Pending().message.decodedFpsX100 == 3000);
  }
  std::printf("[11] r5: a window over a generation change reports its measured part as a rate\n");
  {
    ClientTelemetryWindow w;
    w.Reset(0, 0, 0);
#ifdef REMOTE60_DECODE_SPAN
    ClientDecodeStats d = decoded(15, 500'000);  // 15 pictures in the last half second of the window
#else
    ClientDecodeStats d = decoded(15);
    d.measuredUs = 500'000;  // 15 pictures in the last half second of the window
#endif
    w.Close(1'000'000, 15, 60'000, &d, nullptr);
    check("[11] 15 pictures over the measured 0.5 s = 30 fps, not 15", w.Pending().message.decodedFpsX100 == 3000,
          "decodedFpsX100=" + std::to_string(w.Pending().message.decodedFpsX100));
  }
#endif
#ifdef REMOTE60_DECODE_SPAN
  std::printf("[13] r6: Hold -- the minimum sample, its bound, what it leaves alone\n");
  {
    auto span = [](uint64_t id, uint64_t frames, uint64_t us) {
      ClientDecodeStats d;
      d.width = 1920;
      d.height = 1080;
      d.spanId = id;
      d.spanFrames = frames;
      d.spanUs = us;
      return d;
    };
    ClientTelemetryWindow w;
    w.Reset(0, 0, 0);
    const ClientDecodeStats a = span(1, 30, 1'000'000);
    w.Close(1'000'000, 30, 120'000, &a, nullptr);
    const ClientDecodeStats a2 = span(1, 60, 2'000'000);
    check("[13] the same measurement a second on is not held, and is its own second",
          !w.Hold(2'000'000, &a2, false) && w.Close(2'000'000, 60, 240'000, &a2, nullptr) &&
              w.Pending().message.decodedFpsX100 == 3000);
    // A new measurement 50 ms old at the due pass: held until it is 250 ms old.
    const ClientDecodeStats b = span(2, 2, 50'000);
    const bool heldB = w.Hold(3'000'000, &b, false);
    check("[13] a measurement 50 ms old is held; due again at 250 ms, not before, not a second later",
          heldB && !w.Due(3'190'000) && w.Due(3'200'000));
    check("[13] ...the unsent snapshot is left as it was", w.Pending(3'010'000).updatedQpcUs == 2'000'000);
    const ClientDecodeStats b2 = span(2, 8, 250'000);
    const bool closedB = !w.Hold(3'200'000, &b2, false) && w.Close(3'200'000, 96, 384'000, &b2, nullptr);
    check("[13] at 250 ms it closes, over the measurement's own time (8 in 0.25 s = 32 fps)",
          closedB && w.Pending().message.decodedFpsX100 == 3200, std::to_string(w.Pending().message.decodedFpsX100));
    check("[13] ...and the next window is due a second after that close", !w.Due(4'190'000) && w.Due(4'200'000));
    // A measurement that keeps restarting: the window is held at most 250 ms past its first hold.
    uint64_t now = 4'200'000, id = 10;
    bool closed = false;
    while (now < 5'000'000 && !closed) {
      const ClientDecodeStats c = span(++id, 0, 30'000);  // a new one every pass, 30 ms old
      if (w.Due(now) && !w.Hold(now, &c, false)) closed = w.Close(now, 96, 384'000, &c, nullptr);
      now += 10'000;
    }
    check("[13] RESTARTING EVERY PASS DOES NOT HOLD IT FOR EVER: closed 250 ms past the first hold",
          closed && now - 10'000 == 4'450'000 && w.hold_limited() == 1,
          "closed at " + std::to_string(now - 10'000) + " us, limited " + std::to_string(w.hold_limited()));
    // Paused or not valid while held: no hold (Close decides).
    const ClientDecodeStats d = span(50, 1, 10'000);
    (void)w.Hold(5'450'000, &d, false);
    check("[13] paused ends a hold: no hold, Close then withholds", !w.Hold(5'460'000, &d, true) && w.Due(5'460'000));
    check("[13] not valid ends a hold too", !w.Hold(5'470'000, nullptr, false));
  }
#endif

  TestChain();

  std::printf("\nclient_telemetry_window_test: %d passed, %d failed\n", gChecks - gFailures, gFailures);
  std::printf("RESULT: %s\n", gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
