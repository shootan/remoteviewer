#pragma once

// Win32 window / monitor enumeration and lookup for the host's target picker and capture-window
// binding.
//
// Role:    list shareable top-level windows (WindowListEntry) and attached displays
//          (MonitorListEntry) for the client picker; resolve a picked id back to an HWND; find the
//          capture window by pid / process name / title needle (CaptureWindowCriteria); describe a
//          window for input diagnostics; hit-test a top-level window at a screen point.
// Thread:  pure Win32 queries, no shared state (should_exclude_window_process caches its env-driven
//          pid set in a function-local static, initialised once). Called from the control thread
//          (window/monitor list requests) and the main loop (capture window rebind).
// Input:   HWND / window id / CaptureWindowCriteria / screen point.
// Output:  entry lists, CaptureWindowInfo, UTF-8 descriptions.
// Callers: native_video_host_main.cpp (control session, capture target resolution),
//          host_input_inject (find_top_level_window_at_point, describe_input_target).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-4). Definitions
// live in host_window_enum.cpp; behavior is byte-identical.

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace remote60::native_poc {

uint64_t hwnd_to_id(HWND hwnd);
HWND window_id_to_hwnd(uint64_t id);

struct WindowListEntry {
  uint64_t id = 0;
  HWND hwnd = nullptr;
  uint32_t pid = 0;
  int width = 0;
  int height = 0;
  bool minimized = false;
  std::string title;
};

// One attached display. Ordered with the primary first and the rest left to right, so "monitor 2"
// on the phone means the same screen every time rather than whatever the OS enumerated first.
struct MonitorListEntry {
  HMONITOR handle = nullptr;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool primary = false;
  std::string name;
};

std::string get_window_process_name(HWND hwnd, uint32_t* outPid);
std::string get_window_class_name(HWND hwnd);
std::wstring get_window_title(HWND hwnd);
std::string describe_input_target(HWND hwnd);

bool should_exclude_window_process(uint32_t pid, const std::string& processName);
bool should_include_window(HWND hwnd);
// Report the client extent, not the outer window rect (the stream is cropped to the client area).
void window_content_extent(HWND hwnd, const RECT& windowRect, int* outW, int* outH);

std::vector<MonitorListEntry> enumerate_monitors();
std::vector<WindowListEntry> enumerate_shareable_windows();
std::optional<WindowListEntry> find_window_by_id(uint64_t id);

struct CaptureWindowCriteria {
  uint32_t pid = 0;
  std::unordered_set<std::string> processNamesLower;
  std::wstring titleNeedleLower;
  bool enabled() const {
    return pid != 0 || !processNamesLower.empty() || !titleNeedleLower.empty();
  }
};

struct CaptureWindowInfo {
  HWND hwnd = nullptr;
  uint32_t pid = 0;
  std::string processName;
  std::wstring title;
};

bool match_capture_window(HWND hwnd, const CaptureWindowCriteria& criteria, CaptureWindowInfo* outInfo);
bool find_capture_window(const CaptureWindowCriteria& criteria, CaptureWindowInfo* outInfo);
bool find_capture_window_input_target(const CaptureWindowCriteria& criteria, CaptureWindowInfo* outInfo);
bool find_top_level_window_at_point(POINT screenPt, CaptureWindowInfo* outInfo);

}  // namespace remote60::native_poc
