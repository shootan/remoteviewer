// Replays recorded arrival patterns through the playout clock and checks what would reach
// the screen. The clock ships to a phone, where "slightly uneven" is the whole bug and there
// is no debugger; these patterns are the ones that were measured misbehaving.

#include "video_playout_clock.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using remote60::native_poc::VideoPlayoutClock;

namespace {

int gFailures = 0;

void report(const struct Playout& p, uint64_t targetUs);

void expect(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++gFailures;
  }
}

struct Arrival {
  uint64_t nowUs = 0;            // when the frame reached the client
  uint64_t remoteCaptureUs = 0;  // host capture timestamp it carried
};

struct Playout {
  std::vector<uint64_t> gapsUs;
  uint64_t reanchors = 0;
  uint64_t lateFrames = 0;  // scheduled for a moment that had already passed
  uint64_t stepUs = 0;
  uint64_t maxHeadroomUs = 0;
  uint64_t minHeadroomUs = ~0ull;

  uint64_t Percentile(int pct) const {
    if (gapsUs.empty()) return 0;
    std::vector<uint64_t> sorted = gapsUs;
    std::sort(sorted.begin(), sorted.end());
    const size_t idx = std::min(sorted.size() - 1, (sorted.size() * pct) / 100);
    return sorted[idx];
  }
  size_t CountAtLeast(uint64_t thresholdUs) const {
    size_t n = 0;
    for (uint64_t g : gapsUs) {
      if (g >= thresholdUs) ++n;
    }
    return n;
  }
  double FractionAtLeast(uint64_t thresholdUs) const {
    if (gapsUs.empty()) return 0.0;
    return static_cast<double>(CountAtLeast(thresholdUs)) / static_cast<double>(gapsUs.size());
  }
};

Playout Run(const std::vector<Arrival>& arrivals, uint64_t targetIntervalUs) {
  VideoPlayoutClock clock;
  clock.SetTargetFrameIntervalUs(targetIntervalUs);

  Playout out;
  uint64_t lastDisplayUs = 0;
  for (const Arrival& a : arrivals) {
    const auto d = clock.Schedule(a.nowUs, a.remoteCaptureUs, 1);
    if (d.reanchored) ++out.reanchors;
    // A frame whose slot is already past cannot be paced; it lands on arrival, which is what
    // the device reports as an immediate release.
    const uint64_t displayUs = (d.presentAtUs > a.nowUs) ? d.presentAtUs : a.nowUs;
    if (d.presentAtUs <= a.nowUs) ++out.lateFrames;
    if (displayUs > a.nowUs) {
      const uint64_t headroom = displayUs - a.nowUs;
      out.maxHeadroomUs = std::max(out.maxHeadroomUs, headroom);
      out.minHeadroomUs = std::min(out.minHeadroomUs, headroom);
    } else {
      out.minHeadroomUs = 0;
    }
    if (lastDisplayUs != 0 && displayUs > lastDisplayUs) {
      out.gapsUs.push_back(displayUs - lastDisplayUs);
    }
    lastDisplayUs = displayUs;
  }
  out.stepUs = clock.StepUs();
  return out;
}

void report(const Playout& p, uint64_t targetUs) {
  std::printf(
      "  p50=%.1fms p95=%.1fms max=%.1fms  >=1.5x=%.1f%% >=2x=%.1f%%  reanchor=%llu late=%llu "
      "step=%.1fms headroom=%.0f..%.0fms\n",
      p.Percentile(50) / 1000.0, p.Percentile(95) / 1000.0,
      p.gapsUs.empty() ? 0.0 : *std::max_element(p.gapsUs.begin(), p.gapsUs.end()) / 1000.0,
      p.FractionAtLeast(targetUs * 3 / 2) * 100.0, p.FractionAtLeast(targetUs * 2) * 100.0,
      static_cast<unsigned long long>(p.reanchors), static_cast<unsigned long long>(p.lateFrames),
      p.stepUs / 1000.0, (p.minHeadroomUs == ~0ull ? 0.0 : p.minHeadroomUs / 1000.0),
      p.maxHeadroomUs / 1000.0);
}

// A host that captures on a clean cadence but hands the network bursts of three, which is
// what an async hardware encoder releasing several access units per drain looks like.
std::vector<Arrival> BurstyDelivery(uint64_t captureStepUs, size_t frames, size_t burst) {
  std::vector<Arrival> arrivals;
  const uint64_t startUs = 1000000;
  for (size_t i = 0; i < frames; ++i) {
    Arrival a;
    a.remoteCaptureUs = startUs + i * captureStepUs;
    // Frames leave the host in groups: the whole group arrives when the last of it was
    // captured, so the first two of every three are already "old" on arrival.
    const size_t groupEnd = (i / burst) * burst + (burst - 1);
    a.nowUs = startUs + groupEnd * captureStepUs + 5000;
    arrivals.push_back(a);
  }
  return arrivals;
}

void TestPacesBurstsIntoAnEvenCadence() {
  std::printf("bursty delivery is paced back to an even cadence\n");
  const uint64_t targetUs = 33333;
  const Playout p = Run(BurstyDelivery(targetUs, 600, 3), targetUs);
  report(p, targetUs);

  expect(p.reanchors <= 1, "reanchors=" + std::to_string(p.reanchors) + " (want <=1, init only)");
  expect(p.FractionAtLeast(targetUs * 2) < 0.05,
         "frames >=2x target = " + std::to_string(p.FractionAtLeast(targetUs * 2) * 100) + "%");
  // The whole point: gaps land near the cadence instead of alternating ~0 and ~100 ms.
  expect(p.Percentile(50) > targetUs * 3 / 4 && p.Percentile(50) < targetUs * 5 / 4,
         "p50=" + std::to_string(p.Percentile(50)) + " (want near " + std::to_string(targetUs) + ")");
  expect(p.Percentile(95) < targetUs * 3 / 2,
         "p95=" + std::to_string(p.Percentile(95)) + " (want < 1.5x target)");
}

void TestUnderdeliveryDoesNotForceReanchors() {
  // The regression this clock was written for. The host is asked for 60 fps and delivers 33;
  // a clock advancing one *requested* period per arrival falls behind by the shortfall every
  // second and has to jump to recover, over and over.
  std::printf("a host delivering 33 fps against a 60 fps request stays anchored\n");
  const uint64_t requestedUs = 16666;
  const uint64_t deliveredUs = 30000;
  const Playout p = Run(BurstyDelivery(deliveredUs, 600, 3), requestedUs);
  report(p, deliveredUs);

  expect(p.reanchors <= 1, "reanchors=" + std::to_string(p.reanchors) + " (want <=1)");
  // The cadence must converge on what arrives, not on what was asked for.
  expect(p.stepUs > deliveredUs * 9 / 10 && p.stepUs < deliveredUs * 11 / 10,
         "learned step=" + std::to_string(p.stepUs) + " (want near " +
             std::to_string(deliveredUs) + ")");
  expect(p.FractionAtLeast(deliveredUs * 2) < 0.05,
         "frames >=2x delivered = " +
             std::to_string(p.FractionAtLeast(deliveredUs * 2) * 100) + "%");
}

void TestSteadyDeliveryIsPassedThroughCleanly() {
  std::printf("a perfectly even sender stays even, at bounded latency\n");
  const uint64_t targetUs = 33333;
  std::vector<Arrival> arrivals;
  for (size_t i = 0; i < 300; ++i) {
    Arrival a;
    a.remoteCaptureUs = 1000000 + i * targetUs;
    a.nowUs = a.remoteCaptureUs + 8000;  // constant transport delay
    arrivals.push_back(a);
  }
  const Playout p = Run(arrivals, targetUs);
  report(p, targetUs);

  expect(p.reanchors <= 1, "reanchors=" + std::to_string(p.reanchors));
  expect(p.lateFrames == 0, "late frames=" + std::to_string(p.lateFrames));
  expect(p.Percentile(95) < targetUs * 5 / 4, "p95=" + std::to_string(p.Percentile(95)));
  // Buffering is a cost, so it has to stay near the target rather than creeping upward.
  expect(p.maxHeadroomUs <= VideoPlayoutClock::kLeadMaxUs * 2,
         "max headroom=" + std::to_string(p.maxHeadroomUs) + "us");
}

void TestRecoversFromAStall() {
  std::printf("a one-second stall costs one reanchor, not a permanent limp\n");
  const uint64_t targetUs = 33333;
  std::vector<Arrival> arrivals;
  uint64_t captureUs = 1000000;
  uint64_t arriveUs = 1008000;
  for (size_t i = 0; i < 150; ++i) {
    arrivals.push_back({arriveUs, captureUs});
    captureUs += targetUs;
    arriveUs += targetUs;
  }
  captureUs += 1000000;  // nothing captured or delivered for a second
  arriveUs += 1000000;
  for (size_t i = 0; i < 300; ++i) {
    arrivals.push_back({arriveUs, captureUs});
    captureUs += targetUs;
    arriveUs += targetUs;
  }
  const Playout p = Run(arrivals, targetUs);
  report(p, targetUs);

  // Init, plus the discontinuity itself. Anything more means it never re-settled.
  expect(p.reanchors <= 2, "reanchors=" + std::to_string(p.reanchors) + " (want <=2)");
  // One gap is the stall; the rest must be clean.
  expect(p.CountAtLeast(targetUs * 2) <= 1,
         "gaps >=2x target = " + std::to_string(p.CountAtLeast(targetUs * 2)) + " (want <=1)");
}

void TestLatencyDoesNotCreepWhenArrivalsDriftEarly() {
  std::printf("arrivals drifting earlier shrink the buffer instead of banking latency\n");
  const uint64_t targetUs = 33333;
  std::vector<Arrival> arrivals;
  uint64_t captureUs = 1000000;
  uint64_t delayUs = 150000;  // starts far behind, then the link catches up
  for (size_t i = 0; i < 600; ++i) {
    arrivals.push_back({captureUs + delayUs, captureUs});
    captureUs += targetUs;
    if (delayUs > 8000) delayUs -= 400;
  }
  const Playout p = Run(arrivals, targetUs);
  report(p, targetUs);

  expect(p.reanchors <= 1, "reanchors=" + std::to_string(p.reanchors));
  expect(p.maxHeadroomUs <= VideoPlayoutClock::kLeadMaxUs * 3,
         "max headroom=" + std::to_string(p.maxHeadroomUs) + "us (want bounded)");
  expect(p.FractionAtLeast(targetUs * 2) < 0.05,
         "frames >=2x target = " + std::to_string(p.FractionAtLeast(targetUs * 2) * 100) + "%");
}

void TestPreviousDesignWouldHaveFailed() {
  // Guards the diagnosis, not just the fix: reproduce the old rule (advance by the *requested*
  // period per arrival, 30 ms of headroom) and confirm it stutters on the same input the new
  // clock handles. If this ever passes, the explanation in the commit message is wrong.
  std::printf("the previous rule still fails the pattern that motivated the change\n");
  const uint64_t requestedUs = 16666;
  const uint64_t deliveredUs = 30000;
  const std::vector<Arrival> arrivals = BurstyDelivery(deliveredUs, 600, 3);

  uint64_t lastPtsUs = 0;
  uint64_t lastDisplayUs = 0;
  uint64_t reanchors = 0;
  size_t bigGaps = 0;
  size_t gaps = 0;
  for (const Arrival& a : arrivals) {
    uint64_t ptsUs;
    if (lastPtsUs == 0) {
      ptsUs = a.nowUs + 30000;
      ++reanchors;
    } else {
      ptsUs = lastPtsUs + requestedUs;
      if (a.nowUs > ptsUs + requestedUs) {
        ptsUs = a.nowUs + 30000;
        ++reanchors;
      }
    }
    lastPtsUs = ptsUs;
    const uint64_t displayUs = std::max(ptsUs, a.nowUs);
    if (lastDisplayUs != 0 && displayUs > lastDisplayUs) {
      ++gaps;
      if (displayUs - lastDisplayUs >= deliveredUs * 2) ++bigGaps;
    }
    lastDisplayUs = displayUs;
  }
  const double bigFraction = gaps ? static_cast<double>(bigGaps) / static_cast<double>(gaps) : 0.0;
  std::printf("  old rule: reanchors=%llu, frames >=2x = %.1f%%\n",
              static_cast<unsigned long long>(reanchors), bigFraction * 100.0);
  expect(reanchors > 10, "old rule reanchors=" + std::to_string(reanchors) + " (want many)");
}

}  // namespace

int main() {
  TestPacesBurstsIntoAnEvenCadence();
  TestUnderdeliveryDoesNotForceReanchors();
  TestSteadyDeliveryIsPassedThroughCleanly();
  TestRecoversFromAStall();
  TestLatencyDoesNotCreepWhenArrivalsDriftEarly();
  TestPreviousDesignWouldHaveFailed();

  if (gFailures != 0) {
    std::printf("video_playout_clock_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("video_playout_clock_test: PASS\n");
  return 0;
}
