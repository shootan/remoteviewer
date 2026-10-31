// The picker gesture latch (viewer split refactor Phase 2-6). Each member is the sequence the mouse
// (WM_LBUTTONDOWN / WM_LBUTTONUP) and touch (WM_POINTERDOWN / WM_POINTERUP) handlers performed on
// ctx.picker.pressTargetId / shownAtUs, with the time as an argument.

#include "viewer_picker_state.hpp"

namespace remote60::native_poc::viewer {

void PickerState::PressTarget(uint64_t hitId, uint64_t nowUs) {
  // Remember which target (if any) this press started on; the UP handler only selects when
  // it ends on the same one. A press on empty picker space latches "none", and so does a
  // press within the first 300ms after the picker appeared -- the gesture must START after
  // the picker is stable, or a long-press begun against the old screen could still select.
  uint64_t pressedId = hitId;
  if (nowUs < shownAtUs.load(std::memory_order_relaxed) + kPickerSelectMinShownUs) {
    pressedId = kPickerPressNone;
  }
  pressTargetId.store(pressedId, std::memory_order_relaxed);
}

uint64_t PickerState::ReleaseTarget() {
  // Consume the press latch FIRST, unconditionally: any UP ends the gesture, and an early
  // return in the caller must not leave a stale latch to approve a later unrelated UP.
  return pressTargetId.exchange(kPickerPressNone, std::memory_order_relaxed);
}

void PickerState::CancelPress() {
  pressTargetId.store(kPickerPressNone, std::memory_order_relaxed);
}

bool PickerState::ShownLongEnough(uint64_t nowUs) const {
  return nowUs >= shownAtUs.load(std::memory_order_relaxed) + kPickerSelectMinShownUs;
}

uint64_t PickerState::ShownAgeMs(uint64_t nowUs) const {
  return (nowUs - shownAtUs.load(std::memory_order_relaxed)) / 1000;
}

bool PickerState::SelectAllowed(uint64_t pressedId, uint64_t hitId, uint64_t nowUs) const {
  return ShownLongEnough(nowUs) && pressedId == hitId;
}

}  // namespace remote60::native_poc::viewer
