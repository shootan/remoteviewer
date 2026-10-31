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

// One remote-cursor report: position in the host's capture space, the capture size it is
// expressed in, the stream generation it belongs to, and when it arrived.
struct RemoteCursorSample {
  int32_t x = 0;
  int32_t y = 0;
  uint32_t capW = 0;
  uint32_t capH = 0;
  uint64_t generation = 0;  // stream generation the sample belongs to
  bool visible = false;
  uint64_t updateUs = 0;
};

struct RemoteCursorState {
  // cross-thread: recv publishes a whole sample, the UI timer copies a whole sample. Seven separate
  // atomics let the reader see a position from one packet with the capture size / generation of
  // another -- the same-generation size-change case the ledger flagged -- so the sample is now
  // one value under one lock. (F-15.)
  std::mutex mu;
  RemoteCursorSample sample;
  void Publish(const RemoteCursorSample& s) {
    std::lock_guard<std::mutex> lk(mu);
    sample = s;
  }
  RemoteCursorSample Snapshot() {
    std::lock_guard<std::mutex> lk(mu);
    return sample;
  }
  // UI thread only.
  HWND overlayHwnd = nullptr;
};

}  // namespace remote60::native_poc::viewer
