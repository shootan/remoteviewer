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
// [5] decode validity: no decode fields before this connection's first output; a not-valid window after
//     it is withheld (not reported as 0); Reset (a new connection) drops pending and seen.
// [6] two windows (two controllers) are independent.

#include <cstdio>
#include <string>
#include <vector>

#include "client_telemetry_window.hpp"
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

ClientDecodeStats decoded(uint32_t n, uint32_t w = 1920, uint32_t h = 1080) {
  ClientDecodeStats d;
  d.decodedFrames = n;
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
    const ClientDecodeStats d = decoded(static_cast<uint32_t>(n));
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
    const ClientDecodeStats d1 = decoded(150);
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
    const ClientDecodeStats none = decoded(0, 0, 0);
    const bool made0 = w.Close(1'000'000, 10, 40'000, &none, nullptr);
    check("[5] before this connection's first decoder output: no report (a 0 would read as a collapse)",
          !made0 && !w.decode_seen() && w.Pending().updatedQpcUs == 0);
    ClientPresentationStats p{};
    p.sampleCount = 5;
    const bool madeP = w.Close(2'000'000, 20, 80'000, &none, &p);
    check("[5] ...a present-only report then carries no decode field (as before)",
          madeP && w.Pending().message.decodedFpsX100 == 0 && w.Pending().message.width == 0 &&
              w.Pending().message.recvFpsX100 == 0);
    const ClientDecodeStats d = decoded(28);
    w.Close(3'000'000, 50, 200'000, &d, nullptr);
    check("[5] the first output turns decode fields on", w.decode_seen() && w.Pending().message.decodedFpsX100 == 2800);
    const uint64_t before = w.Pending().updatedQpcUs;
    const bool madeU = w.Close(4'000'000, 80, 320'000, nullptr, &p);  // not valid (no surface / selection wait)
    check("[5] a not-valid window after it is withheld -- not reported as 0, the pending one is not restamped",
          !madeU && w.withheld() == 1 && w.Pending().updatedQpcUs == before);
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

  std::printf("\nclient_telemetry_window_test: %d passed, %d failed\n", gChecks - gFailures, gFailures);
  std::printf("RESULT: %s\n", gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
