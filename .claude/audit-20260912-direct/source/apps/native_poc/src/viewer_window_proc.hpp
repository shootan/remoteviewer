#pragma once

// The viewer window: class registration, creation, and its window procedure.
//
// Role:    WndProc (every WM_* the session window handles: mouse/touch/keyboard/IME forwarding,
//          picker gestures, the reveal message, WM_PAINT present path, timers) and create_window.
// Thread:  UI only.
// Input:   window messages.
// Output:  input queued for the control thread, picker/selection state, presented frames.
// Callers: main() (create_window), the message pump.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-13). Still 870
// lines: Phase 2-8 moves the WM_PAINT body to viewer_present.cpp and 2-9 splits the handlers.

#include "viewer_common.hpp"
#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {

// Finds its ViewerState through GWLP_USERDATA, stored at WM_NCCREATE from the lpParam that
// create_window passes (viewer ledger F-17). Messages that arrive before that -- or to a window
// created without one -- take the default path, as they did before.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

// UNICODE is not defined for this target, so the generic Win32 names resolve to the ANSI
// entry points. This window is registered and created wide, so every message API it touches
// must be the explicit *W form -- DefWindowProcA on a Unicode window read the wide title as
// ANSI and truncated it to "r", and delivered WM_CHAR as ANSI.
bool create_window(ViewerState& ctx);

}  // namespace remote60::native_poc::viewer
