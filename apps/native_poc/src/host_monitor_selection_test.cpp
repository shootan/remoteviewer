// t-970r4zgo: which screen a desktop capture opens once the picked one may be gone
// (host_monitor_selection.hpp). Pure logic: the enumeration is written out here, no display is
// touched. The process-level path -- a real host, a screen unplugged through the test build's
// enumeration seam -- is host_monitor_fallback_e2e_test.

#include "host_monitor_selection.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
int gChecks = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
  if (!cond) ++gFailures;
}

MonitorListEntry screen(const wchar_t* device, int32_t x, bool primary) {
  MonitorListEntry e;
  e.handle = reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(0x1000 + x));
  e.x = x;
  e.width = 1920;
  e.height = 1080;
  e.primary = primary;
  e.device = device;
  return e;
}

std::string said(const SelectedMonitorChoice& c) {
  return std::string(selected_monitor_outcome_name(c.outcome)) + " index=" + std::to_string(c.index);
}

}  // namespace

int main() {
  const auto primary = screen(L"\\\\.\\DISPLAY1", 0, true);
  const auto second = screen(L"\\\\.\\DISPLAY2", 1920, false);
  const auto third = screen(L"\\\\.\\DISPLAY3", 3840, false);

  {
    std::puts("\n--- two screens, the second picked (id=1), then only one attached ---");
    const auto before = choose_selected_monitor({primary, second}, second.device);
    check("with both attached the picked screen is found where it is (id=1)",
          before.outcome == SelectedMonitorOutcome::Found && before.index == 1, said(before));
    const auto after = choose_selected_monitor({primary}, second.device);
    check("once it is gone the capture falls back to the primary (id=0), not a retry of the missing one",
          after.outcome == SelectedMonitorOutcome::FellBack && after.index == 0, said(after));
  }
  {
    std::puts("\n--- a display reconfiguration in progress: nothing enumerated ---");
    const auto c = choose_selected_monitor({}, second.device);
    check("absence is not established: no fallback, the existing retry keeps aiming at the same screen",
          c.outcome == SelectedMonitorOutcome::Unconfirmed, said(c));
  }
  {
    std::puts("\n--- the picked screen is attached but its index moved ---");
    const auto c = choose_selected_monitor({primary, third}, third.device);
    check("three screens, the middle one unplugged: the picked third is found at its new index 1",
          c.outcome == SelectedMonitorOutcome::Found && c.index == 1, said(c));
  }
  {
    std::puts("\n--- a screen still listed but its capture failing is not this function's case ---");
    const auto c = choose_selected_monitor({primary, second}, second.device);
    check("listed = Found: an attach failure there is the caller's transient retry, not a fallback",
          c.outcome == SelectedMonitorOutcome::Found && c.index == 1, said(c));
  }
  {
    std::puts("\n--- nothing picked ---");
    const auto c = choose_selected_monitor({primary, second}, L"");
    check("no selection: the primary as before", c.outcome == SelectedMonitorOutcome::NoSelection, said(c));
  }
  {
    std::puts("\n--- the fallback is the first listed screen ---");
    // enumerate_monitors() lists the primary first; without one flagged, the leftmost is first.
    const auto c = choose_selected_monitor({second, third}, primary.device);
    check("no primary flagged: the first (leftmost) screen", c.outcome == SelectedMonitorOutcome::FellBack && c.index == 0,
          said(c));
  }
  {
    std::puts("\n--- not reverted ---");
    // The restart records the fallback's device as the new selection. When the old screen returns,
    // that device is still attached, so the capture stays where it is until the user picks again.
    const auto fell = choose_selected_monitor({primary}, second.device);
    const std::wstring nowSelected = std::vector<MonitorListEntry>{primary}[fell.index].device;
    const auto back = choose_selected_monitor({primary, second}, nowSelected);
    check("the old screen plugged back in: still the primary (id=0)",
          back.outcome == SelectedMonitorOutcome::Found && back.index == 0, said(back));
  }

  if (gFailures == 0) {
    std::printf("\nRESULT: ALL PASS  (%d checks, 0 failed)\n", gChecks);
    return 0;
  }
  std::printf("\nRESULT: %d FAILED  (%d checks)\n", gFailures, gChecks);
  return 1;
}
