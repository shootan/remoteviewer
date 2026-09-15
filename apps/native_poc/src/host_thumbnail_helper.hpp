#pragma once

// Capture one window preview in a process the host can end.
//
// Role:    run GNLinkCapture in --thumbnail mode for one window, wait with a deadline, and give
//          back either pixels or the reason there are none.
// Thread:  the control thread. One capture at a time -- the control dispatcher is one per process
//          and handles thumbnail requests synchronously, so there is never a second in flight.
// Input:   an HWND (nullptr = the whole desktop), a size cap, and a deadline.
// Output:  a ThumbnailOutcome and, when it is Ok, the pixels.
// Callers: host_control_session.cpp.
//
// Why a process rather than a thread. capture_window_thumbnail can block indefinitely, and on
// this OS it blocks inside DWM rather than inside the target application -- PW_RENDERFULLCONTENT
// is served from the redirection surface, measured, with the window never receiving WM_PRINT. A
// thread stuck there cannot be cancelled from user mode by any means. Ending a process can.
//
// On 2026-09-15 that block held the host's only control dispatcher for 1h50m and every session on
// the machine failed for the duration.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "host_thumbnail_budget.hpp"

namespace remote60::native_poc {

// How long the host is willing to hold its control thread for one preview.
//
// Measured on this machine, 12 captures per case, stable across four runs: a 1920x1080 window
// costs ~33ms median and 41.6ms at worst, and that worst case was a window presenting through a
// flip-model D3D11 swapchain -- the most expensive surface type tried. Smaller windows are ~16ms.
// A second is about twenty five times the worst sample.
//
// Deliberately not tight. Being late costs one preview; being early costs a preview that would
// have arrived, and on a machine under load a snug deadline would start skipping healthy windows.
// The budget is what keeps a genuinely stuck window from charging this every time.
//
// The isolated path costs about twice the capture, measured the same way: 72ms median and 107ms
// worst for the whole thing -- CreateProcessW, the mapping, the wait, the teardown -- against 34ms
// median for the same capture called directly. Roughly 39ms of that is the isolation itself. Still
// about a tenth of the deadline at its worst.
//
// Not measured, and worth stating rather than glossing: real application windows. UWP cannot be
// created by the test at all, and pointing the probe at the user's running windows is out of
// scope. A window backed by a large GPU surface could cost more than anything measured here --
// what was tried tops out at a 1920x1080 flip-model swapchain, which cost the same as plain GDI
// at that size.
constexpr uint64_t kThumbnailDeadlineUs = 1000ull * 1000;

struct ThumbnailCaptureResult {
  ThumbnailOutcome outcome = ThumbnailOutcome::Failed;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> bgra;
  uint64_t elapsedUs = 0;
  // Diagnostic only; never the basis of a decision. "no result" is the done event not being set.
  std::string detail;
};

/**
 * One preview, with a deadline.
 *
 * Never throws and never blocks past `deadlineUs` plus the time it takes to end a process that
 * missed it. A helper that cannot even be started is reported as Failed, which costs the window
 * a retry and nothing else.
 */
ThumbnailCaptureResult capture_thumbnail_isolated(HWND hwnd, uint32_t maxW, uint32_t maxH,
                                                  uint64_t deadlineUs,
                                                  HANDLE cancelEvent = nullptr);

}  // namespace remote60::native_poc
