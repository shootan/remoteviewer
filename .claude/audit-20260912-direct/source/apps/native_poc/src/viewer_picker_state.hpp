#pragma once

// Target picker state of the viewer (Phase 1-6 state struct).
//
// Role:    the window/monitor panel model, picker visibility and gesture latches, the card grid
//          scroll row, the mis-click guard timestamps, the macro button latch, and the preview
//          thumbnails with their fetch queue.
// Thread:  UI owns visibility, scroll, latches and the macro button; the control thread applies
//          window/monitor lists to `windowPanel` (mutex inside) and fetches thumbnails into
//          `thumbs` under `thumbMu`; recv reads `visible` to pause presents / catch-up.
// Input:   gestures, host replies.
// Output:  what WM_PAINT draws and which target gets selected.
// Callers: viewer_window_proc, viewer_picker, viewer_overlay_draw, viewer_layout, control thread, recv thread.
//
// Fields are the former globals gWindowPanelState / gWindowPickerVisible / gWindowPickerToggleDown /
// gGridScrollRow / gPickerShownAtUs / gPickerPressTargetId / gMacroButtonDown / gThumbMu / gThumbs /
// gThumbFetchQueue / gHostSupportsThumbnails, initialisers unchanged (viewer split refactor Phase 1-6).

#include "viewer_common.hpp"
#include "viewer_constants.hpp"

namespace remote60::native_poc::viewer {

// Preview thumbnails for the target picker, fetched over the control channel when the host
// advertises kControlWindowListFlagThumbnails. Keyed by window id; id 0 is the desktop.
struct WindowThumb {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> bgra;
  uint64_t fetchedUs = 0;
};

struct PickerState {
  // cross-thread: control applies lists, UI selects/scrolls (the model has its own mutex).
  WindowPanelStateModel windowPanel;
  // cross-thread: UI writes; recv and control read.
  std::atomic<bool> visible{true};
  // UI thread only.
  std::atomic<bool> toggleDown{false};
  std::atomic<int> gridScrollRow{0};  // card grid scroll, in whole rows
  // Picker mis-click guard (see kPickerPressNone / kPickerSelectMinShownUs in viewer_constants.hpp).
  std::atomic<uint64_t> shownAtUs{0};
  std::atomic<uint64_t> pressTargetId{kPickerPressNone};
  std::atomic<bool> macroButtonDown{false};
  // cross-thread: control fills, UI paints, both under thumbMu.
  std::mutex thumbMu;
  std::unordered_map<uint64_t, std::shared_ptr<const WindowThumb>> thumbs;
  std::deque<uint64_t> thumbFetchQueue;
  std::atomic<bool> hostSupportsThumbnails{false};

  // The picker gesture latch (viewer_picker_state.cpp), shared by the mouse and touch paths: a
  // selection needs DOWN and UP on the SAME target and a picker that has been visible for at least
  // kPickerSelectMinShownUs. Time is an argument, so viewer_picker_gesture_test drives it.
  void PressTarget(uint64_t hitId, uint64_t nowUs);   // DOWN: latch hitId (kPickerPressNone = empty space) unless the picker is too young
  uint64_t ReleaseTarget();                           // UP: consume the latch, return what DOWN latched
  void CancelPress();                                 // capture / focus lost, or a selection is pending
  bool ShownLongEnough(uint64_t nowUs) const;
  uint64_t ShownAgeMs(uint64_t nowUs) const;
  bool SelectAllowed(uint64_t pressedId, uint64_t hitId, uint64_t nowUs) const;  // ShownLongEnough && pressedId == hitId
};

}  // namespace remote60::native_poc::viewer
