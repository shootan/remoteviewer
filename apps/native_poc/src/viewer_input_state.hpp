#pragma once

// Local input state of the viewer window (Phase 1-8 state struct).
//
// Role:    which mouse buttons this client holds down on the host, the last mapped video
//          coordinate (for releases), which keys had a forwarded down, the touch-to-mouse
//          suppression window, the active touch pointer, and the macro engine.
// Thread:  UI only (WndProc and the macro window on the same thread); the fields are atomics
//          because the monolith kept them so.
// Input:   window messages.
// Output:  input events queued for the control thread (through viewer_input_forward).
// Callers: viewer_window_proc, viewer_input_forward, main() (macro stop at shutdown).
//
// Fields are the former globals gMouseButtons / gLastInputVideoX / gLastInputVideoY /
// gForwardedKeyDown / gSuppressMouseUntilUs / gActiveTouchPointerId / gActiveTouchDown /
// gInputMacro, initialisers unchanged (viewer split refactor Phase 1-8).

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

struct InputState {
  std::atomic<uint16_t> mouseButtons{0};
  std::atomic<int32_t> lastVideoX{0};
  std::atomic<int32_t> lastVideoY{0};
  // Which keys this client forwarded a down for, so the matching up is forwarded by memory
  // rather than by re-deciding. The decision depends on modifier state, and re-evaluating it
  // at release time strands keys on the host: Ctrl+A with Ctrl released first re-classifies
  // the A as text on the way up, and the host holds A down forever.
  std::atomic<bool> forwardedKeyDown[256]{};
  std::atomic<uint64_t> suppressMouseUntilUs{0};
  std::atomic<uint32_t> activeTouchPointerId{0};
  std::atomic<bool> activeTouchDown{false};
  // P0 telemetry (input serialization diagnosis, history #351): every mouse-move that passes the
  // guards and is enqueued, counted BEFORE latest-wins coalescing. Compared per second against the
  // count actually sent (inputEventsSent) to show whether a drag generates ~60/s but only ~1/RTT
  // reaches the host.
  std::atomic<uint64_t> moveGeneratedCount{0};
  remote60::native_poc::InputMacro macro;
};

}  // namespace remote60::native_poc::viewer
