#pragma once

// Bounded thread join.
//
// Role:    join a thread, but give up after a timeout. The viewer's shutdown used to join the recv
//          thread unconditionally; a recv thread wedged inside a decoder / D3D call never returns,
//          and blocking the UI thread on it turned a frozen picture into a hung process the shell
//          could not restart (Codex condition 4 on history #390 item 5). The caller decides what
//          to do with a thread that did not come back -- shutdown_viewer logs the stage it is
//          stuck in and ends the process, which is a last resort, not a recovery.
// Thread:  the joining thread (UI / main).
// Input:   the thread, a timeout.
// Output:  true = joined; false = still running (left joinable; the caller detaches or exits).
// Callers: shutdown_viewer; viewer_liveness_test.

#include <windows.h>

#include <cstdint>
#include <thread>

namespace remote60::native_poc::viewer {

inline bool join_with_timeout(std::thread& t, uint32_t timeoutMs) {
  if (!t.joinable()) return true;
  const HANDLE h = static_cast<HANDLE>(t.native_handle());
  const DWORD waited = WaitForSingleObject(h, timeoutMs);
  if (waited != WAIT_OBJECT_0) return false;
  t.join();
  return true;
}

}  // namespace remote60::native_poc::viewer
