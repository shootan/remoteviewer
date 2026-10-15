#pragma once

#ifdef REMOTE60_PINNED_SELECT_TEST_SEAM
#include <windows.h>

namespace remote60::native_poc {

// TEST BUILD ONLY (GNLinkStreamPinnedSelectTest, keepalive r3 R3): the capture boundary of the pinned test
// host. Defined in host_control_session.cpp, next to the start admission.
//
// The pinned window is found once, at start (pinned_select_seam_admit_start): the exact HWND of the pinned
// process whose title holds the pinned title. Afterwards a target is the pinned one only while that same
// HWND is a window, still belongs to the pinned process (whose handle this host holds, so the pid cannot
// be reused) and still carries the title.
bool pinned_select_seam_target_ok(HWND hwnd);

// The pinned window found at start (null before / without it).
HWND pinned_select_seam_pinned_hwnd();

// Every capture start in this build passes here first: the startup item, each restart (WGC, DXGI, GDI),
// each item a selection or a rebind makes. `window` is the window to be captured; null means a screen.
// Anything but the pinned window ends this host at once (exit 65) -- never a fallback to a screen.
// With REMOTE60_PINNED_TEST_CAPTURE_PROBE=1 the first call reports what it would capture and ends the host
// (66 when admitted) before any capture object exists: the boundary is checked without a picture.
void pinned_select_seam_capture_gate(HWND window, const char* where);

// Ends this host now (exit 65), with the reason on stderr. Used when the pinned target cannot be held.
[[noreturn]] void pinned_select_seam_refuse(const char* where);

// REMOTE60_PINNED_TEST_CAPTURE_PROBE=1 with REMOTE60_PINNED_TEST_PROBE_UNGATED=1: the negative control.
// The admission's window check, the startup refusal and the gate's verdict are skipped (the 2abb8be
// behaviour); the probe still ends the host at the first capture call, so this shows where the old path
// would have gone -- the screen -- without ever capturing it. Never honoured without the probe.
bool pinned_select_seam_probe_ungated();

}  // namespace remote60::native_poc
#endif
