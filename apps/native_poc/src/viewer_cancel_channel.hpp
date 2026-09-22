#pragma once

// How the shell calls off a connect that is still running in an older viewer.
//
// Role:    an UNNAMED event, created by the shell and inherited by the viewer it launches. The
//          shell signals it when the user picks the same PC again; the viewer's watcher sets
//          connectCancelled, which Observe, PunchAny and the hello handshake all read as stop.
// Thread:  the watcher is its own thread, started before connect and joined after.
// Input:   the inherited handle, named on the command line by value.
// Output:  ViewerContext::connectCancelled / connectCancelledByOwner.
// Callers: viewer_startup.cpp / native_video_client_main.cpp (wait), client_shell_main.cpp
//          (create, pass, signal).
//
// Why an event and not a window message. During connect the viewer has a window but NO message
// pump -- run_message_pump does not start until the session is up -- so a WM_CLOSE would sit in
// the queue for exactly as long as the thing it was meant to interrupt. The event is read by a
// thread that is running, which is the whole requirement.
//
// Why UNNAMED. The first version used `Local\GNLinkViewerCancel-<pid>` with default security,
// and that is a name any process in the session can open with EVENT_MODIFY_STATE knowing only a
// pid -- so any of them could cancel a user's connection, over and over. Putting a secret in the
// name does not help either: a viewer's command line is readable by the same processes. An
// unnamed object has no name to open. The only way to it is a handle, and the only process given
// one is the child the shell launched.
//
// The handle travels as a number on the command line. That is not a secret and does not need to
// be: a handle value means nothing in another process, and duplicating it out of this one
// already requires the right to open the process.

#include <windows.h>

#include <atomic>
#include <string>
#include <thread>

namespace remote60::native_poc::viewer {

/**
 * Creates the event the shell keeps and the viewer waits on.
 *
 * Manual reset, unsignalled, inheritable, and unnamed. The caller owns the handle: it signals it
 * to cancel that viewer, and closes it when the viewer is gone.
 */
inline HANDLE viewer_create_cancel_event() {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;   // the child is given this one handle, by the whitelist at launch
  sa.lpSecurityDescriptor = nullptr;
  return CreateEventW(&sa, TRUE, FALSE, nullptr);
}

/** The handle value as it is written on the child's command line, and read back off it. */
inline std::wstring viewer_cancel_handle_arg(HANDLE event) {
  return std::to_wstring(reinterpret_cast<unsigned long long>(event));
}

inline HANDLE viewer_cancel_handle_from_arg(const std::wstring& text) {
  if (text.empty()) return nullptr;
  wchar_t* end = nullptr;
  const unsigned long long value = std::wcstoull(text.c_str(), &end, 10);
  if (!end || *end != L'\0' || value == 0) return nullptr;
  return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
}

/**
 * Watches the inherited event.
 *
 * `event` is the handle the shell passed; this does NOT create or open anything. A viewer started
 * by hand has no such handle and simply cannot be cancelled, which is the behaviour it had before
 * any of this existed.
 *
 * Returns true when a watcher was started.
 */
inline bool viewer_start_cancel_watcher(HANDLE event, std::atomic<bool>* cancelled,
                                        std::atomic<bool>* cancelledByOwner,
                                        std::atomic<bool>* stopFlag, std::thread* thread) {
  if (!event || !cancelled || !stopFlag || !thread) return false;
  *thread = std::thread([event, cancelled, cancelledByOwner, stopFlag]() {
    while (!stopFlag->load(std::memory_order_acquire)) {
      const DWORD waited = WaitForSingleObject(event, 200);
      if (waited == WAIT_OBJECT_0) {
        if (cancelledByOwner) cancelledByOwner->store(true, std::memory_order_release);
        cancelled->store(true, std::memory_order_release);
        return;
      }
      if (waited != WAIT_TIMEOUT) return;  // the handle went bad; nothing to watch any more
    }
  });
  return true;
}

/**
 * Stops the watcher. The event is NOT signalled to wake it.
 *
 * An earlier version did signal, to save the last 200ms slice. With a named event that left a
 * SIGNALLED object behind under a pid-derived name, and the next viewer to take that pid
 * cancelled itself on startup. The name is gone now, but the habit is still wrong: signalling
 * something to mean "stop watching" makes "cancelled" and "finished" the same event. The watcher
 * checks stopFlag every slice, so stopping costs 200ms and no more.
 */
inline void viewer_stop_cancel_watcher(std::atomic<bool>* stopFlag, std::thread* thread) {
  if (stopFlag) stopFlag->store(true, std::memory_order_release);
  if (thread && thread->joinable()) thread->join();
}

/** Signals a viewer's cancel event. The shell holds the handle; nobody else can obtain one. */
inline bool viewer_request_cancel(HANDLE event) {
  if (!event) return false;
  return SetEvent(event) != FALSE;
}

}  // namespace remote60::native_poc::viewer
