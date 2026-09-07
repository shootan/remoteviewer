#pragma once

// The decision taken right before a secure-desktop input event is handed to the SYSTEM agent:
// is the agent's target rect the monitor the capture is on RIGHT NOW, and may the event go?
//
// Role:    pure rule over (window mode, a live monitor query, the rect the broker currently
//          holds). Testable without Win32 (host_input_target_rect_test). The control thread's
//          secure_target_rect_ready() (host_loop_helpers.cpp) feeds it the live GetMonitorInfo
//          result and applies the verdict: update the broker, or refuse to send.
// Thread:  control thread (per secure event); the broker's own mutex serialises the rect.
//
// Why per event: the rect is re-derived on capture restarts and once a second (P9), but an
// origin-only display change that happens right after a tick would still reach the first click
// before the next tick. Reading the captured monitor's live rect at dispatch closes that window.
// Why refuse instead of fall back: a query that fails means the host does not know where the
// captured pixels are; sending anyway with the previous rect, or with a zero rect that makes the
// agent aim at the whole virtual screen, is a click at an unknown place. The event is dropped,
// counted and logged; the next event queries again.

#include <cstdint>

#include "host_input_target_rect.hpp"

namespace remote60::native_poc {

struct SecureRectDecision {
  bool send = false;      // hand the event to the agent
  bool update = false;    // the broker's rect must be replaced by `rect` first
  InputTargetRect rect;   // the rect to use (valid only when send)
  const char* why = "";   // for the log / counters
};

inline SecureRectDecision decide_secure_target_rect(bool windowMode, bool queryOk, const MonitorPhysicalRect& live,
                                                    const InputTargetRect& current) {
  SecureRectDecision d;
  if (windowMode) {
    d.why = "window-mode";  // the secure path never routes window-mode input
    return d;
  }
  if (!queryOk || !live.valid()) {
    d.why = queryOk ? "monitor-rect-invalid" : "monitor-query-failed";
    return d;
  }
  d.rect = derive_input_target_rect(false, live);
  d.send = true;
  d.update = !input_target_rect_same(current, d.rect) || current.width == 0 || current.height == 0;
  d.why = d.update ? "rect-updated" : "rect-current";
  return d;
}

}  // namespace remote60::native_poc
