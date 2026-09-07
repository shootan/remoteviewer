#pragma once

// Where the captured pixels live on the physical desktop, for the SYSTEM input agent.
//
// Role:    the one rule that turns the current capture target into the rect the secure-desktop
//          agent maps client coordinates into (secure_input_mapping.hpp map_client_point). Pure;
//          no Win32, so it is unit-testable (host_input_target_rect_test).
// Thread:  main loop only (capture restart / startup), which is where the capture target changes.
// Callers: host_loop_helpers.cpp sync_input_target_rect (every capture (re)start), startup.
//
// Why a rule and not a one-off read: the host used to read the primary monitor ONCE at process
// start and hand it to the agent. A host that started under an RDP session saw the RDP virtual
// display (2236x1232), later ran on the 1920x1080 console -- the capture re-fit itself on every
// restart, the agent's rect never did, and every UAC click landed x*1.165 / y*1.141 away from
// where it was aimed (field 2026-09-07 14:27, P9). The rect must follow what is captured, each
// time that changes, and it must be the monitor the capture session actually opened -- not the
// selected id, not the whole virtual screen.

#include <cstdint>
#include <string>

namespace remote60::native_poc {

/** Physical-pixel rect of one monitor as GetMonitorInfo reports it (rcMonitor). */
struct MonitorPhysicalRect {
  int32_t originX = 0;
  int32_t originY = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool valid() const { return width > 0 && height > 0; }
};

struct InputTargetRect {
  int32_t originX = 0;
  int32_t originY = 0;
  uint32_t width = 0;   // 0x0 = "unknown here": the agent falls back to its own virtual screen
  uint32_t height = 0;
  const char* source = "none";  // for the log line: which rule produced it
};

inline bool input_target_rect_same(const InputTargetRect& a, const InputTargetRect& b) {
  return a.originX == b.originX && a.originY == b.originY && a.width == b.width && a.height == b.height;
}

/**
 * The agent's target rect for the current capture target.
 *
 * - Window mode: 0x0. The secure path never routes window-mode input to the agent (the agent
 *   would be aiming at a window that does not exist on the secure desktop), and a zero rect makes
 *   the agent fall back to the live virtual screen rather than a stale monitor.
 * - Desktop mode: the physical rect of the monitor the capture session opened (DXGI duplicates
 *   it, WGC's CreateForMonitor item wraps it, the GDI worker BitBlts it). Origin included, so a
 *   monitor left of the primary (negative origin) maps where it is.
 * - Desktop mode with no monitor known (a query failed): 0x0, same fallback, logged by the caller.
 *
 * DPI: both sides speak physical pixels (the agent is per-monitor-v2 aware), so no scaling here.
 */
inline InputTargetRect derive_input_target_rect(bool windowMode, const MonitorPhysicalRect& monitor) {
  InputTargetRect rect;
  if (windowMode) {
    rect.source = "window-mode";
    return rect;
  }
  if (!monitor.valid()) {
    rect.source = "monitor-unknown";
    return rect;
  }
  rect.originX = monitor.originX;
  rect.originY = monitor.originY;
  rect.width = monitor.width;
  rect.height = monitor.height;
  rect.source = "capture-monitor";
  return rect;
}

inline std::string describe_input_target_rect(const InputTargetRect& r) {
  return "(" + std::to_string(r.originX) + "," + std::to_string(r.originY) + ")/" + std::to_string(r.width) +
         "x" + std::to_string(r.height) + " source=" + r.source;
}

}  // namespace remote60::native_poc
