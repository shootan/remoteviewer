#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace remote60::native_poc {

// t-970r4zgo: the wire target id of a monitor selected through the window-select transaction
// (kControlWindowListFlagMonitorSelectTransactionV1). It names a REQUEST, never a capture state:
// the host keeps "desktop mode + the picked screen" internally and echoes this id in its
// ControlWindowSelected; it is never cast to an HWND or stored as a selected window.
//
// A window id is an HWND widened to 64 bits, and Windows guarantees an HWND is 32 significant
// bits, zero- or sign-extended -- so its top 32 bits are 0x00000000 or 0xFFFFFFFF, never
// 0x70000000. The whole 0x7 top nibble is reserved for this namespace: an id there with any
// other upper bit set is malformed and refused, not looked up as a window. monitorId 0 is valid
// (the first listed screen) and distinct from windowId 0 (desktop mode).
constexpr uint64_t kMonitorSelectTargetBase = 0x7000000000000000ULL;
constexpr uint64_t kMonitorSelectTargetMask = 0xFFFFFFFF00000000ULL;

enum class SelectTargetKind { Window, Monitor, Malformed };

struct SelectTarget {
  SelectTargetKind kind = SelectTargetKind::Window;
  uint32_t monitorId = 0;  // meaningful only for Monitor
};

inline uint64_t encode_monitor_select_target(uint32_t monitorId) {
  return kMonitorSelectTargetBase | static_cast<uint64_t>(monitorId);
}

// Classify BEFORE any HWND conversion.
inline SelectTarget classify_select_target(uint64_t windowId) {
  if ((windowId >> 60) != 0x7u) return {SelectTargetKind::Window, 0};
  if ((windowId & kMonitorSelectTargetMask) != kMonitorSelectTargetBase) return {SelectTargetKind::Malformed, 0};
  return {SelectTargetKind::Monitor, static_cast<uint32_t>(windowId & 0xFFFFFFFFULL)};
}

// The host's first look at a ControlWindowSelect target, on the control thread: refused (reason),
// or a window/desktop, or a monitor resolved to the device name of the screen THIS connection's
// last monitor list gave that id. `listed` is that list's device names in order.
struct SelectRequestResolution {
  const char* refused = nullptr;  // non-null: answer with this reason, apply nothing
  bool monitor = false;
  std::wstring device;  // the monitor's, when monitor
};

inline SelectRequestResolution resolve_select_request(uint64_t windowId, const std::vector<std::wstring>& listed) {
  SelectRequestResolution r;
  const SelectTarget t = classify_select_target(windowId);
  if (t.kind == SelectTargetKind::Malformed) {
    r.refused = "invalid_target";
  } else if (t.kind == SelectTargetKind::Monitor) {
    r.monitor = true;
    if (t.monitorId < listed.size() && !listed[t.monitorId].empty()) {
      r.device = listed[t.monitorId];
    } else {
      r.refused = "monitor_not_listed";
    }
  }
  return r;
}

}  // namespace remote60::native_poc
