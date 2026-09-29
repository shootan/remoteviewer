#pragma once

// Clipboard image v1: the transfer bar (3rd rate agreement ④, DECISIONS §5).
//
// Role:    While an image copied on this PC is on its way to the remote PC, a small bar at the
//          bottom centre of the viewer says so -- percent, size, seconds -- with a Cancel button,
//          and once it ends, one line saying how it ended -- including a copy that never left
//          (too large, unreadable) and one the host would not take. On a lossy long path 5 MiB
//          takes about 30 s (measured, 40 ms / 1 %), so the user must not be told it is instant:
//          past 10 s the line says it is taking time (not why -- nothing here measures why).
//          Cancel stops sending at once, but the line says "cancelling" until the host has said
//          what happened: it may already have published the image.
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
#include "file_copy_client.hpp"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace remote60::native_poc {

enum class ClipBarPhase : uint8_t { Hidden = 0, Sending, Cancelling, Result };

/** What the bar shows. Pure data, so the text can be asserted without a window. */
struct ClipBarView {
  ClipBarPhase phase = ClipBarPhase::Hidden;
  uint64_t bytesDone = 0;
  uint64_t bytesTotal = 0;
  uint64_t elapsedMs = 0;
  uint8_t cancellingWhy = 0;                // ClipImageReason (Cancelling only)
  ClipOutcome outcome = ClipOutcome::None;  // Result only
  uint8_t detail = 0;                       // Result only (see ClipOutcome)
  // File copy (t-zdmsd4gb D6): when set, the bar is showing a file paste and draws `fileText`; Cancel is
  // offered while the paste runs and no cancel is pending.
  bool isFile = false;
  bool fileCancel = false;
  bool imageForFile = false;  // D5: the image is being stopped because files are being pasted
  std::wstring fileText;
};

/** The one line the bar draws. */
std::wstring clip_transfer_bar_text(const ClipBarView& v);

/** Whether the Cancel button is offered (only while sending). */
inline bool clip_transfer_bar_has_cancel(const ClipBarView& v) {
  return v.isFile ? v.fileCancel : v.phase == ClipBarPhase::Sending;
}

/**
 * File copy (D6): the bar's view of the file state, Hidden when there is nothing to say. What is known
 * is said apart: an offer published (a paste is POSSIBLE -- 5 s), bytes moving either way, a cancel
 * asked and not confirmed, and how the last paste ended (5 s) -- "완료" only when the consumer ended it
 * successfully, a failure after bytes moved says a partial file may be left. `state` keeps what was
 * already shown (like seenFinished for images).
 */
struct FileBarState {
  bool primed = false;
  uint64_t seenFinished = 0, seenOffered = 0, seenAvailable = 0, seenNoHelper = 0;
  uint64_t resultUntilUs = 0, offeredUntilUs = 0, availableUntilUs = 0, noHelperUntilUs = 0;
};
ClipBarView file_transfer_bar_view(const FileCopyClient::Progress& p, uint64_t nowUs, FileBarState* state);

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
  // File copy (D6). While the file view has something to say it takes the bar; Cancel then cancels the paste.
  std::function<FileCopyClient::Progress()> fileProgress;
  std::function<void()> onFileCancel;
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
