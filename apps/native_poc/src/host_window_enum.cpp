// See host_window_enum.hpp for the module summary. Bodies below are moved verbatim from
// native_video_host_main.cpp (host split refactor Phase 0-4); no logic change.

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "host_string_util.hpp"
#include "host_window_enum.hpp"

namespace remote60::native_poc {

namespace {

BOOL CALLBACK enum_window_collect_proc(HWND hwnd, LPARAM lparam) {
  auto* out = reinterpret_cast<std::vector<WindowListEntry>*>(lparam);
  if (!out) return TRUE;
  if (!should_include_window(hwnd)) return TRUE;

  RECT r{};
  if (!GetWindowRect(hwnd, &r)) return TRUE;

  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  const int titleLen = GetWindowTextLengthW(hwnd);
  if (titleLen <= 0) return TRUE;
  std::wstring wtitle(static_cast<size_t>(titleLen) + 1, L'\0');
  const int got = GetWindowTextW(hwnd, wtitle.data(), titleLen + 1);
  if (got <= 0) return TRUE;
  wtitle.resize(static_cast<size_t>(got));
  const std::string title = wide_to_utf8(wtitle);
  if (title.empty()) return TRUE;

  WindowListEntry e;
  e.id = hwnd_to_id(hwnd);
  e.hwnd = hwnd;
  e.pid = static_cast<uint32_t>(pid);
  window_content_extent(hwnd, r, &e.width, &e.height);
  e.minimized = (IsIconic(hwnd) != 0);
  e.title = title;
  out->push_back(std::move(e));
  return TRUE;
}

}  // namespace

uint64_t hwnd_to_id(HWND hwnd) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd));
}

HWND window_id_to_hwnd(uint64_t id) {
  return reinterpret_cast<HWND>(static_cast<uintptr_t>(id));
}

bool should_exclude_window_process(uint32_t pid, const std::string& processName) {
  if (processName.empty()) return false;
  if (processName == "textinputhost.exe") return true;
  if (processName != "dnplayer.exe" &&
      processName != "dnmultiplayer.exe" &&
      processName != "ldplayer.exe" &&
      processName != "hd-player.exe") {
    return false;
  }

  static const std::unordered_set<uint32_t> excludedPids = []() {
    std::unordered_set<uint32_t> out;
    const char* raw = std::getenv("REMOTE60_NATIVE_WINDOWLIST_EXCLUDE_PIDS");
    if (!raw) return out;
    const std::string csv(raw);
    size_t start = 0;
    while (start <= csv.size()) {
      const size_t comma = csv.find(',', start);
      const size_t end = (comma == std::string::npos) ? csv.size() : comma;
      const std::string token = trim_ascii(csv.substr(start, end - start));
      if (!token.empty()) {
        try {
          const auto parsed = static_cast<uint32_t>(std::stoul(token));
          if (parsed != 0) out.insert(parsed);
        } catch (...) {
        }
      }
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    return out;
  }();
  return pid != 0 && excludedPids.find(pid) != excludedPids.end();
}

bool should_include_window(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return false;
  if (hwnd == GetShellWindow()) return false;
  if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;
  if (!IsWindowVisible(hwnd)) return false;
  if (GetWindowTextLengthW(hwnd) <= 0) return false;
  const LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
  if ((exStyle & WS_EX_TOOLWINDOW) != 0) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == GetCurrentProcessId()) return false;
  if (should_exclude_window_process(static_cast<uint32_t>(pid), get_window_process_name(hwnd, nullptr))) return false;
  RECT r{};
  if (!GetWindowRect(hwnd, &r)) return false;
  const int w = r.right - r.left;
  const int h = r.bottom - r.top;
  if (w < 60 || h < 60) return false;
  return true;
}

// Report the client extent, not the outer window rect. The stream is cropped to the client
// area, so advertising the outer size made the viewer letterbox and map touches against a
// slightly wrong aspect until the first frame decoded.
void window_content_extent(HWND hwnd, const RECT& windowRect, int* outW, int* outH) {
  *outW = windowRect.right - windowRect.left;
  *outH = windowRect.bottom - windowRect.top;
  RECT clientRect{};
  if (!GetClientRect(hwnd, &clientRect)) return;
  const int clientW = clientRect.right - clientRect.left;
  const int clientH = clientRect.bottom - clientRect.top;
  if (clientW > 1 && clientH > 1) {
    *outW = clientW;
    *outH = clientH;
  }
}

std::vector<MonitorListEntry> enumerate_monitors() {
  std::vector<MonitorListEntry> out;
  auto cb = [](HMONITOR mon, HDC, LPRECT, LPARAM lParam) -> BOOL {
    auto* list = reinterpret_cast<std::vector<MonitorListEntry>*>(lParam);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(mon, &info)) return TRUE;
    MonitorListEntry e{};
    e.handle = mon;
    e.x = info.rcMonitor.left;
    e.y = info.rcMonitor.top;
    e.width = static_cast<uint32_t>(std::max<LONG>(0, info.rcMonitor.right - info.rcMonitor.left));
    e.height = static_cast<uint32_t>(std::max<LONG>(0, info.rcMonitor.bottom - info.rcMonitor.top));
    e.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    // The device name is "\\.\DISPLAY1", which means nothing to a user; the client numbers them.
    char narrow[64]{};
    WideCharToMultiByte(CP_UTF8, 0, info.szDevice, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
    e.name = narrow;
    if (e.width > 0 && e.height > 0) list->push_back(std::move(e));
    return TRUE;
  };
  EnumDisplayMonitors(nullptr, nullptr, cb, reinterpret_cast<LPARAM>(&out));
  std::stable_sort(out.begin(), out.end(),
                   [](const MonitorListEntry& a, const MonitorListEntry& b) {
                     if (a.primary != b.primary) return a.primary;
                     if (a.x != b.x) return a.x < b.x;
                     return a.y < b.y;
                   });
  return out;
}

std::vector<WindowListEntry> enumerate_shareable_windows() {
  std::vector<WindowListEntry> out;
  EnumWindows(enum_window_collect_proc, reinterpret_cast<LPARAM>(&out));
  std::sort(out.begin(), out.end(), [](const WindowListEntry& a, const WindowListEntry& b) {
    return a.title < b.title;
  });
  return out;
}

std::optional<WindowListEntry> find_window_by_id(uint64_t id) {
  if (id == 0) return std::nullopt;
  const HWND hwnd = window_id_to_hwnd(id);
  if (!should_include_window(hwnd)) return std::nullopt;
  RECT r{};
  if (!GetWindowRect(hwnd, &r)) return std::nullopt;
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  const int titleLen = GetWindowTextLengthW(hwnd);
  if (titleLen <= 0) return std::nullopt;
  std::wstring wtitle(static_cast<size_t>(titleLen) + 1, L'\0');
  const int got = GetWindowTextW(hwnd, wtitle.data(), titleLen + 1);
  if (got <= 0) return std::nullopt;
  wtitle.resize(static_cast<size_t>(got));
  WindowListEntry e;
  e.id = id;
  e.hwnd = hwnd;
  e.pid = static_cast<uint32_t>(pid);
  window_content_extent(hwnd, r, &e.width, &e.height);
  e.minimized = (IsIconic(hwnd) != 0);
  e.title = wide_to_utf8(wtitle);
  return e;
}

std::string get_window_process_name(HWND hwnd, uint32_t* outPid) {
  if (outPid) *outPid = 0;
  if (!hwnd) return std::string{};
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0) return std::string{};
  if (outPid) *outPid = static_cast<uint32_t>(pid);
  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) return std::string{};
  char buf[1024] = {};
  DWORD len = static_cast<DWORD>(sizeof(buf));
  std::string out;
  if (QueryFullProcessImageNameA(proc, 0, buf, &len) != 0 && len > 0) {
    out.assign(buf, buf + len);
  }
  CloseHandle(proc);
  return base_name_lower(out);
}

std::string get_window_class_name(HWND hwnd) {
  if (!hwnd) return std::string{};
  wchar_t buf[256] = {};
  const int copied = GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
  if (copied <= 0) return std::string{};
  return wide_to_utf8(std::wstring(buf, buf + copied));
}

std::string describe_input_target(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return "target=<invalid>";
  uint32_t pid = 0;
  const std::string processName = get_window_process_name(hwnd, &pid);
  const std::wstring title = get_window_title(hwnd);
  const std::string className = get_window_class_name(hwnd);
  std::ostringstream oss;
  oss << " targetHwnd=0x" << std::hex << reinterpret_cast<uintptr_t>(hwnd) << std::dec
      << " targetPid=" << pid
      << " targetProc=" << (processName.empty() ? "<unknown>" : processName)
      << " targetClass=" << (className.empty() ? "<unknown>" : className)
      << " targetTitle=" << (title.empty() ? "<empty>" : wide_to_utf8(title));
  return oss.str();
}

std::wstring get_window_title(HWND hwnd) {
  if (!hwnd) return std::wstring{};
  const int len = GetWindowTextLengthW(hwnd);
  if (len <= 0) return std::wstring{};
  std::wstring title(static_cast<size_t>(len), L'\0');
  const int copied = GetWindowTextW(hwnd, title.data(), len + 1);
  if (copied <= 0) return std::wstring{};
  title.resize(static_cast<size_t>(copied));
  return title;
}

bool match_capture_window(HWND hwnd, const CaptureWindowCriteria& criteria, CaptureWindowInfo* outInfo) {
  if (!hwnd || !IsWindow(hwnd)) return false;
  if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
  const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  if ((style & WS_VISIBLE) == 0) return false;
  if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;

  uint32_t pid = 0;
  const std::string processName = get_window_process_name(hwnd, &pid);
  if (criteria.pid != 0 && pid != criteria.pid) return false;
  if (!criteria.processNamesLower.empty()) {
    if (processName.empty()) return false;
    if (criteria.processNamesLower.find(processName) == criteria.processNamesLower.end()) return false;
  }
  const std::wstring title = get_window_title(hwnd);
  if (!criteria.titleNeedleLower.empty()) {
    if (title.empty()) return false;
    const std::wstring titleLower = wide_lower(title);
    if (titleLower.find(criteria.titleNeedleLower) == std::wstring::npos) return false;
  }

  if (outInfo) {
    outInfo->hwnd = hwnd;
    outInfo->pid = pid;
    outInfo->processName = processName;
    outInfo->title = title;
  }
  return true;
}

bool find_capture_window(const CaptureWindowCriteria& criteria, CaptureWindowInfo* outInfo) {
  if (!criteria.enabled()) return false;
  struct EnumState {
    const CaptureWindowCriteria* criteria = nullptr;
    CaptureWindowInfo info{};
    bool found = false;
  } state;
  state.criteria = &criteria;

  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* s = reinterpret_cast<EnumState*>(lParam);
        if (!s || !s->criteria) return TRUE;
        CaptureWindowInfo candidate{};
        if (match_capture_window(hwnd, *s->criteria, &candidate)) {
          s->info = candidate;
          s->found = true;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&state));

  if (!state.found) return false;
  if (outInfo) *outInfo = state.info;
  return true;
}

bool find_capture_window_input_target(const CaptureWindowCriteria& criteria, CaptureWindowInfo* outInfo) {
  if (!criteria.enabled()) return false;
  if (find_capture_window(criteria, outInfo)) return true;
  // Some packaged apps can intermittently fail process-name lookup; if both
  // filters were provided, retry with title-only matching before giving up.
  if (!criteria.processNamesLower.empty() && !criteria.titleNeedleLower.empty()) {
    CaptureWindowCriteria titleOnly{};
    titleOnly.titleNeedleLower = criteria.titleNeedleLower;
    if (find_capture_window(titleOnly, outInfo)) return true;
  }
  return false;
}

bool find_top_level_window_at_point(POINT screenPt, CaptureWindowInfo* outInfo) {
  CaptureWindowCriteria anyCriteria{};
  for (HWND hwnd = GetTopWindow(nullptr); hwnd != nullptr; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
    RECT wr{};
    if (!GetWindowRect(hwnd, &wr)) continue;
    if (screenPt.x < wr.left || screenPt.x >= wr.right || screenPt.y < wr.top || screenPt.y >= wr.bottom) {
      continue;
    }
    CaptureWindowInfo info{};
    if (match_capture_window(hwnd, anyCriteria, &info)) {
      if (outInfo) *outInfo = info;
      return true;
    }
  }
  return false;
}

}  // namespace remote60::native_poc
