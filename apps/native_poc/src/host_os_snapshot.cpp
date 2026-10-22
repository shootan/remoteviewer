// See host_os_snapshot.hpp (dxgi-wedge-fix r1, Codex B8).

#include <winsock2.h>
#include <windows.h>

#include "host_os_snapshot.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

namespace remote60::native_poc {

namespace {

std::atomic<bool> gCollecting{false};

std::string rect_text(const RECT& r) {
  char b[64];
  std::snprintf(b, sizeof(b), "%ld,%ld,%ld,%ld", r.left, r.top, r.right - r.left, r.bottom - r.top);
  return b;
}

// Printable ASCII only, no spaces (a log field).
std::string field_safe(const std::wstring& w) {
  std::string s;
  for (wchar_t c : w) s.push_back((c > 0x20 && c < 0x7f) ? static_cast<char>(c) : '_');
  return s.empty() ? std::string("unknown") : s;
}

}  // namespace

std::string collect_os_snapshot_line(const std::string& at, const std::string& phase, unsigned long long phaseAgeMs) {
  const auto t0 = std::chrono::steady_clock::now();
  std::string fgPid = "unknown", fgExe = "unknown", fgRect = "unknown";
  std::string monitor = "unknown", monRect = "unknown", mode = "unknown", covers = "unknown";
  if (HWND fg = GetForegroundWindow()) {
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid) {
      fgPid = std::to_string(pid);
      if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH * 2] = {};
        DWORD n = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(p, 0, path, &n)) {
          std::wstring full(path, n);
          const size_t slash = full.find_last_of(L"\\/");
          fgExe = field_safe(slash == std::wstring::npos ? full : full.substr(slash + 1));  // file name only
        }
        CloseHandle(p);
      }
    }
    RECT wr{};
    const bool haveRect = GetWindowRect(fg, &wr) != FALSE;
    if (haveRect) fgRect = rect_text(wr);
    if (HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST)) {
      MONITORINFOEXW mi{};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfoW(mon, &mi)) {
        monitor = field_safe(mi.szDevice);
        monRect = rect_text(mi.rcMonitor);
        if (haveRect) covers = EqualRect(&wr, &mi.rcMonitor) ? "1" : "0";
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
          char b[64];
          std::snprintf(b, sizeof(b), "%lux%lu@%luHz/%lubpp", dm.dmPelsWidth, dm.dmPelsHeight,
                        dm.dmDisplayFrequency, dm.dmBitsPerPel);
          mode = b;
        }
      }
    }
  }
  const long long ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  return "[native-video-host][os-snapshot] at=" + at + " phase=" + phase + " phaseAgeMs=" + std::to_string(phaseAgeMs) +
         " fgPid=" + fgPid + " fgExe=" + fgExe + " fgRect=" + fgRect + " monitor=" + monitor + " monRect=" + monRect +
         " mode=" + mode + " coversMonitor=" + covers + " collectMs=" + std::to_string(ms) + "\n";
}

std::function<void()>& OsSnapshotStallForTest() {
  static std::function<void()> hook;
  return hook;
}

bool RequestOsSnapshot(const char* at, const char* phase, unsigned long long phaseAgeMs) {
  bool expected = false;
  if (!gCollecting.compare_exchange_strong(expected, true)) return false;
  try {
    std::thread([at = std::string(at), phase = std::string(phase), phaseAgeMs] {
      if (OsSnapshotStallForTest()) OsSnapshotStallForTest()();
      const std::string line = collect_os_snapshot_line(at, phase, phaseAgeMs);
      std::cout << line;
      std::cout.flush();
      gCollecting.store(false);
    }).detach();
  } catch (...) {
    gCollecting.store(false);
    return false;
  }
  return true;
}

}  // namespace remote60::native_poc
