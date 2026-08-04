// Replays the offer patterns a desktop actually produces through the submit gate and checks
// what cadence comes out. The measured patterns are the point: a busy screen offering far
// more than the target, and a quiet one offering less, which is where a 60 fps request went
// wrong on the device.

#include "capture_cadence_gate.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using remote60::native_poc::CaptureCadenceGate;

namespace {

int gFailures = 0;

void expect(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++gFailures;
  }
}

struct GateResult {
  std::vector<uint64_t> acceptedAtUs;
  size_t offered = 0;

  double AcceptedPerSecond(uint64_t spanUs) const {
    return spanUs ? acceptedAtUs.size() * 1000000.0 / static_cast<double>(spanUs) : 0.0;
  }
  // Spread of the gaps between accepted frames, relative to their own mean. This is what
  // "even" means here: not that the rate matched the request, but that the frames that did
  // arrive were spaced alike.
  double GapSpreadRatio() const {
    if (acceptedAtUs.size() < 3) return 0.0;
    std::vector<uint64_t> gaps;
    for (size_t i = 1; i < acceptedAtUs.size(); ++i) {
      gaps.push_back(acceptedAtUs[i] - acceptedAtUs[i - 1]);
    }
    std::sort(gaps.begin(), gaps.end());
    const uint64_t p50 = gaps[gaps.size() / 2];
    const uint64_t p95 = gaps[std::min(gaps.size() - 1, gaps.size() * 95 / 100)];
    return p50 ? static_cast<double>(p95) / static_cast<double>(p50) : 0.0;
  }
};

// Feeds offers at the given per-second rates, one entry per second, spacing each second's
// offers evenly within it.
GateResult Run(const std::vector<uint32_t>& offersPerSecond, uint32_t targetFps, bool) {
  CaptureCadenceGate gate;
  gate.SetRequestedIntervalUs(1000000u / targetFps);
  gate.SetEnabled(true);

  GateResult out;
  uint64_t nowUs = 1000000;
  for (uint32_t offers : offersPerSecond) {
    const uint64_t stepUs = offers ? 1000000ull / offers : 1000000ull;
    for (uint32_t i = 0; i < offers; ++i) {
      ++out.offered;
      if (gate.ShouldAccept(nowUs, true)) out.acceptedAtUs.push_back(nowUs);
      nowUs += stepUs;
    }
  }
  return out;
}

// A busy screen: far more offers than any target needs. This is the case that already worked
// and must keep working.
void TestBusyScreenStillPacesToTheRequest() {
  std::printf("a screen offering 80/s still yields an even 30 at a 30 fps request\n");
  const std::vector<uint32_t> offers(10, 80);
  const GateResult r = Run(offers, 30, true);
  const double fps = r.AcceptedPerSecond(10 * 1000000ull);
  std::printf("  offered=%zu accepted=%zu (%.1f/s) gapSpread=%.2f\n", r.offered,
              r.acceptedAtUs.size(), fps, r.GapSpreadRatio());
  expect(fps > 28.0 && fps < 31.0, "accepted rate near 30, got " + std::to_string(fps));
  expect(r.GapSpreadRatio() < 1.3, "gaps stay even, spread " + std::to_string(r.GapSpreadRatio()));
}

void TestBusyScreenReaches60WhenAsked() {
  std::printf("the same screen yields an even 60 at a 60 fps request\n");
  const std::vector<uint32_t> offers(10, 110);
  const GateResult r = Run(offers, 60, true);
  const double fps = r.AcceptedPerSecond(10 * 1000000ull);
  std::printf("  offered=%zu accepted=%zu (%.1f/s) gapSpread=%.2f\n", r.offered,
              r.acceptedAtUs.size(), fps, r.GapSpreadRatio());
  expect(fps > 55.0, "accepted rate near 60, got " + std::to_string(fps));
  expect(r.GapSpreadRatio() < 1.3, "gaps stay even, spread " + std::to_string(r.GapSpreadRatio()));
}

// The device case, kept as a record of what the gate cannot do. A 60 fps request against a
// desktop whose update rate swings between 7 and 52 a second -- the range measured on the
// phone. Pacing to a sustainable rate was implemented and measured here: it moved the spread
// from 2.75 to 2.50 and cost a sixth of the frames, because a gate can only discard and the
// second that offered seven has nothing to discard. The unevenness is the screen's.
void TestIrregularQuietScreenAt60() {
  std::printf("a 60 fps request against a desktop swinging 7..52 offers a second\n");
  const std::vector<uint32_t> offers = {33, 30, 7,  52, 28, 33, 12, 47,
                                        30, 33, 9,  50, 31, 29, 35, 33};
  const uint64_t spanUs = static_cast<uint64_t>(offers.size()) * 1000000ull;

  const GateResult r = Run(offers, 60, true);
  std::printf("  accepted=%zu (%.1f/s) gapSpread=%.2f\n", r.acceptedAtUs.size(),
              r.AcceptedPerSecond(spanUs), r.GapSpreadRatio());

  // The swing reaches the encoder, and that is the honest outcome: at 60 fps there is nothing
  // spare to reject, so the gate takes what it is given.
  expect(r.GapSpreadRatio() > 2.0,
         "the swing reaches the encoder, spread " + std::to_string(r.GapSpreadRatio()));
  expect(r.acceptedAtUs.size() > r.offered * 4 / 5,
         "almost nothing is rejected: " + std::to_string(r.acceptedAtUs.size()) + " of " +
             std::to_string(r.offered));
}

void TestQuietScreenNeverPacedFasterThanAsked() {
  std::printf("a 30 fps request is never exceeded, however much the screen offers\n");
  const std::vector<uint32_t> offers(8, 120);
  const GateResult r = Run(offers, 30, true);
  const double fps = r.AcceptedPerSecond(8 * 1000000ull);
  std::printf("  offered=%zu accepted=%zu (%.1f/s)\n", r.offered, r.acceptedAtUs.size(), fps);
  expect(fps <= 31.0, "never above the request, got " + std::to_string(fps));
}

void TestStillScreenDoesNotStallTheCadence() {
  std::printf("a long still stretch does not drag the cadence to a crawl\n");
  std::vector<uint32_t> offers = {33, 33, 2, 2, 2, 2, 33, 33, 33, 33};
  const GateResult r = Run(offers, 30, true);
  // The recovery seconds must come back to something close to the request rather than
  // staying stuck at the rate the still stretch implied.
  size_t lateAccepted = 0;
  const uint64_t tailStartUs = 1000000 + 6 * 1000000ull;
  for (uint64_t t : r.acceptedAtUs) {
    if (t >= tailStartUs) ++lateAccepted;
  }
  std::printf("  accepted in the last 4 seconds: %zu\n", lateAccepted);
  expect(lateAccepted >= 90, "recovers to near 30/s, got " + std::to_string(lateAccepted / 4) +
                                 "/s");
}

}  // namespace

int main() {
  TestBusyScreenStillPacesToTheRequest();
  TestBusyScreenReaches60WhenAsked();
  TestQuietScreenNeverPacedFasterThanAsked();
  TestIrregularQuietScreenAt60();
  TestStillScreenDoesNotStallTheCadence();

  if (gFailures != 0) {
    std::printf("capture_cadence_gate_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("capture_cadence_gate_test: PASS\n");
  return 0;
}
