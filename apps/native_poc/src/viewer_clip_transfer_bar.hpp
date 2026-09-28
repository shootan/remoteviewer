#pragma once

// Clipboard image v1: the transfer bar (3rd rate agreement ④, DECISIONS §5).
//
// Role:    While an image copied on this PC is on its way to the remote PC, a small bar at the
//          bottom centre of the viewer says so -- percent, size, seconds -- with a Cancel button,
//          and once it ends, one line saying how it ended. On a lossy long path 5 MiB takes about
//          30 s (measured, 40 ms / 1 %), so the user must not be told it is instant: past 10 s the
//          line also says the network is why.
// Why a window of its own: the video is a flip-model swapchain, which composites over anything
//          GDI draws into the viewer's client area (see compute_client_layout, the session
//          toolbar). The session toolbar hides itself unless summoned, so the status cannot live
//          there either.
// Thread:  the viewer's UI thread (create, follow, destroy, the window procedure). The progress
//          provider is called on that thread from a 250 ms timer; ClipImageClient::GetProgress is
//          safe there.
// Callers: viewer_clip_image_wiring.cpp (product and viewer_clip_bar_e2e_test), clip_image_core
//          tests for the text.

// clip_image_client.hpp first: it brings winsock2.h, which must precede windows.h.
#include "clip_image_client.hpp"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace remote60::native_poc {

enum class ClipBarPhase : uint8_t { Hidden = 0, Sending, Result };

/** What the bar shows. Pure data, so the text can be asserted without a window. */
struct ClipBarView {
  ClipBarPhase phase = ClipBarPhase::Hidden;
  uint64_t bytesDone = 0;
  uint64_t bytesTotal = 0;
  uint64_t elapsedMs = 0;
  uint8_t state = 0;   // ClipImageState of the result (Result only)
  uint8_t reason = 0;  // ClipImageReason of the result (Result only)
};

/** The one line the bar draws. */
std::wstring clip_transfer_bar_text(const ClipBarView& v);

/** Whether the Cancel button is offered (only while sending). */
inline bool clip_transfer_bar_has_cancel(const ClipBarView& v) { return v.phase == ClipBarPhase::Sending; }

/**
 * The view for a progress snapshot. `seenFinished` is the finished-count the bar last showed a
 * result for, `resultUntilUs` when the current result line stops being shown; both are updated.
 * A result is shown for kClipBarResultUs after the transfer ends, then the bar hides.
 */
ClipBarView clip_transfer_bar_view(const ClipImageClient::Progress& p, uint64_t nowUs, uint64_t* seenFinished,
                                   uint64_t* resultUntilUs);
constexpr uint64_t kClipBarResultUs = 5000000;

struct ClipTransferBarHooks {
  std::function<ClipImageClient::Progress()> progress;
  std::function<void()> onCancel;
  std::function<void(const std::string&)> onLog;
};

/** Creates the bar for `owner` (hidden until a transfer starts). Later calls are ignored. */
bool clip_transfer_bar_create(HWND owner, ClipTransferBarHooks hooks);
/** The bar is a window of its own: it follows the owner's moves and sizes. */
void clip_transfer_bar_follow_owner();
void clip_transfer_bar_destroy();

/** For tests: the bar's window and its Cancel button (client coordinates; empty when not offered). */
HWND clip_transfer_bar_window();
RECT clip_transfer_bar_cancel_rect();
/** For tests: what the bar is showing now. */
ClipBarView clip_transfer_bar_current();

}  // namespace remote60::native_poc
