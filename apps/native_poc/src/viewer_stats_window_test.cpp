// motion-settle r1, M4: the PC viewer's once-a-second stats window, end to end through the pieces
// that carry it to the host's ABR -- the viewer's StatsWindow (viewer_stats_window.hpp), the product
// ClientControlScheduler that sends one ControlClientMetrics per new report, the host's
// ClientMetricsSnapshot with its 3 s freshness gate, and RateControlState::DecideAbrProfile.
// Time is simulated (1 ms steps), so a 190 s gap costs nothing.
//
// The field case (2026-10-12 18:04, viewer 0.2.156 -> AMD host 0.2.157 over relay): after ~190 s
// with no frame (keyWaitMaxUs=190069856), the viewer's window deadline sat 190 s in the past and
// moved only one second per processed frame -- so every frame for the next 16 s closed a "window"
// of its own: recvFrames=1, published as 1 fps, dozens of reports a second (host: clientReports=89
// in 9 s). The host was sending a full cadence (sentFrames=29), read 1 fps as a relay collapse and
// demoted high_to_mid_severe at 18:04:28, then stayed soft for two minutes.
//
// What must hold after the fix, and what must still demote (Codex r1 짠1, M4 contract):
//   gap      190 s with no frame, then 30 fps: <= 2 reports a second, no fake low fps, no demote
//   steady   30 fps: about one report a second, ~30 fps, no demote
//   sparse   a still screen sending a frame every 1.5 s: no demote (the host's sparse hold)
//   lowdec   frames arrive at 30 fps but 2 a second decode: demotes (real decoder collapse)
//   lowrecv  3 fps arrive while the host sends 30: demotes (real loss)
//   silent   frames stop arriving while the host keeps sending: demotes (P7 stale_active)
//
//   remote60_viewer_stats_window_test        (pure-logic; prints PASS/FAIL lines and a summary)

#include "host_abr.hpp"
#include "host_client_metrics.hpp"
#include "native_video_client_shared_core.hpp"
#include "viewer_stats_window.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace remote60::native_poc;
using remote60::native_poc::viewer::StatsWindow;
using remote60::native_poc::viewer::StatsWindowClose;

namespace {

int gFailures = 0;
int gPasses = 0;

void expect(bool condition, const std::string& what) {
  if (condition) {
    std::printf("PASS %s\n", what.c_str());
    ++gPasses;
  } else {
    std::printf("FAIL %s\n", what.c_str());
    ++gFailures;
  }
}

constexpr uint64_t kMs = 1000ULL;
constexpr uint64_t kSec = 1000000ULL;
constexpr uint32_t kFps = 30;

// A frame the viewer's recv thread processes: whether it decoded.
struct Arrival {
  uint64_t atUs = 0;
  bool decoded = true;
};

// One second of what the host offered.
struct Scenario {
  std::string name;
  uint64_t lengthUs = 0;
  std::vector<Arrival> arrivals;           // sorted
  std::function<uint64_t(uint64_t)> hostSentInSecond;  // host tick time -> frames sent that second
};

struct Result {
  int profile = 0;
  int firstDemoteAtSec = -1;
  std::string firstReason;
  uint32_t maxReportsPerSecond = 0;      // over any host second
  uint32_t minPublishedDecodedFpsX100 = 0xFFFFFFFFu;  // over reports whose host second had a full send
  uint64_t reports = 0;
  uint32_t maxStaleActiveSeconds = 0;
};

// The viewer: the recv thread's counting as in flush_stats_if_due, then the control thread's
// scheduler, then the host's snapshot and once-a-second ABR tick.
Result Run(const Scenario& s) {
  Result r;
  StatsWindow window;
  const uint64_t viewerStartUs = 1 * kSec;
  window.Start(viewerStartUs);
  uint64_t recvFrames = 0, decodedFrames = 0;

  ClientControlMetricsSnapshot published{};
  ClientControlScheduler scheduler;
  scheduler.Reset(kClientControlIntervalMsDefault, viewerStartUs);
  WindowPanelStateModel panel;
  StreamStateControl streamState;
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframes(120000, 300000, 3);
  RuntimeTuneState tune(300000, 30000000, 250000, 1, 240);
  ClientInputQueue inputs;

  ClientMetricsSnapshot hostMetrics;
  RateControlState rate;
  rate.abrEnabled = true;
  rate.abrHasMidProfile = true;
  rate.abrHasLowProfile = true;
  rate.abrHasLowerResolution = true;
  rate.abrHighBitrate = 6000000;
  rate.abrMidBitrate = 4500000;
  rate.abrLowBitrate = 3000000;
  const uint64_t hostStartUs = 0;

  uint32_t reportsThisSecond = 0;
  size_t next = 0;
  for (uint64_t t = viewerStartUs; t <= s.lengthUs; t += kMs) {
    // recv thread
    while (next < s.arrivals.size() && s.arrivals[next].atUs <= t) {
      ++recvFrames;
      if (s.arrivals[next].decoded) ++decodedFrames;
      ++next;
      const StatsWindowClose c = window.Poll(t);
      if (c.due) {
        if (c.publish) {
          published.message.width = 1920;
          published.message.height = 1080;
          published.message.recvFpsX100 = StatsWindow::PerSecondX100(recvFrames, c.elapsedUs);
          published.message.decodedFpsX100 = StatsWindow::PerSecondX100(decodedFrames, c.elapsedUs);
          published.message.avgLatencyUs = 40000;
          published.message.avgDecodeTailUs = 20000;
          published.updatedQpcUs = t;
        }
        recvFrames = 0;
        decodedFrames = 0;
      }
    }
    // control thread: drain what the scheduler has
    for (int guard = 0; guard < 16; ++guard) {
      ControlOutboundAction out{};
      if (!scheduler.NextAction(t, published, &panel, &streamState, &captureMode, &keyframes, &tune,
                                &inputs, &out) ||
          out.kind == ControlOutboundActionKind::None) {
        break;
      }
      if (out.kind == ControlOutboundActionKind::Ping) {
        scheduler.OnPingCompleted(t);
        continue;
      }
      if (out.kind == ControlOutboundActionKind::Metrics) {
        ViewerMetrics v;
        v.width = out.metrics.width;
        v.height = out.metrics.height;
        v.recvFpsX100 = out.metrics.recvFpsX100;
        v.decodedFpsX100 = out.metrics.decodedFpsX100;
        v.avgLatencyUs = out.metrics.avgLatencyUs;
        v.avgDecodeTailUs = out.metrics.avgDecodeTailUs;
        v.updatedUs = t;  // host arrival (loopback)
        hostMetrics.Publish(v);
        ++reportsThisSecond;
        ++r.reports;
      }
    }
    // host 1 s tick
    if (t > hostStartUs && (t - hostStartUs) % kSec == 0) {
      const ViewerMetrics viewer = hostMetrics.Snapshot();
      const bool fresh = viewer.updatedUs > 0 && t >= viewer.updatedUs && (t - viewer.updatedUs) <= 3 * kSec;
      AbrInputs in;
      in.metricsFresh = fresh;
      in.clDecodedFpsX100 = fresh ? viewer.decodedFpsX100 : 0;
      in.clAvgLatencyUs = fresh ? viewer.avgLatencyUs : 0;
      in.clAvgDecodeTailUs = fresh ? viewer.avgDecodeTailUs : 0;
      in.cb2eAvgUs = 5000;
      in.sentFrames = s.hostSentInSecond(t);
      in.staticMode = false;
      in.activeFps = kFps;
      in.startUs = hostStartUs;
      in.clientEverReported = viewer.firstUs != 0;
      in.clientDecodeReported = viewer.decodeReported;
      const AbrDecision d = rate.DecideAbrProfile(in, t);
      r.maxStaleActiveSeconds = std::max(r.maxStaleActiveSeconds, rate.abrStaleActiveSeconds);
      if (d.targetProfile != rate.abrProfile) {
        if (r.firstDemoteAtSec < 0) {
          r.firstDemoteAtSec = static_cast<int>(t / kSec);
          r.firstReason = d.reason;
        }
        rate.CommitAbrProfile(d.targetProfile, false, t);
      }
      r.maxReportsPerSecond = std::max(r.maxReportsPerSecond, reportsThisSecond);
      if (fresh && in.sentFrames >= kFps - 1 && reportsThisSecond > 0) {
        r.minPublishedDecodedFpsX100 = std::min(r.minPublishedDecodedFpsX100, viewer.decodedFpsX100);
      }
      reportsThisSecond = 0;
    }
  }
  r.profile = rate.abrProfile;
  return r;
}

void Add(std::vector<Arrival>* a, uint64_t fromUs, uint64_t toUs, uint32_t fps, uint32_t decodedPerSecond = 0xFFFFFFFFu) {
  const uint64_t step = kSec / fps;
  uint32_t inSecond = 0;
  uint64_t secondStart = fromUs;
  for (uint64_t t = fromUs; t < toUs; t += step) {
    if (t - secondStart >= kSec) {
      secondStart = t;
      inSecond = 0;
    }
    a->push_back({t, inSecond < decodedPerSecond});
    ++inSecond;
  }
}

std::function<uint64_t(uint64_t)> SendWhile(std::vector<std::pair<uint64_t, uint64_t>> spans, uint64_t perSecond) {
  return [spans, perSecond](uint64_t t) -> uint64_t {
    for (const auto& sp : spans) {
      if (t > sp.first && t <= sp.second) return perSecond;
    }
    return 0;
  };
}

std::string Describe(const Result& r) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "profile=%d firstDemote=%ds(%s) maxReports/s=%u minPublishedFps=%.2f reports=%llu maxStaleActive=%u",
                r.profile, r.firstDemoteAtSec, r.firstReason.c_str(), r.maxReportsPerSecond,
                r.minPublishedDecodedFpsX100 == 0xFFFFFFFFu ? -1.0 : r.minPublishedDecodedFpsX100 / 100.0,
                static_cast<unsigned long long>(r.reports), r.maxStaleActiveSeconds);
  return buf;
}

void TestGapThenFullCadence() {
  // 10 s healthy, 190 s with nothing (host idle too), then 40 s at 30 fps.
  Scenario s;
  s.name = "gap";
  s.lengthUs = 240 * kSec;
  Add(&s.arrivals, 1 * kSec, 10 * kSec, kFps);
  Add(&s.arrivals, 200 * kSec, 240 * kSec, kFps);
  s.hostSentInSecond = SendWhile({{1 * kSec, 10 * kSec}, {200 * kSec, 240 * kSec}}, kFps);
  const Result r = Run(s);
  std::printf("  gap: %s\n", Describe(r).c_str());
  expect(r.maxReportsPerSecond <= 2, "M4 gap: after 190 s without frames the viewer sends <= 2 reports a second (field: 89 in 9 s)");
  expect(r.minPublishedDecodedFpsX100 >= kFps * 100 / 2,
         "M4 gap: no published decoded fps under half the cadence while the host sends a full one (field: 1 fps)");
  expect(r.profile == 0 && r.firstDemoteAtSec < 0, "M4 gap: the host ABR does not demote (field: high_to_mid_severe)");
  expect(r.maxStaleActiveSeconds <= 1, "M4 gap: the resumed stream is reported fresh before P7 counts it as feedback lost");
}

void TestShortGapsToo() {
  // 30 s and 180 s gaps, 60 fps on resume (Codex: 30~180 s, 30/60 fps).
  for (uint64_t gapSec : {30ULL, 180ULL}) {
    for (uint32_t fps : {30u, 60u}) {
      Scenario s;
      s.lengthUs = (20 + gapSec + 30) * kSec;
      Add(&s.arrivals, 1 * kSec, 20 * kSec, fps);
      Add(&s.arrivals, (20 + gapSec) * kSec, s.lengthUs, fps);
      s.hostSentInSecond = SendWhile({{1 * kSec, 20 * kSec}, {(20 + gapSec) * kSec, s.lengthUs}}, fps);
      // ABR judges against kFps; feed fps as the active rate by scaling: only the 30 fps case
      // drives ABR verdicts, the 60 fps case checks the report rate and published fps.
      const Result r = Run(s);
      char name[96];
      std::snprintf(name, sizeof(name), "M4 gap %llus -> %ufps", static_cast<unsigned long long>(gapSec), fps);
      std::printf("  %s: %s\n", name, Describe(r).c_str());
      expect(r.maxReportsPerSecond <= 2, std::string(name) + ": <= 2 reports a second");
      expect(r.minPublishedDecodedFpsX100 >= fps * 100 / 2, std::string(name) + ": no fake low fps published");
      expect(r.firstDemoteAtSec < 0, std::string(name) + ": no demote");
    }
  }
}

void TestSteady() {
  Scenario s;
  s.lengthUs = 60 * kSec;
  Add(&s.arrivals, 1 * kSec, 60 * kSec, kFps);
  s.hostSentInSecond = SendWhile({{1 * kSec, 60 * kSec}}, kFps);
  const Result r = Run(s);
  std::printf("  steady: %s\n", Describe(r).c_str());
  expect(r.profile == 0 && r.maxReportsPerSecond <= 2 && r.reports >= 55 && r.reports <= 61,
         "M4 steady 30 fps: one report a second, no demote");
  expect(r.minPublishedDecodedFpsX100 >= 2800 && r.minPublishedDecodedFpsX100 <= 3100,
         "M4 steady 30 fps: published ~30 fps");
}

void TestSparseStill() {
  Scenario s;
  s.lengthUs = 60 * kSec;
  for (uint64_t t = 1 * kSec; t < 60 * kSec; t += 1500 * kMs) s.arrivals.push_back({t, true});
  s.hostSentInSecond = [](uint64_t) -> uint64_t { return 1; };
  const Result r = Run(s);
  std::printf("  sparse: %s\n", Describe(r).c_str());
  expect(r.profile == 0, "M4 sparse still screen (a frame every 1.5 s): no demote");
}

void TestRealLowDecodeStillDemotes() {
  Scenario s;
  s.lengthUs = 30 * kSec;
  Add(&s.arrivals, 1 * kSec, 30 * kSec, kFps, 2);
  s.hostSentInSecond = SendWhile({{1 * kSec, 30 * kSec}}, kFps);
  const Result r = Run(s);
  std::printf("  lowdec: %s\n", Describe(r).c_str());
  expect(r.profile > 0 && r.firstReason.find("severe") != std::string::npos,
         "M4 kept: 30 fps arriving, 2 a second decoded -> still demotes severe");
}

void TestRealLowArrivalStillDemotes() {
  Scenario s;
  s.lengthUs = 30 * kSec;
  Add(&s.arrivals, 1 * kSec, 30 * kSec, 3);
  s.hostSentInSecond = SendWhile({{1 * kSec, 30 * kSec}}, kFps);
  const Result r = Run(s);
  std::printf("  lowrecv: %s\n", Describe(r).c_str());
  expect(r.profile > 0 && r.firstReason.find("severe") != std::string::npos,
         "M4 kept: 3 fps arriving while the host sends 30 -> still demotes severe");
}

void TestSilentStillDemotes() {
  Scenario s;
  s.lengthUs = 40 * kSec;
  Add(&s.arrivals, 1 * kSec, 15 * kSec, kFps);
  s.hostSentInSecond = SendWhile({{1 * kSec, 40 * kSec}}, kFps);
  const Result r = Run(s);
  std::printf("  silent: %s\n", Describe(r).c_str());
  expect(r.profile > 0 && r.firstDemoteAtSec >= 15 && r.firstDemoteAtSec <= 22,
         "M4 kept: frames stop arriving while the host sends -> stale_active still demotes");
}

void TestSparseArrivalUnderFullSendStillDemotes() {
  // A collapse in which a frame gets through only every 3 s while the host sends 30 a second: every
  // window spans a gap, so nothing is published -- the host must still see it as feedback lost.
  Scenario s;
  s.lengthUs = 40 * kSec;
  Add(&s.arrivals, 1 * kSec, 10 * kSec, kFps);
  for (uint64_t t = 10 * kSec; t < 40 * kSec; t += 3 * kSec) s.arrivals.push_back({t, true});
  s.hostSentInSecond = SendWhile({{1 * kSec, 40 * kSec}}, kFps);
  const Result r = Run(s);
  std::printf("  sparse-arrival: %s\n", Describe(r).c_str());
  expect(r.profile > 0 && r.firstDemoteAtSec >= 10 && r.firstDemoteAtSec <= 17,
         "M4 kept: one frame in 3 s arriving under a full send -> still demotes");
}

void TestRates() {
  expect(StatsWindow::PerSecondX100(30, kSec) == 3000, "rate: 30 in 1 s = 30.00/s");
  expect(StatsWindow::PerSecondX100(15, kSec / 2) == 3000, "rate: 15 in 0.5 s = 30.00/s");
  expect(StatsWindow::PerSecondX100(1, 190 * kSec) == 0, "rate: 1 in 190 s rounds to 0");
  expect(StatsWindow::PerSecondX100(5, 0) == 0, "rate: an empty window is 0, not a division by zero");
  expect(StatsWindow::Mbps(750000, kSec) == 6.0, "rate: 750000 bytes in 1 s = 6 Mbps");
}

}  // namespace

int main() {
  TestRates();
  TestSteady();
  TestGapThenFullCadence();
  TestShortGapsToo();
  TestSparseStill();
  TestRealLowDecodeStillDemotes();
  TestRealLowArrivalStillDemotes();
  TestSilentStillDemotes();
  TestSparseArrivalUnderFullSendStillDemotes();
  std::printf("viewer_stats_window_test: %d passed, %d failed\n", gPasses, gFailures);
  return gFailures == 0 ? 0 : 1;
}
