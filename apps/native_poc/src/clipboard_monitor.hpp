#pragma once

// Clipboard text sync (K1): the host's clipboard monitor and the hub the control sessions read.
//
// Role:    ClipboardMonitor owns a message-only window on its own thread so it can hear
//          WM_CLIPBOARDUPDATE -- the host process (GNLinkStream) has no message pump of its own, so
//          it cannot use AddClipboardFormatListener on any window it already has. HostClipboardHub
//          wraps the monitor with the generation/echo bookkeeping the control protocol needs: it
//          publishes the host's own clipboard changes as new generations, and applies a client's
//          clipboard to the host without letting that application bounce back out as a new change.
// Thread:  the monitor runs its own thread. HostClipboardHub is called from the control threads
//          (the TCP dispatcher and the UDP dispatcher, concurrently) and guards its state with a
//          mutex. The monitor's OnText callback runs on the monitor thread.
// Callers: native_video_host_main.cpp (owns the hub, starts/stops it), host_control_session.cpp
//          (Get for a poll reply, ApplyRemote for a client update).
//
// This header is compiled into the host only. The viewer reaches the same clipboard through
// AddClipboardFormatListener on the window it already pumps, so it needs no thread of its own.

#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "clip_image_clipboard.hpp"
#include "clipboard_sync.hpp"

namespace remote60::native_poc {

// A message-only window on a dedicated thread that reports clipboard text changes and can set the
// clipboard on request. All clipboard access happens on the monitor thread, which is the thread
// that owns the listener window.
class ClipboardMonitor {
 public:
  using OnTextFn = std::function<void(const std::u16string&)>;
  // File copy (t-zdmsd4gb): every clipboard change, with the sequence number and what CF_HDROP names
  // (empty = no files). Path strings only; nothing is opened.
  using OnFilesFn = std::function<void(uint64_t seq, std::vector<std::wstring> paths)>;

  ~ClipboardMonitor() { Stop(); }

  // Starts the thread and waits until the listener window is up. Returns false if it could not be
  // created (in which case the host simply has no clipboard sync and says so).
  bool Start(OnTextFn onText);
  void Stop();

  // Sets the clipboard to `text`. Safe to call from any thread; the work is marshalled to the
  // monitor thread. Returns false if the monitor is not running.
  bool SetText(const std::u16string& text);

  // Runs `fn` on the monitor thread (with the listener window as the clipboard owner) and waits up
  // to `timeoutMs` for it. False if the monitor is not running or the wait timed out -- in which
  // case `fn` may still run later, so it must own everything it touches.
  bool Invoke(std::function<void(HWND)> fn, DWORD timeoutMs);

  bool running() const { return running_.load(std::memory_order_acquire); }
  // Any time: from now on every change is also reported to `fn`, and the current content is reported
  // once right away (on the monitor thread). Null stops it.
  void SetOnFiles(OnFilesFn fn);

 private:
  void ReportFiles(HWND hwnd);
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  void ThreadMain();

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> ready_{false};
  std::atomic<HWND> hwnd_{nullptr};
  DWORD threadId_ = 0;
  OnTextFn onText_;
  std::mutex filesMu_;
  OnFilesFn onFiles_;
};

// The host's clipboard, seen by the control protocol. Generation rises only on genuine local
// changes; applying a client's clipboard does not raise it (so it is not served straight back to
// the client that sent it, and the OS change notification the write provokes is suppressed as an
// echo). Nothing is seeded at startup: a viewer receives the host's clipboard only after the host
// changes it while connected, so connecting never silently overwrites the viewer's clipboard.
class HostClipboardHub {
 public:
  ~HostClipboardHub() { Stop(); }

  bool Start();
  void Stop();
  bool enabled() const { return started_.load(std::memory_order_acquire); }

  struct Snapshot {
    uint64_t generation = 0;
    uint64_t hash = 0;
    std::u16string text;
  };
  Snapshot Get();

  // Put a client's clipboard text on the host clipboard. Does nothing for empty text or content
  // already present. Records the content as applied first, so the resulting change notification is
  // recognised as an echo and does not raise the generation.
  void ApplyRemote(const std::u16string& text, uint64_t hash);

  // Clipboard image v1: publish PNG + CF_DIBV5 (+ same-copy text) on the monitor thread, with the
  // clipboard held and only if its sequence number is still `expectSequence` (plan r2 8-4). Takes
  // ownership of both HGLOBALs. On Published the text is recorded as applied before the change
  // notification the write provokes is processed (same thread, queued behind this), so it is not
  // served back to the viewer that sent it.
  ClipPublishResult PublishImage(uint64_t expectSequence, HGLOBAL pngGlobal, HGLOBAL dibv5,
                                 const std::u16string& text);
  // File copy: where this PC's copies of files go (HostFileCopyService::OnHostClipboard).
  void SetFileListener(ClipboardMonitor::OnFilesFn fn) { monitor_.SetOnFiles(std::move(fn)); }

 private:
  void OnLocalText(const std::u16string& text);

  ClipboardMonitor monitor_;
  std::mutex mu_;
  ClipboardSyncCore core_;
  uint64_t generation_ = 0;
  uint64_t hash_ = 0;
  std::u16string text_;
  std::atomic<bool> started_{false};
};

}  // namespace remote60::native_poc
