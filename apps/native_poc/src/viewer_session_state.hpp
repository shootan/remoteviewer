#pragma once

// Process-wide session state of the viewer (Phase 1-1 state struct).
//
// Role:    the run flag, the media socket, the main window handle and its requested size, whether
//          the input channel is on, which path (relay/direct) won, the shell-requested monitor, the
//          input-event counter, the overlay configuration snapshot, the stdout log mutex, and the
//          toolbar refresh clock of the message pump.
// Thread:  main creates and tears down; every thread reads `running`; `sock` is read by recv and
//          the control tunnel send; `inputEnabled` is set at connect / cleared at shutdown and read
//          by the UI input path; `logMu` serialises stdout from any thread.
// Input:   startup (args, connect results).
// Output:  read everywhere.
// Callers: main(), viewer_window_proc, viewer_input_forward, viewer_picker, viewer_log, recv/control threads.
//
// Fields are the former globals gRunning / gSock / gHwnd / gWindowW / gWindowH / gInputEnabled /
// gRelayPath / gRequestedMonitorId / gInputEventsSent / gOverlayConfig / gLogMu and the message
// pump's `static nextToolbarPushUs`, initialisers unchanged (viewer split refactor Phase 1-1).

#include "viewer_common.hpp"
#include "viewer_constants.hpp"

namespace remote60::native_poc::viewer {

struct OverlayConfigSnapshot {
  std::string host = "127.0.0.1";
  uint16_t port = 43000;
  uint16_t controlPort = 0;
  std::string transport = "tcp";
  std::string codec = "raw";
  uint32_t fpsHint = 30;
  uint32_t controlIntervalMs = 1000;
  uint32_t tcpRecvBufKb = 0;
  uint32_t tcpSendBufKb = 0;
  uint32_t udpMtu = 1200;
  uint32_t udpSimDropPm = 0;
  uint64_t keyReqMinIntervalUs = kKeyframeRequestMinIntervalUsDefault;
  uint64_t keyReqTokenRefillUs = kKeyframeRequestTokenRefillUsDefault;
  uint32_t keyReqTokenCapacity = kKeyframeRequestTokenCapacityDefault;
};

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
  // Which candidate won the race. The relay is billed per byte, so the session says which one it
  // is rather than leaving the user to guess from the bill.
  std::atomic<bool> relayPath{false};
  // Which screen the shell asked for. Applied once the host has said it understands the monitor
  // messages, which it does in the window list.
  uint32_t requestedMonitorId = 0;
  // Counted at the point the exchange succeeded, so it can be compared against the acks: the two
  // diverging is what tells "the host never answered" apart from "nothing was ever sent".
  std::atomic<uint64_t> inputEventsSent{0};
  OverlayConfigSnapshot overlayConfig;  // dead: F-03 (write-only since the stats overlay went)
  std::mutex logMu;
  // UI thread only: the 500ms toolbar push clock of the message pump (was a function static).
  uint64_t nextToolbarPushUs = 0;
};

}  // namespace remote60::native_poc::viewer
