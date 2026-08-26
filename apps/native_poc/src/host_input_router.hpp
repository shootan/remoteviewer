#pragma once

// Viewer input routing state (InputRouterState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <atomic>
#include <climits>
#include <cstdint>

#include "host_input_inject.hpp"
#include "host_window_enum.hpp"
#include "secure_input_broker.hpp"

namespace remote60::native_poc {

// Viewer input routing (Phase 1-9 state struct): the configured injection mode, the SYSTEM
// input-broker client used when the secure desktop (UAC / lock screen) blocks direct injection,
// the client's input coordinate domain, the explicit --input-target criteria, the last-target /
// keyboard state, the per-cause failure counters the field logs are read by (see the comment
// blocks in main() that explain each counter), and the remote-cursor forwarder's last sent state.
// thread: control thread owns injection + counters (atomics: the stats tick on the main loop
// reads them); domainW/H are written by the main loop on geometry change and read by control;
// the cursor* fields are main-loop only (pump_cursor_forward).
struct InputRouterState {
  InputInjectionMode injectionMode = InputInjectionMode::Disabled;
  bool injectionEnabled = false;
  SecureInputBrokerClient broker;
  std::atomic<uint64_t> events{0};
  // Split of what happened while a security prompt or the lock screen was in front.
  std::atomic<uint64_t> secureAttempts{0};             // events seen while the secure desktop was up
  std::atomic<uint64_t> secureDelivered{0};            // handed to the SYSTEM agent
  std::atomic<uint64_t> secureBrokerFailed{0};         // agent unreachable; fell back, cannot land
  std::atomic<uint64_t> secureSkipWindowMode{0};       // window mode never routes to the agent
  std::atomic<uint64_t> secureSkipUnauthenticated{0};  // no directory capability to act on
  // cross-thread: the size the client's coordinates are expressed in (main writes, control reads).
  std::atomic<uint32_t> domainW{0};
  std::atomic<uint32_t> domainH{0};
  DesktopInputState desktopState;
  CaptureWindowCriteria targetCriteria{};
  // Outcome counters for the direct injection paths.
  std::atomic<uint64_t> ignoredMove{0};
  std::atomic<uint64_t> noTarget{0};
  std::atomic<uint64_t> unsupported{0};
  std::atomic<uint64_t> injectFail{0};
  std::atomic<uint64_t> freshProbeSecure{0};    // cached-default event failed, uncached re-probe saw secure
  std::atomic<uint64_t> freshProbeReroute{0};   // ...of those, the SYSTEM broker then landed the retry
  std::atomic<uint64_t> injectFailDefault{0};   // genuine failure on the interactive desktop
  std::atomic<uint64_t> failSetCursorPos{0};    // per-stage: which API the direct injection died in
  std::atomic<uint64_t> failSendInputMouse{0};
  std::atomic<uint64_t> failSendInputKey{0};
  std::atomic<uint64_t> failPostMessage{0};
  std::atomic<uint64_t> defaultBrokerFallback{0};  // default-desktop failures retried via the SYSTEM agent
  std::atomic<uint64_t> defaultBrokerQueued{0};    // ...of those, written to the agent's pipe (queued, NOT landed)
  std::atomic<uint64_t> defaultBrokerPipeFail{0};  // the pipe write itself failed
  // Remote cursor forwarder: last sent position/visibility (movement + 250ms heartbeat while visible).
  uint64_t cursorSendLastUs = 0;
  int32_t cursorSentX = INT32_MIN;
  int32_t cursorSentY = INT32_MIN;
  bool cursorSentVisible = false;
};

}  // namespace remote60::native_poc
