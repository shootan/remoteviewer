#pragma once

// Present-path counters and trace switches (Phase 1-3 state struct).
//
// Role:    D3D / GDI-fallback present counters, the trace_every/trace_max switches and their
//          printed counts, the expected present interval for telemetry, and the WM_PAINT
//          bookkeeping that used to live in function statics (first-frame flag, last present
//          time, last user-feedback line).
// Thread:  UI writes the counters and the paint bookkeeping in WM_PAINT; the recv thread reads the
//          counters once a second for the stats line (deltas) and bumps traceRecvPrinted; main
//          sets the trace switches and the present interval once at startup.
// Input:   present outcomes.
// Output:  the [present]/[trace_present]/[user-feedback] lines and the stats-line present fields.
// Callers: viewer_window_proc WM_PAINT, recv thread stats, main().
//
// Fields are the former globals gD3dPresentSuccessCount / gD3dPresentFailCount /
// gGdiFallbackPresentedCount / gFallbackInitFailCount / gFallbackRenderFailCount /
// gFallbackNv12ConvertFailCount / gTraceEvery / gTraceMax / gTracePresentPrinted /
// gTraceRecvPrinted / gPresentFrameIntervalUs and WM_PAINT's static hasPresentedAtLeastOneFrame /
// lastPresentUs / lastUserFeedbackUs / lastUserFeedbackOverwrite, initialisers unchanged
// (viewer split refactor Phase 1-3).

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

struct PresentStats {
  // cross-thread: UI increments, recv reads (1s stats deltas).
  std::atomic<uint64_t> d3dPresentSuccessCount{0};
  std::atomic<uint64_t> d3dPresentFailCount{0};
  std::atomic<uint64_t> gdiFallbackPresentedCount{0};
  std::atomic<uint64_t> fallbackInitFailCount{0};
  std::atomic<uint64_t> fallbackRenderFailCount{0};
  std::atomic<uint64_t> fallbackNv12ConvertFailCount{0};
  // cross-thread: main sets once; UI (present trace) and recv (recv trace) read.
  std::atomic<uint32_t> traceEvery{0};
  std::atomic<uint32_t> traceMax{0};
  // Diagnostics-only: expected present interval (from fpsHint), published once at startup so the
  // present-stage stream telemetry on the UI thread can flag gaps past 1.5x cadence without reaching
  // into the recv thread's Args. 0 => fall back to a 60fps assumption.
  std::atomic<uint32_t> presentFrameIntervalUs{0};
  std::atomic<uint64_t> tracePresentPrinted{0};
  std::atomic<uint64_t> traceRecvPrinted{0};
  // UI thread only (WM_PAINT). Per stream episode: ResetForNewEpisode() clears them when a new
  // selection is revealed, so a second target in the same process does not inherit the first
  // one's "already painted" flag and feedback rate-limit. (F-14.)
  bool hasPresentedAtLeastOneFrame = false;
  uint64_t lastPresentUs = 0;
  uint64_t lastUserFeedbackUs = 0;
  uint64_t lastUserFeedbackOverwrite = 0;
  void ResetForNewEpisode() {
    hasPresentedAtLeastOneFrame = false;
    lastPresentUs = 0;
    lastUserFeedbackUs = 0;
    lastUserFeedbackOverwrite = 0;
  }
};

}  // namespace remote60::native_poc::viewer
