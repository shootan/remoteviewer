#pragma once

// The latest remote hardware-cursor sample and its overlay window (Phase 1-9 state struct).
//
// Role:    Remote hardware-cursor state (UdpCursorPosPacket, DXGI desktop capture only). The host's
//          pipeline drops pointer-only frames, so this side channel is what keeps the remote cursor
//          visibly moving on a still screen. Drawn as a layered overlay; hidden when stale (>500ms).
// Thread:  recv writes the sample (latest wins, atomics); the UI timer reads it and owns overlayHwnd;
//          the picker (UI) clears updateUs on a new selection.
// Input:   UdpCursorPosPacket.
// Output:  the overlay window position/visibility.
// Callers: recv thread, viewer_cursor_overlay, viewer_picker.
//
// Fields are the former globals gRemoteCursorX / gRemoteCursorY / gRemoteCursorCapW /
// gRemoteCursorCapH / gRemoteCursorGeneration / gRemoteCursorVisible / gRemoteCursorUpdateUs /
// gCursorOverlayHwnd, initialisers unchanged (viewer split refactor Phase 1-9).

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

struct RemoteCursorState {
  // cross-thread: recv writes, UI timer reads.
  std::atomic<int32_t> x{0};
  std::atomic<int32_t> y{0};
  std::atomic<uint32_t> capW{0};
  std::atomic<uint32_t> capH{0};
  std::atomic<uint64_t> generation{0};  // stream generation the sample belongs to
  std::atomic<bool> visible{false};
  std::atomic<uint64_t> updateUs{0};
  // UI thread only.
  HWND overlayHwnd = nullptr;
};

}  // namespace remote60::native_poc::viewer
