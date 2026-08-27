#pragma once

// Remote-cursor overlay: a layered, click-through popup that follows the host cursor sample.
//
// Role:    ensure_cursor_overlay (create + rasterise the ring marker once), update_cursor_overlay
//          (WM_TIMER body: map the latest sample into the letterboxed video rect, show/hide/move).
// Thread:  UI only (owned window + timer); the sample itself is written by the recv thread.
// Input:   gRemoteCursor* sample, picker visibility, active stream generation.
// Output:  the overlay window position/visibility. Off unless REMOTE60_NATIVE_REMOTE_CURSOR is set.
// Callers: WndProc WM_TIMER (kCursorOverlayTimerId).
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-12).

#include "viewer_common.hpp"
#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

// Remote-cursor overlay: a small layered, click-through popup owned by the video window. GDI
// drawn over a flip-model swapchain does not compose reliably, so the cursor lives in its own
// window that just moves. Content is a blue ring with a center dot (a deliberately distinct
// marker -- a second arrow would ghost behind the local one by an RTT), rasterized once.
void ensure_cursor_overlay(HWND owner);

// Timer body: maps the latest remote-cursor sample (capture pixels) into the letterboxed video
// rect and moves the overlay; hides it when stale (>500ms), invisible, occluded by the picker,
// or when the window is minimized.
void update_cursor_overlay(HWND hwnd);

}  // namespace remote60::native_poc::viewer
