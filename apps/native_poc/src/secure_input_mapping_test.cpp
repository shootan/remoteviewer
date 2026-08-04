// The bug being pinned: mapping onto the primary monitor's metrics made a UAC prompt on a
// secondary display unreachable, and SetCursorPos reported success while the click landed on the
// wrong screen. Every case here is one where the old code produced a plausible-looking
// coordinate that was simply somewhere else.

#include "secure_input_mapping.hpp"

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

}  // namespace

int main() {
  TestSingleMonitorUnchanged();
  TestSecondaryMonitorIsReachable();
  TestNegativeOriginIsHonoured();
  TestScaledClientSpace();
  TestOutOfRangeIsClampedNotDropped();
  TestDegenerateInputSpace();

  if (gFailures != 0) {
    std::printf("secure_input_mapping_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("secure_input_mapping_test: PASS\n");
  return 0;
}
