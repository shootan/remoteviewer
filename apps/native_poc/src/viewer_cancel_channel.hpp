#pragma once

// How the shell calls off a connect that is still running in an older viewer.
//
// Role:    one named event per viewer process. The shell signals it when the user picks another
//          PC; the viewer's watcher sets connectCancelled, which Observe, PunchAny and the hello
//          handshake all read as their stop.
// Thread:  the watcher is its own thread, started before connect and joined after.
// Input:   the event, signalled by another process.
// Output:  ViewerContext::connectCancelled / connectCancelledByOwner.
// Callers: viewer_startup.cpp (start/stop), client_shell_main.cpp (signal).
//
// Why an event and not a window message. During connect the viewer has a window but NO message
// pump -- run_message_pump does not start until the session is up -- so a WM_CLOSE would sit in
// the queue for exactly as long as the thing it was meant to interrupt. The event is read by a
// thread that is running, which is the whole requirement.
//
// The name carries the process id, so it is per viewer rather than per machine: two viewers
// connecting to two different PCs are an ordinary thing and cancelling both would be wrong.
//
// Local\ rather than Global\: same session, same user, no privilege needed, and nothing outside
// this desktop session can reach it.

#include <windows.h>

#include <atomic>
#include <string>
#include <thread>

namespace remote60::native_poc::viewer {

/** `Local\GNLinkViewerCancel-<pid>`. Both sides derive it the same way; neither stores it. */
inline std::wstring viewer_cancel_event_name(DWORD processId) {
  return L"Local\\GNLinkViewerCancel-" + std::to_wstring(processId);
}

/**
 * Opens (or creates) this process's cancel event and watches it.
 *
 * Manual reset: once cancelled, cancelled. The watcher exits when the event is signalled or when
 * `stopFlag` is set and the wait times out, so the ordinary path costs one 200ms wakeup.
 *
 * Returns the event handle, which the caller closes after joining. On failure the handle is null
 * and nothing is watched -- a viewer that cannot create its event still works, it just cannot be
 * called off, and that is the behaviour it had before this existed.
 */
inline HANDLE viewer_start_cancel_watcher(std::atomic<bool>* cancelled,
                                          std::atomic<bool>* cancelledByOwner,
                                          std::atomic<bool>* stopFlag, std::thread* thread) {
  if (!cancelled || !stopFlag || !thread) return nullptr;
  const std::wstring name = viewer_cancel_event_name(GetCurrentProcessId());
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
  if (!event) return nullptr;
  // An existing object under this name is somebody else's leftover -- a process id that has
  // come round again. Its state is not this viewer's business, so it starts from not-cancelled.
  // CreateEventW does not reset an object it merely opened, which is the whole hazard.
  if (GetLastError() == ERROR_ALREADY_EXISTS) ResetEvent(event);
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
  return event;
}

/** Stops the watcher and closes the event. Safe with a null handle or an unstarted thread. */
inline void viewer_stop_cancel_watcher(HANDLE event, std::atomic<bool>* stopFlag,
                                       std::thread* thread) {
  if (stopFlag) stopFlag->store(true, std::memory_order_release);
  // NOT signalled to wake it. The first version did, to save the last 200ms slice, and left a
  // SIGNALLED event behind under a name derived from the process id -- so the next viewer that
  // opened that name cancelled itself the moment it started. Windows reuses process ids, and
  // CreateEventW on an existing name opens the existing object rather than making a fresh one.
  // It cost three identical screenshots of a failure screen before the cause was read out of
  // the log. The watcher checks stopFlag every slice, so stopping costs 200ms and no more.
  if (thread && thread->joinable()) thread->join();
  if (event) CloseHandle(event);
}

/**
 * Signals another viewer's cancel event. Used by the shell.
 *
 * False means there was nothing to signal -- the process is gone, or never created its event,
 * which is what an older build does. The caller carries on either way: this is an improvement on
 * the old behaviour, not a precondition for the new session.
 */
inline bool viewer_request_cancel(DWORD processId) {
  const std::wstring name = viewer_cancel_event_name(processId);
  HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
  if (!event) return false;
  const BOOL ok = SetEvent(event);
  CloseHandle(event);
  return ok != FALSE;
}

}  // namespace remote60::native_poc::viewer
