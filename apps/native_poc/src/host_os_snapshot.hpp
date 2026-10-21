#pragma once

// dxgi-wedge-fix r1 (Codex B8, Bⓒ): what was on screen when the DXGI worker stalled.
//
// The field logs could not say whether a game, a fullscreen switch or a display-mode change was involved:
// the host records none of that. At the watchdog's 3 s "slow" mark one line is written:
//   [native-video-host][os-snapshot] at=<why> phase=<p> phaseAgeMs=<n> fgPid=<pid> fgExe=<file name>
//     fgRect=<x,y,w,h> monitor=<device> monRect=<x,y,w,h> mode=<w>x<h>@<hz>Hz/<bpp>bpp coversMonitor=<0|1>
//     collectMs=<n>
// coversMonitor is a rectangle comparison, NOT a claim of exclusive fullscreen. Anything that cannot be
// read is "unknown". Never collected: window titles, document names, keystrokes, the environment.
//
// The watchdog never waits for it (Codex B8): RequestOsSnapshot only claims a one-at-a-time slot and
// starts a detached thread that does the user32/process calls and the write; a wedged GPU, a stuck
// stdout pipe or a slow call there cannot delay the 5 s terminate. At most one collection runs at a time.

#include <functional>
#include <string>

namespace remote60::native_poc {

// Collect now, on the calling thread (the collector's). The line, with a trailing newline.
std::string collect_os_snapshot_line(const std::string& at, const std::string& phase, unsigned long long phaseAgeMs);
// Non-blocking: start a collection unless one is already running. False when one was running.
bool RequestOsSnapshot(const char* at, const char* phase, unsigned long long phaseAgeMs);
// TEST SEAM (null in the product): runs on the collector thread before collecting -- lets a test stall it.
std::function<void()>& OsSnapshotStallForTest();

}  // namespace remote60::native_poc
