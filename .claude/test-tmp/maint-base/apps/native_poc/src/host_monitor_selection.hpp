#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "host_window_enum.hpp"

namespace remote60::native_poc {

// Which monitor a desktop capture opens, given the screen the user picked (its stable device name,
// "\\.\DISPLAYn"; empty = never picked, i.e. the primary) and what is attached right now
// (enumerate_monitors(): primary first, then left to right).
//
// t-970r4zgo: a picked monitor that was unplugged used to pin the host to it forever -- every
// reattach failed and retried every 5 s, and a viewer that came back saw the window list but never
// a picture. Absence is now an answer, not a retry:
//   Found       the picked screen is attached; `index` is where it is now.
//   FellBack    the enumeration listed screens and the picked one is not among them: open
//               `index` (0, the primary -- or the leftmost if none is flagged primary) instead.
//               Not reverted when the screen comes back; the user picks it again (it is listed).
//   Unconfirmed nothing was enumerated, so absence is not established (a display reconfiguration
//               in progress): the caller keeps its existing retry, aimed at the same screen.
//   NoSelection nothing was picked; the caller opens the primary as before.
// The capture restart and the monitor-list reply both answer from this, so the selection the
// viewer is told is the screen it is shown.
enum class SelectedMonitorOutcome { NoSelection, Found, FellBack, Unconfirmed };

struct SelectedMonitorChoice {
  SelectedMonitorOutcome outcome = SelectedMonitorOutcome::NoSelection;
  size_t index = 0;
};

inline SelectedMonitorChoice choose_selected_monitor(const std::vector<MonitorListEntry>& monitors,
                                                     const std::wstring& selectedDevice) {
  if (selectedDevice.empty()) return {SelectedMonitorOutcome::NoSelection, 0};
  for (size_t i = 0; i < monitors.size(); ++i) {
    if (monitors[i].device == selectedDevice) return {SelectedMonitorOutcome::Found, i};
  }
  if (monitors.empty()) return {SelectedMonitorOutcome::Unconfirmed, 0};
  return {SelectedMonitorOutcome::FellBack, 0};
}

inline const char* selected_monitor_outcome_name(SelectedMonitorOutcome o) {
  switch (o) {
    case SelectedMonitorOutcome::NoSelection: return "no-selection";
    case SelectedMonitorOutcome::Found: return "found";
    case SelectedMonitorOutcome::FellBack: return "fell-back";
    case SelectedMonitorOutcome::Unconfirmed: return "unconfirmed";
  }
  return "?";
}

}  // namespace remote60::native_poc
