// Unit test for the viewer's picker gesture latch (viewer split refactor Phase 2 / T3): a target is
// selected only when DOWN and UP land on the same card / the Desktop button and the picker has been
// visible for at least kPickerSelectMinShownUs; a press begun too early, on empty space, or a
// gesture that lost capture selects nothing. Time is an argument; no window.
//
// Build: remote60_viewer_picker_gesture_test (CMake). Run: prints "viewer_picker_gesture_test: PASS".

#include <cstdint>
#include <cstdio>

#include "viewer_constants.hpp"
#include "viewer_picker_state.hpp"

using namespace remote60::native_poc::viewer;

namespace {

int gFailures = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
      ++gFailures;                                                                   \
    }                                                                                \
  } while (0)

const uint64_t kMs = 1000;
const uint64_t kDesktop = 0;
const uint64_t kCardA = 0x1001;
const uint64_t kCardB = 0x1002;

// The UP handler's rule, as both the mouse and touch paths apply it after ReleaseTarget().
bool up_selects(PickerState& p, uint64_t hitId, uint64_t nowUs) {
  const uint64_t pressedId = p.ReleaseTarget();
  return p.SelectAllowed(pressedId, hitId, nowUs);
}

void test_same_target_after_stable() {
  std::printf("[T3] DOWN and UP on the same card after 300 ms select; the latch is consumed by the UP\n");
  PickerState p;
  const uint64_t shown = 10000 * kMs;
  p.shownAtUs.store(shown);
  // card A, 500 ms after the picker appeared
  p.PressTarget(kCardA, shown + 500 * kMs);
  CHECK(p.pressTargetId.load() == kCardA);
  CHECK(up_selects(p, kCardA, shown + 520 * kMs));
  CHECK(p.pressTargetId.load() == kPickerPressNone);   // consumed
  // a second UP without its own DOWN selects nothing
  CHECK(!up_selects(p, kCardA, shown + 540 * kMs));
  // the Desktop button (id 0) is a valid target, distinct from "nothing pressed"
  p.PressTarget(kDesktop, shown + 600 * kMs);
  CHECK(p.pressTargetId.load() == kDesktop);
  CHECK(up_selects(p, kDesktop, shown + 620 * kMs));
  CHECK(p.ShownAgeMs(shown + 620 * kMs) == 620);
}

void test_early_press_and_empty_space() {
  std::printf("[T3] a DOWN within the first 300 ms latches nothing; so does empty picker space\n");
  PickerState p;
  const uint64_t shown = 20000 * kMs;
  p.shownAtUs.store(shown);
  p.PressTarget(kCardA, shown + 100 * kMs);            // too early
  CHECK(p.pressTargetId.load() == kPickerPressNone);
  CHECK(!up_selects(p, kCardA, shown + 400 * kMs));     // even though the UP is late enough
  p.PressTarget(kPickerPressNone, shown + 500 * kMs);   // empty space
  CHECK(!up_selects(p, kCardA, shown + 520 * kMs));
  // exactly at the boundary: 300 ms counts as stable (>=), 299 ms does not
  p.PressTarget(kCardA, shown + kPickerSelectMinShownUs);
  CHECK(p.pressTargetId.load() == kCardA);
  CHECK(p.ShownLongEnough(shown + kPickerSelectMinShownUs));
  CHECK(!p.ShownLongEnough(shown + kPickerSelectMinShownUs - 1));
  p.PressTarget(kCardA, shown + kPickerSelectMinShownUs - 1);
  CHECK(p.pressTargetId.load() == kPickerPressNone);
}

void test_different_target_and_late_up() {
  std::printf("[T3] UP on another card, or an UP before the picker is stable, selects nothing\n");
  PickerState p;
  const uint64_t shown = 30000 * kMs;
  p.shownAtUs.store(shown);
  p.PressTarget(kCardA, shown + 400 * kMs);
  CHECK(!up_selects(p, kCardB, shown + 420 * kMs));
  // DOWN latched (stable) but the picker got re-shown (shownAtUs moved) before the UP: not stable at UP time
  p.PressTarget(kCardA, shown + 400 * kMs);
  p.shownAtUs.store(shown + 410 * kMs);
  CHECK(!up_selects(p, kCardA, shown + 420 * kMs));
  CHECK(p.pressTargetId.load() == kPickerPressNone);
}

void test_cancel() {
  std::printf("[T3] capture / focus loss or a pending selection cancels the latch\n");
  PickerState p;
  const uint64_t shown = 40000 * kMs;
  p.shownAtUs.store(shown);
  p.PressTarget(kCardA, shown + 400 * kMs);
  p.CancelPress();
  CHECK(p.pressTargetId.load() == kPickerPressNone);
  CHECK(!up_selects(p, kCardA, shown + 420 * kMs));
  // the default latch value is "nothing pressed"
  PickerState fresh;
  CHECK(fresh.pressTargetId.load() == kPickerPressNone);
  CHECK(fresh.ReleaseTarget() == kPickerPressNone);
}

}  // namespace

int main() {
  test_same_target_after_stable();
  test_early_press_and_empty_space();
  test_different_target_and_late_up();
  test_cancel();
  if (gFailures != 0) {
    std::printf("viewer_picker_gesture_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("viewer_picker_gesture_test: PASS\n");
  return 0;
}
