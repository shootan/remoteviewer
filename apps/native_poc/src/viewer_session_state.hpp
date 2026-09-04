#pragma once

// Process-wide session state of the viewer (Phase 1-1 state struct).
//
// Role:    the run flag, the media socket, the main window handle and its requested size, whether
//          the input channel is on, which path (relay/direct) won, the shell-requested monitor, the
//          input-event counter, the stdout log mutex, and the toolbar refresh clock of the
//          message pump.
// Thread:  main creates and tears down; every thread reads `running`; `sock` is read by recv and
//          the control tunnel send; `inputEnabled` is set at connect / cleared at shutdown and read
//          by the UI input path; `logMu` serialises stdout from any thread.
// Input:   startup (args, connect results).
// Output:  read everywhere.
// Callers: main(), viewer_window_proc, viewer_input_forward, viewer_picker, viewer_log, recv/control threads.
//
// Fields are the former globals gRunning / gSock / gHwnd / gWindowW / gWindowH / gInputEnabled /
// gRelayPath / gRequestedMonitorId / gInputEventsSent / gLogMu and the message pump's
// `static nextToolbarPushUs`, initialisers unchanged (viewer split refactor Phase 1-1). The
// write-only gOverlayConfig snapshot that came with them is gone (F-03).

#include "viewer_common.hpp"
#include "viewer_constants.hpp"

namespace remote60::native_poc::viewer {

struct SessionState {
  // cross-thread: every thread polls `running`; recv reads `sock`; the control tunnel sends on it.
  std::atomic<bool> running{true};
  SOCKET sock = INVALID_SOCKET;
  // cross-thread: set once by create_window; recv/control thread call InvalidateRect/PostMessage on it.
  HWND hwnd = nullptr;
  uint32_t windowW = 1600;
  uint32_t windowH = 900;
  // cross-thread: main sets at connect / clears at shutdown; UI input path and toolbar read.
  std::atomic<bool> inputEnabled{false};
  // Host advertised sealed host-side IME support (kCaptureFlagHostImeV1). Host-IME key path is used
  // only when this is set AND the user opted in (env REMOTE60_HOST_IME=1). (Codex #366.)
  std::atomic<bool> hostImeSupported{false};
  std::atomic<bool> unlockRequested{false};  // set by the unlock trigger; the control thread runs it
  std::atomic<bool> unlockSupported{false};  // host advertised kCaptureFlagUnlockSealedV1 (Pong)
  // Which candidate won the race. The relay is billed per byte, so the session says which one it
  // is rather than leaving the user to guess from the bill.
  std::atomic<bool> relayPath{false};
  // Which screen the shell asked for. Applied once the host has said it understands the monitor
  // messages, which it does in the window list.
  uint32_t requestedMonitorId = 0;
  // Counted at the point the exchange succeeded, so it can be compared against the acks: the two
  // diverging is what tells "the host never answered" apart from "nothing was ever sent".
  std::atomic<uint64_t> inputEventsSent{0};
  std::mutex logMu;
  // UI thread only: the 500ms toolbar push clock of the message pump (was a function static).
  uint64_t nextToolbarPushUs = 0;  // main-loop only; 0 = push on the next tick (F-14 resets it per episode)
};

}  // namespace remote60::native_poc::viewer
