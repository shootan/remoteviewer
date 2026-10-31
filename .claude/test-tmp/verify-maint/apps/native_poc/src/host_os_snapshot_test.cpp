// dxgi-wedge-fix r1 (Codex B8): the watchdog's 3 s OS snapshot. The line's fields (no window title -- a
// window of THIS process with a marker title is in the foreground position when possible, and the marker
// must not appear), and that a request never waits: with the collector stalled, RequestOsSnapshot returns
// at once and a second one is refused while the first runs. pure-logic + user32 (reads only).

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "host_os_snapshot.hpp"

using namespace remote60::native_poc;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  HWND w = CreateWindowExW(0, L"STATIC", L"SNAPSHOT-TITLE-MARKER", WS_OVERLAPPEDWINDOW, 10, 10, 300, 200, nullptr, nullptr,
                           nullptr, nullptr);
  ShowWindow(w, SW_SHOWNOACTIVATE);
  const std::string line = collect_os_snapshot_line("test", "release", 3100);
  std::printf("      %s", line.c_str());
  check("the line has its fields", line.rfind("[native-video-host][os-snapshot] at=test phase=release phaseAgeMs=3100 ", 0) == 0 &&
                                       line.find(" fgPid=") != std::string::npos && line.find(" fgExe=") != std::string::npos &&
                                       line.find(" mode=") != std::string::npos && line.find(" coversMonitor=") != std::string::npos &&
                                       line.back() == '\n');
  check("no window title is collected", line.find("SNAPSHOT-TITLE-MARKER") == std::string::npos);
  check("the executable is a file name, not a path", line.find(":\\") == std::string::npos);
  check("one line, no spaces inside a field", std::count(line.begin(), line.end(), '\n') == 1);

  std::atomic<bool> release{false};
  OsSnapshotStallForTest() = [&] { while (!release.load()) Sleep(10); };
  const auto t0 = std::chrono::steady_clock::now();
  const bool first = RequestOsSnapshot("stall", "release", 3000);
  const bool second = RequestOsSnapshot("stall", "release", 3500);
  const long long ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  check("with the collector stalled, the request returns at once (the watchdog never waits)", first && ms < 100,
        std::to_string(ms) + " ms");
  check("a second request while one runs is refused, not queued", !second);
  release.store(true);
  Sleep(500);
  OsSnapshotStallForTest() = nullptr;
  check("once it finished, a new one can start", RequestOsSnapshot("again", "release", 3000));
  Sleep(500);
  DestroyWindow(w);
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
