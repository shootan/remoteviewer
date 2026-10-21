#pragma once

// dxgi-wedge-fix r1 (Codex B6, Bⓑ1): what an automatic reconnect brings back.
//
// Field (10-21 15:17): the host's streaming child wedged and restarted; the viewer closed (session-dead,
// exit 43) and the shell reconnected it a second later -- onto the PICKER. The user, who had been on the
// desktop, saw a window list instead of their screen and left. This is the handoff that lets the
// reconnected viewer go straight back to what was on screen.
//
// The rules (Codex B6):
//   * only a target that was CONFIRMED by the host and RENDERED (its first frame decoded) is reported --
//     never a pick still in flight, never a value from an older session;
//   * the viewer reports it on its stdout, which only the shell that launched it reads; the shell keeps
//     it per operation (child, owner epoch, host) and hands it to the next AUTOMATIC reconnect of that
//     same host only. No capability, token or password is in it -- the reconnect authenticates afresh;
//   * a screen is identified by what the NEW host lists for it (its device name and rectangle), never by
//     a raw index or HMONITOR; gone, changed or ambiguous -> the picker, with the reason;
//   * a window is not restored (minimum implementation allowed by B6): the picker, with the reason.
//
// Wire forms (both ASCII; the device name is hex so nothing in it can break a field or a command line):
//   viewer stdout:  [viewer-restore] target=<arg>
//   viewer arg:     --restore-target <arg>
//   <arg> = "window" | "desktop" | "desktop:<hex device name>:<x>,<y>,<w>,<h>:<primary 0|1>"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace remote60::native_poc::viewer {

struct RestoreTarget {
  enum class Kind { Desktop, Window };
  Kind kind = Kind::Desktop;
  bool hasMonitor = false;  // desktop: the screen is known
  std::string monitorName;  // the host's device name ("\\.\DISPLAY2")
  int32_t x = 0, y = 0;
  uint32_t width = 0, height = 0;
  bool primary = false;
};

// One screen as the new host lists it.
struct RestoreMonitor {
  uint32_t id = 0;
  std::string name;
  int32_t x = 0, y = 0;
  uint32_t width = 0, height = 0;
  bool primary = false;
};

enum class RestoreMatchKind { Matched, Gone, Changed, Ambiguous };
struct RestoreMatch {
  RestoreMatchKind kind = RestoreMatchKind::Gone;
  uint32_t monitorId = 0;
  bool primary = false;
  const char* reason() const;  // "monitor_gone" / "monitor_changed" / "monitor_ambiguous" / "matched"
};

std::string FormatRestoreArg(const RestoreTarget& t);
std::optional<RestoreTarget> ParseRestoreArg(const std::string& arg);
// The stdout line (no newline) and its strict parse (the shell side).
std::string FormatRestoreLine(const RestoreTarget& t);
std::optional<RestoreTarget> ParseRestoreLine(const std::string& line);
// The screen of `t` among what the new host lists: the same device name AND the same rectangle, exactly
// one. A name that is there with another rectangle is Changed (a resolution or layout change); none is Gone.
RestoreMatch MatchRestoreMonitor(const RestoreTarget& t, const std::vector<RestoreMonitor>& monitors);

}  // namespace remote60::native_poc::viewer
