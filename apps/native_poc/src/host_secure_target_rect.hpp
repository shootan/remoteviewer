#pragma once

// Secure-desktop input dispatch: one rule, one path, for every event the host hands to the
// SYSTEM agent (secure branch, the cached-default re-probe, the default-desktop broker fallback,
// text). The rect the agent maps into is verified for THIS event against the capture target as it
// is right now, stamped into this message, and the message is written under one lock.
//
// Role:    pure decision over injected ports (host_input_target_rect_test drives it with fakes);
//          the production ports (host_loop_helpers.cpp secure_dispatch_event/_text) read the
//          capture's versioned snapshot, GetMonitorInfo, and the broker's write-with-rect.
// Thread:  control thread (per event).
//
// Contract (P9):
//   - A capture-target snapshot (window mode, HMONITOR, stream generation, version) is taken
//     under the capture's lock and is valid for this one event only; nothing is cached across
//     events. If the target changed while the monitor was being queried (version moved), the
//     event is not sent -- never with a rect of a target that is gone.
//   - The rect is the live physical rect of the captured monitor, read now. A query that fails,
//     an empty rect, window mode or an unknown monitor refuses the event: no previous rect, no
//     zero rect (which would make the agent aim at the whole virtual screen).
//   - The verified rect travels inside the message: the periodic sync on the main thread cannot
//     change what this event carries between the check and the write.

#include <cstdint>
#include <functional>

#include "host_input_target_rect.hpp"

namespace remote60::native_poc {

/** What the capture is targeting, read atomically as one record (CaptureState::InputTargetSnapshot). */
struct InputTargetSnapshotView {
  uint64_t version = 0;        // bumped whenever any field below changes
  bool windowMode = false;
  uint64_t monitorHandle = 0;  // HMONITOR the capture opened; 0 = unknown / window mode
  uint64_t streamGeneration = 0;
};

struct SecureDispatchPorts {
  std::function<InputTargetSnapshotView()> snapshot;
  std::function<bool(uint64_t monitorHandle, MonitorPhysicalRect* out)> queryMonitor;
  // Stamps `rect` into this message and writes it to the agent; false = the write failed.
  std::function<bool(const InputTargetRect& rect)> send;
};

struct SecureDispatchResult {
  bool sent = false;
  bool writeFailed = false;   // the broker write failed (vs. the event being refused)
  const char* why = "";
  InputTargetRect rect;
  uint64_t version = 0;
  uint64_t streamGeneration = 0;
};

inline SecureDispatchResult secure_dispatch(const SecureDispatchPorts& p) {
  SecureDispatchResult r;
  const InputTargetSnapshotView snap = p.snapshot();
  r.version = snap.version;
  r.streamGeneration = snap.streamGeneration;
  if (snap.windowMode) {
    r.why = "window-mode";
    return r;
  }
  if (snap.monitorHandle == 0) {
    r.why = "monitor-unknown";
    return r;
  }
  MonitorPhysicalRect live;
  if (!p.queryMonitor(snap.monitorHandle, &live)) {
    r.why = "monitor-query-failed";
    return r;
  }
  r.rect = derive_input_target_rect(false, live);
  if (r.rect.width == 0 || r.rect.height == 0) {
    r.why = "monitor-rect-invalid";
    return r;
  }
  // The target may have been replaced while the monitor was queried: this rect belongs to the
  // snapshot it was derived from, and only that.
  const InputTargetSnapshotView again = p.snapshot();
  if (again.version != snap.version) {
    r.why = "target-changed";
    return r;
  }
  r.sent = p.send(r.rect);
  r.writeFailed = !r.sent;
  r.why = r.sent ? "sent" : "broker-write-failed";
  return r;
}

}  // namespace remote60::native_poc
