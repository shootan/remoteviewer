// The bug being pinned: mapping onto the primary monitor's metrics made a UAC prompt on a
// secondary display unreachable, and SetCursorPos reported success while the click landed on the
// wrong screen. Every case here is one where the old code produced a plausible-looking
// coordinate that was simply somewhere else.

#include "secure_input_mapping.hpp"
#include "secure_input_diag_budget.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using remote60::native_poc::DesktopRect;
using remote60::native_poc::map_client_point;

namespace {

int gFailures = 0;

void expect(const std::string& what, int32_t x, int32_t y, uint32_t inW, uint32_t inH,
            const DesktopRect& target, int32_t wantX, int32_t wantY) {
  const auto got = map_client_point(x, y, inW, inH, target);
  if (got.x != wantX || got.y != wantY) {
    std::printf("  FAIL %s: (%d,%d) in %ux%u -> (%d,%d), wanted (%d,%d)\n", what.c_str(), x, y,
                inW, inH, got.x, got.y, wantX, wantY);
    ++gFailures;
  } else {
    std::printf("  ok   %s: (%d,%d) -> (%d,%d)\n", what.c_str(), x, y, got.x, got.y);
  }
}

void TestSingleMonitorUnchanged() {
  std::printf("the ordinary single-monitor case maps corner to corner\n");
  const DesktopRect screen{0, 0, 1920, 1080};
  expect("top-left", 0, 0, 1920, 1080, screen, 0, 0);
  expect("bottom-right", 1919, 1079, 1920, 1080, screen, 1919, 1079);
  expect("centre", 960, 540, 1920, 1080, screen, 960, 540);
}

// The measured layout: 2720x1080 of virtual desktop across two monitors. A click on the right
// half has to land on the second monitor, which the old primary-only mapping could never reach.
void TestSecondaryMonitorIsReachable() {
  std::printf("a click on the right half lands on the second monitor\n");
  const DesktopRect virtualScreen{0, 0, 2720, 1080};
  expect("far right edge", 2719, 500, 2720, 1080, virtualScreen, 2719, 500);
  // Anything past 1920 was unreachable when the target was the primary monitor alone.
  expect("just past the primary", 2000, 100, 2720, 1080, virtualScreen, 2000, 100);
}

// A monitor placed to the left of the primary gives the virtual screen a negative origin, and
// mapping that assumes an origin of zero puts every click one screen to the right.
void TestNegativeOriginIsHonoured() {
  std::printf("a monitor left of the primary has a negative origin\n");
  const DesktopRect virtualScreen{-1920, 0, 3840, 1080};
  expect("leftmost pixel", 0, 0, 3840, 1080, virtualScreen, -1920, 0);
  expect("primary origin", 1920, 0, 3840, 1080, virtualScreen, 0, 0);
}

// The client scales the picture, so its coordinate space rarely equals the desktop's.
void TestScaledClientSpace() {
  std::printf("a scaled client space still reaches both ends\n");
  const DesktopRect screen{0, 0, 1920, 1080};
  expect("top-left", 0, 0, 960, 540, screen, 0, 0);
  expect("bottom-right", 959, 539, 960, 540, screen, 1919, 1079);
  expect("centre", 480, 270, 960, 540, screen, 960, 540);
}

// Rounding in the client's own scaling can produce a coordinate one past the edge. Dropping the
// event would make the edge of the screen intermittently dead.
void TestOutOfRangeIsClampedNotDropped() {
  std::printf("out-of-range input is clamped to the edge\n");
  const DesktopRect screen{0, 0, 1920, 1080};
  expect("past the right edge", 5000, 0, 1920, 1080, screen, 1919, 0);
  expect("negative", -20, -20, 1920, 1080, screen, 0, 0);
}

void TestDegenerateInputSpace() {
  std::printf("a one-pixel input space does not divide by zero\n");
  const DesktopRect screen{100, 200, 1920, 1080};
  expect("single pixel", 0, 0, 1, 1, screen, 100, 200);
}

// The 2026-09-07 defect, pinned (P9): the same click in the same 1920x1080 domain lands where it
// was aimed with the live console rect and 138/99 px down-right with the RDP-era rect the host
// kept from its start. The mapper is right both times; the rect it was fed was not.
void TestStaleRdpRectScalesTheClick() {
  std::printf("a stale 2236x1232 rect scales a 1920x1080 click; the live rect does not\n");
  const DesktopRect stale{0, 0, 2236, 1232};
  const DesktopRect live{0, 0, 1920, 1080};
  expect("field click 14:27:25, stale rect", 841, 703, 1920, 1080, stale, 979, 802);
  expect("field click 14:27:25, live rect", 841, 703, 1920, 1080, live, 841, 703);
  expect("field click 14:27:27, stale rect", 808, 704, 1920, 1080, stale, 941, 803);
  expect("field click 14:27:27, live rect", 808, 704, 1920, 1080, live, 808, 704);
}

void check(bool cond, const char* what) {
  if (!cond) {
    std::printf("  FAIL %s\n", what);
    ++gFailures;
  } else {
    std::printf("  ok   %s\n", what);
  }
}

// The landing diag budget: 12 per episode, refilled by a 2 s quiet gap, 60 per minute at most.
void TestLandingBudgetRefillsPerEpisode() {
  std::printf("the landing diag budget refills per episode and caps per minute\n");
  using remote60::native_poc::DiagLandingBudget;
  using remote60::native_poc::diag_landing_budget_take;
  DiagLandingBudget b;
  uint64_t t = 1'000'000;
  int granted = 0;
  for (int i = 0; i < 20; ++i) {
    if (diag_landing_budget_take(b, t)) ++granted;
    t += 50'000;
  }
  check(granted == 12, "12 lines in the first episode, the 13th+ refused");
  // The next UAC prompt, 2.5 s later, gets its own 12 -- the case the one-shot budget lost.
  t += 2'500'000;
  granted = 0;
  for (int i = 0; i < 20; ++i) {
    if (diag_landing_budget_take(b, t)) ++granted;
    t += 50'000;
  }
  check(granted == 12, "a 2 s gap refills the episode");
  // A click every 100 ms for 3 s stays in one episode: no refill inside it.
  granted = 0;
  for (int i = 0; i < 30; ++i) {
    if (diag_landing_budget_take(b, t)) ++granted;
    t += 100'000;
  }
  check(granted == 0, "continuous clicking does not refill");
  // Pathological: 30 prompts 2.5 s apart. Minute one (started at t=1.0 s) already granted 24, so
  // 36 more; minute two grants at most 60; the rest are refused. 96 lines for 360 clicks.
  granted = 0;
  for (int e = 0; e < 30; ++e) {
    t += 2'500'000;
    for (int i = 0; i < 12; ++i) {
      if (diag_landing_budget_take(b, t)) ++granted;
      t += 1'000;
    }
  }
  check(granted == 96, "the per-minute ceiling holds across many episodes");
}

}  // namespace

int main() {
  TestSingleMonitorUnchanged();
  TestSecondaryMonitorIsReachable();
  TestNegativeOriginIsHonoured();
  TestScaledClientSpace();
  TestOutOfRangeIsClampedNotDropped();
  TestDegenerateInputSpace();
  TestStaleRdpRectScalesTheClick();
  TestLandingBudgetRefillsPerEpisode();

  if (gFailures != 0) {
    std::printf("secure_input_mapping_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("secure_input_mapping_test: PASS\n");
  return 0;
}
