#pragma once

// The UI-thread session watchdog.
//
// Role:    once a second (from the 50 ms overlay timer), sample the recv thread's liveness
//          heartbeat and the control channel, log a stalled recv thread or a silent link with the
//          ages that tell them apart, and on a provably dead session (control gone for good, video
//          not progressing) set the panel status and -- unless REMOTE60_NATIVE_DEAD_SESSION_EXIT=0
//          -- end the session so the shell returns to the host list instead of leaving the last
//          picture frozen. (history #390 item 5; verdict logic in viewer_recv_liveness.hpp.)
// Thread:  UI only (WM_TIMER); reads relaxed atomics the recv thread writes.
// Input:   ctx.recvLive, ctx.control.connected / udpControl, the clock.
// Output:  [liveness] log lines, panel status, WM_CLOSE on a dead session.
// Callers: WndProc (kCursorOverlayTimerId).

#include "viewer_common.hpp"
#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {

void poll_session_liveness(ViewerState& ctx, HWND hwnd);

}  // namespace remote60::native_poc::viewer
