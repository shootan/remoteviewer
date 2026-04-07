#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "mf_h264_codec.hpp"
#include "json_profile.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace {

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::native_poc::ControlInputAckMessage;
using remote60::native_poc::ControlInputEventMessage;
using remote60::native_poc::ControlInputTextMessage;
using remote60::native_poc::ControlStreamStateMessage;
using remote60::native_poc::ControlClientMetricsMessage;
using remote60::native_poc::ControlRequestKeyFrameMessage;
using remote60::native_poc::ControlRuntimeEncoderConfigMessage;
using remote60::native_poc::ControlCaptureModeRequestMessage;
using remote60::native_poc::ControlWindowEntry;
using remote60::native_poc::ControlWindowListMessage;
using remote60::native_poc::ControlWindowListRequestMessage;
using remote60::native_poc::ControlWindowSelectMessage;
using remote60::native_poc::ControlWindowSelectedMessage;
using remote60::native_poc::ControlPingMessage;
using remote60::native_poc::ControlPongMessage;
using remote60::native_poc::H264EncodeFrameStats;
using remote60::native_poc::EncodedFrameHeader;
using remote60::native_poc::H264AccessUnit;
using remote60::native_poc::H264Encoder;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::RawFrameHeader;
using remote60::native_poc::UdpCodec;
using remote60::native_poc::UdpHelloPacket;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;
using remote60::native_poc::VideoTransport;
using remote60::native_poc::bgra_to_nv12;
using remote60::native_poc::clamp_udp_mtu;
using remote60::native_poc::parse_video_transport;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::video_transport_name;
namespace json_profile = remote60::native_poc::json_profile;

#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT
#define REMOTE60_NATIVE_ENCODED_EXPERIMENT 0
#endif
constexpr bool kInputPolicyForceBlock = false;
constexpr uint64_t kMaxEncodedFrameAgeUs = 250000;  // 250ms
constexpr uint32_t kMaxConsecutiveStaleEncodedFrames = 8;
constexpr int kCaptureFramePoolBuffersDefault = 2;
constexpr uint64_t kMaxPreEncodeFrameAgeUs = 25000;  // 25ms
constexpr uint64_t kHostUserFeedbackWarnUs = 90000;  // 90ms
constexpr uint64_t kHostUserFeedbackMinIntervalUs = 1000000;  // 1s
constexpr uint64_t kCaptureStallKeepaliveIntervalUs = 1000000;  // 1s
constexpr uint64_t kCaptureCallbackStallRestartUs = 1200000;  // 1.2s
constexpr uint64_t kCaptureCallbackRestartCooldownUs = 3000000;  // 3s
constexpr uint64_t kQueueWaitTimeoutUsDefault = 100000;  // 100ms
constexpr uint64_t kQueueWaitTimeoutUsMin = 5000;  // 5ms
constexpr uint32_t kCaptureInputMinPushPerSecDefault = 10;
constexpr uint32_t kCaptureInputStallConsecutiveSecDefault = 3;
constexpr uint32_t kCaptureInputStallWarmupSecDefault = 4;
constexpr uint32_t kFrameGatingStaticFpsDefault = 8;
constexpr uint32_t kFrameGatingStaticThresholdPermilleDefault = 6;
constexpr uint32_t kFrameGatingMotionThresholdPermilleDefault = 14;
constexpr uint32_t kFrameGatingEnterFramesDefault = 10;
constexpr uint32_t kFrameGatingExitFramesDefault = 2;
constexpr uint32_t kFrameGatingSampleTargetDefault = 2048;
constexpr uint32_t kKeyReqMinIntervalUsDefault = 120000;  // 120ms
constexpr uint32_t kKeyReqTokenRefillUsDefault = 300000;  // 300ms / token
constexpr uint32_t kKeyReqTokenCapacityDefault = 3;

struct HostBottleneckStage {
  uint32_t code = 0;
  uint64_t us = 0;
  const char* name = "none";
};

void update_host_bottleneck_stage(uint32_t code, uint64_t us, const char* name,
                                  HostBottleneckStage* stage) {
  if (!stage || !name) return;
  if (us > stage->us) {
    stage->code = code;
    stage->us = us;
    stage->name = name;
  }
}

HostBottleneckStage detect_host_bottleneck_stage(uint64_t queueWaitUs, uint64_t queueToEncodeUs,
                                                 uint64_t preEncodePrepUs, uint64_t scaleUs,
                                                 uint64_t nv12Us, uint64_t encUs,
                                                 uint64_t queueToSendUs, uint64_t sendDurUs,
                                                 uint64_t sendIntervalErrUs) {
  HostBottleneckStage stage{};
  update_host_bottleneck_stage(1, queueWaitUs, "queue_wait", &stage);
  update_host_bottleneck_stage(2, queueToEncodeUs, "queue_to_encode", &stage);
  update_host_bottleneck_stage(3, preEncodePrepUs, "pre_encode_prep", &stage);
  update_host_bottleneck_stage(4, scaleUs, "scale", &stage);
  update_host_bottleneck_stage(5, nv12Us, "bgra_to_nv12", &stage);
  update_host_bottleneck_stage(6, encUs, "encoder", &stage);
  update_host_bottleneck_stage(7, queueToSendUs, "queue_to_send", &stage);
  update_host_bottleneck_stage(8, sendDurUs, "send_io", &stage);
  update_host_bottleneck_stage(9, sendIntervalErrUs, "send_interval_jitter", &stage);
  return stage;
}

uint32_t encoder_api_path_code(const char* backendName) {
  if (!backendName) return 0;
  const std::string name = backendName;
  if (name.find("amf") != std::string::npos) return 1;
  if (name.find("nvenc") != std::string::npos || name.find("nvidia") != std::string::npos) return 2;
  if (name.find("qsv") != std::string::npos || name.find("intel") != std::string::npos) return 3;
  if (name.find("mft") != std::string::npos) return 4;
  if (name.find("clsid") != std::string::npos) return 5;
  return 6;
}

std::string trim_ascii(std::string v) {
  size_t start = 0;
  while (start < v.size() && std::isspace(static_cast<unsigned char>(v[start])) != 0) {
    ++start;
  }
  size_t end = v.size();
  while (end > start && std::isspace(static_cast<unsigned char>(v[end - 1])) != 0) {
    --end;
  }
  return v.substr(start, end - start);
}

std::string ascii_lower(std::string v) {
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return v;
}

std::wstring utf8_to_wide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring{};
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (n <= 1) return std::wstring{};
  std::wstring out(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
  return out;
}

std::string wide_to_utf8(const std::wstring& wide) {
  if (wide.empty()) return std::string{};
  const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return std::string{};
  std::string out(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring wide_lower(std::wstring v) {
  std::transform(v.begin(), v.end(), v.begin(), [](wchar_t c) {
    return static_cast<wchar_t>(std::towlower(c));
  });
  return v;
}

std::vector<std::string> parse_csv_lower(const std::string& raw) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= raw.size()) {
    const size_t comma = raw.find(',', start);
    const size_t end = (comma == std::string::npos) ? raw.size() : comma;
    const std::string token = trim_ascii(raw.substr(start, end - start));
    if (!token.empty()) {
      out.push_back(ascii_lower(token));
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

std::string base_name_lower(std::string path) {
  if (path.empty()) return path;
  const size_t slashPos = path.find_last_of("\\/");
  if (slashPos != std::string::npos && slashPos + 1 < path.size()) {
    path = path.substr(slashPos + 1);
  }
  return ascii_lower(path);
}

uint64_t hwnd_to_id(HWND hwnd) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd));
}

HWND window_id_to_hwnd(uint64_t id) {
  return reinterpret_cast<HWND>(static_cast<uintptr_t>(id));
}

struct WindowListEntry {
  uint64_t id = 0;
  HWND hwnd = nullptr;
  uint32_t pid = 0;
  int width = 0;
  int height = 0;
  bool minimized = false;
  std::string title;
};

std::string get_window_process_name(HWND hwnd, uint32_t* outPid);

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
  e.width = r.right - r.left;
  e.height = r.bottom - r.top;
  e.minimized = (IsIconic(hwnd) != 0);
  e.title = title;
  out->push_back(std::move(e));
  return TRUE;
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
  e.width = r.right - r.left;
  e.height = r.bottom - r.top;
  e.minimized = (IsIconic(hwnd) != 0);
  e.title = wide_to_utf8(wtitle);
  return e;
}

std::string env_string_or_empty(const char* key) {
  if (!key) return std::string{};
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string{};
}

bool backend_request_is_any(const std::string& requestLower, const char* const* values,
                            size_t valueCount) {
  if (!values || valueCount == 0) return false;
  for (size_t i = 0; i < valueCount; ++i) {
    const char* v = values[i];
    if (v && requestLower == v) return true;
  }
  return false;
}

bool backend_request_satisfied(const std::string& requestLower, const std::string& resolvedLower) {
  if (requestLower.empty()) return true;
  if (requestLower == "auto" || requestLower == "mft_auto") return true;
  if (requestLower == "hw" || requestLower == "mft_hw") {
    return resolvedLower.find("mft_enum_hw") != std::string::npos;
  }
  if (requestLower == "sw" || requestLower == "mft_sw") {
    return resolvedLower.find("mft_enum_sw") != std::string::npos ||
           resolvedLower.find("clsid_cmsh264") != std::string::npos;
  }
  static const char* const kAmfAliases[] = {"amf_hw", "amf_mft", "amd_hw", "amd_mft", "amd"};
  static const char* const kNvencAliases[] = {
      "nvenc_hw", "nvenc_mft", "nvenc", "nvidia_hw", "nvidia_mft", "nvidia"};
  static const char* const kQsvAliases[] = {
      "qsv_hw", "qsv_mft", "qsv", "intel_hw", "intel_mft", "intel"};
  if (backend_request_is_any(requestLower, kAmfAliases, sizeof(kAmfAliases) / sizeof(kAmfAliases[0]))) {
    return resolvedLower.find("amf") != std::string::npos;
  }
  if (backend_request_is_any(requestLower, kNvencAliases,
                             sizeof(kNvencAliases) / sizeof(kNvencAliases[0]))) {
    return resolvedLower.find("nvenc") != std::string::npos ||
           resolvedLower.find("nvidia") != std::string::npos;
  }
  if (backend_request_is_any(requestLower, kQsvAliases, sizeof(kQsvAliases) / sizeof(kQsvAliases[0]))) {
    return resolvedLower.find("qsv") != std::string::npos ||
           resolvedLower.find("intel") != std::string::npos;
  }
  return resolvedLower.find(requestLower) != std::string::npos;
}

bool backend_request_is_vendor_specific(const std::string& requestLower) {
  static const char* const kAmfAliases[] = {"amf_hw", "amf_mft", "amd_hw", "amd_mft", "amd"};
  static const char* const kNvencAliases[] = {
      "nvenc_hw", "nvenc_mft", "nvenc", "nvidia_hw", "nvidia_mft", "nvidia"};
  static const char* const kQsvAliases[] = {
      "qsv_hw", "qsv_mft", "qsv", "intel_hw", "intel_mft", "intel"};
  return backend_request_is_any(requestLower, kAmfAliases, sizeof(kAmfAliases) / sizeof(kAmfAliases[0])) ||
         backend_request_is_any(requestLower, kNvencAliases,
                                sizeof(kNvencAliases) / sizeof(kNvencAliases[0])) ||
         backend_request_is_any(requestLower, kQsvAliases, sizeof(kQsvAliases) / sizeof(kQsvAliases[0]));
}

std::string backend_fallback_reason(const std::string& requestedRaw, const char* resolvedBackendRaw) {
  const std::string requestLower = ascii_lower(trim_ascii(requestedRaw));
  const std::string resolvedLower =
      ascii_lower(trim_ascii(resolvedBackendRaw ? std::string(resolvedBackendRaw) : std::string{}));
  if (requestLower.empty()) return "default_policy";
  if (backend_request_satisfied(requestLower, resolvedLower)) return "none";
  if (resolvedLower.find("_unavailable") != std::string::npos) {
    return "requested_backend_unavailable";
  }
  if (backend_request_is_vendor_specific(requestLower) &&
      (resolvedLower.find("mft_enum_hw") != std::string::npos ||
       resolvedLower.find("mft_enum_sw") != std::string::npos ||
       resolvedLower.find("clsid_cmsh264") != std::string::npos)) {
    return "requested_backend_unavailable";
  }
  if (resolvedLower.find("mft_enum_sw") != std::string::npos ||
      resolvedLower.find("clsid_cmsh264") != std::string::npos) {
    return "fallback_to_software";
  }
  if (resolvedLower.find("mft_enum_hw") != std::string::npos) {
    return "fallback_to_generic_hw";
  }
  return "requested_backend_mismatch";
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

std::wstring get_window_title(HWND hwnd);

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

enum class InputInjectionMode : uint8_t {
  Disabled = 0,
  BackgroundMessage = 1,
};

InputInjectionMode parse_input_injection_mode(std::string raw) {
  raw = ascii_lower(trim_ascii(raw));
  if (raw.empty() || raw == "none" || raw == "disabled" || raw == "off") {
    return InputInjectionMode::Disabled;
  }
  if (raw == "background_message") {
    return InputInjectionMode::BackgroundMessage;
  }
  return InputInjectionMode::Disabled;
}

const char* input_injection_mode_name(InputInjectionMode mode) {
  switch (mode) {
    case InputInjectionMode::Disabled:
      return "disabled";
    case InputInjectionMode::BackgroundMessage:
      return "background_message";
    default:
      return "unknown";
  }
}

WPARAM mouse_button_wparam(uint16_t buttons) {
  WPARAM wp = 0;
  if ((buttons & 0x1u) != 0) wp |= MK_LBUTTON;
  if ((buttons & 0x2u) != 0) wp |= MK_RBUTTON;
  if ((buttons & 0x4u) != 0) wp |= MK_MBUTTON;
  return wp;
}

uint16_t mouse_vk_to_mask(uint32_t vk) {
  switch (vk) {
    case VK_LBUTTON:
      return 0x1u;
    case VK_RBUTTON:
      return 0x2u;
    case VK_MBUTTON:
      return 0x4u;
    default:
      return 0x0u;
  }
}

UINT mouse_vk_to_message(uint16_t kind, uint32_t vk) {
  if (kind == 2) {
    if (vk == VK_RBUTTON) return WM_RBUTTONDOWN;
    if (vk == VK_MBUTTON) return WM_MBUTTONDOWN;
    return WM_LBUTTONDOWN;
  }
  if (kind == 3) {
    if (vk == VK_RBUTTON) return WM_RBUTTONUP;
    if (vk == VK_MBUTTON) return WM_MBUTTONUP;
    return WM_LBUTTONUP;
  }
  return 0;
}

bool is_extended_vk(uint32_t vk) {
  switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT:  // Page Up / Page Down
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_NUMLOCK: case VK_SNAPSHOT: case VK_CANCEL:  // Print Screen / Break
    case VK_DIVIDE:   // Numpad /
    case VK_RCONTROL: case VK_RMENU:  // Right Ctrl / Right Alt
    case VK_RETURN:   // Numpad Enter shares VK_RETURN but scan code differs
      return true;
    default:
      return false;
  }
}

LPARAM key_event_lparam(uint32_t keyCode, bool keyUp) {
  const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(keyCode), MAPVK_VK_TO_VSC);
  DWORD lp = 1u | ((scanCode & 0xFFu) << 16);
  if (is_extended_vk(keyCode)) lp |= (1u << 24);  // extended key flag
  if (keyUp) lp |= (1u << 30) | (1u << 31);
  return static_cast<LPARAM>(lp);
}

void set_keyboard_state_flag(BYTE* keyboardState, uint32_t keyCode, bool down) {
  if (!keyboardState || keyCode >= 256) return;
  if (down) {
    keyboardState[keyCode] = static_cast<BYTE>(keyboardState[keyCode] | 0x80u);
  } else {
    keyboardState[keyCode] = static_cast<BYTE>(keyboardState[keyCode] & ~0x80u);
  }
}

void update_synthetic_keyboard_state(BYTE* keyboardState, uint32_t keyCode, bool keyUp) {
  if (!keyboardState || keyCode >= 256) return;
  const bool down = !keyUp;
  set_keyboard_state_flag(keyboardState, keyCode, down);
  switch (keyCode) {
    case VK_SHIFT:
      set_keyboard_state_flag(keyboardState, VK_SHIFT, down);
      set_keyboard_state_flag(keyboardState, VK_LSHIFT, down);
      set_keyboard_state_flag(keyboardState, VK_RSHIFT, down);
      break;
    case VK_LSHIFT:
    case VK_RSHIFT:
      set_keyboard_state_flag(keyboardState, VK_SHIFT, down);
      break;
    case VK_CONTROL:
      set_keyboard_state_flag(keyboardState, VK_CONTROL, down);
      set_keyboard_state_flag(keyboardState, VK_LCONTROL, down);
      set_keyboard_state_flag(keyboardState, VK_RCONTROL, down);
      break;
    case VK_LCONTROL:
    case VK_RCONTROL:
      set_keyboard_state_flag(keyboardState, VK_CONTROL, down);
      break;
    case VK_MENU:
      set_keyboard_state_flag(keyboardState, VK_MENU, down);
      set_keyboard_state_flag(keyboardState, VK_LMENU, down);
      set_keyboard_state_flag(keyboardState, VK_RMENU, down);
      break;
    case VK_LMENU:
    case VK_RMENU:
      set_keyboard_state_flag(keyboardState, VK_MENU, down);
      break;
    case VK_CAPITAL:
      if (down) {
        keyboardState[VK_CAPITAL] = static_cast<BYTE>(keyboardState[VK_CAPITAL] ^ 0x01u);
      }
      break;
    default:
      break;
  }
}

bool keycode_to_unicode_char(uint32_t keyCode, const BYTE* keyboardState, wchar_t* outCh) {
  if (!outCh) return false;
  *outCh = 0;
  BYTE localKeyboardState[256] = {};
  if (keyboardState) {
    std::memcpy(localKeyboardState, keyboardState, sizeof(localKeyboardState));
  } else if (!GetKeyboardState(localKeyboardState)) {
    std::memset(localKeyboardState, 0, sizeof(localKeyboardState));
  }
  const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(keyCode), MAPVK_VK_TO_VSC);
  WCHAR buffer[8] = {};
  const int rc = ToUnicodeEx(static_cast<UINT>(keyCode), scanCode, localKeyboardState, buffer,
                             static_cast<int>(std::size(buffer)), 0, GetKeyboardLayout(0));
  if (rc == 1 && buffer[0] >= 0x20) {
    *outCh = buffer[0];
    return true;
  }
  switch (keyCode) {
    case VK_SPACE:
      *outCh = L' ';
      return true;
    case VK_RETURN:
      *outCh = L'\r';
      return true;
    case VK_TAB:
      *outCh = L'\t';
      return true;
    case VK_BACK:
      *outCh = L'\b';
      return true;
    default:
      return false;
  }
}

int scale_input_coord(int32_t coord, uint32_t inputExtent, int outputExtent) {
  if (outputExtent <= 1) return 0;
  if (inputExtent <= 1) return std::clamp<int>(coord, 0, outputExtent - 1);
  const int32_t clamped = std::clamp<int32_t>(coord, 0, static_cast<int32_t>(inputExtent - 1));
  const uint64_t numerator = static_cast<uint64_t>(clamped) * static_cast<uint64_t>(outputExtent - 1) +
                             static_cast<uint64_t>((inputExtent - 1) / 2);
  return static_cast<int>(numerator / static_cast<uint64_t>(inputExtent - 1));
}

bool make_scaled_client_lparam(HWND hwnd, int32_t x, int32_t y, uint32_t inputW, uint32_t inputH,
                               LPARAM* outLp, POINT* outClientPt = nullptr) {
  if (!outLp || !hwnd || !IsWindow(hwnd)) return false;
  RECT rc{};
  if (!GetClientRect(hwnd, &rc)) return false;
  const int w = std::max<int>(1, rc.right - rc.left);
  const int h = std::max<int>(1, rc.bottom - rc.top);
  const int cx = scale_input_coord(x, inputW, w);
  const int cy = scale_input_coord(y, inputH, h);
  if (outClientPt) {
    outClientPt->x = cx;
    outClientPt->y = cy;
  }
  *outLp = MAKELPARAM(cx, cy);
  return true;
}

struct DesktopInputState {
  std::mutex mu;
  HWND lastHwnd = nullptr;
  POINT lastScreenPt{};
  bool hasLastScreenPt = false;
  BYTE keyState[256] = {};
};

HWND choose_desktop_seed_window(const POINT& screenPt, DesktopInputState* state) {
  CaptureWindowInfo topLevel{};
  if (find_top_level_window_at_point(screenPt, &topLevel) && topLevel.hwnd && IsWindow(topLevel.hwnd)) {
    return topLevel.hwnd;
  }
  if (state) {
    std::lock_guard<std::mutex> lk(state->mu);
    if (state->lastHwnd && IsWindow(state->lastHwnd)) return state->lastHwnd;
  }
  const HWND pointHwnd = WindowFromPoint(screenPt);
  return (pointHwnd && IsWindow(pointHwnd)) ? pointHwnd : nullptr;
}

HWND choose_text_target_window(HWND fallbackHwnd, DesktopInputState* state) {
  if (state) {
    std::lock_guard<std::mutex> lk(state->mu);
    if (state->lastHwnd && IsWindow(state->lastHwnd)) return state->lastHwnd;
  }
  if (fallbackHwnd && IsWindow(fallbackHwnd)) return fallbackHwnd;
  const HWND foreground = GetForegroundWindow();
  return (foreground && IsWindow(foreground)) ? foreground : nullptr;
}

bool resolve_message_target_at_screen_point(HWND seedHwnd, const POINT& screenPt, HWND* outHwnd,
                                            POINT* outClientPt) {
  if (!outHwnd || !outClientPt) return false;
  HWND hwnd = seedHwnd;
  if (!hwnd || !IsWindow(hwnd)) hwnd = WindowFromPoint(screenPt);
  if (!hwnd || !IsWindow(hwnd)) return false;

  POINT clientPt = screenPt;
  if (!ScreenToClient(hwnd, &clientPt)) return false;

  HWND target = hwnd;
  POINT targetClient = clientPt;
  for (int depth = 0; depth < 16; ++depth) {
    HWND child = ChildWindowFromPointEx(target, targetClient,
                                        CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
    if (!child || child == target) break;
    POINT nextClient = screenPt;
    if (!ScreenToClient(child, &nextClient)) break;
    target = child;
    targetClient = nextClient;
  }

  *outHwnd = target;
  *outClientPt = targetClient;
  return true;
}

bool map_input_to_primary_monitor_point(int32_t x, int32_t y, uint32_t inputW, uint32_t inputH,
                                        POINT* outPt) {
  if (!outPt) return false;
  HMONITOR primaryMon = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO monInfo{};
  monInfo.cbSize = sizeof(monInfo);
  if (!GetMonitorInfo(primaryMon, &monInfo)) {
    monInfo.rcMonitor.left = 0;
    monInfo.rcMonitor.top = 0;
    monInfo.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
    monInfo.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
  }
  const int monW = std::max<int>(1, monInfo.rcMonitor.right - monInfo.rcMonitor.left);
  const int monH = std::max<int>(1, monInfo.rcMonitor.bottom - monInfo.rcMonitor.top);
  outPt->x = monInfo.rcMonitor.left + scale_input_coord(x, inputW, monW);
  outPt->y = monInfo.rcMonitor.top + scale_input_coord(y, inputH, monH);
  return true;
}

void remember_input_target(DesktopInputState* state, HWND hwnd, const POINT& screenPt) {
  if (!state) return;
  std::lock_guard<std::mutex> lk(state->mu);
  state->lastHwnd = hwnd;
  state->lastScreenPt = screenPt;
  state->hasLastScreenPt = true;
}

bool resolve_desktop_input_target(const POINT& screenPt, DesktopInputState* state, HWND* outHwnd = nullptr,
                                  POINT* outClientPt = nullptr, std::string* resolvedTargetOut = nullptr) {
  const HWND seed = choose_desktop_seed_window(screenPt, state);
  HWND resolved = nullptr;
  POINT clientPt{};
  if (!resolve_message_target_at_screen_point(seed, screenPt, &resolved, &clientPt)) return false;
  remember_input_target(state, resolved, screenPt);
  if (outHwnd) *outHwnd = resolved;
  if (outClientPt) *outClientPt = clientPt;
  if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(resolved);
  return true;
}

DWORD mouse_vk_to_sendinput_flag(uint16_t kind, uint32_t vk) {
  if (kind == 2) {
    if (vk == VK_RBUTTON) return MOUSEEVENTF_RIGHTDOWN;
    if (vk == VK_MBUTTON) return MOUSEEVENTF_MIDDLEDOWN;
    return MOUSEEVENTF_LEFTDOWN;
  }
  if (kind == 3) {
    if (vk == VK_RBUTTON) return MOUSEEVENTF_RIGHTUP;
    if (vk == VK_MBUTTON) return MOUSEEVENTF_MIDDLEUP;
    return MOUSEEVENTF_LEFTUP;
  }
  return 0;
}

bool send_desktop_mouse_input(DWORD flags, DWORD mouseData = 0) {
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = flags;
  in.mi.mouseData = mouseData;
  return SendInput(1, &in, sizeof(INPUT)) == 1;
}

enum class InputInjectResult : uint8_t {
  Injected = 0,
  IgnoredMove = 1,
  NoTarget = 2,
  Unsupported = 3,
  Failed = 4,
};

InputInjectResult inject_background_input_event(const ControlInputEventMessage& input,
                                                const CaptureWindowCriteria& explicitCriteria,
                                                const std::atomic<uint64_t>& captureTargetHwnd,
                                                bool desktopMode,
                                                uint32_t inputDomainW,
                                                uint32_t inputDomainH,
                                                DesktopInputState* desktopInputState,
                                                std::string* resolvedTargetOut = nullptr) {
  if (inputDomainW == 0 || inputDomainH == 0) return InputInjectResult::Failed;

  CaptureWindowInfo explicitTarget{};
  const bool explicitTargetEnabled = explicitCriteria.enabled();
  if (explicitTargetEnabled) {
    if (!find_capture_window_input_target(explicitCriteria, &explicitTarget)) return InputInjectResult::NoTarget;
    if (!explicitTarget.hwnd || !IsWindow(explicitTarget.hwnd) || IsIconic(explicitTarget.hwnd)) {
      return InputInjectResult::NoTarget;
    }
    desktopMode = false;
  }

  if (!desktopMode) {
    HWND targetHwnd = explicitTargetEnabled ? explicitTarget.hwnd
                                            : reinterpret_cast<HWND>(static_cast<uintptr_t>(
                                                  captureTargetHwnd.load(std::memory_order_acquire)));
    if (!targetHwnd || !IsWindow(targetHwnd)) return InputInjectResult::NoTarget;
    POINT rootClientPt{};
    LPARAM rootLp = 0;
    if (!make_scaled_client_lparam(targetHwnd, input.x, input.y, inputDomainW, inputDomainH, &rootLp, &rootClientPt)) {
      return InputInjectResult::Failed;
    }
    POINT screenPt = rootClientPt;
    if (!ClientToScreen(targetHwnd, &screenPt)) return InputInjectResult::Failed;
    HWND resolvedTargetHwnd = targetHwnd;
    POINT resolvedClientPt = rootClientPt;
    if (!resolve_message_target_at_screen_point(targetHwnd, screenPt, &resolvedTargetHwnd, &resolvedClientPt)) {
      return InputInjectResult::NoTarget;
    }
    if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(resolvedTargetHwnd);
    remember_input_target(desktopInputState, resolvedTargetHwnd, screenPt);
    if (input.kind == 1) {
      if ((input.buttons & 0x7u) == 0) return InputInjectResult::IgnoredMove;
      const WPARAM wp = mouse_button_wparam(static_cast<uint16_t>(input.buttons & 0x7u));
      const LPARAM lp = MAKELPARAM(static_cast<short>(resolvedClientPt.x), static_cast<short>(resolvedClientPt.y));
      return PostMessageW(resolvedTargetHwnd, WM_MOUSEMOVE, wp, lp) ? InputInjectResult::Injected
                                                                    : InputInjectResult::Failed;
    }
    if (input.kind == 2 || input.kind == 3) {
      const UINT msg = mouse_vk_to_message(input.kind, input.keyCode);
      if (msg == 0) return InputInjectResult::Unsupported;
      uint16_t buttons = static_cast<uint16_t>(input.buttons & 0x7u);
      const uint16_t eventMask = mouse_vk_to_mask(input.keyCode);
      if (input.kind == 2) {
        buttons = static_cast<uint16_t>(buttons | eventMask);
      } else if (input.kind == 3) {
        buttons = static_cast<uint16_t>(buttons & static_cast<uint16_t>(~eventMask));
      }
      const WPARAM wp = mouse_button_wparam(buttons);
      const LPARAM lp = MAKELPARAM(static_cast<short>(resolvedClientPt.x), static_cast<short>(resolvedClientPt.y));
      const bool moved = PostMessageW(resolvedTargetHwnd, WM_MOUSEMOVE, wp, lp) != 0;
      const bool clicked = PostMessageW(resolvedTargetHwnd, msg, wp, lp) != 0;
      return (moved && clicked) ? InputInjectResult::Injected : InputInjectResult::Failed;
    }
    if (input.kind == 4) {
      const WPARAM wp =
          MAKEWPARAM(mouse_button_wparam(static_cast<uint16_t>(input.buttons & 0x7u)),
                     static_cast<WORD>(static_cast<SHORT>(input.wheelDelta)));
      const LPARAM screenLp =
          MAKELPARAM(static_cast<short>(screenPt.x), static_cast<short>(screenPt.y));
      return PostMessageW(resolvedTargetHwnd, WM_MOUSEWHEEL, wp, screenLp) ? InputInjectResult::Injected
                                                                           : InputInjectResult::Failed;
    }
    if (input.kind == 5 || input.kind == 6) {
      HWND keyTargetHwnd = choose_text_target_window(targetHwnd, desktopInputState);
      if (!keyTargetHwnd || !IsWindow(keyTargetHwnd)) return InputInjectResult::NoTarget;
      if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(keyTargetHwnd);
      const bool keyUp = (input.kind == 6);
      const UINT msg = keyUp ? WM_KEYUP : WM_KEYDOWN;
      const LPARAM lp = key_event_lparam(input.keyCode, keyUp);
      if (!PostMessageW(keyTargetHwnd, msg, static_cast<WPARAM>(input.keyCode), lp)) {
        return InputInjectResult::Failed;
      }
      if (desktopInputState) {
        std::lock_guard<std::mutex> lk(desktopInputState->mu);
        update_synthetic_keyboard_state(desktopInputState->keyState, input.keyCode, keyUp);
      }
      return InputInjectResult::Injected;
    }
    return InputInjectResult::Unsupported;
  }

  POINT screenPt{};
  if (!map_input_to_primary_monitor_point(input.x, input.y, inputDomainW, inputDomainH, &screenPt)) {
    return InputInjectResult::Failed;
  }
  resolve_desktop_input_target(screenPt, desktopInputState, nullptr, nullptr, resolvedTargetOut);

  if (input.kind == 1) {
    if ((input.buttons & 0x7u) == 0) return InputInjectResult::IgnoredMove;
    return SetCursorPos(screenPt.x, screenPt.y) ? InputInjectResult::Injected : InputInjectResult::Failed;
  }
  if (input.kind == 2 || input.kind == 3) {
    const DWORD mouseFlag = mouse_vk_to_sendinput_flag(input.kind, input.keyCode);
    if (mouseFlag == 0) return InputInjectResult::Unsupported;
    if (!SetCursorPos(screenPt.x, screenPt.y)) return InputInjectResult::Failed;
    return send_desktop_mouse_input(mouseFlag) ? InputInjectResult::Injected : InputInjectResult::Failed;
  }
  if (input.kind == 4) {
    if (!SetCursorPos(screenPt.x, screenPt.y)) return InputInjectResult::Failed;
    return send_desktop_mouse_input(MOUSEEVENTF_WHEEL,
                                    static_cast<DWORD>(static_cast<SHORT>(input.wheelDelta)))
               ? InputInjectResult::Injected
               : InputInjectResult::Failed;
  }
  if (input.kind == 5 || input.kind == 6) {
    HWND targetHwnd = nullptr;
    targetHwnd = choose_text_target_window(targetHwnd, desktopInputState);
    if (!targetHwnd || !IsWindow(targetHwnd)) return InputInjectResult::NoTarget;
    if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(targetHwnd);
    const bool keyUp = (input.kind == 6);
    const UINT msg = keyUp ? WM_KEYUP : WM_KEYDOWN;
    const LPARAM lp = key_event_lparam(input.keyCode, keyUp);
    if (!PostMessageW(targetHwnd, msg, static_cast<WPARAM>(input.keyCode), lp)) {
      return InputInjectResult::Failed;
    }
    if (desktopInputState) {
      std::lock_guard<std::mutex> lk(desktopInputState->mu);
      update_synthetic_keyboard_state(desktopInputState->keyState, input.keyCode, keyUp);
    }
    return InputInjectResult::Injected;
  }
  return InputInjectResult::Unsupported;
}

InputInjectResult apply_input_text_message(const ControlInputTextMessage& text,
                                           const std::atomic<uint64_t>& captureTargetHwnd,
                                           bool desktopMode,
                                           DesktopInputState* desktopInputState,
                                           std::string* resolvedTargetOut = nullptr) {
  if (text.utf16Count == 0 || text.utf16Count > remote60::native_poc::kControlInputTextMaxUtf16) {
    return InputInjectResult::Unsupported;
  }
  HWND fallbackHwnd = nullptr;
  if (!desktopMode) {
    fallbackHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(
        captureTargetHwnd.load(std::memory_order_acquire)));
  }
  HWND targetHwnd = choose_text_target_window(fallbackHwnd, desktopInputState);
  if (!targetHwnd || !IsWindow(targetHwnd)) return InputInjectResult::NoTarget;
  if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(targetHwnd);
  for (uint16_t i = 0; i < text.utf16Count; ++i) {
    const uint16_t ch = text.utf16[i];
    if (ch == 0) continue;
    if (!PostMessageW(targetHwnd, WM_CHAR, static_cast<WPARAM>(ch), 1)) {
      return InputInjectResult::Failed;
    }
  }
  return InputInjectResult::Injected;
}

bool compute_window_client_crop(HWND hwnd, uint32_t frameW, uint32_t frameH, uint32_t* outX,
                                uint32_t* outY, uint32_t* outW, uint32_t* outH) {
  if (!hwnd || frameW < 2 || frameH < 2 || !outX || !outY || !outW || !outH) return false;
  RECT windowRect{};
  RECT clientRect{};
  if (!GetWindowRect(hwnd, &windowRect)) return false;
  if (!GetClientRect(hwnd, &clientRect)) return false;
  POINT tl{clientRect.left, clientRect.top};
  POINT br{clientRect.right, clientRect.bottom};
  if (!ClientToScreen(hwnd, &tl) || !ClientToScreen(hwnd, &br)) return false;
  const int windowW = windowRect.right - windowRect.left;
  const int windowH = windowRect.bottom - windowRect.top;
  const int clientW = br.x - tl.x;
  const int clientH = br.y - tl.y;
  if (windowW <= 0 || windowH <= 0 || clientW <= 1 || clientH <= 1) return false;
  const double scaleX = static_cast<double>(frameW) / static_cast<double>(windowW);
  const double scaleY = static_cast<double>(frameH) / static_cast<double>(windowH);
  int cropX = static_cast<int>((tl.x - windowRect.left) * scaleX + 0.5);
  int cropY = static_cast<int>((tl.y - windowRect.top) * scaleY + 0.5);
  int cropW = static_cast<int>(clientW * scaleX + 0.5);
  int cropH = static_cast<int>(clientH * scaleY + 0.5);
  cropX = std::clamp(cropX, 0, static_cast<int>(frameW) - 1);
  cropY = std::clamp(cropY, 0, static_cast<int>(frameH) - 1);
  cropW = std::clamp(cropW, 1, static_cast<int>(frameW) - cropX);
  cropH = std::clamp(cropH, 1, static_cast<int>(frameH) - cropY);
  *outX = static_cast<uint32_t>(cropX);
  *outY = static_cast<uint32_t>(cropY);
  *outW = static_cast<uint32_t>(cropW);
  *outH = static_cast<uint32_t>(cropH);
  return (*outW >= 2 && *outH >= 2);
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForPrimaryMonitor(
    HWND preferredWindow = nullptr, const char* preferredSource = nullptr) {
  auto interop = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};

  auto logHresult = [](const char* label, const winrt::hresult_error& e) {
    std::cerr << "[native-video-host] " << label << ", hr=0x" << std::hex
              << static_cast<unsigned long>(e.code()) << std::dec << "\n";
  };

  auto createForMonitor = [&](HMONITOR monitor, const char* source) {
    if (!monitor) return false;
    if (!item) {
      try {
        interop->CreateForMonitor(monitor, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                                 winrt::put_abi(item));
      } catch (const winrt::hresult_error& e) {
        logHresult("CreateForMonitor failed", e);
        item = nullptr;
      }
    }
    if (!item) {
      std::cerr << "[native-video-host] CreateForMonitor failed, source=" << source << "\n";
    } else {
      std::cout << "[native-video-host] capture item source=" << source << "\n";
    }
    return static_cast<bool>(item);
  };

  auto createForWindow = [&](HWND hwnd, const char* source) {
    if (!hwnd) return false;
    if (!item) {
      try {
        interop->CreateForWindow(hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                                winrt::put_abi(item));
      } catch (const winrt::hresult_error& e) {
        logHresult("CreateForWindow failed", e);
        item = nullptr;
      }
    }
    if (!item) {
      std::cerr << "[native-video-host] CreateForWindow failed, source=" << source << "\n";
    } else {
      std::cout << "[native-video-host] capture item source=" << source << "\n";
    }
    return static_cast<bool>(item);
  };

  if (preferredWindow) {
    createForWindow(preferredWindow, preferredSource ? preferredSource : "CreateForWindow(preferred)");
    if (item) return item;
  }

  HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);

  createForMonitor(monitor, "MonitorFromWindow(GetDesktopWindow())");
  if (item) return item;

  monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  createForMonitor(monitor, "MonitorFromPoint(0,0)");
  if (item) return item;

  struct EnumFirstMonitorState {
    HMONITOR monitor = nullptr;
  };
  EnumFirstMonitorState enumState{};
  auto enumCb = [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
    auto* state = reinterpret_cast<EnumFirstMonitorState*>(lParam);
    if (state && !state->monitor) {
      state->monitor = hMonitor;
    }
    return TRUE;
  };
  EnumDisplayMonitors(nullptr, nullptr, enumCb, reinterpret_cast<LPARAM>(&enumState));
  if (enumState.monitor) {
    createForMonitor(enumState.monitor, "EnumDisplayMonitors(first)");
    if (item) return item;
  }

  createForWindow(GetForegroundWindow(), "CreateForWindow(GetForegroundWindow)");
  if (item) return item;

  createForWindow(GetDesktopWindow(), "CreateForWindow(GetDesktopWindow())");
  if (item) return item;

  createForWindow(GetShellWindow(), "CreateForWindow(GetShellWindow())");
  if (item) return item;

  createForWindow(GetConsoleWindow(), "CreateForWindow(GetConsoleWindow())");
  if (item) return item;

  struct EnumCaptureWindowState {
    HWND hwnd = nullptr;
  };
  EnumCaptureWindowState enumWindowState{};
  auto enumWindowCb = [](HWND hwnd, LPARAM lParam) -> BOOL {
    if (!hwnd) return TRUE;
    auto* state = reinterpret_cast<EnumCaptureWindowState*>(lParam);
    if (!state->hwnd) {
      const LONG style = GetWindowLongPtr(hwnd, GWL_STYLE);
      if ((style & WS_VISIBLE) && (style & WS_OVERLAPPEDWINDOW)) {
        state->hwnd = hwnd;
      }
    }
    return state->hwnd ? FALSE : TRUE;
  };
  EnumWindows(enumWindowCb, reinterpret_cast<LPARAM>(&enumWindowState));
  if (enumWindowState.hwnd) {
    createForWindow(enumWindowState.hwnd, "EnumWindows(first visible overlapped)");
    if (item) return item;
  }

  HWND shellWorkerW = FindWindowW(L"Progman", nullptr);
  if (shellWorkerW) {
    createForWindow(shellWorkerW, "FindWindowW(Progman)");
    if (item) return item;
  }

  return item;
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> SurfaceToTexture(
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface) {
  winrt::com_ptr<::IInspectable> inspectable = surface.as<::IInspectable>();
  Microsoft::WRL::ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
  winrt::check_hresult(inspectable->QueryInterface(IID_PPV_ARGS(&access)));
  Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
  winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),
                                            reinterpret_cast<void**>(tex.GetAddressOf())));
  return tex;
}

struct WinsockScope {
  bool ok = false;
  WinsockScope() {
    WSADATA wsa{};
    ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
  }
  ~WinsockScope() {
    if (ok) WSACleanup();
  }
};

struct Args {
  uint16_t bindPort = 43000;
  uint16_t controlPort = 0;
  uint32_t tcpSendBufKb = 0;
  uint32_t udpMtu = 1200;
  uint32_t traceEvery = 0;
  uint32_t traceMax = 0;
  uint32_t inputLogEvery = 120;
  bool enableInputInjection = false;
  std::string inputInjectionMode = "background_message";
  uint32_t inputTargetPid = 0;
  std::string inputTargetProcess;
  std::string inputTargetTitle;
  std::string transport;
  std::string codec = "raw";
  uint32_t fps = 30;
  uint32_t seconds = 0;  // 0: infinite
  uint32_t bitrate = 1100000;
  uint32_t keyint = 15;
  uint32_t encodeWidth = 0;
  uint32_t encodeHeight = 0;
  uint32_t captureWindowPid = 0;
  std::string captureWindowProcess;
  std::string captureWindowTitle;
  bool captureWindowClientOnly = false;
  uint32_t captureWindowRebindIntervalMs = 1000;
};

bool parse_u32(const char* s, uint32_t* out) {
  if (!s || !out) return false;
  char* end = nullptr;
  const unsigned long v = std::strtoul(s, &end, 10);
  if (!end || *end != '\0') return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

bool env_truthy(const char* key) {
  if (!key) return false;
  const char* v = std::getenv(key);
  if (!v) return false;
  const std::string s = v;
  return s == "1" || s == "true" || s == "TRUE" || s == "on" || s == "ON";
}

uint32_t env_u32_clamped(const char* key, uint32_t fallback, uint32_t minValue, uint32_t maxValue) {
  if (!key) return fallback;
  const char* raw = std::getenv(key);
  if (!raw) return fallback;
  uint32_t parsed = 0;
  if (!parse_u32(raw, &parsed)) return fallback;
  return std::clamp<uint32_t>(parsed, minValue, maxValue);
}

Args parse_args(int argc, char** argv) {
  Args a;
  std::string configPath;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    if (k == "--config" && i + 1 < argc) {
      configPath = argv[++i];
    }
  }
  if (!configPath.empty()) {
    std::string jsonText;
    std::string errorText;
    if (!json_profile::load_json_text_file(configPath, &jsonText, &errorText)) {
      std::cerr << "[native-video-host] failed to load --config file: " << configPath
                << " (" << errorText << ")\n";
    } else {
      uint32_t v = 0;
      std::string s;
      bool b = false;
      if (json_profile::json_get_u32(jsonText, "port", &v)) {
        a.bindPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
      }
      if (json_profile::json_get_u32(jsonText, "bindPort", &v)) {
        a.bindPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
      }
      if (json_profile::json_get_u32(jsonText, "controlPort", &v)) {
        a.controlPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
      }
      if (json_profile::json_get_u32(jsonText, "tcpSendBufKb", &v)) a.tcpSendBufKb = v;
      if (json_profile::json_get_u32(jsonText, "udpMtu", &v)) a.udpMtu = clamp_udp_mtu(v);
      if (json_profile::json_get_u32(jsonText, "traceEvery", &v)) a.traceEvery = v;
      if (json_profile::json_get_u32(jsonText, "traceMax", &v)) a.traceMax = v;
      if (json_profile::json_get_u32(jsonText, "inputLogEvery", &v)) {
        a.inputLogEvery = std::max<uint32_t>(1, v);
      }
      if (json_profile::json_get_bool(jsonText, "enableInputInjection", &b)) a.enableInputInjection = b;
      if (json_profile::json_get_string(jsonText, "inputInjectionMode", &s)) a.inputInjectionMode = s;
      if (json_profile::json_get_u32(jsonText, "inputTargetPid", &v)) a.inputTargetPid = v;
      if (json_profile::json_get_string(jsonText, "inputTargetProcess", &s)) a.inputTargetProcess = s;
      if (json_profile::json_get_string(jsonText, "inputTargetTitle", &s)) a.inputTargetTitle = s;
      if (json_profile::json_get_string(jsonText, "codec", &s)) a.codec = s;
      if (json_profile::json_get_string(jsonText, "transport", &s)) a.transport = s;
      if (json_profile::json_get_u32(jsonText, "fps", &v)) a.fps = std::clamp<uint32_t>(v, 1, 120);
      if (json_profile::json_get_u32(jsonText, "seconds", &v)) a.seconds = v;
      if (json_profile::json_get_u32(jsonText, "bitrate", &v)) {
        a.bitrate = std::max<uint32_t>(100000, v);
      }
      if (json_profile::json_get_u32(jsonText, "keyint", &v)) a.keyint = std::max<uint32_t>(1, v);
      if (json_profile::json_get_u32(jsonText, "encodeWidth", &v)) a.encodeWidth = v;
      if (json_profile::json_get_u32(jsonText, "encodeHeight", &v)) a.encodeHeight = v;
      if (json_profile::json_get_u32(jsonText, "captureWindowPid", &v)) a.captureWindowPid = v;
      if (json_profile::json_get_string(jsonText, "captureWindowProcess", &s)) a.captureWindowProcess = s;
      if (json_profile::json_get_string(jsonText, "captureWindowTitle", &s)) a.captureWindowTitle = s;
      if (json_profile::json_get_bool(jsonText, "captureWindowClientOnly", &b)) {
        a.captureWindowClientOnly = b;
      }
      if (json_profile::json_get_u32(jsonText, "captureWindowRebindIntervalMs", &v)) {
        a.captureWindowRebindIntervalMs = std::clamp<uint32_t>(v, 200, 10000);
      }
      json_profile::apply_runtime_env_overrides_from_json(jsonText);
    }
  }
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    if (k == "--config" && i + 1 < argc) {
      ++i;
      continue;
    }
    if (k == "--bind-port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.bindPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--control-port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.controlPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--tcp-sendbuf-kb" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.tcpSendBufKb = v;
    } else if (k == "--udp-mtu" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.udpMtu = clamp_udp_mtu(v);
    } else if (k == "--trace-every" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.traceEvery = v;
    } else if (k == "--trace-max" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.traceMax = v;
    } else if (k == "--input-log-every" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.inputLogEvery = std::max<uint32_t>(1, v);
    } else if (k == "--enable-input-injection") {
      a.enableInputInjection = true;
    } else if (k == "--input-injection-mode" && i + 1 < argc) {
      a.inputInjectionMode = argv[++i];
    } else if (k == "--input-target-pid" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.inputTargetPid = v;
    } else if (k == "--input-target-process" && i + 1 < argc) {
      a.inputTargetProcess = argv[++i];
    } else if (k == "--input-target-title" && i + 1 < argc) {
      a.inputTargetTitle = argv[++i];
    } else if (k == "--codec" && i + 1 < argc) {
      a.codec = argv[++i];
    } else if (k == "--transport" && i + 1 < argc) {
      a.transport = argv[++i];
    } else if (k == "--fps" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.fps = std::clamp<uint32_t>(v, 1, 120);
    } else if (k == "--seconds" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.seconds = v;
    } else if (k == "--bitrate" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.bitrate = std::max<uint32_t>(100000, v);
    } else if (k == "--keyint" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.keyint = std::max<uint32_t>(1, v);
    } else if (k == "--encode-width" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.encodeWidth = v;
    } else if (k == "--encode-height" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.encodeHeight = v;
    } else if (k == "--capture-window-pid" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.captureWindowPid = v;
    } else if (k == "--capture-window-process" && i + 1 < argc) {
      a.captureWindowProcess = argv[++i];
    } else if (k == "--capture-window-title" && i + 1 < argc) {
      a.captureWindowTitle = argv[++i];
    } else if (k == "--capture-window-client-only") {
      a.captureWindowClientOnly = true;
    } else if (k == "--capture-window-rebind-interval-ms" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) {
        a.captureWindowRebindIntervalMs = std::clamp<uint32_t>(v, 200, 10000);
      }
    }
  }
  return a;
}

uint32_t clamp_even_dim(uint32_t v, uint32_t minValue, uint32_t maxValue) {
  if (v < minValue) v = minValue;
  if (v > maxValue) v = maxValue;
  if (v & 1u) {
    if (v < maxValue) {
      ++v;
    } else if (v > minValue) {
      --v;
    }
  }
  return v;
}

void choose_h264_encode_size(const Args& args, uint32_t captureW, uint32_t captureH,
                             uint32_t* outW, uint32_t* outH, bool* outAutoFallback720) {
  if (!outW || !outH || !outAutoFallback720) return;
  *outAutoFallback720 = false;
  uint32_t targetW = captureW;
  uint32_t targetH = captureH;
  if (args.encodeWidth > 0 && args.encodeHeight > 0) {
    targetW = clamp_even_dim(args.encodeWidth, 2, captureW);
    targetH = clamp_even_dim(args.encodeHeight, 2, captureH);
  } else {
    // At low bitrate, avoid encoder queue buildup by auto-falling back toward 720p.
    if (args.bitrate <= 1500000 && (captureW > 1280 || captureH > 720)) {
      const double sx = 1280.0 / static_cast<double>(captureW);
      const double sy = 720.0 / static_cast<double>(captureH);
      const double scale = std::min(sx, sy);
      if (scale > 0.0 && scale < 1.0) {
        targetW = static_cast<uint32_t>(captureW * scale);
        targetH = static_cast<uint32_t>(captureH * scale);
        targetW = clamp_even_dim(targetW, 2, captureW);
        targetH = clamp_even_dim(targetH, 2, captureH);
        *outAutoFallback720 = true;
      }
    }
  }
  *outW = targetW;
  *outH = targetH;
}

void choose_abr_720_size(uint32_t captureW, uint32_t captureH, uint32_t* outW, uint32_t* outH) {
  if (!outW || !outH) return;
  uint32_t targetW = captureW;
  uint32_t targetH = captureH;
  if (captureW > 1280 || captureH > 720) {
    const double sx = 1280.0 / static_cast<double>(captureW);
    const double sy = 720.0 / static_cast<double>(captureH);
    const double scale = std::min(sx, sy);
    if (scale > 0.0 && scale < 1.0) {
      targetW = static_cast<uint32_t>(captureW * scale);
      targetH = static_cast<uint32_t>(captureH * scale);
    }
  }
  targetW = clamp_even_dim(targetW, 2, captureW);
  targetH = clamp_even_dim(targetH, 2, captureH);
  *outW = targetW;
  *outH = targetH;
}

uint32_t estimate_bgra_change_permille(const uint8_t* a, const uint8_t* b, size_t sizeBytes,
                                       uint32_t sampleTarget) {
  if (!a || !b || sizeBytes < 4) return 1000;
  const size_t pixels = sizeBytes / 4;
  if (pixels == 0) return 1000;
  const size_t stridePixels = std::max<size_t>(1, pixels / std::max<uint32_t>(1, sampleTarget));
  uint64_t diffSum = 0;
  uint64_t sampleCount = 0;
  for (size_t i = 0; i < pixels; i += stridePixels) {
    const size_t idx = i * 4;
    const int db = std::abs(static_cast<int>(a[idx + 0]) - static_cast<int>(b[idx + 0]));
    const int dg = std::abs(static_cast<int>(a[idx + 1]) - static_cast<int>(b[idx + 1]));
    const int dr = std::abs(static_cast<int>(a[idx + 2]) - static_cast<int>(b[idx + 2]));
    diffSum += static_cast<uint64_t>((db + dg + dr) / 3);
    ++sampleCount;
  }
  if (sampleCount == 0) return 1000;
  const uint64_t avgDiff = diffSum / sampleCount;  // 0..255
  const uint64_t permille = (avgDiff * 1000ULL) / 255ULL;
  return static_cast<uint32_t>(std::min<uint64_t>(permille, 1000ULL));
}

bool resize_bgra_bilinear(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t srcStride,
                          uint32_t dstW, uint32_t dstH, std::vector<uint8_t>* outBgra) {
  if (!src || srcW == 0 || srcH == 0 || srcStride < (srcW * 4) || dstW == 0 || dstH == 0 || !outBgra) {
    return false;
  }
  outBgra->resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
  auto* dst = outBgra->data();
  const uint64_t xScale =
      (dstW > 1) ? ((static_cast<uint64_t>(srcW - 1) << 16) / static_cast<uint64_t>(dstW - 1)) : 0;
  const uint64_t yScale =
      (dstH > 1) ? ((static_cast<uint64_t>(srcH - 1) << 16) / static_cast<uint64_t>(dstH - 1)) : 0;
  for (uint32_t y = 0; y < dstH; ++y) {
    const uint32_t srcYFixed = static_cast<uint32_t>(static_cast<uint64_t>(y) * yScale);
    const uint32_t y0 = std::min<uint32_t>(srcH - 1, srcYFixed >> 16);
    const uint32_t y1 = std::min<uint32_t>(srcH - 1, y0 + 1);
    const uint32_t wy = (srcYFixed & 0xFFFFu) >> 8;
    const uint32_t invWy = 256u - wy;
    const uint8_t* srcRow0 = src + static_cast<size_t>(y0) * srcStride;
    const uint8_t* srcRow1 = src + static_cast<size_t>(y1) * srcStride;
    uint8_t* dstRow = dst + static_cast<size_t>(y) * dstW * 4;
    for (uint32_t x = 0; x < dstW; ++x) {
      const uint32_t srcXFixed = static_cast<uint32_t>(static_cast<uint64_t>(x) * xScale);
      const uint32_t x0 = std::min<uint32_t>(srcW - 1, srcXFixed >> 16);
      const uint32_t x1 = std::min<uint32_t>(srcW - 1, x0 + 1);
      const uint32_t wx = (srcXFixed & 0xFFFFu) >> 8;
      const uint32_t invWx = 256u - wx;

      const uint8_t* p00 = srcRow0 + static_cast<size_t>(x0) * 4;
      const uint8_t* p10 = srcRow0 + static_cast<size_t>(x1) * 4;
      const uint8_t* p01 = srcRow1 + static_cast<size_t>(x0) * 4;
      const uint8_t* p11 = srcRow1 + static_cast<size_t>(x1) * 4;
      uint8_t* outPx = dstRow + static_cast<size_t>(x) * 4;
      for (int c = 0; c < 4; ++c) {
        const uint32_t top = p00[c] * invWx + p10[c] * wx;
        const uint32_t bottom = p01[c] * invWx + p11[c] * wx;
        const uint32_t blended = (top * invWy + bottom * wy + 32768u) >> 16;
        outPx[c] = static_cast<uint8_t>(blended);
      }
    }
  }
  return true;
}

struct GpuBgraScaler {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> srcTexture;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> dstTexture;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> dstStaging;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
  std::mutex* d3dMutex = nullptr;
  uint32_t srcW = 0;
  uint32_t srcH = 0;
  uint32_t dstW = 0;
  uint32_t dstH = 0;
  bool initialized = false;

  bool initialize(ID3D11Device* d, ID3D11DeviceContext* c, std::mutex* mu) {
    if (!d || !c) return false;
    device = d;
    context = c;
    d3dMutex = mu;
    if (FAILED(device.As(&videoDevice)) || !videoDevice) return false;
    if (FAILED(context.As(&videoContext)) || !videoContext) return false;
    initialized = true;
    return true;
  }

  bool ensure_resources(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH) {
    if (!initialized || !videoDevice || !videoContext) return false;
    if (inW == 0 || inH == 0 || outW == 0 || outH == 0) return false;
    if (srcTexture && dstTexture && dstStaging && inputView && outputView &&
        srcW == inW && srcH == inH && dstW == outW && dstH == outH) {
      return true;
    }

    enumerator.Reset();
    processor.Reset();
    srcTexture.Reset();
    dstTexture.Reset();
    dstStaging.Reset();
    inputView.Reset();
    outputView.Reset();

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputWidth = inW;
    desc.InputHeight = inH;
    desc.OutputWidth = outW;
    desc.OutputHeight = outH;
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&desc, &enumerator)) || !enumerator) return false;

    UINT formatSupport = 0;
    if (FAILED(enumerator->CheckVideoProcessorFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, &formatSupport))) {
      return false;
    }
    const UINT requiredFormatSupport =
        D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT;
    if ((formatSupport & requiredFormatSupport) != requiredFormatSupport) return false;

    if (FAILED(videoDevice->CreateVideoProcessor(enumerator.Get(), 0, &processor)) || !processor) return false;

    D3D11_TEXTURE2D_DESC srcDesc{};
    srcDesc.Width = inW;
    srcDesc.Height = inH;
    srcDesc.MipLevels = 1;
    srcDesc.ArraySize = 1;
    srcDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srcDesc.SampleDesc.Count = 1;
    srcDesc.Usage = D3D11_USAGE_DEFAULT;
    srcDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(device->CreateTexture2D(&srcDesc, nullptr, &srcTexture)) || !srcTexture) return false;

    D3D11_TEXTURE2D_DESC dstDesc = srcDesc;
    dstDesc.Width = outW;
    dstDesc.Height = outH;
    if (FAILED(device->CreateTexture2D(&dstDesc, nullptr, &dstTexture)) || !dstTexture) return false;

    D3D11_TEXTURE2D_DESC stagingDesc = dstDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &dstStaging)) || !dstStaging) return false;

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inViewDesc{};
    inViewDesc.FourCC = 0;
    inViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inViewDesc.Texture2D.MipSlice = 0;
    inViewDesc.Texture2D.ArraySlice = 0;
    if (FAILED(videoDevice->CreateVideoProcessorInputView(
            srcTexture.Get(), enumerator.Get(), &inViewDesc, &inputView)) || !inputView) {
      return false;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outViewDesc{};
    outViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outViewDesc.Texture2D.MipSlice = 0;
    if (FAILED(videoDevice->CreateVideoProcessorOutputView(
            dstTexture.Get(), enumerator.Get(), &outViewDesc, &outputView)) || !outputView) {
      return false;
    }

    srcW = inW;
    srcH = inH;
    dstW = outW;
    dstH = outH;
    return true;
  }

  bool scale(const uint8_t* src, uint32_t inW, uint32_t inH, uint32_t srcStride,
             uint32_t outW, uint32_t outH, std::vector<uint8_t>* outBgra) {
    if (!src || !outBgra || srcStride < inW * 4) return false;
    std::lock_guard<std::mutex> lk(*d3dMutex);
    if (!ensure_resources(inW, inH, outW, outH)) return false;

    context->UpdateSubresource(srcTexture.Get(), 0, nullptr, src, srcStride, 0);

    RECT srcRect{};
    srcRect.left = 0;
    srcRect.top = 0;
    srcRect.right = static_cast<LONG>(inW);
    srcRect.bottom = static_cast<LONG>(inH);
    RECT dstRect{};
    dstRect.left = 0;
    dstRect.top = 0;
    dstRect.right = static_cast<LONG>(outW);
    dstRect.bottom = static_cast<LONG>(outH);

    videoContext->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE, &dstRect);
    videoContext->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, &srcRect);
    videoContext->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &dstRect);
    videoContext->VideoProcessorSetStreamFrameFormat(
        processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();
    if (FAILED(videoContext->VideoProcessorBlt(processor.Get(), outputView.Get(), 0, 1, &stream))) {
      return false;
    }

    context->CopyResource(dstStaging.Get(), dstTexture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(dstStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    outBgra->resize(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4);
    const uint32_t outStride = outW * 4;
    auto* dst = outBgra->data();
    const auto* mappedData = reinterpret_cast<const uint8_t*>(mapped.pData);
    for (uint32_t row = 0; row < outH; ++row) {
      std::memcpy(dst + static_cast<size_t>(row) * outStride,
                  mappedData + static_cast<size_t>(row) * mapped.RowPitch, outStride);
    }
    context->Unmap(dstStaging.Get(), 0);
    return true;
  }
};

bool send_all(SOCKET s, const void* data, size_t len) {
  const char* p = reinterpret_cast<const char*>(data);
  size_t sent = 0;
  while (sent < len) {
    const int n = send(s, p + sent, static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

struct SendPathStats {
  uint64_t headerUs = 0;
  uint64_t payloadUs = 0;
  uint64_t headerCallCount = 0;
  uint64_t payloadCallCount = 0;
  uint64_t payloadChunkCount = 0;
  uint64_t payloadChunkMaxUs = 0;
};

bool send_all_timed(SOCKET s, const void* data, size_t len, uint64_t* outUs,
                    uint64_t* outCallCount) {
  const char* p = reinterpret_cast<const char*>(data);
  size_t sent = 0;
  uint64_t calls = 0;
  const uint64_t startUs = qpc_now_us();
  while (sent < len) {
    const uint64_t callStartUs = qpc_now_us();
    const int n = send(s, p + sent, static_cast<int>(len - sent), 0);
    const uint64_t callDoneUs = qpc_now_us();
    if (n <= 0) return false;
    ++calls;
    sent += static_cast<size_t>(n);
  }
  const uint64_t doneUs = qpc_now_us();
  if (outUs) *outUs = (doneUs >= startUs) ? (doneUs - startUs) : 0;
  if (outCallCount) *outCallCount = calls;
  return true;
}

bool recv_all(SOCKET s, void* out, size_t len) {
  auto* p = reinterpret_cast<uint8_t*>(out);
  size_t got = 0;
  while (got < len) {
    const int n = recv(s, reinterpret_cast<char*>(p + got), static_cast<int>(len - got), 0);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

bool recv_discard(SOCKET s, size_t len) {
  std::vector<uint8_t> scratch(1024);
  size_t left = len;
  while (left > 0) {
    const size_t chunk = std::min(left, scratch.size());
    if (!recv_all(s, scratch.data(), chunk)) return false;
    left -= chunk;
  }
  return true;
}

bool send_udp_chunks(SOCKET s, const sockaddr_in& peer, const uint8_t* payload, size_t payloadSize,
                     const UdpVideoChunkHeader& baseHeader, uint32_t mtuBytes) {
  if (!payload || payloadSize == 0 || s == INVALID_SOCKET) return false;
  const uint32_t safeMtu = clamp_udp_mtu(mtuBytes);
  if (safeMtu <= sizeof(UdpVideoChunkHeader)) return false;
  const uint32_t maxChunk = safeMtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  std::vector<uint8_t> datagram(safeMtu);
  size_t offset = 0;

  while (offset < payloadSize) {
    const uint32_t chunkSize = static_cast<uint32_t>(std::min<size_t>(maxChunk, payloadSize - offset));
    UdpVideoChunkHeader h = baseHeader;
    h.chunkOffset = static_cast<uint32_t>(offset);
    h.chunkSize = chunkSize;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u));
    if (offset == 0) h.flags |= 0x2u;
    if (offset + chunkSize >= payloadSize) h.flags |= 0x4u;
    std::memcpy(datagram.data(), &h, sizeof(h));
    std::memcpy(datagram.data() + sizeof(h), payload + offset, chunkSize);
    const int n = sendto(s, reinterpret_cast<const char*>(datagram.data()),
                         static_cast<int>(sizeof(h) + chunkSize), 0,
                         reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    if (n <= 0) return false;
    offset += chunkSize;
  }
  return true;
}

bool send_udp_chunks_timed(SOCKET s, const sockaddr_in& peer, const uint8_t* payload, size_t payloadSize,
                          const UdpVideoChunkHeader& baseHeader, uint32_t mtuBytes,
                          SendPathStats* stats) {
  if (!payload || payloadSize == 0 || s == INVALID_SOCKET) return false;
  const uint64_t startUs = qpc_now_us();
  const uint32_t safeMtu = clamp_udp_mtu(mtuBytes);
  if (safeMtu <= sizeof(UdpVideoChunkHeader)) return false;
  const uint32_t maxChunk = safeMtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  std::vector<uint8_t> datagram(safeMtu);
  size_t offset = 0;
  if (!stats) {
    return send_udp_chunks(s, peer, payload, payloadSize, baseHeader, mtuBytes);
  }

  while (offset < payloadSize) {
    const uint32_t chunkSize = static_cast<uint32_t>(std::min<size_t>(maxChunk, payloadSize - offset));
    UdpVideoChunkHeader h = baseHeader;
    h.chunkOffset = static_cast<uint32_t>(offset);
    h.chunkSize = chunkSize;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u));
    if (offset == 0) h.flags |= 0x2u;
    if (offset + chunkSize >= payloadSize) h.flags |= 0x4u;
    std::memcpy(datagram.data(), &h, sizeof(h));
    std::memcpy(datagram.data() + sizeof(h), payload + offset, chunkSize);

    const uint64_t callStartUs = qpc_now_us();
    const int n = sendto(s, reinterpret_cast<const char*>(datagram.data()),
                         static_cast<int>(sizeof(h) + chunkSize), 0,
                         reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    const uint64_t callDoneUs = qpc_now_us();
    if (n <= 0) return false;
    ++stats->payloadChunkCount;
    ++stats->payloadCallCount;
    const uint64_t callUs = (callDoneUs >= callStartUs) ? (callDoneUs - callStartUs) : 0;
    if (callUs > stats->payloadChunkMaxUs) stats->payloadChunkMaxUs = callUs;
    stats->payloadUs += callUs;
    offset += chunkSize;
  }
  const uint64_t doneUs = qpc_now_us();
  stats->payloadUs = (doneUs >= startUs) ? (doneUs - startUs) : stats->payloadUs;
  return true;
}

struct FrameState {
  std::mutex mu;
  std::condition_variable cv;
  uint64_t version = 0;
  uint32_t seq = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint64_t streamGeneration = 0;
  uint64_t captureUs = 0;
  uint64_t callbackUs = 0;
  uint64_t callbackIntervalUs = 0;
  uint64_t captureAgeAtCallbackUs = 0;
  uint64_t captureClockSkewUs = 0;
  uint64_t queuePushUs = 0;
  uint64_t captureIntervalUs = 0;
  std::shared_ptr<std::vector<uint8_t>> payload;
};

}  // namespace

int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);

  const Args args = parse_args(argc, argv);
  const InputInjectionMode configuredInputInjectionMode = parse_input_injection_mode(args.inputInjectionMode);
  const bool inputInjectionEnabled =
      args.enableInputInjection &&
      (configuredInputInjectionMode == InputInjectionMode::BackgroundMessage) &&
      !kInputPolicyForceBlock;
  const bool useRaw = (args.codec == "raw");
  const bool useH264 = (args.codec == "h264");
  const bool guardStaleEncoded = env_truthy("REMOTE60_NATIVE_GUARD_STALE_ENCODED");
  const bool noPacingH264 = env_truthy("REMOTE60_NATIVE_H264_NO_PACING");
  const bool guardStalePreEncode = env_truthy("REMOTE60_NATIVE_GUARD_STALE_PREENCODE");
  const bool abrEnabled = useH264 && !env_truthy("REMOTE60_NATIVE_ABR_DISABLE");
  const bool abrQualityFirst = env_truthy("REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST");
  const bool m9Enabled = useH264 && env_truthy("REMOTE60_NATIVE_M9_ENABLE");
  const bool m9Apply = m9Enabled && env_truthy("REMOTE60_NATIVE_M9_APPLY");
  const uint32_t m9CooldownSec = env_u32_clamped("REMOTE60_NATIVE_M9_COOLDOWN_SEC", 4, 1, 60);
  const uint32_t m9DownRequireSec = env_u32_clamped("REMOTE60_NATIVE_M9_DOWN_REQUIRE_SEC", 2, 1, 20);
  const uint32_t m9UpRequireSec = env_u32_clamped("REMOTE60_NATIVE_M9_UP_REQUIRE_SEC", 8, 1, 60);
  const uint32_t m9DecodedFpsFloorX100 = env_u32_clamped("REMOTE60_NATIVE_M9_DECODED_FPS_FLOOR_X100", 2000, 500, 12000);
  const uint32_t m9DecodedFpsRecoverX100 = env_u32_clamped(
      "REMOTE60_NATIVE_M9_DECODED_FPS_RECOVER_X100", 2500, 500, 12000);
  const uint32_t m9QueueDepthHighFrames = env_u32_clamped("REMOTE60_NATIVE_M9_QUEUE_DEPTH_HIGH_FRAMES", 4, 1, 120);
  const uint32_t m9QueueDepthLowFrames = env_u32_clamped("REMOTE60_NATIVE_M9_QUEUE_DEPTH_LOW_FRAMES", 1, 0, 120);
  const uint32_t m9UdpDropPmHigh = env_u32_clamped("REMOTE60_NATIVE_M9_UDP_DROP_PM_HIGH", 120, 1, 1000);
  const uint32_t m9UdpDropPmLow = env_u32_clamped("REMOTE60_NATIVE_M9_UDP_DROP_PM_LOW", 30, 0, 1000);
  const uint32_t m9LatencyHighUs = env_u32_clamped("REMOTE60_NATIVE_M9_LATENCY_HIGH_US", 140000, 10000, 1000000);
  const uint32_t m9LatencyLowUs = env_u32_clamped("REMOTE60_NATIVE_M9_LATENCY_LOW_US", 90000, 10000, 1000000);
  const uint32_t m9TailHighUs = env_u32_clamped("REMOTE60_NATIVE_M9_TAIL_HIGH_US", 110000, 10000, 1000000);
  const uint32_t m9TailLowUs = env_u32_clamped("REMOTE60_NATIVE_M9_TAIL_LOW_US", 70000, 10000, 1000000);
  const bool frameGatingEnabled = useH264 && !env_truthy("REMOTE60_NATIVE_FRAME_GATING_DISABLE");
  const uint32_t frameGatingStaticFps = env_u32_clamped(
      "REMOTE60_NATIVE_STATIC_SCENE_FPS", kFrameGatingStaticFpsDefault, 1, 30);
  const uint32_t frameGatingStaticThresholdPermille = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_STATIC_THRESHOLD_PM",
      kFrameGatingStaticThresholdPermilleDefault, 1, 400);
  const uint32_t frameGatingMotionThresholdPermille = std::max<uint32_t>(
      frameGatingStaticThresholdPermille + 1,
      env_u32_clamped("REMOTE60_NATIVE_FRAME_GATING_MOTION_THRESHOLD_PM",
                      kFrameGatingMotionThresholdPermilleDefault, 2, 500));
  const uint32_t frameGatingEnterFrames = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_ENTER_FRAMES", kFrameGatingEnterFramesDefault, 1, 120);
  const uint32_t frameGatingExitFrames = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_EXIT_FRAMES", kFrameGatingExitFramesDefault, 1, 30);
  const uint32_t frameGatingSampleTarget = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_SAMPLE_TARGET", kFrameGatingSampleTargetDefault, 128, 16384);
  const uint32_t keyReqMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_MIN_INTERVAL_US", kKeyReqMinIntervalUsDefault, 10000, 1000000);
  const uint32_t keyReqTokenRefillUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_TOKEN_REFILL_US", kKeyReqTokenRefillUsDefault, 10000, 2000000);
  const uint32_t keyReqTokenCapacity = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_TOKEN_CAPACITY", kKeyReqTokenCapacityDefault, 1, 16);
  const uint32_t captureInputMinPushPerSec = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_INPUT_MIN_PUSH_PER_SEC", kCaptureInputMinPushPerSecDefault, 1, 120);
  const uint32_t captureInputStallConsecutiveSec = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_INPUT_STALL_SEC", kCaptureInputStallConsecutiveSecDefault, 1, 30);
  const uint32_t captureInputStallWarmupSec = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_INPUT_STALL_WARMUP_SEC", kCaptureInputStallWarmupSecDefault, 0, 60);
  const uint32_t captureStallKeepaliveIntervalUsOverride = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_STALL_KEEPALIVE_INTERVAL_US", 0, 0,
      static_cast<uint32_t>(kCaptureStallKeepaliveIntervalUs));
  const uint32_t queueWaitTimeoutUsOverride = env_u32_clamped(
      "REMOTE60_NATIVE_QUEUE_WAIT_TIMEOUT_US", 0, 0,
      static_cast<uint32_t>(kQueueWaitTimeoutUsDefault));
  const bool gpuScalerRequested = useH264 && !env_truthy("REMOTE60_NATIVE_DISABLE_GPU_SCALER");
  int captureFramePoolBuffers = kCaptureFramePoolBuffersDefault;
  if (const char* poolEnv = std::getenv("REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS")) {
    const int requested = std::atoi(poolEnv);
    if (requested >= 1 && requested <= 4) {
      captureFramePoolBuffers = requested;
    }
  }
  const bool encodedExperimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  const std::string encoderTuneMode = [&]() {
    const char* raw = std::getenv("REMOTE60_NATIVE_ENCODER_TUNE_MODE");
    if (!raw || !*raw) return std::string("low_latency");
    return ascii_lower(trim_ascii(std::string(raw)));
  }();

  if (!useRaw && !useH264) {
    std::cerr << "[native-video-host] unsupported codec: " << args.codec << " (supported: raw,h264)\n";
    return 11;
  }
  if (useH264 && !encodedExperimentEnabled) {
    std::cerr << "[native-video-host] unsupported codec: " << args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 11;
  }

  WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-host] WSAStartup failed\n";
    return 1;
  }
  std::string effectiveTransport = args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = useH264 ? "udp" : "tcp";
  }
  VideoTransport transport = VideoTransport::Tcp;
  if (!parse_video_transport(effectiveTransport, &transport)) {
    std::cerr << "[native-video-host] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 15;
  }
  if (transport == VideoTransport::Udp && useRaw) {
    std::cerr << "[native-video-host] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 16;
  }

  std::cout << "[native-video-host] waiting client bindPort=" << args.bindPort
            << " transport=" << video_transport_name(transport)
            << " fps=" << args.fps;
  if (useH264) std::cout << " bitrate=" << args.bitrate;
  std::cout << " seconds=" << args.seconds << "\n";
  if (useH264) {
    std::cout << "[native-video-host] h264 pacing=" << (noPacingH264 ? "off" : "on")
              << " stalePreEncodeGuard=" << (guardStalePreEncode ? 1 : 0)
              << " capturePoolBuffers=" << captureFramePoolBuffers
              << " encoderTuneMode=" << encoderTuneMode
              << " abr=" << (abrEnabled ? "on" : "off")
              << " abrMode=" << (abrQualityFirst ? "quality-first" : "default")
              << " frameGating=" << (frameGatingEnabled ? "on" : "off")
              << " staticSceneFps=" << frameGatingStaticFps
              << " gatingStaticPm=" << frameGatingStaticThresholdPermille
              << " gatingMotionPm=" << frameGatingMotionThresholdPermille
              << " m9=" << (m9Enabled ? "on" : "off")
              << " m9Mode=" << (m9Apply ? "apply" : "dry-run")
              << " keyReqMinUs=" << keyReqMinIntervalUs
              << " keyReqBucketCap=" << keyReqTokenCapacity
              << " captureInputMinPushPerSec=" << captureInputMinPushPerSec
              << " captureInputStallSec=" << captureInputStallConsecutiveSec
              << " captureInputWarmupSec=" << captureInputStallWarmupSec
              << " captureIdlePollIntervalUs="
              << (captureStallKeepaliveIntervalUsOverride > 0
                      ? static_cast<uint64_t>(captureStallKeepaliveIntervalUsOverride)
                      : std::max<uint64_t>(kQueueWaitTimeoutUsMin, (1000000ULL / std::max<uint64_t>(1, args.fps))))
              << " queueWaitTimeoutUs="
              << (queueWaitTimeoutUsOverride > 0 ? static_cast<uint64_t>(queueWaitTimeoutUsOverride)
                                                : std::max<uint64_t>(kQueueWaitTimeoutUsMin,
                                                                     (1000000ULL / std::max<uint64_t>(1, args.fps)) /
                                                                         4ULL))
              << "\n";
  }
  if (kInputPolicyForceBlock) {
    std::cout << "[native-video-host] input injection blocked by compile-time policy\n";
  } else if (!args.enableInputInjection) {
    std::cout << "[native-video-host] input injection disabled (enableInputInjection=false)\n";
  } else if (!inputInjectionEnabled) {
    std::cout << "[native-video-host] input injection disabled (unsupported mode) mode="
              << args.inputInjectionMode << "\n";
  } else {
    std::cout << "[native-video-host] input injection enabled mode="
              << input_injection_mode_name(configuredInputInjectionMode)
              << " targetPid=" << args.inputTargetPid
              << " targetProcess=" << trim_ascii(args.inputTargetProcess)
              << " targetTitle=" << trim_ascii(args.inputTargetTitle)
              << "\n";
  }

  SOCKET listenSock = INVALID_SOCKET;
  SOCKET clientSock = INVALID_SOCKET;
  sockaddr_in udpPeer{};
  bool udpPeerReady = false;
  if (transport == VideoTransport::Tcp) {
    listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] listen socket create failed\n";
      return 2;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(args.bindPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenSock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
      std::cerr << "[native-video-host] bind failed port=" << args.bindPort << "\n";
      closesocket(listenSock);
      return 3;
    }
    if (listen(listenSock, 1) != 0) {
      std::cerr << "[native-video-host] listen failed\n";
      closesocket(listenSock);
      return 4;
    }

    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    clientSock = accept(listenSock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    if (clientSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] accept failed\n";
      closesocket(listenSock);
      listenSock = INVALID_SOCKET;
      return 5;
    }

    int noDelay = 1;
    setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  } else {
    clientSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] udp socket create failed\n";
      return 2;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(args.bindPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(clientSock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
      std::cerr << "[native-video-host] udp bind failed port=" << args.bindPort << "\n";
      closesocket(clientSock);
      return 3;
    }

    for (;;) {
      UdpHelloPacket hello{};
      sockaddr_in peer{};
      int peerLen = sizeof(peer);
      const int n = recvfrom(clientSock, reinterpret_cast<char*>(&hello), sizeof(hello), 0,
                             reinterpret_cast<sockaddr*>(&peer), &peerLen);
      if (n <= 0) {
        std::cerr << "[native-video-host] udp handshake recv failed\n";
        closesocket(clientSock);
        return 5;
      }
      if (n < static_cast<int>(sizeof(UdpHelloPacket)) ||
          hello.magic != remote60::native_poc::kMagic ||
          hello.kind != static_cast<uint16_t>(UdpPacketKind::Hello)) {
        continue;
      }

      UdpHelloPacket ack{};
      ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
      (void)sendto(clientSock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                   reinterpret_cast<const sockaddr*>(&peer), peerLen);
      udpPeer = peer;
      udpPeerReady = true;
      break;
    }
    u_long nonBlocking = 1;
    (void)ioctlsocket(clientSock, FIONBIO, &nonBlocking);
  }

  if (transport == VideoTransport::Udp && args.tcpSendBufKb == 0) {
    const int sendBuf = 1024 * 1024;
    (void)setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }
  if (args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
    setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }
  int effectiveSendBuf = 0;
  int effectiveSendBufLen = sizeof(effectiveSendBuf);
  (void)getsockopt(clientSock, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
  std::cout << "[native-video-host] client connected transport=" << video_transport_name(transport) << "\n";
  std::cout << "[native-video-host] socket sndbuf=" << effectiveSendBuf << " bytes\n";

  std::atomic<bool> stop{false};
  std::atomic<uint64_t> inputEvents{0};
  std::atomic<uint64_t> clientMetricsUpdatedUs{0};
  std::atomic<uint32_t> clientMetricsWidth{0};
  std::atomic<uint32_t> clientMetricsHeight{0};
  std::atomic<uint32_t> clientMetricsRecvFpsX100{0};
  std::atomic<uint32_t> clientMetricsDecodedFpsX100{0};
  std::atomic<uint32_t> clientMetricsRecvMbpsX1000{0};
  std::atomic<uint32_t> clientMetricsSkippedFrames{0};
  std::atomic<uint64_t> clientMetricsAvgLatencyUs{0};
  std::atomic<uint64_t> clientMetricsMaxLatencyUs{0};
  std::atomic<uint64_t> clientMetricsAvgDecodeTailUs{0};
  std::atomic<uint64_t> clientMetricsMaxDecodeTailUs{0};
  std::atomic<uint32_t> clientMetricsCongestionState{0};
  std::atomic<uint32_t> clientMetricsCongestionTransitions{0};
  std::atomic<uint32_t> clientMetricsCongestionRecoveryCount{0};
  std::atomic<uint32_t> clientMetricsCongestionRecoveryReq{0};
  std::atomic<uint32_t> clientMetricsCongestionRecoveryMaxUs{0};
  std::atomic<uint32_t> clientMetricsQueueDepthMax{0};
  std::atomic<uint32_t> clientMetricsQueueDepthH4p{0};
  std::atomic<uint32_t> clientMetricsUdpAssemblyDropPm{0};
  std::atomic<uint32_t> hostCaptureTargetPid{0};
  std::atomic<uint32_t> hostCaptureTargetFlags{0};
  std::atomic<uint32_t> hostCaptureRebindCount{0};
  std::atomic<uint64_t> hostCaptureTargetHwnd{0};
  std::mutex hostCaptureMetaMu;
  std::string hostCaptureTargetProcess = "monitor";
  std::string hostCaptureTargetTitle;
  std::atomic<uint64_t> selectedWindowIdState{0};
  std::atomic<uint64_t> captureStreamGenerationState{1};
  std::atomic<bool> windowSelectionLocked{false};
  std::atomic<uint32_t> inputDomainW{0};
  std::atomic<uint32_t> inputDomainH{0};
  DesktopInputState desktopInputState;
  std::atomic<bool> clientRequestedKeyFrame{false};
  std::atomic<uint16_t> clientKeyFrameReason{0};
  std::atomic<uint64_t> clientKeyFrameRequestCount{0};
  std::atomic<uint64_t> clientKeyFrameRequestDropped{0};
  std::atomic<bool> streamControlActive{true};
  std::atomic<bool> runtimeTunePending{false};
  std::atomic<uint32_t> runtimeTuneBitrate{0};
  std::atomic<uint32_t> runtimeTuneKeyint{0};
  std::atomic<uint32_t> runtimeTuneFps{0};
  std::atomic<uint32_t> runtimeTuneSeq{0};
  std::atomic<bool> captureModeReqPending{false};
  std::atomic<uint32_t> captureModeReqSeq{0};
  std::atomic<uint16_t> captureModeReqMode{0};
  std::atomic<uint32_t> captureModeReqXPermille{5000};
  std::atomic<uint32_t> captureModeReqYPermille{5000};
  struct WindowSelectionTxn {
    std::mutex mu;
    std::condition_variable cv;
    bool pending = false;
    bool completed = false;
    uint32_t reqSeq = 0;
    uint64_t requestedWindowId = 0;
    uint32_t responseFlags = 0;
    uint64_t responseWindowId = 0;
    uint64_t responseStreamGeneration = 0;
    std::string responseReason;
    std::string responseTitle;
  } windowSelectionTxn;
  double keyReqTokens = static_cast<double>(keyReqTokenCapacity);
  uint64_t keyReqLastRefillUs = 0;
  uint64_t keyReqNextAllowedUs = 0;
  CaptureWindowCriteria inputTargetCriteria{};
  inputTargetCriteria.pid = args.inputTargetPid;
  for (const auto& name : parse_csv_lower(args.inputTargetProcess)) {
    inputTargetCriteria.processNamesLower.insert(name);
  }
  inputTargetCriteria.titleNeedleLower = wide_lower(utf8_to_wide(trim_ascii(args.inputTargetTitle)));
  std::atomic<uint64_t> inputIgnoredMove{0};
  std::atomic<uint64_t> inputNoTarget{0};
  std::atomic<uint64_t> inputUnsupported{0};
  std::atomic<uint64_t> inputInjectFail{0};
  SOCKET controlListenSock = INVALID_SOCKET;
  std::atomic<SOCKET> controlClientSock{INVALID_SOCKET};
  std::thread controlThread;
  if (args.controlPort > 0) {
    controlListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (controlListenSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] control listen socket create failed port=" << args.controlPort << "\n";
    } else {
      sockaddr_in ctlLocal{};
      ctlLocal.sin_family = AF_INET;
      ctlLocal.sin_port = htons(args.controlPort);
      ctlLocal.sin_addr.s_addr = htonl(INADDR_ANY);
      if (bind(controlListenSock, reinterpret_cast<const sockaddr*>(&ctlLocal), sizeof(ctlLocal)) != 0 ||
          listen(controlListenSock, 1) != 0) {
        std::cerr << "[native-video-host] control bind/listen failed port=" << args.controlPort << "\n";
        closesocket(controlListenSock);
        controlListenSock = INVALID_SOCKET;
      } else {
        std::cout << "[native-video-host] control waiting port=" << args.controlPort << "\n";
        controlThread = std::thread([&]() {
          while (!stop.load()) {
            sockaddr_in cpeer{};
            int cpeerLen = sizeof(cpeer);
            SOCKET acceptedSock = accept(controlListenSock, reinterpret_cast<sockaddr*>(&cpeer), &cpeerLen);
            if (acceptedSock == INVALID_SOCKET) {
              if (stop.load()) break;
              Sleep(50);
              continue;
            }
            controlClientSock = acceptedSock;
            int ctlNoDelay = 1;
            setsockopt(acceptedSock, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
            std::cout << "[native-video-host][control] client connected\n";
            auto send_window_list = [&](uint32_t seq) -> bool {
              ControlWindowListMessage rsp{};
              rsp.header.magic = remote60::native_poc::kMagic;
              rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowList);
              rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
              rsp.seq = seq;
              if (windowSelectionLocked.load(std::memory_order_relaxed)) rsp.flags |= 0x1u;
              rsp.selectedWindowId = selectedWindowIdState.load(std::memory_order_relaxed);
              const auto windows = enumerate_shareable_windows();
              rsp.itemCount = std::min<uint32_t>(
                  static_cast<uint32_t>(windows.size()), remote60::native_poc::kControlWindowListMaxEntries);
              for (uint32_t i = 0; i < rsp.itemCount; ++i) {
                const auto& src = windows[i];
                auto& dst = rsp.items[i];
                dst.id = src.id;
                dst.pid = src.pid;
                dst.width = static_cast<uint32_t>(std::max<int>(0, src.width));
                dst.height = static_cast<uint32_t>(std::max<int>(0, src.height));
                if (src.minimized) dst.flags |= 0x1u;
                std::snprintf(dst.title, sizeof(dst.title), "%s", src.title.c_str());
              }
              return send_all(acceptedSock, &rsp, sizeof(rsp));
            };
            auto send_input_ack = [&](uint32_t seq) -> bool {
              ControlInputAckMessage ack{};
              ack.header.magic = remote60::native_poc::kMagic;
              ack.header.type = static_cast<uint16_t>(MessageType::ControlInputAck);
              ack.header.size = static_cast<uint16_t>(sizeof(ack));
              ack.seq = seq;
              ack.hostRecvQpcUs = qpc_now_us();
              ack.hostSendQpcUs = qpc_now_us();
              return send_all(acceptedSock, &ack, sizeof(ack));
            };

            while (!stop.load()) {
              MessageHeader header{};
              if (!recv_all(acceptedSock, &header, sizeof(header))) break;
              if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
              const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
              const auto type = static_cast<MessageType>(header.type);

              if (type == MessageType::ControlPing && header.size == sizeof(ControlPingMessage)) {
                ControlPingMessage ping{};
                ping.header = header;
                if (!recv_all(acceptedSock, &ping.seq, sizeof(ping) - sizeof(MessageHeader))) break;
                ControlPongMessage pong{};
                pong.header.magic = remote60::native_poc::kMagic;
                pong.header.type = static_cast<uint16_t>(MessageType::ControlPong);
                pong.header.size = static_cast<uint16_t>(sizeof(pong));
                pong.seq = ping.seq;
                pong.clientSendQpcUs = ping.clientSendQpcUs;
                pong.hostRecvQpcUs = qpc_now_us();
                pong.hostSendQpcUs = qpc_now_us();
                pong.captureTargetPid = hostCaptureTargetPid.load(std::memory_order_relaxed);
                pong.captureTargetFlags = hostCaptureTargetFlags.load(std::memory_order_relaxed);
                pong.captureRebindCount = hostCaptureRebindCount.load(std::memory_order_relaxed);
                pong.captureTargetHwnd = hostCaptureTargetHwnd.load(std::memory_order_relaxed);
                {
                  std::string processName;
                  std::string titleText;
                  {
                    std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
                    processName = hostCaptureTargetProcess;
                    titleText = hostCaptureTargetTitle;
                  }
                  std::snprintf(pong.captureTargetProcess, sizeof(pong.captureTargetProcess), "%s",
                                processName.c_str());
                  std::snprintf(pong.captureTargetTitle, sizeof(pong.captureTargetTitle), "%s",
                                titleText.c_str());
                }
                if (!send_all(acceptedSock, &pong, sizeof(pong))) break;
                continue;
              }

              if (type == MessageType::ControlInputEvent && header.size == sizeof(ControlInputEventMessage)) {
                ControlInputEventMessage input{};
                input.header = header;
                if (!recv_all(acceptedSock, &input.seq, sizeof(input) - sizeof(MessageHeader))) break;
                std::string resolvedTarget;
                if (inputInjectionEnabled) {
                  const bool desktopMode =
                      !inputTargetCriteria.enabled() &&
                      (selectedWindowIdState.load(std::memory_order_acquire) == 0);
                  const InputInjectResult injectResult =
                      inject_background_input_event(input, inputTargetCriteria, hostCaptureTargetHwnd, desktopMode,
                                                    inputDomainW.load(std::memory_order_acquire),
                                                    inputDomainH.load(std::memory_order_acquire),
                                                    &desktopInputState, &resolvedTarget);
                  if (injectResult == InputInjectResult::Injected) {
                    const uint64_t n = inputEvents.fetch_add(1) + 1;
                    if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
                      std::cout << "[native-video-host][input] injected seq=" << input.seq
                                << " kind=" << input.kind
                                << " x=" << input.x
                                << " y=" << input.y
                                << " buttons=" << input.buttons
                                << " key=" << input.keyCode
                                << " mode=" << (desktopMode ? "desktop" : "window")
                                << resolvedTarget
                                << "\n";
                    }
                  } else if (injectResult == InputInjectResult::IgnoredMove) {
                    inputIgnoredMove.fetch_add(1, std::memory_order_relaxed);
                  } else if (injectResult == InputInjectResult::NoTarget) {
                    const uint64_t n = inputNoTarget.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
                      std::cout << "[native-video-host][input] no-target seq=" << input.seq
                                << " kind=" << input.kind
                                << " filterPid=" << args.inputTargetPid
                                << " filterProc=" << trim_ascii(args.inputTargetProcess)
                                << " filterTitle=" << trim_ascii(args.inputTargetTitle)
                                << resolvedTarget
                                << "\n";
                    }
                  } else if (injectResult == InputInjectResult::Unsupported) {
                    const uint64_t n = inputUnsupported.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
                      std::cout << "[native-video-host][input] unsupported seq=" << input.seq
                                << " kind=" << input.kind
                                << " key=" << input.keyCode
                                << "\n";
                    }
                  } else {
                    const uint64_t n = inputInjectFail.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
                      std::cout << "[native-video-host][input] inject-fail seq=" << input.seq
                                << " kind=" << input.kind
                                << " key=" << input.keyCode
                                << resolvedTarget
                                << "\n";
                    }
                  }
                } else if (args.inputLogEvery > 0 && (input.seq % args.inputLogEvery) == 0) {
                  std::cout << "[native-video-host][input] blocked seq=" << input.seq
                            << " key=" << input.keyCode
                            << " kind=" << input.kind
                            << "\n";
                }
                if (!send_input_ack(input.seq)) break;
                continue;
              }

              if (type == MessageType::ControlInputText && header.size == sizeof(ControlInputTextMessage)) {
                ControlInputTextMessage text{};
                text.header = header;
                if (!recv_all(acceptedSock, &text.seq, sizeof(text) - sizeof(MessageHeader))) break;
                std::string resolvedTarget;
                if (inputInjectionEnabled) {
                  const bool desktopMode =
                      !inputTargetCriteria.enabled() &&
                      (selectedWindowIdState.load(std::memory_order_acquire) == 0);
                  const InputInjectResult injectResult =
                      apply_input_text_message(text, hostCaptureTargetHwnd, desktopMode,
                                               &desktopInputState, &resolvedTarget);
                  if (injectResult == InputInjectResult::Injected) {
                    const uint64_t n = inputEvents.fetch_add(1) + 1;
                    if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
                      std::cout << "[native-video-host][input-text] injected seq=" << text.seq
                                << " utf16Count=" << text.utf16Count
                                << " mode=" << (desktopMode ? "desktop" : "window")
                                << resolvedTarget
                                << "\n";
                    }
                  } else if (injectResult == InputInjectResult::NoTarget) {
                    const uint64_t n = inputNoTarget.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
                      std::cout << "[native-video-host][input-text] no-target seq=" << text.seq
                                << " utf16Count=" << text.utf16Count
                                << resolvedTarget
                                << "\n";
                    }
                  } else if (injectResult == InputInjectResult::Unsupported) {
                    const uint64_t n = inputUnsupported.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
                      std::cout << "[native-video-host][input-text] unsupported seq=" << text.seq
                                << " utf16Count=" << text.utf16Count
                                << "\n";
                    }
                  } else {
                    const uint64_t n = inputInjectFail.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
                      std::cout << "[native-video-host][input-text] inject-fail seq=" << text.seq
                                << " utf16Count=" << text.utf16Count
                                << resolvedTarget
                                << "\n";
                    }
                  }
                }
                if (!send_input_ack(text.seq)) break;
                continue;
              }

              if (type == MessageType::ControlWindowListRequest &&
                  header.size == sizeof(ControlWindowListRequestMessage)) {
                ControlWindowListRequestMessage req{};
                req.header = header;
                if (!recv_all(acceptedSock, &req.seq, sizeof(req) - sizeof(MessageHeader))) break;
                if (!send_window_list(req.seq)) break;
                continue;
              }

              if (type == MessageType::ControlWindowSelect &&
                  header.size == sizeof(ControlWindowSelectMessage)) {
                ControlWindowSelectMessage req{};
                req.header = header;
                if (!recv_all(acceptedSock, &req.seq, sizeof(req) - sizeof(MessageHeader))) break;

                ControlWindowSelectedMessage rsp{};
                rsp.header.magic = remote60::native_poc::kMagic;
                rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowSelected);
                rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
                rsp.seq = req.seq;
                rsp.windowId = req.windowId;
                rsp.streamGeneration = captureStreamGenerationState.load(std::memory_order_acquire);
                rsp.hostSendQpcUs = qpc_now_us();

                if (windowSelectionLocked.load(std::memory_order_acquire)) {
                  rsp.flags |= 0x2u;
                  std::snprintf(rsp.reason, sizeof(rsp.reason), "%s", "selection_locked_by_config");
                  if (req.windowId == 0) {
                    std::snprintf(rsp.title, sizeof(rsp.title), "%s", "desktop");
                  }
                } else {
                  {
                    std::lock_guard<std::mutex> lk(windowSelectionTxn.mu);
                    windowSelectionTxn.pending = true;
                    windowSelectionTxn.completed = false;
                    windowSelectionTxn.reqSeq = req.seq;
                    windowSelectionTxn.requestedWindowId = req.windowId;
                    windowSelectionTxn.responseFlags = 0;
                    windowSelectionTxn.responseWindowId = req.windowId;
                    windowSelectionTxn.responseStreamGeneration = 0;
                    windowSelectionTxn.responseReason.clear();
                    windowSelectionTxn.responseTitle.clear();
                  }
                  windowSelectionTxn.cv.notify_all();

                  std::unique_lock<std::mutex> lk(windowSelectionTxn.mu);
                  windowSelectionTxn.cv.wait(lk, [&]() {
                    return stop.load() || windowSelectionTxn.completed;
                  });
                  rsp.flags = windowSelectionTxn.responseFlags;
                  rsp.windowId = windowSelectionTxn.responseWindowId;
                  rsp.streamGeneration = windowSelectionTxn.responseStreamGeneration;
                  rsp.hostSendQpcUs = qpc_now_us();
                  std::snprintf(rsp.reason, sizeof(rsp.reason), "%s", windowSelectionTxn.responseReason.c_str());
                  std::snprintf(rsp.title, sizeof(rsp.title), "%s", windowSelectionTxn.responseTitle.c_str());
                }

                if (!send_all(acceptedSock, &rsp, sizeof(rsp))) break;
                continue;
              }

              if (type == MessageType::ControlClientMetrics &&
                  header.size == sizeof(ControlClientMetricsMessage)) {
                ControlClientMetricsMessage metrics{};
                metrics.header = header;
                if (!recv_all(acceptedSock, &metrics.seq, sizeof(metrics) - sizeof(MessageHeader))) break;
                clientMetricsWidth = metrics.width;
                clientMetricsHeight = metrics.height;
                clientMetricsRecvFpsX100 = metrics.recvFpsX100;
                clientMetricsDecodedFpsX100 = metrics.decodedFpsX100;
                clientMetricsRecvMbpsX1000 = metrics.recvMbpsX1000;
                clientMetricsSkippedFrames = metrics.skippedFrames;
                clientMetricsAvgLatencyUs = metrics.avgLatencyUs;
                clientMetricsMaxLatencyUs = metrics.maxLatencyUs;
                clientMetricsAvgDecodeTailUs = metrics.avgDecodeTailUs;
                clientMetricsMaxDecodeTailUs = metrics.maxDecodeTailUs;
                clientMetricsCongestionState = metrics.congestionState;
                clientMetricsCongestionTransitions = metrics.congestionTransitions;
                clientMetricsCongestionRecoveryCount = metrics.congestionRecoveryCount;
                clientMetricsCongestionRecoveryReq = metrics.congestionRecoveryReq;
                clientMetricsCongestionRecoveryMaxUs = metrics.congestionRecoveryMaxUs;
                clientMetricsQueueDepthMax = metrics.queueDepthMax;
                clientMetricsQueueDepthH4p = metrics.queueDepthH4p;
                clientMetricsUdpAssemblyDropPm = metrics.udpAssemblyDropPm;
                clientMetricsUpdatedUs = qpc_now_us();
                continue;
              }

              if (type == MessageType::ControlRequestKeyFrame &&
                  header.size == sizeof(ControlRequestKeyFrameMessage)) {
                ControlRequestKeyFrameMessage req{};
                req.header = header;
                if (!recv_all(acceptedSock, &req.seq, sizeof(req) - sizeof(MessageHeader))) break;
                const uint64_t nowUs = qpc_now_us();
                if (keyReqLastRefillUs == 0) keyReqLastRefillUs = nowUs;
                if (nowUs > keyReqLastRefillUs) {
                  const double refill =
                      static_cast<double>(nowUs - keyReqLastRefillUs) / static_cast<double>(keyReqTokenRefillUs);
                  if (refill > 0.0) {
                    keyReqTokens = std::min<double>(static_cast<double>(keyReqTokenCapacity), keyReqTokens + refill);
                    keyReqLastRefillUs = nowUs;
                  }
                }
                const bool minIntervalOk = (keyReqNextAllowedUs == 0 || nowUs >= keyReqNextAllowedUs);
                if (keyReqTokens >= 1.0 && minIntervalOk) {
                  keyReqTokens -= 1.0;
                  keyReqNextAllowedUs = nowUs + keyReqMinIntervalUs;
                  clientRequestedKeyFrame = true;
                  clientKeyFrameReason = req.reason;
                  const uint64_t reqCount = clientKeyFrameRequestCount.fetch_add(1) + 1;
                  std::cout << "[native-video-host][control] keyframe-request seq=" << req.seq
                            << " reason=" << req.reason
                            << " total=" << reqCount
                            << "\n";
                } else {
                  const uint64_t dropCount = clientKeyFrameRequestDropped.fetch_add(1) + 1;
                  if ((dropCount % 60) == 1) {
                    std::cout << "[native-video-host][control] keyframe-request-throttled seq=" << req.seq
                              << " reason=" << req.reason
                              << " dropped=" << dropCount
                              << " tokens=" << keyReqTokens
                              << "\n";
                  }
                }
                continue;
              }

              if (type == MessageType::ControlRuntimeEncoderConfig &&
                  header.size == sizeof(ControlRuntimeEncoderConfigMessage)) {
                ControlRuntimeEncoderConfigMessage tune{};
                tune.header = header;
                if (!recv_all(acceptedSock, &tune.seq, sizeof(tune) - sizeof(MessageHeader))) break;
                const bool hasBitrate = ((tune.flags & 0x1u) != 0) && tune.bitrate >= 100000;
                const bool hasKeyint = ((tune.flags & 0x2u) != 0) && tune.keyint >= 1;
                const bool hasFps = ((tune.flags & 0x4u) != 0) && tune.fps >= 1;
                if (hasBitrate || hasKeyint || hasFps) {
                  if (hasBitrate) runtimeTuneBitrate.store(tune.bitrate, std::memory_order_release);
                  if (hasKeyint) runtimeTuneKeyint.store(tune.keyint, std::memory_order_release);
                  if (hasFps) runtimeTuneFps.store(tune.fps, std::memory_order_release);
                  runtimeTuneSeq.store(tune.seq, std::memory_order_release);
                  runtimeTunePending.store(true, std::memory_order_release);
                  std::cout << "[native-video-host][control] runtime-config seq=" << tune.seq
                            << " bitrate=" << (hasBitrate ? tune.bitrate : 0)
                            << " keyint=" << (hasKeyint ? tune.keyint : 0)
                            << " fps=" << (hasFps ? tune.fps : 0)
                            << " flags=" << tune.flags
                            << "\n";
                }
                continue;
              }

              if (type == MessageType::ControlStreamState &&
                  header.size == sizeof(ControlStreamStateMessage)) {
                ControlStreamStateMessage req{};
                req.header = header;
                if (!recv_all(acceptedSock, &req.seq, sizeof(req) - sizeof(MessageHeader))) break;
                const bool active = ((req.flags & 0x1u) != 0);
                streamControlActive.store(active, std::memory_order_release);
                std::cout << "[native-video-host][control] stream-state seq=" << req.seq
                          << " active=" << (active ? 1 : 0)
                          << "\n";
                continue;
              }

              if (type == MessageType::ControlCaptureModeRequest &&
                  header.size == sizeof(ControlCaptureModeRequestMessage)) {
                ControlCaptureModeRequestMessage req{};
                req.header = header;
                if (!recv_all(acceptedSock, &req.seq, sizeof(req) - sizeof(MessageHeader))) break;
                if (req.mode == 1 || req.mode == 2) {
                  captureModeReqSeq.store(req.seq, std::memory_order_release);
                  captureModeReqMode.store(req.mode, std::memory_order_release);
                  captureModeReqXPermille.store(std::min<uint32_t>(10000u, req.xPermille), std::memory_order_release);
                  captureModeReqYPermille.store(std::min<uint32_t>(10000u, req.yPermille), std::memory_order_release);
                  captureModeReqPending.store(true, std::memory_order_release);
                  std::cout << "[native-video-host][control] capture-mode-request seq=" << req.seq
                            << " mode=" << req.mode
                            << " xPermille=" << req.xPermille
                            << " yPermille=" << req.yPermille
                            << "\n";
                }
                continue;
              }

              if (bodySize > 0 && !recv_discard(acceptedSock, bodySize)) break;
            }
            if (acceptedSock != INVALID_SOCKET) {
              shutdown(acceptedSock, SD_BOTH);
              closesocket(acceptedSock);
            }
            {
              SOCKET expected = acceptedSock;
              controlClientSock.compare_exchange_strong(expected, INVALID_SOCKET);
            }
            streamControlActive.store(false, std::memory_order_release);
            std::cout << "[native-video-host][control] client disconnected\n";
          }
        });
      }
    }
  }

  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  if (!GraphicsCaptureSession::IsSupported()) {
    std::cerr << "[native-video-host] WGC not supported\n";
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    return 6;
  }

  bool mfStarted = false;
  H264Encoder encoder;
  if (useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-host] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      closesocket(clientSock);
      if (listenSock != INVALID_SOCKET) closesocket(listenSock);
      return 12;
    }
    mfStarted = true;
  }

  Microsoft::WRL::ComPtr<ID3D11Device> d3d;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
  std::mutex d3dContextMu;
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &d3d, &fl, &ctx);
  if (FAILED(hr)) {
    std::cerr << "[native-video-host] D3D11CreateDevice failed\n";
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    if (mfStarted) MFShutdown();
    return 7;
  }
  if (useH264) {
    (void)encoder.set_d3d11_device(d3d.Get());
  }
  GpuBgraScaler gpuScaler;
  bool gpuScalerHealthy = false;
  if (gpuScalerRequested) {
    gpuScalerHealthy = gpuScaler.initialize(d3d.Get(), ctx.Get(), &d3dContextMu);
    std::cout << "[native-video-host] gpuScalerRequested=1 gpuScalerReady="
              << (gpuScalerHealthy ? 1 : 0) << "\n";
  }

  CaptureWindowCriteria captureWindowCriteria{};
  captureWindowCriteria.pid = args.captureWindowPid;
  for (const auto& name : parse_csv_lower(args.captureWindowProcess)) {
    captureWindowCriteria.processNamesLower.insert(name);
  }
  captureWindowCriteria.titleNeedleLower = wide_lower(utf8_to_wide(trim_ascii(args.captureWindowTitle)));
  const bool selectionLockedByConfig = captureWindowCriteria.enabled() || inputTargetCriteria.enabled();
  windowSelectionLocked.store(selectionLockedByConfig, std::memory_order_release);
  const bool windowTargetConfigured = captureWindowCriteria.enabled();
  std::atomic<bool> captureWindowModeActive{false};
  std::atomic<bool> captureWindowClientOnlyActive{args.captureWindowClientOnly};
  CaptureWindowInfo captureWindowInfo{};
  if (windowTargetConfigured && find_capture_window(captureWindowCriteria, &captureWindowInfo)) {
    captureWindowModeActive = true;
    std::cout << "[native-video-host] capture-window target hwnd=0x" << std::hex
              << reinterpret_cast<uintptr_t>(captureWindowInfo.hwnd) << std::dec
              << " pid=" << captureWindowInfo.pid
              << " process=" << (captureWindowInfo.processName.empty() ? "unknown" : captureWindowInfo.processName)
              << " title=" << (captureWindowInfo.title.empty() ? "<empty>" : wide_to_utf8(captureWindowInfo.title))
              << " clientOnly=" << (args.captureWindowClientOnly ? 1 : 0)
              << "\n";
  } else if (windowTargetConfigured) {
    std::cout << "[native-video-host] capture-window target not found; fallback=monitor"
              << " pidFilter=" << args.captureWindowPid
              << " processFilter=" << trim_ascii(args.captureWindowProcess)
              << " titleFilter=" << trim_ascii(args.captureWindowTitle)
              << "\n";
  }
  selectedWindowIdState.store(captureWindowModeActive ? hwnd_to_id(captureWindowInfo.hwnd) : 0u,
                              std::memory_order_release);
  hostCaptureTargetFlags.store((captureWindowModeActive ? 0x1u : 0x0u) |
                                   ((captureWindowModeActive && captureWindowClientOnlyActive) ? 0x2u : 0x0u),
                               std::memory_order_relaxed);
  hostCaptureTargetPid.store(captureWindowModeActive ? captureWindowInfo.pid : 0u, std::memory_order_relaxed);
  hostCaptureTargetHwnd.store(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
                                  captureWindowModeActive ? captureWindowInfo.hwnd : nullptr)),
                              std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
    hostCaptureTargetProcess =
        (captureWindowModeActive && !captureWindowInfo.processName.empty()) ? captureWindowInfo.processName : "monitor";
    hostCaptureTargetTitle =
        (captureWindowModeActive && !captureWindowInfo.title.empty()) ? wide_to_utf8(captureWindowInfo.title)
                                                                       : std::string{};
  }

  auto item = captureWindowModeActive
                  ? CreateItemForPrimaryMonitor(captureWindowInfo.hwnd, "CreateForWindow(target-window)")
                  : CreateItemForPrimaryMonitor();
  if (!item) {
    std::cerr << "[native-video-host] capture item create failed\n";
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    if (mfStarted) MFShutdown();
    return 8;
  }

  auto captureSize = item.Size();
  uint32_t captureWidth = static_cast<uint32_t>(captureSize.Width);
  uint32_t captureHeight = static_cast<uint32_t>(captureSize.Height);
  if (captureWidth < 2 || captureHeight < 2) {
    std::cerr << "[native-video-host] invalid capture size\n";
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    if (mfStarted) MFShutdown();
    return 9;
  }

  uint32_t encodeW = captureWidth;
  uint32_t encodeH = captureHeight;
  bool autoFallback720 = false;
  if (useH264) {
    choose_h264_encode_size(args, captureWidth, captureHeight, &encodeW, &encodeH, &autoFallback720);
  }

  const uint32_t abrHighW = encodeW;
  const uint32_t abrHighH = encodeH;
  const uint32_t abrMidW = abrHighW;
  const uint32_t abrMidH = abrHighH;
  uint32_t abrLowW = abrHighW;
  uint32_t abrLowH = abrHighH;
  if (useH264) {
    choose_abr_720_size(abrHighW, abrHighH, &abrLowW, &abrLowH);
  }
  const bool abrHasLowerResolution = (abrLowW < abrHighW || abrLowH < abrHighH);
  const uint32_t abrHighBitrate = args.bitrate;
  const uint32_t abrMidBitrate = std::min<uint32_t>(
      abrHighBitrate, std::max<uint32_t>(2000000u, (abrHighBitrate * 75u) / 100u));
  const uint32_t abrLowBitrate = std::min<uint32_t>(
      abrHighBitrate, std::max<uint32_t>(1500000u, (abrHighBitrate * 55u) / 100u));
  const bool abrHasMidProfile = (abrMidBitrate < abrHighBitrate);
  const bool abrHasLowProfile = abrHasLowerResolution || (abrLowBitrate < abrMidBitrate);
  const uint32_t m9BitrateLevel0 = abrHighBitrate;
  const uint32_t m9BitrateLevel1 = std::min<uint32_t>(
      m9BitrateLevel0, std::max<uint32_t>(1500000u, (m9BitrateLevel0 * 80u) / 100u));
  const uint32_t m9BitrateLevel2 = std::min<uint32_t>(
      m9BitrateLevel1, std::max<uint32_t>(1200000u, (m9BitrateLevel0 * 65u) / 100u));
  const uint32_t m9BitrateLevel3 = std::min<uint32_t>(
      m9BitrateLevel2, std::max<uint32_t>(900000u, (m9BitrateLevel0 * 50u) / 100u));
  const uint32_t m9FpsLevel0 = args.fps;
  const uint32_t m9FpsLevel1 = args.fps;
  const uint32_t m9FpsLevel2 = std::max<uint32_t>(20u, (args.fps * 80u) / 100u);
  const uint32_t m9FpsLevel3 = std::max<uint32_t>(15u, (args.fps * 67u) / 100u);
  const uint32_t m9WidthLevel0 = abrHighW;
  const uint32_t m9HeightLevel0 = abrHighH;
  const uint32_t m9WidthLevel1 = abrHighW;
  const uint32_t m9HeightLevel1 = abrHighH;
  const uint32_t m9WidthLevel2 = abrHighW;
  const uint32_t m9HeightLevel2 = abrHighH;
  const uint32_t m9WidthLevel3 = abrLowW;
  const uint32_t m9HeightLevel3 = abrLowH;
  int abrProfile = 0;  // 0: high, 1: mid, 2: low
  uint32_t activeEncodeW = abrHighW;
  uint32_t activeEncodeH = abrHighH;
  uint32_t activeFps = args.fps;
  uint32_t activeBitrate = abrHighBitrate;
  uint32_t activeKeyint = args.keyint;
  uint64_t activeFrameIntervalUs =
      std::max<uint64_t>(1, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, activeFps)));
  uint64_t frameGatingStaticIntervalUs =
      std::max<uint64_t>(activeFrameIntervalUs, std::max<uint64_t>(1, 1000000ULL / frameGatingStaticFps));
  inputDomainW.store(activeEncodeW, std::memory_order_release);
  inputDomainH.store(activeEncodeH, std::memory_order_release);
  bool runtimeTuneManualOverride = false;
  uint64_t abrCooldownUntilUs = 0;
  uint32_t abrGoodSeconds = 0;
  uint32_t abrModeratePressureSeconds = 0;
  uint32_t abrSeverePressureSeconds = 0;
  int m9Level = 0;
  uint64_t m9CooldownUntilUs = 0;
  uint32_t m9DownPressureSeconds = 0;
  uint32_t m9UpPressureSeconds = 0;
  bool forceKeyNext = true;
  int64_t captureTimelineOriginUs = -1;
  int64_t auTimelineOriginUs = -1;
  auto resetHostTimelineAnchors = [&]() {
    captureTimelineOriginUs = -1;
    auTimelineOriginUs = -1;
  };
  auto refresh_frame_intervals = [&]() {
    activeFrameIntervalUs =
        std::max<uint64_t>(1, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, activeFps)));
    frameGatingStaticIntervalUs =
        std::max<uint64_t>(activeFrameIntervalUs, std::max<uint64_t>(1, 1000000ULL / frameGatingStaticFps));
  };
  auto apply_encoder_target = [&](uint32_t targetW, uint32_t targetH, uint32_t targetFps,
                                  uint32_t targetBitrate, uint32_t targetKeyint) -> bool {
    const bool keyintChanged = (targetKeyint != activeKeyint);
    const bool fpsChanged = (targetFps != activeFps);
    const bool resizeChanged = (targetW != activeEncodeW || targetH != activeEncodeH);
    const bool bitrateChanged = (targetBitrate != activeBitrate);

    if (keyintChanged || fpsChanged || resizeChanged) {
      encoder.shutdown();
      if (!encoder.initialize(targetW, targetH, targetFps, targetBitrate, targetKeyint)) {
        return false;
      }
      resetHostTimelineAnchors();
    } else if (bitrateChanged) {
      if (!encoder.reconfigure_bitrate(targetBitrate)) {
        encoder.shutdown();
        if (!encoder.initialize(targetW, targetH, targetFps, targetBitrate, targetKeyint)) {
          return false;
        }
        resetHostTimelineAnchors();
      }
    }

    activeEncodeW = targetW;
    activeEncodeH = targetH;
    activeFps = targetFps;
    activeBitrate = targetBitrate;
    activeKeyint = targetKeyint;
    inputDomainW.store(activeEncodeW, std::memory_order_release);
    inputDomainH.store(activeEncodeH, std::memory_order_release);
    refresh_frame_intervals();
    return true;
  };

  auto apply_capture_ui_quality_mode = [&](bool overviewMode, uint64_t nowUs) -> bool {
    if (!useH264) return true;
    const uint32_t targetW = overviewMode ? m9WidthLevel3 : m9WidthLevel0;
    const uint32_t targetH = overviewMode ? m9HeightLevel3 : m9HeightLevel0;
    const uint32_t targetFps = overviewMode ? m9FpsLevel3 : m9FpsLevel0;
    const uint32_t targetBitrate = overviewMode ? m9BitrateLevel3 : m9BitrateLevel0;
    const uint32_t targetKeyint = overviewMode ? std::max<uint32_t>(activeKeyint, 60u) : args.keyint;
    if (!apply_encoder_target(targetW, targetH, targetFps, targetBitrate, targetKeyint)) {
      return false;
    }
    runtimeTuneManualOverride = true;
    abrCooldownUntilUs = nowUs + 3000000ULL;
    abrGoodSeconds = 0;
    abrModeratePressureSeconds = 0;
    abrSeverePressureSeconds = 0;
    m9Level = overviewMode ? 3 : 0;
    m9CooldownUntilUs = nowUs + static_cast<uint64_t>(m9CooldownSec) * 1000000ULL;
    m9DownPressureSeconds = 0;
    m9UpPressureSeconds = 0;
    forceKeyNext = true;
    return true;
  };

  if (useH264) {
    if (!encoder.initialize(activeEncodeW, activeEncodeH, activeFps, activeBitrate, activeKeyint)) {
      std::cerr << "[native-video-host] H264 encoder initialize failed\n";
      closesocket(clientSock);
      if (listenSock != INVALID_SOCKET) closesocket(listenSock);
      if (mfStarted) MFShutdown();
      return 13;
    }
    resetHostTimelineAnchors();
    const std::string requestedEncoderBackend = env_string_or_empty("REMOTE60_NATIVE_ENCODER_BACKEND");
    const std::string requestedEncoderBackendPrint =
        requestedEncoderBackend.empty() ? "default(mft_auto)" : requestedEncoderBackend;
    const std::string backendFallbackReason =
        backend_fallback_reason(requestedEncoderBackend, encoder.backend_name());
    std::cout << "[native-video-host] H264 encoder backend=" << encoder.backend_name()
              << " backendRequested=" << requestedEncoderBackendPrint
              << " backendResolved=" << encoder.backend_name()
              << " backendFallbackReason=" << backendFallbackReason
              << " hw=" << (encoder.using_hardware() ? 1 : 0)
              << " captureSize=" << captureWidth << "x" << captureHeight
              << " encodeSize=" << activeEncodeW << "x" << activeEncodeH
              << " auto720=" << (autoFallback720 ? 1 : 0)
              << " abrMidProfile=" << abrMidW << "x" << abrMidH
              << " abrMidBitrate=" << abrMidBitrate
              << " abrLowProfile=" << abrLowW << "x" << abrLowH
              << " abrLowBitrate=" << abrLowBitrate
              << "\n";
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
  d3d.As(&dxgi);
  winrt::com_ptr<::IInspectable> inspectable;
  winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
  auto d3dDevice = inspectable.as<IDirect3DDevice>();

  Direct3D11CaptureFramePool pool{nullptr};
  GraphicsCaptureSession session{nullptr};
  winrt::event_token token{};
  std::atomic<bool> captureSessionReady{false};
  uint64_t captureSessionStartedUs = 0;
  uint64_t captureRestartCount = 0;
  uint64_t lastCaptureRestartUs = 0;

  std::mutex captureResourceMu;
  std::atomic<uint32_t> captureSizeChangePending{0};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  auto create_staging = [&](uint32_t srcW, uint32_t srcH) -> bool {
    D3D11_TEXTURE2D_DESC stDesc{};
    stDesc.Width = srcW;
    stDesc.Height = srcH;
    stDesc.MipLevels = 1;
    stDesc.ArraySize = 1;
    stDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stDesc.SampleDesc.Count = 1;
    stDesc.Usage = D3D11_USAGE_STAGING;
    stDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> nextStaging;
    if (FAILED(d3d->CreateTexture2D(&stDesc, nullptr, &nextStaging)) || !nextStaging) {
      return false;
    }
    std::lock_guard<std::mutex> lk(captureResourceMu);
    staging = nextStaging;
    return true;
  };
  if (!create_staging(captureWidth, captureHeight)) {
    std::cerr << "[native-video-host] staging texture create failed\n";
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    if (mfStarted) MFShutdown();
    return 10;
  }

  FrameState frame;
  const auto update_u64_max = [](std::atomic<uint64_t>& target, const uint64_t value) {
    auto old = target.load(std::memory_order_relaxed);
    while (value > old && !target.compare_exchange_weak(old, value, std::memory_order_release, std::memory_order_relaxed)) {
    }
  };
  std::atomic<uint64_t> callbackFrames{0};
  std::atomic<int64_t> captureClockOffsetUs{std::numeric_limits<int64_t>::max()};
  std::atomic<uint64_t> queuePushCount{0};
  uint64_t queuePushCountLastSample = 0;  // only read from main thread
  uint64_t queuePushPerSecLatest = 0;
  uint32_t captureInputLowPushStreakSec = 0;
  uint64_t captureDeadRestartCount = 0;
  uint64_t queuePopCount = 0;
  uint64_t queueWaitTimeoutCount = 0;
  uint64_t queueWaitNoWorkCount = 0;
  std::atomic<uint64_t> lastPopFrameVersion{0};
  std::atomic<uint64_t> queueDepthMax{0};
  std::atomic<uint64_t> lastCallbackUs{0};
  std::atomic<uint64_t> lastCaptureUsForInterval{0};
  std::atomic<uint64_t> firstCallbackLoggedGeneration{0};
  auto describe_active_capture_target = [&]() -> std::string {
    const uint64_t targetHwnd = hostCaptureTargetHwnd.load(std::memory_order_acquire);
    const uint32_t targetPid = hostCaptureTargetPid.load(std::memory_order_acquire);
    std::string targetProcess = "monitor";
    std::string targetTitle;
    {
      std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
      targetProcess = hostCaptureTargetProcess;
      targetTitle = hostCaptureTargetTitle;
    }
    std::ostringstream oss;
    oss << " streamGen=" << captureStreamGenerationState.load(std::memory_order_acquire)
        << " selectedId=" << selectedWindowIdState.load(std::memory_order_acquire)
        << " targetHwnd=0x" << std::hex << targetHwnd << std::dec
        << " pid=" << targetPid
        << " process=" << targetProcess
        << " title=" << (targetTitle.empty() ? "<empty>" : targetTitle);
    return oss.str();
  };

  auto attach_frame_arrived = [&]() {
    token = pool.FrameArrived([&](Direct3D11CaptureFramePool const& sender,
                                  winrt::Windows::Foundation::IInspectable const&) {
      if (stop.load()) return;
      try {
        auto latest = sender.TryGetNextFrame();
        if (!latest) return;
        // Drain queued frames and keep only the newest one to avoid stale-frame backlog.
        while (auto newer = sender.TryGetNextFrame()) {
          latest = newer;
        }
        if (!streamControlActive.load(std::memory_order_acquire)) return;

        auto src = SurfaceToTexture(latest.Surface());
        if (!src) return;
        uint32_t frameW = 0;
        uint32_t frameH = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingLocal;
        {
          std::lock_guard<std::mutex> lk(captureResourceMu);
          frameW = captureWidth;
          frameH = captureHeight;
          stagingLocal = staging;
        }
        if (!stagingLocal || frameW < 2 || frameH < 2) return;
        D3D11_TEXTURE2D_DESC srcDesc{};
        src->GetDesc(&srcDesc);
        if (srcDesc.Width != frameW || srcDesc.Height != frameH) {
          captureSizeChangePending.store(1, std::memory_order_release);
          return;
        }
        uint32_t stride = frameW * 4;
        auto payload = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(stride) * frameH);
        {
          std::lock_guard<std::mutex> d3dLock(d3dContextMu);
          ctx->CopyResource(stagingLocal.Get(), src.Get());
          D3D11_MAPPED_SUBRESOURCE map{};
          if (FAILED(ctx->Map(stagingLocal.Get(), 0, D3D11_MAP_READ, 0, &map))) return;
          auto* dst = payload->data();
          auto* srcRow = reinterpret_cast<const uint8_t*>(map.pData);
          for (uint32_t y = 0; y < frameH; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * stride,
                        srcRow + static_cast<size_t>(y) * map.RowPitch, stride);
          }
          ctx->Unmap(stagingLocal.Get(), 0);
        }
        if (captureWindowModeActive && captureWindowClientOnlyActive) {
          const HWND cropHwnd = reinterpret_cast<HWND>(
              static_cast<uintptr_t>(hostCaptureTargetHwnd.load(std::memory_order_acquire)));
          uint32_t cropX = 0;
          uint32_t cropY = 0;
          uint32_t cropW = 0;
          uint32_t cropH = 0;
          if (cropHwnd && compute_window_client_crop(cropHwnd, frameW, frameH, &cropX, &cropY, &cropW, &cropH)) {
            if (cropW < frameW || cropH < frameH || cropX > 0 || cropY > 0) {
              const uint32_t croppedStride = cropW * 4;
              auto cropped = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(croppedStride) * cropH);
              const auto* srcBase = payload->data();
              auto* dstBase = cropped->data();
              for (uint32_t y = 0; y < cropH; ++y) {
                const size_t srcOff =
                    static_cast<size_t>(cropY + y) * stride + static_cast<size_t>(cropX) * 4u;
                const size_t dstOff = static_cast<size_t>(y) * croppedStride;
                std::memcpy(dstBase + dstOff, srcBase + srcOff, croppedStride);
              }
              payload = std::move(cropped);
              frameW = cropW;
              frameH = cropH;
              stride = croppedStride;
            }
          }
        }
        const uint64_t callbackUs = qpc_now_us();
        const uint64_t queuePushUs = qpc_now_us();
        const uint64_t prevCallbackUs = lastCallbackUs.load(std::memory_order_acquire);
        const uint64_t prevCaptureUs = lastCaptureUsForInterval.load(std::memory_order_acquire);
        uint64_t sourceCaptureUs = callbackUs;
        uint64_t captureAgeAtCallbackUs = 0;
        uint64_t captureClockSkewUs = 0;
        uint64_t callbackIntervalUs = 0;
        uint64_t captureIntervalUs = 0;
        // Align WGC frame timestamp to qpc_now_us domain using a minimum-offset estimator.
        const auto relTime = latest.SystemRelativeTime();
        const int64_t t100ns = relTime.count();
        if (t100ns > 0) {
          const int64_t wgcUs = t100ns / 10;
          if (static_cast<int64_t>(callbackUs) >= wgcUs) {
            captureAgeAtCallbackUs = static_cast<uint64_t>(static_cast<int64_t>(callbackUs) - wgcUs);
          }
          const int64_t offsetCandidate = static_cast<int64_t>(callbackUs) - wgcUs;
          if (offsetCandidate > 0) {
            int64_t cur = captureClockOffsetUs.load(std::memory_order_acquire);
            if (cur == std::numeric_limits<int64_t>::max()) {
              captureClockOffsetUs.store(offsetCandidate, std::memory_order_release);
              cur = offsetCandidate;
            } else {
              while (offsetCandidate < cur &&
                     !captureClockOffsetUs.compare_exchange_weak(cur, offsetCandidate, std::memory_order_acq_rel,
                                                                std::memory_order_acquire)) {
              }
            }
            const int64_t bestOffset = captureClockOffsetUs.load();
            if (bestOffset != std::numeric_limits<int64_t>::max()) {
              const int64_t aligned = wgcUs + bestOffset;
              const int64_t alignedSkewUs = aligned - static_cast<int64_t>(callbackUs);
              if (aligned > 0 && alignedSkewUs >= -500000 && alignedSkewUs <= 500000) {
                captureClockSkewUs = alignedSkewUs >= 0
                    ? static_cast<uint64_t>(alignedSkewUs)
                    : static_cast<uint64_t>(-alignedSkewUs);
                sourceCaptureUs = static_cast<uint64_t>(aligned);
              }
            }
          }
        }
        if (prevCallbackUs > 0 && callbackUs >= prevCallbackUs) {
          callbackIntervalUs = callbackUs - prevCallbackUs;
        }
        if (prevCaptureUs > 0 && sourceCaptureUs >= prevCaptureUs) {
          captureIntervalUs = sourceCaptureUs - prevCaptureUs;
        }
        lastCallbackUs.store(callbackUs, std::memory_order_release);
        lastCaptureUsForInterval.store(sourceCaptureUs, std::memory_order_release);
        const uint64_t streamGeneration = captureStreamGenerationState.load(std::memory_order_acquire);
        uint64_t currentVersion = 0;
        {
          std::lock_guard<std::mutex> lk(frame.mu);
          frame.payload = std::move(payload);
          frame.width = frameW;
          frame.height = frameH;
          frame.stride = stride;
          frame.streamGeneration = streamGeneration;
          frame.captureUs = sourceCaptureUs;
          frame.callbackUs = callbackUs;
          frame.captureAgeAtCallbackUs = captureAgeAtCallbackUs;
          frame.captureClockSkewUs = captureClockSkewUs;
          frame.queuePushUs = queuePushUs;
          frame.callbackIntervalUs = callbackIntervalUs;
          frame.captureIntervalUs = captureIntervalUs;
          frame.seq += 1;
          frame.version += 1;
          currentVersion = frame.version;
        }
        const uint64_t currentPopVersion = lastPopFrameVersion.load(std::memory_order_acquire);
        const uint64_t depthNow = (currentVersion >= currentPopVersion) ? (currentVersion - currentPopVersion) : 0;
        update_u64_max(queueDepthMax, depthNow);
        ++queuePushCount;
        callbackFrames += 1;
        uint64_t loggedGeneration = firstCallbackLoggedGeneration.load(std::memory_order_acquire);
        if (streamGeneration != 0 && loggedGeneration != streamGeneration &&
            firstCallbackLoggedGeneration.compare_exchange_strong(
                loggedGeneration, streamGeneration,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
          std::cout << "[native-video-host] capture-switch first-callback"
                    << describe_active_capture_target()
                    << " callbackUs=" << callbackUs
                    << " captureUs=" << sourceCaptureUs
                    << "\n";
        }
        frame.cv.notify_one();
      } catch (...) {
      }
    });
  };

  auto detach_capture_session = [&]() {
    captureSessionReady.store(false, std::memory_order_release);
    try {
      if (pool) {
        pool.FrameArrived(token);
      }
    } catch (...) {
    }
    token = winrt::event_token{};
    try {
      if (session) session.Close();
    } catch (...) {
    }
    try {
      if (pool) pool.Close();
    } catch (...) {
    }
    session = nullptr;
    pool = nullptr;
  };

  auto restart_capture_session = [&]() -> bool {
    detach_capture_session();
    try {
      if (captureWindowModeActive) {
        const uintptr_t hwndRaw = static_cast<uintptr_t>(hostCaptureTargetHwnd.load(std::memory_order_relaxed));
        HWND targetHwnd = reinterpret_cast<HWND>(hwndRaw);
        if (targetHwnd && IsWindow(targetHwnd)) {
          auto refreshedItem = CreateItemForPrimaryMonitor(targetHwnd, "CreateForWindow(restart-refresh)");
          if (refreshedItem) {
            item = refreshedItem;
          }
        }
      } else {
        auto refreshedItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(restart-refresh)");
        if (refreshedItem) {
          item = refreshedItem;
        }
      }
      const auto newSize = item.Size();
      const uint32_t newW = static_cast<uint32_t>(newSize.Width);
      const uint32_t newH = static_cast<uint32_t>(newSize.Height);
      if (newW < 2 || newH < 2) {
        std::cerr << "[native-video-host] invalid capture size on restart\n";
        return false;
      }
      uint32_t prevW = 0;
      uint32_t prevH = 0;
      {
        std::lock_guard<std::mutex> lk(captureResourceMu);
        prevW = captureWidth;
        prevH = captureHeight;
      }
      if (!create_staging(newW, newH)) {
        std::cerr << "[native-video-host] staging texture recreate failed size="
                  << newW << "x" << newH << "\n";
        return false;
      }
      {
        std::lock_guard<std::mutex> lk(captureResourceMu);
        captureSize = newSize;
        captureWidth = newW;
        captureHeight = newH;
      }
      if (prevW != newW || prevH != newH) {
        std::cout << "[native-video-host] capture-size-updated old=" << prevW << "x" << prevH
                  << " new=" << newW << "x" << newH << "\n";
      }
      pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
          d3dDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
          captureFramePoolBuffers, captureSize);
      session = pool.CreateCaptureSession(item);
      attach_frame_arrived();
      session.StartCapture();
      captureSessionStartedUs = qpc_now_us();
      captureSessionReady.store(true, std::memory_order_release);
      captureSizeChangePending.store(0, std::memory_order_release);
      return true;
    } catch (...) {
      detach_capture_session();
      return false;
    }
  };

  if (!restart_capture_session()) {
    std::cerr << "[native-video-host] capture session start failed\n";
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    if (mfStarted) MFShutdown();
    return 10;
  }

  const uint64_t startUs = qpc_now_us();
  uint64_t nextTickUs = startUs;
  // For encoded path, latency is prioritized over strict send pacing.
  // Raw path keeps legacy pacing to avoid excessive CPU/bandwidth burst.
  const bool paceByTick = useRaw || !noPacingH264;
  const uint64_t captureWindowRebindIntervalUs =
      static_cast<uint64_t>(std::max<uint32_t>(200, args.captureWindowRebindIntervalMs)) * 1000ULL;
  uint64_t nextCaptureWindowCheckUs = startUs + captureWindowRebindIntervalUs;
  uint64_t statAtUs = startUs + 1000000ULL;
  uint64_t sentFrames = 0;
  uint64_t encodedFrames = 0;
  uint64_t sentBytes = 0;
  uint64_t rawEquivalentBytes = 0;
  uint64_t udpTxFrames = 0;
  uint64_t udpTxChunks = 0;
  uint64_t udpTxBytes = 0;
  uint64_t udpTxFail = 0;
  uint64_t udpTxNoPeer = 0;
  uint64_t skippedByOverwrite = 0;
  uint64_t lastVersionSent = 0;
  uint64_t tracePrinted = 0;
  uint32_t encodedSeq = 0;
  uint64_t encodeFailCount = 0;
  uint64_t staleEncodedDropCount = 0;
  uint64_t stalePreEncodeDropCount = 0;
  uint64_t encoderResetCount = 0;
  uint64_t gpuScaleAttempts = 0;
  uint64_t gpuScaleSuccess = 0;
  uint64_t gpuScaleFail = 0;
  uint64_t gpuScaleCpuFallback = 0;
  uint64_t captureAgeSumUs = 0;
  uint64_t captureAgeMaxUs = 0;
  uint64_t callbackToEncodeStartSumUs = 0;
  uint64_t callbackToEncodeStartMaxUs = 0;
  uint32_t consecutiveStaleEncodedFrames = 0;
  uint64_t idleHoldTotal = 0;
  uint64_t lastSendStartUs = 0;
  std::shared_ptr<std::vector<uint8_t>> frameGatingRefPayload;
  uint32_t frameGatingRefW = 0;
  uint32_t frameGatingRefH = 0;
  uint32_t frameGatingRefStride = 0;
  uint32_t frameGatingStaticStreak = 0;
  uint32_t frameGatingMotionStreak = 0;
  bool frameGatingStaticMode = false;
  uint64_t frameGatingLastSentUs = 0;
  uint64_t frameGatingSkipCount = 0;
  uint64_t frameGatingStaticSkipCount = 0;
  uint64_t frameGatingChangePermilleLast = 1000;
  uint64_t frameGatingChangePermilleSum = 0;
  uint64_t frameGatingChangePermilleCount = 0;
  uint64_t firstSentLoggedGeneration = 0;
  bool streamActiveApplied = true;
  auto effective_queue_wait_timeout_us = [&]() -> uint64_t {
    if (queueWaitTimeoutUsOverride > 0) {
      return std::max<uint64_t>(kQueueWaitTimeoutUsMin, queueWaitTimeoutUsOverride);
    }
    const uint64_t keepaliveIntervalUs =
        (captureStallKeepaliveIntervalUsOverride > 0)
            ? std::max<uint64_t>(kQueueWaitTimeoutUsMin, captureStallKeepaliveIntervalUsOverride)
            : std::max<uint64_t>(kQueueWaitTimeoutUsMin, activeFrameIntervalUs);
    const uint64_t dynamicTimeoutUs =
        std::max<uint64_t>(kQueueWaitTimeoutUsMin, keepaliveIntervalUs / 4ULL);
    return std::min<uint64_t>(kQueueWaitTimeoutUsDefault, dynamicTimeoutUs);
  };
  auto pump_udp_hello = [&]() {
    if (transport != VideoTransport::Udp) return;
    for (;;) {
      UdpHelloPacket hello{};
      sockaddr_in peer{};
      int peerLen = sizeof(peer);
      const int n = recvfrom(clientSock, reinterpret_cast<char*>(&hello), sizeof(hello), 0,
                             reinterpret_cast<sockaddr*>(&peer), &peerLen);
      if (n <= 0) {
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) break;
        break;
      }
      if (n < static_cast<int>(sizeof(UdpHelloPacket)) ||
          hello.magic != remote60::native_poc::kMagic ||
          hello.kind != static_cast<uint16_t>(UdpPacketKind::Hello)) {
        continue;
      }
      UdpHelloPacket ack{};
      ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
      (void)sendto(clientSock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                   reinterpret_cast<const sockaddr*>(&peer), peerLen);
      const bool peerChanged =
          (!udpPeerReady ||
           (std::memcmp(&udpPeer, &peer, sizeof(sockaddr_in)) != 0));
      udpPeer = peer;
      udpPeerReady = true;
      if (peerChanged) {
        forceKeyNext = true;
        std::cout << "[native-video-host] udp peer updated; forcing keyframe\n";
      }
    }
  };
  auto reconnect_tcp_data_session = [&](const char* reason) -> bool {
    if (transport != VideoTransport::Tcp) return false;
    if (args.seconds > 0) return false;
    if (listenSock == INVALID_SOCKET) return false;
    if (clientSock != INVALID_SOCKET) {
      shutdown(clientSock, SD_BOTH);
      closesocket(clientSock);
      clientSock = INVALID_SOCKET;
    }
    std::cout << "[native-video-host] data disconnected reason="
              << (reason ? reason : "unknown")
              << " waiting reconnect\n";
    while (!stop.load()) {
      sockaddr_in peer{};
      int peerLen = sizeof(peer);
      SOCKET accepted = accept(listenSock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
      if (accepted == INVALID_SOCKET) {
        if (stop.load()) return false;
        Sleep(50);
        continue;
      }
      clientSock = accepted;
      int noDelay = 1;
      setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
      if (args.tcpSendBufKb > 0) {
        const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
        setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
      }
      int effectiveSendBuf = 0;
      int effectiveSendBufLen = sizeof(effectiveSendBuf);
      (void)getsockopt(clientSock, SOL_SOCKET, SO_SNDBUF,
                       reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
      std::cout << "[native-video-host] client reconnected transport=tcp sndbuf="
                << effectiveSendBuf << " bytes\n";
      clientMetricsUpdatedUs = 0;
      clientMetricsCongestionState = 0;
      clientMetricsCongestionTransitions = 0;
      clientMetricsCongestionRecoveryCount = 0;
      clientMetricsCongestionRecoveryReq = 0;
      clientMetricsCongestionRecoveryMaxUs = 0;
      clientMetricsQueueDepthMax = 0;
      clientMetricsQueueDepthH4p = 0;
      clientMetricsUdpAssemblyDropPm = 0;
      clientRequestedKeyFrame = false;
      clientKeyFrameReason = 0;
      runtimeTunePending = false;
      runtimeTuneBitrate = 0;
      runtimeTuneKeyint = 0;
      runtimeTuneFps = 0;
      runtimeTuneSeq = 0;
      keyReqTokens = static_cast<double>(keyReqTokenCapacity);
      keyReqLastRefillUs = 0;
      keyReqNextAllowedUs = 0;
      forceKeyNext = true;
      encodedSeq = 0;
      lastSendStartUs = 0;
      frameGatingLastSentUs = 0;
      {
        std::lock_guard<std::mutex> lk(frame.mu);
        lastVersionSent = frame.version;
      }
      return true;
    }
    return false;
  };
  auto flush_capture_pipeline_state = [&](const char* reason) {
    frameGatingRefPayload.reset();
    frameGatingRefW = 0;
    frameGatingRefH = 0;
    frameGatingRefStride = 0;
    frameGatingStaticStreak = 0;
    frameGatingMotionStreak = 0;
    frameGatingStaticMode = false;
    frameGatingLastSentUs = 0;
    firstSentLoggedGeneration = 0;
    firstCallbackLoggedGeneration.store(0, std::memory_order_release);

    uint64_t flushedVersion = 0;
    {
      std::lock_guard<std::mutex> lk(frame.mu);
      frame.payload.reset();
      frame.width = 0;
      frame.height = 0;
      frame.stride = 0;
      frame.streamGeneration = captureStreamGenerationState.load(std::memory_order_acquire);
      frame.captureUs = 0;
      frame.callbackUs = 0;
      frame.callbackIntervalUs = 0;
      frame.captureAgeAtCallbackUs = 0;
      frame.captureClockSkewUs = 0;
      frame.queuePushUs = 0;
      frame.captureIntervalUs = 0;
      frame.seq += 1;
      frame.version += 1;
      flushedVersion = frame.version;
      lastVersionSent = flushedVersion;
    }
    lastPopFrameVersion.store(flushedVersion, std::memory_order_release);
    frame.cv.notify_all();
    std::cout << "[native-video-host] capture-pipeline-flushed reason="
              << (reason ? reason : "unknown")
              << describe_active_capture_target()
              << " version=" << flushedVersion
              << "\n";
  };
  auto log_first_sent_generation = [&](const char* path, uint64_t streamGeneration, uint64_t sendStartUs,
                                       uint64_t captureStampUs, uint32_t width, uint32_t height) {
    if (streamGeneration == 0 || firstSentLoggedGeneration == streamGeneration) return;
    firstSentLoggedGeneration = streamGeneration;
    std::cout << "[native-video-host] capture-switch first-frame"
              << " path=" << (path ? path : "unknown")
              << describe_active_capture_target()
              << " sendQpcUs=" << sendStartUs
              << " captureQpcUs=" << captureStampUs
              << " size=" << width << "x" << height
              << "\n";
  };

  auto apply_selected_window_capture = [&](uint64_t requestedWindowId, uint64_t nowUs,
                                           uint32_t* outFlags, uint64_t* outWindowId,
                                           uint64_t* outStreamGeneration,
                                           std::string* outReason, std::string* outTitle) -> bool {
    if (outFlags) *outFlags = 0;
    if (outWindowId) *outWindowId = requestedWindowId;
    if (outStreamGeneration) {
      *outStreamGeneration = captureStreamGenerationState.load(std::memory_order_acquire);
    }
    if (outReason) outReason->clear();
    if (outTitle) outTitle->clear();
    if (windowSelectionLocked.load(std::memory_order_acquire)) {
      if (outFlags) *outFlags |= 0x2u;
      if (outReason) *outReason = "selection_locked_by_config";
      if (requestedWindowId == 0 && outTitle) *outTitle = "desktop";
      return false;
    }

    const auto prevItem = item;
    const bool prevCaptureWindowModeActive = captureWindowModeActive.load(std::memory_order_acquire);
    const bool prevCaptureWindowClientOnlyActive =
        captureWindowClientOnlyActive.load(std::memory_order_acquire);
    const auto prevCaptureWindowCriteria = captureWindowCriteria;
    const auto prevCaptureWindowInfo = captureWindowInfo;
    const uint64_t prevSelectedWindowId = selectedWindowIdState.load(std::memory_order_acquire);
    const uint64_t prevCaptureStreamGeneration = captureStreamGenerationState.load(std::memory_order_acquire);
    const uint32_t prevHostCaptureFlags = hostCaptureTargetFlags.load(std::memory_order_acquire);
    const uint32_t prevHostCapturePid = hostCaptureTargetPid.load(std::memory_order_acquire);
    const uint64_t prevHostCaptureHwnd = hostCaptureTargetHwnd.load(std::memory_order_acquire);
    const uint32_t prevHostCaptureRebindCount = hostCaptureRebindCount.load(std::memory_order_acquire);
    std::string prevHostCaptureProcess;
    std::string prevHostCaptureTitle;
    {
      std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
      prevHostCaptureProcess = hostCaptureTargetProcess;
      prevHostCaptureTitle = hostCaptureTargetTitle;
    }

    auto restore_previous_target = [&]() {
      item = prevItem;
      captureWindowModeActive.store(prevCaptureWindowModeActive, std::memory_order_release);
      captureWindowClientOnlyActive.store(prevCaptureWindowClientOnlyActive, std::memory_order_release);
      captureWindowCriteria = prevCaptureWindowCriteria;
      captureWindowInfo = prevCaptureWindowInfo;
      selectedWindowIdState.store(prevSelectedWindowId, std::memory_order_release);
      captureStreamGenerationState.store(prevCaptureStreamGeneration, std::memory_order_release);
      hostCaptureTargetFlags.store(prevHostCaptureFlags, std::memory_order_release);
      hostCaptureTargetPid.store(prevHostCapturePid, std::memory_order_release);
      hostCaptureTargetHwnd.store(prevHostCaptureHwnd, std::memory_order_release);
      hostCaptureRebindCount.store(prevHostCaptureRebindCount, std::memory_order_release);
      {
        std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
        hostCaptureTargetProcess = prevHostCaptureProcess;
        hostCaptureTargetTitle = prevHostCaptureTitle;
      }
    };

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem nextItem{nullptr};
    CaptureWindowInfo nextCaptureWindowInfo{};
    bool nextCaptureWindowModeActive = false;
    bool nextCaptureWindowClientOnlyActive = false;
    CaptureWindowCriteria nextCaptureWindowCriteria{};
    uint64_t nextSelectedWindowId = requestedWindowId;
    std::string nextReason = "ok";
    std::string nextTitle;
    std::string nextProcess = "monitor";
    uint32_t nextPid = 0;
    uint64_t nextHwnd = 0;
    uint32_t nextFlags = 0;
    const uint64_t nextCaptureStreamGeneration = prevCaptureStreamGeneration + 1;

    if (requestedWindowId == 0) {
      nextItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(window-select-desktop)");
      if (!nextItem) {
        if (outReason) *outReason = "desktop_capture_item_failed";
        if (outTitle) *outTitle = "desktop";
        return false;
      }
      nextReason = "desktop_mode_selected";
      nextTitle = "desktop";
    } else {
      const auto selected = find_window_by_id(requestedWindowId);
      if (!selected.has_value()) {
        if (outReason) *outReason = "window_not_found_or_not_shareable";
        return false;
      }
      nextItem = CreateItemForPrimaryMonitor(selected->hwnd, "CreateForWindow(window-select)");
      if (!nextItem) {
        if (outReason) *outReason = "window_capture_item_failed";
        if (outTitle) *outTitle = selected->title;
        return false;
      }
      nextCaptureWindowModeActive = true;
      nextCaptureWindowClientOnlyActive = false;
      nextCaptureWindowInfo.hwnd = selected->hwnd;
      nextCaptureWindowInfo.pid = selected->pid;
      nextCaptureWindowInfo.processName = get_window_process_name(selected->hwnd, nullptr);
      nextCaptureWindowInfo.title = utf8_to_wide(selected->title);
      nextSelectedWindowId = selected->id;
      nextReason = "ok";
      nextTitle = selected->title;
      nextProcess = nextCaptureWindowInfo.processName.empty() ? "unknown" : nextCaptureWindowInfo.processName;
      nextPid = selected->pid;
      nextHwnd = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(selected->hwnd));
      nextFlags = 0x1u;
    }

    item = nextItem;
    captureWindowModeActive.store(nextCaptureWindowModeActive, std::memory_order_release);
    captureWindowClientOnlyActive.store(nextCaptureWindowClientOnlyActive, std::memory_order_release);
    captureWindowCriteria = nextCaptureWindowCriteria;
    captureWindowInfo = nextCaptureWindowInfo;
    selectedWindowIdState.store(nextSelectedWindowId, std::memory_order_release);
    captureStreamGenerationState.store(nextCaptureStreamGeneration, std::memory_order_release);
    hostCaptureTargetFlags.store(nextFlags, std::memory_order_release);
    hostCaptureTargetPid.store(nextPid, std::memory_order_release);
    hostCaptureTargetHwnd.store(nextHwnd, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
      hostCaptureTargetProcess = nextProcess;
      hostCaptureTargetTitle = nextTitle == "desktop" ? std::string{} : nextTitle;
    }
    nextCaptureWindowCheckUs = nowUs + captureWindowRebindIntervalUs;
    lastCaptureRestartUs = nowUs;
    if (!restart_capture_session()) {
      restore_previous_target();
      if (outReason) *outReason = "capture_restart_failed";
      if (outTitle) *outTitle = nextTitle;
      return false;
    }

    captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
    lastCaptureUsForInterval.store(0, std::memory_order_release);
    lastCallbackUs.store(0, std::memory_order_release);
    resetHostTimelineAnchors();
    forceKeyNext = true;
    ++captureRestartCount;
    flush_capture_pipeline_state("window-select");

    if (outFlags) *outFlags = 0x1u;
    if (outWindowId) *outWindowId = nextSelectedWindowId;
    if (outStreamGeneration) *outStreamGeneration = nextCaptureStreamGeneration;
    if (outReason) *outReason = nextReason;
    if (outTitle) *outTitle = nextTitle;
    return true;
  };

  while (!stop.load()) {
    const uint64_t nowUs = qpc_now_us();
    uint64_t tickWaitUs = 0;
    if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
      break;
    }
    pump_udp_hello();
    const bool streamActive = streamControlActive.load(std::memory_order_acquire);
    if (!streamActive) {
      if (streamActiveApplied) {
        flush_capture_pipeline_state("stream-inactive");
        streamActiveApplied = false;
        std::cout << "[native-video-host] stream inactive\n";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    if (!streamActiveApplied) {
      streamActiveApplied = true;
      forceKeyNext = true;
      std::cout << "[native-video-host] stream active; forcing keyframe\n";
    }
    if (useH264 && runtimeTunePending.exchange(false, std::memory_order_acq_rel)) {
      const uint32_t reqSeq = runtimeTuneSeq.load(std::memory_order_acquire);
      uint32_t targetBitrate = runtimeTuneBitrate.load(std::memory_order_acquire);
      uint32_t targetKeyint = runtimeTuneKeyint.load(std::memory_order_acquire);
      uint32_t targetFps = runtimeTuneFps.load(std::memory_order_acquire);
      if (targetBitrate < 100000) targetBitrate = activeBitrate;
      if (targetKeyint < 1) targetKeyint = activeKeyint;
      if (targetFps < 1) targetFps = activeFps;
      const bool bitrateChanged = (targetBitrate != activeBitrate);
      const bool keyintChanged = (targetKeyint != activeKeyint);
      const bool fpsChanged = (targetFps != activeFps);
      if (bitrateChanged || keyintChanged || fpsChanged) {
        if (!apply_encoder_target(activeEncodeW, activeEncodeH, targetFps, targetBitrate, targetKeyint)) {
          std::cerr << "[native-video-host][control] runtime-config apply failed seq=" << reqSeq << "\n";
          break;
        }
        runtimeTuneManualOverride = true;
        abrCooldownUntilUs = nowUs + 3000000ULL;
        abrGoodSeconds = 0;
        abrModeratePressureSeconds = 0;
        abrSeverePressureSeconds = 0;
        forceKeyNext = true;
        std::cout << "[native-video-host][control] runtime-config-applied seq=" << reqSeq
                  << " bitrate=" << activeBitrate
                  << " keyint=" << activeKeyint
                  << " fps=" << activeFps
                  << " abrOverride=1\n";
      }
    }
    {
      uint32_t reqSeq = 0;
      uint64_t requestedWindowId = 0;
      bool hasWindowSelectRequest = false;
      {
        std::lock_guard<std::mutex> lk(windowSelectionTxn.mu);
        if (windowSelectionTxn.pending) {
          reqSeq = windowSelectionTxn.reqSeq;
          requestedWindowId = windowSelectionTxn.requestedWindowId;
          hasWindowSelectRequest = true;
          windowSelectionTxn.pending = false;
        }
      }
      if (hasWindowSelectRequest) {
        uint32_t responseFlags = 0;
        uint64_t responseWindowId = requestedWindowId;
        uint64_t responseStreamGeneration = captureStreamGenerationState.load(std::memory_order_acquire);
        std::string responseReason;
        std::string responseTitle;
        const bool applied = apply_selected_window_capture(
            requestedWindowId, nowUs, &responseFlags, &responseWindowId, &responseStreamGeneration,
            &responseReason, &responseTitle);
        {
          std::lock_guard<std::mutex> lk(windowSelectionTxn.mu);
          windowSelectionTxn.responseFlags = responseFlags;
          windowSelectionTxn.responseWindowId = responseWindowId;
          windowSelectionTxn.responseStreamGeneration = responseStreamGeneration;
          windowSelectionTxn.responseReason = responseReason;
          windowSelectionTxn.responseTitle = responseTitle;
          windowSelectionTxn.completed = true;
        }
        windowSelectionTxn.cv.notify_all();

        std::cout << "[native-video-host][control] window-select seq=" << reqSeq
                  << " requestedId=" << requestedWindowId
                  << " applied=" << (applied ? 1 : 0)
                  << " selectedId=" << responseWindowId
                  << " streamGen=" << responseStreamGeneration
                  << " reason=" << (responseReason.empty() ? "none" : responseReason)
                  << " title=" << (responseTitle.empty() ? "<empty>" : responseTitle)
                  << "\n";
      }
    }
    if (captureModeReqPending.exchange(false, std::memory_order_acq_rel)) {
      const uint16_t reqMode = captureModeReqMode.load(std::memory_order_acquire);
      const uint32_t reqSeq = captureModeReqSeq.load(std::memory_order_acquire);
      const uint32_t reqXPermille = std::min<uint32_t>(10000u, captureModeReqXPermille.load(std::memory_order_acquire));
      const uint32_t reqYPermille = std::min<uint32_t>(10000u, captureModeReqYPermille.load(std::memory_order_acquire));
      if (reqMode == 1) {
        auto nextItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(control-overview)");
        if (!nextItem) {
          std::cerr << "[native-video-host][control] capture-mode overview failed seq=" << reqSeq << "\n";
        } else {
          item = nextItem;
          captureWindowModeActive = false;
          captureWindowCriteria.processNamesLower.clear();
          captureWindowCriteria.titleNeedleLower.clear();
          selectedWindowIdState.store(0u, std::memory_order_release);
          hostCaptureTargetFlags.store(0u, std::memory_order_release);
          hostCaptureTargetPid.store(0u, std::memory_order_release);
          hostCaptureTargetHwnd.store(0u, std::memory_order_release);
          {
            std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
            hostCaptureTargetProcess = "monitor";
            hostCaptureTargetTitle.clear();
          }
          lastCaptureRestartUs = nowUs;
          if (restart_capture_session()) {
            ++captureRestartCount;
            captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
            lastCaptureUsForInterval.store(0, std::memory_order_release);
            lastCallbackUs.store(0, std::memory_order_release);
            resetHostTimelineAnchors();
            if (!apply_capture_ui_quality_mode(true, nowUs)) {
              std::cerr << "[native-video-host][control] capture-mode overview quality apply failed seq=" << reqSeq
                        << "\n";
              break;
            }
            std::cout << "[native-video-host][control] capture-mode applied seq=" << reqSeq
                      << " mode=overview"
                      << " bitrate=" << activeBitrate
                      << " fps=" << activeFps
                      << " encode=" << activeEncodeW << "x" << activeEncodeH
                      << "\n";
          } else {
            std::cerr << "[native-video-host][control] capture-mode overview restart failed seq=" << reqSeq << "\n";
          }
        }
      } else if (reqMode == 2) {
        HMONITOR primaryMon = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monInfo{};
        monInfo.cbSize = sizeof(monInfo);
        if (!GetMonitorInfo(primaryMon, &monInfo)) {
          monInfo.rcMonitor.left = 0;
          monInfo.rcMonitor.top = 0;
          monInfo.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
          monInfo.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
        }
        const int monW = std::max<int>(1, monInfo.rcMonitor.right - monInfo.rcMonitor.left);
        const int monH = std::max<int>(1, monInfo.rcMonitor.bottom - monInfo.rcMonitor.top);
        POINT focusPt{};
        focusPt.x = monInfo.rcMonitor.left +
                    static_cast<int>((static_cast<uint64_t>(reqXPermille) * static_cast<uint64_t>(monW - 1) + 5000ULL) /
                                     10000ULL);
        focusPt.y = monInfo.rcMonitor.top +
                    static_cast<int>((static_cast<uint64_t>(reqYPermille) * static_cast<uint64_t>(monH - 1) + 5000ULL) /
                                     10000ULL);
        CaptureWindowInfo selected{};
        if (!find_top_level_window_at_point(focusPt, &selected)) {
          std::cerr << "[native-video-host][control] capture-mode focus no-window seq=" << reqSeq
                    << " xPermille=" << reqXPermille
                    << " yPermille=" << reqYPermille
                    << "\n";
        } else {
          auto nextItem = CreateItemForPrimaryMonitor(selected.hwnd, "CreateForWindow(control-focus-point)");
          if (!nextItem) {
            std::cerr << "[native-video-host][control] capture-mode focus create-item failed seq=" << reqSeq << "\n";
          } else {
            item = nextItem;
            captureWindowModeActive = true;
            captureWindowClientOnlyActive = true;
            captureWindowCriteria.processNamesLower.clear();
            if (!selected.processName.empty()) {
              captureWindowCriteria.processNamesLower.insert(selected.processName);
            }
            captureWindowCriteria.titleNeedleLower.clear();
            selectedWindowIdState.store(hwnd_to_id(selected.hwnd), std::memory_order_release);
            hostCaptureTargetFlags.store(0x1u | 0x2u, std::memory_order_release);
            hostCaptureTargetPid.store(selected.pid, std::memory_order_release);
            hostCaptureTargetHwnd.store(
                static_cast<uint64_t>(reinterpret_cast<uintptr_t>(selected.hwnd)), std::memory_order_release);
            {
              std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
              hostCaptureTargetProcess = selected.processName.empty() ? "unknown" : selected.processName;
              hostCaptureTargetTitle = selected.title.empty() ? std::string{} : wide_to_utf8(selected.title);
            }
            nextCaptureWindowCheckUs = nowUs + captureWindowRebindIntervalUs;
            lastCaptureRestartUs = nowUs;
            if (restart_capture_session()) {
              ++captureRestartCount;
              captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
              lastCaptureUsForInterval.store(0, std::memory_order_release);
              lastCallbackUs.store(0, std::memory_order_release);
              resetHostTimelineAnchors();
              if (!apply_capture_ui_quality_mode(false, nowUs)) {
                std::cerr << "[native-video-host][control] capture-mode focus quality apply failed seq=" << reqSeq
                          << "\n";
                break;
              }
              std::cout << "[native-video-host][control] capture-mode applied seq=" << reqSeq
                        << " mode=focus-window"
                        << " pid=" << selected.pid
                        << " process=" << (selected.processName.empty() ? "unknown" : selected.processName)
                        << " title=" << (selected.title.empty() ? "<empty>" : wide_to_utf8(selected.title))
                        << " bitrate=" << activeBitrate
                        << " fps=" << activeFps
                        << " encode=" << activeEncodeW << "x" << activeEncodeH
                        << "\n";
            } else {
              std::cerr << "[native-video-host][control] capture-mode focus restart failed seq=" << reqSeq << "\n";
            }
          }
        }
      }
    }
    if (captureWindowModeActive && captureWindowCriteria.enabled() && nowUs >= nextCaptureWindowCheckUs) {
      nextCaptureWindowCheckUs = nowUs + captureWindowRebindIntervalUs;
      CaptureWindowInfo latestWindowInfo{};
      if (find_capture_window(captureWindowCriteria, &latestWindowInfo)) {
        const uintptr_t currentRaw = static_cast<uintptr_t>(hostCaptureTargetHwnd.load(std::memory_order_acquire));
        const uintptr_t nextRaw = reinterpret_cast<uintptr_t>(latestWindowInfo.hwnd);
        if (nextRaw != currentRaw) {
          const auto nextItem =
              CreateItemForPrimaryMonitor(latestWindowInfo.hwnd, "CreateForWindow(target-window-rebind)");
          if (nextItem) {
            item = nextItem;
            selectedWindowIdState.store(hwnd_to_id(latestWindowInfo.hwnd), std::memory_order_release);
            hostCaptureTargetHwnd.store(static_cast<uint64_t>(nextRaw), std::memory_order_release);
            hostCaptureTargetPid.store(latestWindowInfo.pid, std::memory_order_release);
            {
              std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
              hostCaptureTargetProcess =
                  latestWindowInfo.processName.empty() ? std::string("unknown") : latestWindowInfo.processName;
              hostCaptureTargetTitle =
                  latestWindowInfo.title.empty() ? std::string{} : wide_to_utf8(latestWindowInfo.title);
            }
            const uint32_t rebindCount = hostCaptureRebindCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            hostCaptureTargetFlags.store((captureWindowModeActive ? 0x1u : 0x0u) |
                                             ((captureWindowModeActive && captureWindowClientOnlyActive) ? 0x2u : 0x0u),
                                         std::memory_order_release);
            std::string targetProc = "unknown";
            std::string targetTitle;
            {
              std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
              targetProc = hostCaptureTargetProcess;
              targetTitle = hostCaptureTargetTitle;
            }
            lastCaptureRestartUs = nowUs;
            if (restart_capture_session()) {
              ++captureRestartCount;
              captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
              lastCaptureUsForInterval.store(0, std::memory_order_release);
              lastCallbackUs.store(0, std::memory_order_release);
              resetHostTimelineAnchors();
              forceKeyNext = true;
              std::cout << "[native-video-host] capture-window rebound hwnd=0x" << std::hex << nextRaw << std::dec
                        << " pid=" << hostCaptureTargetPid.load(std::memory_order_relaxed)
                        << " process=" << targetProc
                        << " title=" << (targetTitle.empty() ? "<empty>" : targetTitle)
                        << " rebindCount=" << rebindCount
                        << " restartCount=" << captureRestartCount
                        << "\n";
            } else {
              std::cerr << "[native-video-host] capture-window rebind restart failed\n";
            }
          }
        }
      }
    }
    if (captureSizeChangePending.exchange(0, std::memory_order_acq_rel) != 0) {
      lastCaptureRestartUs = nowUs;
      if (restart_capture_session()) {
        ++captureRestartCount;
        captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
        lastCaptureUsForInterval.store(0, std::memory_order_release);
        lastCallbackUs.store(0, std::memory_order_release);
        resetHostTimelineAnchors();
        forceKeyNext = true;
        std::cout << "[native-video-host] capture session restarted reason=size-change count="
                  << captureRestartCount << "\n";
      } else {
        std::cerr << "[native-video-host] capture session restart failed reason=size-change\n";
      }
    }
    if (captureSessionReady.load(std::memory_order_acquire)) {
      const uint64_t lastCbUs = lastCallbackUs.load(std::memory_order_acquire);
      const uint64_t sessionStartUs = captureSessionStartedUs;
      const uint64_t stallBaseUs = (lastCbUs > 0) ? lastCbUs : sessionStartUs;
      const bool restartCooldownDone =
          (lastCaptureRestartUs == 0 ||
           nowUs >= (lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
      if (stallBaseUs > 0 && nowUs >= (stallBaseUs + kCaptureCallbackStallRestartUs) &&
          restartCooldownDone) {
        const uint64_t stallUs = nowUs - stallBaseUs;
        lastCaptureRestartUs = nowUs;
        const bool restarted = restart_capture_session();
        if (restarted) {
          ++captureRestartCount;
          captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
          lastCaptureUsForInterval.store(0, std::memory_order_release);
          lastCallbackUs.store(0, std::memory_order_release);
          resetHostTimelineAnchors();
          forceKeyNext = true;
          ++captureDeadRestartCount;
          std::cout << "[native-video-host] capture session restarted count=" << captureRestartCount
                    << " captureDeadRestartCount=" << captureDeadRestartCount
                    << " stallUs=" << stallUs
                    << " lastCallbackUs=" << lastCbUs
                    << "\n";
        } else {
          std::cerr << "[native-video-host] capture session restart failed stallUs=" << stallUs
                    << "\n";
        }
      }
    }
    if (paceByTick) {
      if (nowUs < nextTickUs) {
        const uint64_t paceWaitStartUs = qpc_now_us();
        const uint64_t paceWaitTargetUs = nextTickUs - nowUs;
        std::this_thread::sleep_for(std::chrono::microseconds(paceWaitTargetUs));
        const uint64_t paceWaitDoneUs = qpc_now_us();
        tickWaitUs = (paceWaitDoneUs >= paceWaitStartUs) ? (paceWaitDoneUs - paceWaitStartUs) : 0;
        continue;
      }
      if (nowUs > nextTickUs) {
        nextTickUs = nowUs;
      }
      nextTickUs += activeFrameIntervalUs;
    }

    std::shared_ptr<std::vector<uint8_t>> payload;
    uint32_t seq = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t stride = 0;
    uint64_t streamGeneration = 0;
    uint64_t captureUs = 0;
    uint64_t callbackUs = 0;
    uint64_t queuePushUs = 0;
    uint64_t callbackIntervalUs = 0;
    uint64_t captureIntervalUs = 0;
    uint64_t captureClockSkewUs = 0;
    uint64_t captureAgeAtCallbackUs = 0;
    uint64_t version = 0;
    uint32_t queueWaitReason = 0;  // 0: normal, 1: timeout, 2: no-work
    const uint64_t queueSelectStartUs = qpc_now_us();
    bool queueReady = false;
    {
      std::unique_lock<std::mutex> lk(frame.mu);
      queueReady = frame.cv.wait_for(lk, std::chrono::microseconds(effective_queue_wait_timeout_us()), [&] {
        return stop.load() || frame.version != lastVersionSent;
      });
      if (!queueReady && !stop.load()) {
        queueWaitReason = 1;
        ++queueWaitTimeoutCount;
        continue;
      }
      if (stop.load()) break;
      if (frame.version == lastVersionSent || !frame.payload || frame.payload->empty()) {
        queueWaitReason = 2;
        ++queueWaitNoWorkCount;
        continue;
      }
      version = frame.version;
      payload = frame.payload;
      seq = frame.seq;
      w = frame.width;
      h = frame.height;
      stride = frame.stride;
      streamGeneration = frame.streamGeneration;
      captureUs = frame.captureUs;
      callbackUs = frame.callbackUs;
      callbackIntervalUs = frame.callbackIntervalUs;
      captureIntervalUs = frame.captureIntervalUs;
      queuePushUs = frame.queuePushUs;
      captureAgeAtCallbackUs = frame.captureAgeAtCallbackUs;
      captureClockSkewUs = frame.captureClockSkewUs;
    }
  const uint64_t queuePopUs = qpc_now_us();
  const uint64_t queueSelectWaitUs =
      (queuePopUs >= queueSelectStartUs) ? (queuePopUs - queueSelectStartUs) : 0;
  const uint64_t frameAgeAtSelectUs =
      (callbackUs > 0 && queuePopUs >= callbackUs) ? (queuePopUs - callbackUs) : 0;
  const uint64_t captureToCallbackUs =
      (callbackUs > 0 && captureUs > 0)
          ? (callbackUs >= captureUs ? (callbackUs - captureUs) : (captureUs - callbackUs))
          : 0;
  const uint64_t captureToQueueUs =
      (queuePushUs > 0 && captureUs > 0)
          ? (queuePushUs >= captureUs ? (queuePushUs - captureUs) : (captureUs - queuePushUs))
          : 0;
    const uint64_t queueWaitUs =
        (queuePopUs > 0 && queuePushUs > 0 && queuePopUs >= queuePushUs) ? (queuePopUs - queuePushUs) : 0;
    const uint64_t queueGapFrames =
        (lastVersionSent > 0 && version > lastVersionSent) ? (version - lastVersionSent - 1) : 0;
    ++queuePopCount;
    const uint64_t lastPopVersionAtRead = lastPopFrameVersion.load(std::memory_order_acquire);
    const uint64_t queueDepthAtPop = (version > lastPopVersionAtRead) ? (version - lastPopVersionAtRead) : 0;
    update_u64_max(queueDepthMax, queueDepthAtPop);
    lastPopFrameVersion.store(version, std::memory_order_release);
    if (frameGatingEnabled && useH264 && payload && !payload->empty()) {
      if (frameGatingRefPayload && !frameGatingRefPayload->empty() &&
          frameGatingRefW == w && frameGatingRefH == h && frameGatingRefStride == stride) {
        frameGatingChangePermilleLast = estimate_bgra_change_permille(
            payload->data(), frameGatingRefPayload->data(), payload->size(), frameGatingSampleTarget);
        frameGatingChangePermilleSum += frameGatingChangePermilleLast;
        ++frameGatingChangePermilleCount;

        if (frameGatingChangePermilleLast <= frameGatingStaticThresholdPermille) {
          frameGatingStaticStreak = std::min<uint32_t>(frameGatingStaticStreak + 1, 60000);
          frameGatingMotionStreak = 0;
        } else if (frameGatingChangePermilleLast >= frameGatingMotionThresholdPermille) {
          frameGatingMotionStreak = std::min<uint32_t>(frameGatingMotionStreak + 1, 60000);
          frameGatingStaticStreak = 0;
        } else {
          if (frameGatingStaticStreak > 0) --frameGatingStaticStreak;
          if (frameGatingMotionStreak > 0) --frameGatingMotionStreak;
        }
      } else {
        frameGatingStaticStreak = 0;
        frameGatingMotionStreak = 0;
        frameGatingChangePermilleLast = 1000;
      }

      const bool prevStaticMode = frameGatingStaticMode;
      if (!frameGatingStaticMode && frameGatingStaticStreak >= frameGatingEnterFrames) {
        frameGatingStaticMode = true;
      } else if (frameGatingStaticMode && frameGatingMotionStreak >= frameGatingExitFrames) {
        frameGatingStaticMode = false;
      }
      if (prevStaticMode != frameGatingStaticMode) {
        std::cout << "[native-video-host] frame-gating mode="
                  << (frameGatingStaticMode ? "static" : "motion")
                  << " changePm=" << frameGatingChangePermilleLast
                  << " staticStreak=" << frameGatingStaticStreak
                  << " motionStreak=" << frameGatingMotionStreak
                  << "\n";
      }

      const bool keyReqPending = clientRequestedKeyFrame.load(std::memory_order_acquire);
      const uint64_t targetIntervalUs = frameGatingStaticMode ? frameGatingStaticIntervalUs : activeFrameIntervalUs;
      if (!keyReqPending &&
          frameGatingLastSentUs > 0 &&
          queuePopUs < (frameGatingLastSentUs + targetIntervalUs)) {
        ++frameGatingSkipCount;
        if (frameGatingStaticMode) ++frameGatingStaticSkipCount;
        lastVersionSent = version;
        continue;
      }
    }
    if (useH264 && guardStalePreEncode && frameAgeAtSelectUs > kMaxPreEncodeFrameAgeUs) {
      ++stalePreEncodeDropCount;
      continue;
    }
    if (lastVersionSent > 0 && version > lastVersionSent + 1) {
      skippedByOverwrite += (version - lastVersionSent - 1);
    }
    lastVersionSent = version;
    const uint64_t captureStampUs = (callbackUs > 0) ? callbackUs : captureUs;

    bool sendFailed = false;
    static uint64_t lastUserFeedbackUs = 0;
    if (useRaw) {
      RawFrameHeader hdr{};
      hdr.header.magic = remote60::native_poc::kMagic;
      hdr.header.type = static_cast<uint16_t>(MessageType::RawFrameBgra);
      hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
      hdr.seq = seq;
      hdr.width = w;
      hdr.height = h;
      hdr.stride = stride;
      hdr.payloadSize = static_cast<uint32_t>(payload->size());
      hdr.streamGeneration = streamGeneration;
      hdr.captureQpcUs = captureStampUs;
      hdr.encodeStartQpcUs = captureStampUs;
      hdr.encodeEndQpcUs = captureStampUs;
      SendPathStats sendPathStats{};
      const uint64_t sendStartUs = qpc_now_us();
      const uint64_t sendIntervalUs =
          (lastSendStartUs > 0 && sendStartUs >= lastSendStartUs) ? (sendStartUs - lastSendStartUs) : 0;
      const uint64_t sendIntervalErrUs =
          (activeFrameIntervalUs > 0 && sendIntervalUs > 0)
              ? ((sendIntervalUs >= activeFrameIntervalUs) ? (sendIntervalUs - activeFrameIntervalUs)
                                                           : (activeFrameIntervalUs - sendIntervalUs))
              : 0;
      const uint64_t queueToSendUs = (sendStartUs >= queuePopUs) ? (sendStartUs - queuePopUs) : 0;
      const uint64_t sendWaitUs = queueToSendUs;
      const uint64_t callbackToSendStartUs = (sendStartUs >= callbackUs) ? (sendStartUs - callbackUs) : 0;
      hdr.sendQpcUs = sendStartUs;
      const bool sentOk =
          (transport == VideoTransport::Tcp) &&
          send_all_timed(clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                         &sendPathStats.headerCallCount) &&
          send_all_timed(clientSock, payload->data(), payload->size(), &sendPathStats.payloadUs,
                         &sendPathStats.payloadCallCount);
      const uint64_t sendDoneUs = qpc_now_us();
      const uint64_t sendDurUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
      const uint64_t sendCallCount = sendPathStats.headerCallCount + sendPathStats.payloadCallCount;
      if (sentOk) {
        lastSendStartUs = sendStartUs;
        log_first_sent_generation("raw", streamGeneration, sendStartUs, hdr.captureQpcUs, hdr.width, hdr.height);
        if (frameGatingEnabled && useH264 && payload && !payload->empty()) {
          frameGatingLastSentUs = sendStartUs;
          frameGatingRefPayload = payload;
          frameGatingRefW = w;
          frameGatingRefH = h;
          frameGatingRefStride = stride;
        }
      }

      if (!sentOk) {
        if (reconnect_tcp_data_session("raw_send_fail")) {
          continue;
        }
        std::cout << "[native-video-host] client disconnected\n";
        break;
      }
      ++sentFrames;
      sentBytes += payload->size();
        if (args.traceEvery > 0 && (seq % args.traceEvery) == 0 &&
            (args.traceMax == 0 || tracePrinted < args.traceMax)) {
        ++tracePrinted;
        const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
        const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
        const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
        const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
            queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
          std::cout << "[native-video-host][trace] seq=" << seq
                    << " captureUs=" << hdr.captureQpcUs
                    << " encodeStartUs=" << hdr.encodeStartQpcUs
                    << " encodeEndUs=" << hdr.encodeEndQpcUs
                    << " sendUs=" << hdr.sendQpcUs
                    << " bottleneckStageCode=" << bottleneck.code
                    << " bottleneckStageUs=" << bottleneck.us
                    << " bottleneckStageName=" << bottleneck.name
                    << " c2eUs=" << c2eUs
                    << " captureToCallbackUs=" << captureToCallbackUs
                    << " callbackIntervalUs=" << callbackIntervalUs
                    << " captureIntervalUs=" << captureIntervalUs
                    << " captureClockSkewUs=" << captureClockSkewUs
                    << " selectWaitUs=" << frameAgeAtSelectUs
                    << " queueSelectWaitUs=" << queueSelectWaitUs
                   << " queueGapFrames=" << queueGapFrames
                   << " queueDepth=" << queueDepthAtPop
                   << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                   << " captureToQueueUs=" << captureToQueueUs
                   << " queueWaitUs=" << queueWaitUs
                   << " queueWaitReason=" << queueWaitReason
                   << " queueToSendUs=" << queueToSendUs
                   << " sendWaitUs=" << sendWaitUs
                   << " sendIntervalUs=" << sendIntervalUs
                   << " sendIntervalErrUs=" << sendIntervalErrUs
                   << " tickWaitUs=" << tickWaitUs
                   << " sendCallCount=" << sendCallCount
                   << " sendHeaderUs=" << sendPathStats.headerUs
                   << " sendPayloadUs=" << sendPathStats.payloadUs
                   << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                   << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                   << " sendChunkCount=" << sendPathStats.payloadChunkCount
                   << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                   << " sendStartUs=" << sendStartUs
                  << " sendDoneUs=" << sendDoneUs
                  << " sendDurUs=" << sendDurUs
                  << " encUs=" << encUs
                  << " e2sUs=" << e2sUs
                  << " payloadBytes=" << hdr.payloadSize
                  << "\n";
      }
      const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
      const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
      const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
      const uint64_t pipeUs = (hdr.sendQpcUs >= hdr.captureQpcUs) ? (hdr.sendQpcUs - hdr.captureQpcUs) : 0;
      const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
          queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
      if (pipeUs >= kHostUserFeedbackWarnUs &&
          (hdr.sendQpcUs >= lastUserFeedbackUs + kHostUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
        std::cout << "[native-video-host][user-feedback] seq=" << seq
                  << " codec=" << "raw"
                  << " pipeUs=" << pipeUs
                  << " bottleneckStageCode=" << bottleneck.code
                  << " bottleneckStageUs=" << bottleneck.us
                  << " bottleneckStageName=" << bottleneck.name
                  << " captureToCallbackUs=" << captureToCallbackUs
                  << " callbackIntervalUs=" << callbackIntervalUs
                  << " captureIntervalUs=" << captureIntervalUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " selectWaitUs=" << frameAgeAtSelectUs
                  << " queueSelectWaitUs=" << queueSelectWaitUs
                  << " captureToQueueUs=" << captureToQueueUs
                   << " queueWaitUs=" << queueWaitUs
                   << " queueWaitReason=" << queueWaitReason
                    << " queueGapFrames=" << queueGapFrames
                    << " queueDepth=" << queueDepthAtPop
                    << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                    << " queueToSendUs=" << queueToSendUs
                    << " sendIntervalUs=" << sendIntervalUs
                    << " sendIntervalErrUs=" << sendIntervalErrUs
                     << " captureClockSkewUs=" << captureClockSkewUs
                     << " sendWaitUs=" << sendWaitUs
                   << " tickWaitUs=" << tickWaitUs
                   << " sendCallCount=" << sendCallCount
                   << " sendHeaderUs=" << sendPathStats.headerUs
                   << " sendPayloadUs=" << sendPathStats.payloadUs
                   << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                   << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                   << " sendChunkCount=" << sendPathStats.payloadChunkCount
                   << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                   << " c2eUs=" << c2eUs
                  << " cb2eUs=" << callbackToSendStartUs
                  << " encUs=" << encUs
                  << " e2sUs=" << e2sUs
                  << " sendStartUs=" << sendStartUs
                  << " sendDoneUs=" << sendDoneUs
                  << " sendDurUs=" << sendDurUs
                  << "\n";
        lastUserFeedbackUs = hdr.sendQpcUs;
      }
      } else {
        const uint8_t* encodeSrc = payload->data();
      uint32_t encodeSrcW = w;
      uint32_t encodeSrcH = h;
      uint32_t encodeSrcStride = stride;
      std::vector<uint8_t> scaledBgra;
      uint64_t preEncodePrepUs = 0;
      uint64_t scaleUs = 0;
      uint64_t nv12Us = 0;
      const uint64_t preEncodeStartUs = qpc_now_us();
      if (activeEncodeW != w || activeEncodeH != h) {
        const uint64_t scaleStartUs = qpc_now_us();
        bool scaleOk = false;
        if (gpuScalerHealthy) {
          ++gpuScaleAttempts;
          scaleOk = gpuScaler.scale(payload->data(), w, h, stride, activeEncodeW, activeEncodeH, &scaledBgra);
          if (scaleOk) {
            ++gpuScaleSuccess;
          } else {
            ++gpuScaleFail;
            gpuScalerHealthy = false;
            std::cout << "[native-video-host] gpu scaler disabled after failure; fallback=cpu\n";
          }
        }
        if (!scaleOk) {
          ++gpuScaleCpuFallback;
          if (!resize_bgra_bilinear(payload->data(), w, h, stride, activeEncodeW, activeEncodeH, &scaledBgra)) {
            continue;
          }
        }
        encodeSrc = scaledBgra.data();
        encodeSrcW = activeEncodeW;
        encodeSrcH = activeEncodeH;
        encodeSrcStride = activeEncodeW * 4;
        const uint64_t scaleDoneUs = qpc_now_us();
        scaleUs = (scaleDoneUs >= scaleStartUs) ? (scaleDoneUs - scaleStartUs) : 0;
      }

      std::vector<uint8_t> nv12;
      const uint64_t nv12StartUs = qpc_now_us();
      if (!bgra_to_nv12(encodeSrc, encodeSrcW, encodeSrcH, encodeSrcStride, &nv12) || nv12.empty()) {
        continue;
      }
      const uint64_t nv12DoneUs = qpc_now_us();
      nv12Us = (nv12DoneUs >= nv12StartUs) ? (nv12DoneUs - nv12StartUs) : 0;
      preEncodePrepUs = (nv12DoneUs >= preEncodeStartUs) ? (nv12DoneUs - preEncodeStartUs) : 0;

      const uint64_t beforeEncodeUs = qpc_now_us();
      const uint64_t frameAgeBeforeEncodeUs =
          (callbackUs > 0 && beforeEncodeUs >= callbackUs) ? (beforeEncodeUs - callbackUs) : 0;
      uint64_t latestVersion = version;
      {
        std::lock_guard<std::mutex> lk(frame.mu);
        latestVersion = frame.version;
      }
      if (guardStalePreEncode &&
          frameAgeBeforeEncodeUs > kMaxPreEncodeFrameAgeUs && latestVersion != version) {
        ++stalePreEncodeDropCount;
        continue;
      }

       if (clientRequestedKeyFrame.exchange(false)) {
        const uint16_t reason = clientKeyFrameReason.load();
        std::cout << "[native-video-host][control] keyframe-request-consumed reason=" << reason << "\n";
        forceKeyNext = true;
      }
       const bool forceKeyFrame =
            forceKeyNext || (encodedSeq == 0) ||
            ((activeKeyint > 0) && ((seq % activeKeyint) == 0));
        const uint64_t encodeStartUs = qpc_now_us();
        const uint64_t encodeInputUs = captureStampUs;
        if (captureTimelineOriginUs < 0) {
          captureTimelineOriginUs = static_cast<int64_t>(encodeInputUs);
        }
        const uint64_t queueToEncodeUs = (encodeStartUs >= queuePopUs) ? (encodeStartUs - queuePopUs) : 0;
       const uint64_t callbackToEncodeStartUs =
            (encodeStartUs >= callbackUs) ? (encodeStartUs - callbackUs) : 0;
        std::vector<H264AccessUnit> units;
        H264EncodeFrameStats encodeStats{};
       if (!encoder.encode_frame(nv12, forceKeyFrame, static_cast<int64_t>(encodeInputUs) * 10, &units,
                                 &encodeStats)) {
        ++encodeFailCount;
        if ((encodeFailCount % 60) == 1) {
          std::cout << "[native-video-host] encode failed count=" << encodeFailCount << "\n";
        }
        continue;
      }
      const uint64_t encodeEndUs = qpc_now_us();
      if (units.empty()) continue;

      captureAgeSumUs += captureAgeAtCallbackUs;
      captureAgeMaxUs = std::max(captureAgeMaxUs, captureAgeAtCallbackUs);
      callbackToEncodeStartSumUs += callbackToEncodeStartUs;
      callbackToEncodeStartMaxUs = std::max(callbackToEncodeStartMaxUs, callbackToEncodeStartUs);

      bool encoderResetTriggered = false;
      bool sessionReconnectTriggered = false;
      bool countedRawForInput = false;
        for (const auto& au : units) {
          if (au.bytes.empty()) continue;
          const int64_t auCaptureUs = (au.sampleTimeHns > 0) ? (au.sampleTimeHns / 10) : static_cast<int64_t>(encodeInputUs);
          if (auTimelineOriginUs < 0 && captureTimelineOriginUs >= 0) {
            auTimelineOriginUs = static_cast<int64_t>(auCaptureUs) -
                                 (static_cast<int64_t>(encodeInputUs) - captureTimelineOriginUs);
          }
          const int64_t captureTimelineRelativeUs = static_cast<int64_t>(encodeInputUs) - captureTimelineOriginUs;
          const int64_t auTimelineRelativeUs = static_cast<int64_t>(auCaptureUs) - auTimelineOriginUs;
          const int64_t captureToAuTimelineDeltaUs = captureTimelineRelativeUs - auTimelineRelativeUs;
          const uint64_t captureToAuTimelineSkewUs =
              (captureToAuTimelineDeltaUs >= 0)
                  ? static_cast<uint64_t>(captureToAuTimelineDeltaUs)
                  : static_cast<uint64_t>(-captureToAuTimelineDeltaUs);
          const int64_t captureToAuSignedDeltaUs = static_cast<int64_t>(auCaptureUs) - static_cast<int64_t>(encodeInputUs);
          const uint64_t captureToAuSkewUs =
              (captureToAuSignedDeltaUs >= 0)
                  ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                  : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
          const uint64_t captureToAuUs = (captureToAuSignedDeltaUs >= 0)
                                             ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                             : 0;
          const uint64_t encodedAgeUs =
              (encodeEndUs >= static_cast<uint64_t>(auCaptureUs))
                  ? (encodeEndUs - static_cast<uint64_t>(auCaptureUs))
                  : 0;
        if (guardStaleEncoded && encodedAgeUs > kMaxEncodedFrameAgeUs) {
          ++staleEncodedDropCount;
          ++consecutiveStaleEncodedFrames;
          if ((staleEncodedDropCount % 60) == 1) {
            std::cout << "[native-video-host] stale encoded drop count=" << staleEncodedDropCount
                      << " encodedAgeUs=" << encodedAgeUs
                      << " thresholdUs=" << kMaxEncodedFrameAgeUs
                      << " consecutive=" << consecutiveStaleEncodedFrames
                      << "\n";
          }
          if (consecutiveStaleEncodedFrames >= kMaxConsecutiveStaleEncodedFrames) {
            std::cout << "[native-video-host] encoder reset due to stale output age="
                      << encodedAgeUs << "us consecutive=" << consecutiveStaleEncodedFrames << "\n";
            encoder.shutdown();
            if (!encoder.initialize(activeEncodeW, activeEncodeH, activeFps, activeBitrate, activeKeyint)) {
            std::cerr << "[native-video-host] encoder reinitialize failed\n";
              sendFailed = true;
              break;
            }
            resetHostTimelineAnchors();
            ++encoderResetCount;
            consecutiveStaleEncodedFrames = 0;
            forceKeyNext = true;
            encoderResetTriggered = true;
            break;
          }
          continue;
        }
        consecutiveStaleEncodedFrames = 0;

        EncodedFrameHeader hdr{};
        hdr.header.magic = remote60::native_poc::kMagic;
        hdr.header.type = static_cast<uint16_t>(MessageType::EncodedFrameH264);
        hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
        hdr.seq = ++encodedSeq;
        hdr.width = activeEncodeW;
        hdr.height = activeEncodeH;
        hdr.payloadSize = static_cast<uint32_t>(au.bytes.size());
        hdr.flags = (au.keyFrame || forceKeyFrame || forceKeyNext) ? 1u : 0u;
        hdr.streamGeneration = streamGeneration;
        hdr.captureQpcUs = encodeInputUs;
        hdr.encodeStartQpcUs = encodeStartUs;
        hdr.encodeEndQpcUs = encodeEndUs;
        SendPathStats sendPathStats{};
        const uint64_t sendStartUs = qpc_now_us();
        const uint64_t sendIntervalUs =
            (lastSendStartUs > 0 && sendStartUs >= lastSendStartUs) ? (sendStartUs - lastSendStartUs) : 0;
        const uint64_t sendIntervalErrUs =
            (activeFrameIntervalUs > 0 && sendIntervalUs > 0)
                ? ((sendIntervalUs >= activeFrameIntervalUs) ? (sendIntervalUs - activeFrameIntervalUs)
                                                             : (activeFrameIntervalUs - sendIntervalUs))
                : 0;
        const uint64_t queueToSendUs = (sendStartUs >= queuePopUs) ? (sendStartUs - queuePopUs) : 0;
        const uint64_t sendToEncodeUs = (sendStartUs >= encodeEndUs) ? (sendStartUs - encodeEndUs) : 0;
        const uint64_t encodeSpanUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
        const uint64_t sendWaitUs =
            (queueToSendUs >= (queueToEncodeUs + encodeSpanUs))
                ? (queueToSendUs - queueToEncodeUs - encodeSpanUs)
                : 0;
        const uint64_t callbackToSendStartUs = (sendStartUs >= callbackUs) ? (sendStartUs - callbackUs) : 0;
        hdr.sendQpcUs = sendStartUs;

        bool sentOk = false;
        if (transport == VideoTransport::Tcp) {
          sentOk = send_all_timed(clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                                  &sendPathStats.headerCallCount) &&
                   send_all_timed(clientSock, au.bytes.data(), au.bytes.size(), &sendPathStats.payloadUs,
                                 &sendPathStats.payloadCallCount);
        } else {
          if (!udpPeerReady) {
            ++udpTxNoPeer;
            sentOk = false;
          } else {
            UdpVideoChunkHeader udpHdr{};
            udpHdr.magic = remote60::native_poc::kMagic;
            udpHdr.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
            udpHdr.size = static_cast<uint16_t>(sizeof(udpHdr));
            udpHdr.seq = hdr.seq;
            udpHdr.codec = static_cast<uint16_t>(UdpCodec::H264);
            udpHdr.flags = (hdr.flags & 1u) ? 0x1u : 0u;
            udpHdr.width = hdr.width;
            udpHdr.height = hdr.height;
            udpHdr.stride = 0;
            udpHdr.payloadSize = hdr.payloadSize;
            udpHdr.streamGeneration = hdr.streamGeneration;
            udpHdr.captureQpcUs = hdr.captureQpcUs;
            udpHdr.encodeStartQpcUs = hdr.encodeStartQpcUs;
            udpHdr.encodeEndQpcUs = hdr.encodeEndQpcUs;
            udpHdr.sendQpcUs = hdr.sendQpcUs;
            sentOk = send_udp_chunks_timed(clientSock, udpPeer, au.bytes.data(), au.bytes.size(),
                                          udpHdr, args.udpMtu, &sendPathStats);
          }
        }
        const uint64_t sendDoneUs = qpc_now_us();
        const uint64_t sendDurUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
        const uint64_t sendCallCount = sendPathStats.headerCallCount + sendPathStats.payloadCallCount;
        if (sentOk) {
          lastSendStartUs = sendStartUs;
          log_first_sent_generation(
              transport == VideoTransport::Tcp ? "h264-tcp" : "h264-udp",
              streamGeneration, sendStartUs, hdr.captureQpcUs, hdr.width, hdr.height);
          if (transport == VideoTransport::Udp) {
            ++udpTxFrames;
            udpTxChunks += sendPathStats.payloadChunkCount;
            udpTxBytes += au.bytes.size();
          }
          if (frameGatingEnabled && payload && !payload->empty()) {
            frameGatingLastSentUs = sendStartUs;
            frameGatingRefPayload = payload;
            frameGatingRefW = w;
            frameGatingRefH = h;
            frameGatingRefStride = stride;
          }
        }
        if (!sentOk) {
          if (transport == VideoTransport::Udp) {
            ++udpTxFail;
            if (args.seconds == 0) {
              sessionReconnectTriggered = true;
              break;
            }
          } else if (reconnect_tcp_data_session("h264_send_fail")) {
            sessionReconnectTriggered = true;
            break;
          }
          sendFailed = true;
          break;
        }

        ++sentFrames;
        ++encodedFrames;
        sentBytes += au.bytes.size();
        if (!countedRawForInput) {
          rawEquivalentBytes += static_cast<uint64_t>(nv12.size());
          countedRawForInput = true;
        }
        if ((hdr.flags & 1u) != 0) {
          forceKeyNext = false;
        }

        if (args.traceEvery > 0 && (hdr.seq % args.traceEvery) == 0 &&
            (args.traceMax == 0 || tracePrinted < args.traceMax)) {
          ++tracePrinted;
          const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
          const uint64_t encQueueUs =
              (encodeStartUs >= static_cast<uint64_t>(auCaptureUs))
                  ? (encodeStartUs - static_cast<uint64_t>(auCaptureUs))
                  : 0;
          const uint64_t encQueueAlignedUs = (encodeStartUs >= encodeInputUs) ? (encodeStartUs - encodeInputUs) : 0;
          const uint64_t auTsFromOutput = au.sampleTimeFromOutput ? 1ull : 0ull;
          const uint64_t auTsSkewUs = (captureToAuSignedDeltaUs >= 0) ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                                                     : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
          const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
          const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
          const char* encBackendName = encoder.backend_name();
          const uint64_t encApiPathCode = encoder_api_path_code(encBackendName);
          const uint64_t encApiHw = encoder.using_hardware() ? 1ull : 0ull;
          const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
              queueWaitUs, queueToEncodeUs, preEncodePrepUs, scaleUs, nv12Us, encUs, queueToSendUs,
              sendDurUs, sendIntervalErrUs);
          std::cout << "[native-video-host][trace] seq=" << hdr.seq
                    << " captureUs=" << hdr.captureQpcUs
                    << " encodeStartUs=" << hdr.encodeStartQpcUs
                    << " encodeEndUs=" << hdr.encodeEndQpcUs
                    << " sendUs=" << hdr.sendQpcUs
                    << " bottleneckStageCode=" << bottleneck.code
                    << " bottleneckStageUs=" << bottleneck.us
                    << " bottleneckStageName=" << bottleneck.name
                    << " c2eUs=" << c2eUs
                    << " captureToCallbackUs=" << captureToCallbackUs
                    << " callbackIntervalUs=" << callbackIntervalUs
                    << " captureIntervalUs=" << captureIntervalUs
                    << " captureClockSkewUs=" << captureClockSkewUs
                    << " selectWaitUs=" << frameAgeAtSelectUs
                     << " queueSelectWaitUs=" << queueSelectWaitUs
                     << " queueGapFrames=" << queueGapFrames
                     << " encQueueUs=" << encQueueUs
                     << " encQueueAlignedUs=" << encQueueAlignedUs
                     << " captureToAuSkewUs=" << captureToAuSkewUs
                     << " captureToAuTimelineDeltaUs="
                     << (captureToAuTimelineDeltaUs >= 0 ? captureToAuTimelineDeltaUs : 0 - captureToAuTimelineDeltaUs)
                      << " captureToAuTimelineSkewUs=" << captureToAuTimelineSkewUs
                      << " auTsFromOutput=" << auTsFromOutput
                      << " auTsSkewUs=" << auTsSkewUs
                      << " captureTimelineOriginUs=" << captureTimelineOriginUs
                     << " auTimelineOriginUs=" << auTimelineOriginUs
                     << " captureTimelineRelativeUs=" << captureTimelineRelativeUs
                     << " auTimelineRelativeUs=" << auTimelineRelativeUs
                      << " frameCaptureUs=" << captureStampUs
                      << " captureToAuUs=" << captureToAuUs
                      << " auCaptureUs=" << static_cast<uint64_t>(auCaptureUs)
                      << " encodeInputUs=" << encodeInputUs
                      << " captureToQueueUs=" << captureToQueueUs
                     << " queueWaitUs=" << queueWaitUs
                     << " queueWaitReason=" << queueWaitReason
                     << " queueToEncodeUs=" << queueToEncodeUs
                     << " queueToSendUs=" << queueToSendUs
                     << " sendIntervalUs=" << sendIntervalUs
                     << " sendIntervalErrUs=" << sendIntervalErrUs
                     << " preEncodePrepUs=" << preEncodePrepUs
                     << " scaleUs=" << scaleUs
                     << " nv12Us=" << nv12Us
                     << " sendWaitUs=" << sendWaitUs
                     << " sendToEncodeUs=" << sendToEncodeUs
                     << " tickWaitUs=" << tickWaitUs
                     << " queueDepth=" << queueDepthAtPop
                    << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                    << " sendCallCount=" << sendCallCount
                    << " sendHeaderUs=" << sendPathStats.headerUs
                    << " sendPayloadUs=" << sendPathStats.payloadUs
                    << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                    << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                    << " sendChunkCount=" << sendPathStats.payloadChunkCount
                    << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                    << " sendStartUs=" << sendStartUs
                    << " sendDoneUs=" << sendDoneUs
                    << " sendDurUs=" << sendDurUs
                    << " cb2eUs=" << callbackToEncodeStartUs
                    << " capAgeUs=" << captureAgeAtCallbackUs
                    << " encUs=" << encUs
                    << " e2sUs=" << e2sUs
                    << " encApiPathCode=" << encApiPathCode
                    << " encApiHw=" << encApiHw
                    << " encApiInputUs=" << encodeStats.processInputUs
                    << " encApiDrainUs=" << encodeStats.processOutputDrainUs
                    << " encApiNotAcceptingCount=" << encodeStats.processInputNotAcceptingCount
                    << " encApiNeedMoreInputCount=" << encodeStats.processOutputNeedMoreInputCount
                    << " encApiStreamChangeCount=" << encodeStats.processOutputStreamChangeCount
                    << " encApiOutputErrorCount=" << encodeStats.processOutputErrorCount
                    << " encApiAsyncEnabled=" << encodeStats.asyncEnabled
                    << " encApiAsyncPollCount=" << encodeStats.asyncPollCount
                    << " encApiAsyncNoEventCount=" << encodeStats.asyncPollNoEventCount
                    << " encApiAsyncNeedInputCount=" << encodeStats.asyncPollNeedInputCount
                    << " encApiAsyncHaveOutputCount=" << encodeStats.asyncPollHaveOutputCount
                    << " payloadBytes=" << hdr.payloadSize
                    << " key=" << ((hdr.flags & 1u) ? 1 : 0)
                    << "\n";
        }
        const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
        const uint64_t encQueueUs =
            (encodeStartUs >= static_cast<uint64_t>(auCaptureUs)) ? (encodeStartUs - static_cast<uint64_t>(auCaptureUs)) : 0;
        const uint64_t encQueueAlignedUs = (encodeStartUs >= encodeInputUs) ? (encodeStartUs - encodeInputUs) : 0;
        const uint64_t auTsFromOutput = au.sampleTimeFromOutput ? 1ull : 0ull;
        const uint64_t auTsSkewUs = (captureToAuSignedDeltaUs >= 0) ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                                                   : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
        const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
        const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
        const uint64_t pipeUs = (hdr.sendQpcUs >= hdr.captureQpcUs) ? (hdr.sendQpcUs - hdr.captureQpcUs) : 0;
        const char* encBackendName = encoder.backend_name();
        const uint64_t encApiPathCode = encoder_api_path_code(encBackendName);
        const uint64_t encApiHw = encoder.using_hardware() ? 1ull : 0ull;
        const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
            queueWaitUs, queueToEncodeUs, preEncodePrepUs, scaleUs, nv12Us, encUs, queueToSendUs,
            sendDurUs, sendIntervalErrUs);
        if (pipeUs >= kHostUserFeedbackWarnUs &&
            (hdr.sendQpcUs >= lastUserFeedbackUs + kHostUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
        std::cout << "[native-video-host][user-feedback] seq=" << hdr.seq
                  << " codec=" << "h264"
                  << " pipeUs=" << pipeUs
                  << " bottleneckStageCode=" << bottleneck.code
                  << " bottleneckStageUs=" << bottleneck.us
                  << " bottleneckStageName=" << bottleneck.name
                  << " captureToCallbackUs=" << captureToCallbackUs
                    << " callbackIntervalUs=" << callbackIntervalUs
                    << " captureIntervalUs=" << captureIntervalUs
                    << " selectWaitUs=" << frameAgeAtSelectUs
                    << " queueSelectWaitUs=" << queueSelectWaitUs
                    << " captureClockSkewUs=" << captureClockSkewUs
                    << " captureToQueueUs=" << captureToQueueUs
                   << " queueWaitUs=" << queueWaitUs
                   << " queueWaitReason=" << queueWaitReason
                     << " queueGapFrames=" << queueGapFrames
                     << " queueDepth=" << queueDepthAtPop
                    << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                    << " queueToEncodeUs=" << queueToEncodeUs
                    << " queueToSendUs=" << queueToSendUs
                    << " sendIntervalUs=" << sendIntervalUs
                    << " sendIntervalErrUs=" << sendIntervalErrUs
                    << " captureClockSkewUs=" << captureClockSkewUs
                    << " sendWaitUs=" << sendWaitUs
                    << " sendToEncodeUs=" << sendToEncodeUs
                     << " tickWaitUs=" << tickWaitUs
                     << " preEncodePrepUs=" << preEncodePrepUs
                     << " scaleUs=" << scaleUs
                     << " nv12Us=" << nv12Us
                     << " c2eUs=" << c2eUs
                      << " encQueueUs=" << encQueueUs
                     << " encQueueAlignedUs=" << encQueueAlignedUs
                      << " captureToAuSkewUs=" << captureToAuSkewUs
                      << " captureToAuTimelineSkewUs=" << captureToAuTimelineSkewUs
                      << " auTsFromOutput=" << auTsFromOutput
                      << " auTsSkewUs=" << auTsSkewUs
                      << " captureToAuTimelineDeltaUs="
                      << (captureToAuTimelineDeltaUs >= 0 ? captureToAuTimelineDeltaUs : 0 - captureToAuTimelineDeltaUs)
                      << " captureTimelineOriginUs=" << captureTimelineOriginUs
                      << " auTimelineOriginUs=" << auTimelineOriginUs
                      << " captureTimelineRelativeUs=" << captureTimelineRelativeUs
                      << " auTimelineRelativeUs=" << auTimelineRelativeUs
                      << " frameCaptureUs=" << captureStampUs
                      << " captureToAuUs=" << captureToAuUs
                     << " auCaptureUs=" << static_cast<uint64_t>(auCaptureUs)
                     << " encodeInputUs=" << encodeInputUs
                   << " cb2eUs=" << callbackToEncodeStartUs
                   << " cb2sUs=" << callbackToSendStartUs
                    << " sendCallCount=" << sendCallCount
                    << " sendHeaderUs=" << sendPathStats.headerUs
                    << " sendPayloadUs=" << sendPathStats.payloadUs
                    << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                    << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                    << " sendChunkCount=" << sendPathStats.payloadChunkCount
                    << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                    << " sendStartUs=" << sendStartUs
                    << " sendDoneUs=" << sendDoneUs
                    << " sendDurUs=" << sendDurUs
                    << " capAgeUs=" << captureAgeAtCallbackUs
                    << " encUs=" << encUs
                    << " e2sUs=" << e2sUs
                    << " encApiPathCode=" << encApiPathCode
                    << " encApiHw=" << encApiHw
                    << " encApiInputUs=" << encodeStats.processInputUs
                    << " encApiDrainUs=" << encodeStats.processOutputDrainUs
                    << " encApiNotAcceptingCount=" << encodeStats.processInputNotAcceptingCount
                    << " encApiNeedMoreInputCount=" << encodeStats.processOutputNeedMoreInputCount
                    << " encApiStreamChangeCount=" << encodeStats.processOutputStreamChangeCount
                    << " encApiOutputErrorCount=" << encodeStats.processOutputErrorCount
                    << " encApiAsyncEnabled=" << encodeStats.asyncEnabled
                    << " encApiAsyncPollCount=" << encodeStats.asyncPollCount
                    << " encApiAsyncNoEventCount=" << encodeStats.asyncPollNoEventCount
                    << " encApiAsyncNeedInputCount=" << encodeStats.asyncPollNeedInputCount
                    << " encApiAsyncHaveOutputCount=" << encodeStats.asyncPollHaveOutputCount
                    << " payloadBytes=" << hdr.payloadSize
                    << " key=" << ((hdr.flags & 1u) ? 1 : 0)
                    << "\n";
          lastUserFeedbackUs = hdr.sendQpcUs;
        }
      }

      if (encoderResetTriggered || sessionReconnectTriggered) {
        continue;
      }
      if (sendFailed) {
        std::cout << "[native-video-host] client disconnected\n";
        break;
      }
    }

    const uint64_t t = qpc_now_us();
    if (t >= statAtUs) {
      const double mbps = (sentBytes * 8.0) / (1000.0 * 1000.0);
      std::string targetProcessName;
      {
        std::lock_guard<std::mutex> lk(hostCaptureMetaMu);
        targetProcessName = hostCaptureTargetProcess;
      }
      const uint64_t queuePushPerSec =
          (queuePushCount >= queuePushCountLastSample) ? (queuePushCount - queuePushCountLastSample) : 0;
      queuePushCountLastSample = queuePushCount;
      queuePushPerSecLatest = queuePushPerSec;
      const uint64_t callbackFramesPerSec = callbackFrames.load(std::memory_order_relaxed);
      const uint64_t idleHoldPerSec =
          (useH264 && captureSessionReady.load(std::memory_order_acquire) && callbackFramesPerSec == 0) ? 1ULL : 0ULL;
      idleHoldTotal += idleHoldPerSec;
      if (useH264 &&
          captureSessionReady.load(std::memory_order_acquire) &&
          !captureWindowModeActive) {
        const bool warmupDone =
            (captureInputStallWarmupSec == 0 ||
             t >= (startUs + static_cast<uint64_t>(captureInputStallWarmupSec) * 1000000ULL));
        if (warmupDone) {
          if (callbackFramesPerSec < static_cast<uint64_t>(captureInputMinPushPerSec)) {
            captureInputLowPushStreakSec += 1;
          } else {
            captureInputLowPushStreakSec = 0;
          }
          const bool restartCooldownDone =
              (lastCaptureRestartUs == 0 ||
               t >= (lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
          if (captureInputLowPushStreakSec >= captureInputStallConsecutiveSec && restartCooldownDone) {
            lastCaptureRestartUs = t;
            const bool restarted = restart_capture_session();
            if (restarted) {
              ++captureRestartCount;
              ++captureDeadRestartCount;
              captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
              lastCaptureUsForInterval.store(0, std::memory_order_release);
              lastCallbackUs.store(0, std::memory_order_release);
              resetHostTimelineAnchors();
              forceKeyNext = true;
              captureInputLowPushStreakSec = 0;
              std::cout << "[native-video-host] capture session restarted reason=capture-input-stall"
                        << " restartCount=" << captureRestartCount
                        << " captureDeadRestartCount=" << captureDeadRestartCount
                        << " callbackFramesPerSec=" << callbackFramesPerSec
                        << " minPushPerSec=" << captureInputMinPushPerSec
                        << " stallStreakSec=" << captureInputStallConsecutiveSec
                        << "\n";
            } else {
              std::cerr << "[native-video-host] capture session restart failed reason=capture-input-stall"
                        << " callbackFramesPerSec=" << callbackFramesPerSec
                        << " minPushPerSec=" << captureInputMinPushPerSec
                        << " streakSec=" << captureInputLowPushStreakSec
                        << "\n";
            }
          }
        }
      }
      if (useRaw) {
        std::cout << "[native-video-host] sentFrames=" << sentFrames
                  << " queuePushCount=" << queuePushCount
                  << " queuePopCount=" << queuePopCount
                  << " queuePushPerSec=" << queuePushPerSecLatest
                  << " idleHoldPerSec=" << idleHoldPerSec
                  << " idleHoldTotal=" << idleHoldTotal
                  << " captureInputLowPushStreakSec=" << captureInputLowPushStreakSec
                  << " captureDeadRestartCount=" << captureDeadRestartCount
                  << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                  << " queueWaitTimeoutCount=" << queueWaitTimeoutCount
                  << " queueWaitNoWorkCount=" << queueWaitNoWorkCount
                  << " captureRestarts=" << captureRestartCount
                  << " captureWindowRebindCount=" << hostCaptureRebindCount.load(std::memory_order_relaxed)
                  << " captureTargetPid=" << hostCaptureTargetPid.load(std::memory_order_relaxed)
                  << " captureTargetProc=" << targetProcessName
                  << " captureTargetHwnd=0x" << std::hex
                  << hostCaptureTargetHwnd.load(std::memory_order_relaxed) << std::dec
                  << " inputEvents=" << inputEvents.load()
                  << " inputIgnoredMove=" << inputIgnoredMove.load(std::memory_order_relaxed)
                  << " inputNoTarget=" << inputNoTarget.load(std::memory_order_relaxed)
                  << " inputUnsupported=" << inputUnsupported.load(std::memory_order_relaxed)
                  << " inputInjectFail=" << inputInjectFail.load(std::memory_order_relaxed)
                  << " keyReqDropTotal=" << clientKeyFrameRequestDropped.load()
                  << " callbackFrames=" << callbackFramesPerSec
                  << " skippedByOverwrite=" << skippedByOverwrite
                  << " frameGatingMode=" << (frameGatingStaticMode ? "static" : "motion")
                  << " frameGatingSkips=" << frameGatingSkipCount
                  << " frameGatingStaticSkips=" << frameGatingStaticSkipCount
                  << " mbps=" << mbps
                  << " size=" << w << "x" << h
                  << "\n";
      } else {
        const uint64_t capAgeAvgUs = (encodedFrames > 0) ? (captureAgeSumUs / encodedFrames) : 0;
        const uint64_t cb2eAvgUs = (encodedFrames > 0) ? (callbackToEncodeStartSumUs / encodedFrames) : 0;
        const uint64_t frameGatingChangeAvgPm =
            (frameGatingChangePermilleCount > 0)
                ? (frameGatingChangePermilleSum / frameGatingChangePermilleCount)
                : frameGatingChangePermilleLast;
        const double rawEquivMbps = (rawEquivalentBytes * 8.0) / (1000.0 * 1000.0);
        const uint64_t encRatioX100 =
            (sentBytes > 0) ? ((rawEquivalentBytes * 100ULL) / sentBytes) : 0;
        const uint64_t udpTxChunkPerFrameX100 =
            (udpTxFrames > 0) ? ((udpTxChunks * 100ULL) / udpTxFrames) : 0;
        std::cout << "[native-video-host] encodedFrames=" << encodedFrames
                  << " sentFrames=" << sentFrames
                  << " queuePushCount=" << queuePushCount
                  << " queuePopCount=" << queuePopCount
                  << " queuePushPerSec=" << queuePushPerSecLatest
                  << " idleHoldPerSec=" << idleHoldPerSec
                  << " idleHoldTotal=" << idleHoldTotal
                  << " captureInputLowPushStreakSec=" << captureInputLowPushStreakSec
                  << " captureDeadRestartCount=" << captureDeadRestartCount
                  << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                  << " queueWaitTimeoutCount=" << queueWaitTimeoutCount
                  << " queueWaitNoWorkCount=" << queueWaitNoWorkCount
                  << " captureRestarts=" << captureRestartCount
                  << " captureWindowRebindCount=" << hostCaptureRebindCount.load(std::memory_order_relaxed)
                  << " captureTargetPid=" << hostCaptureTargetPid.load(std::memory_order_relaxed)
                  << " captureTargetProc=" << targetProcessName
                  << " captureTargetHwnd=0x" << std::hex
                  << hostCaptureTargetHwnd.load(std::memory_order_relaxed) << std::dec
                  << " callbackFrames=" << callbackFramesPerSec
                  << " skippedByOverwrite=" << skippedByOverwrite
                  << " stalePreEncodeDrops=" << stalePreEncodeDropCount
                  << " staleEncodedDrops=" << staleEncodedDropCount
                  << " encoderResets=" << encoderResetCount
                  << " keyReqTotal=" << clientKeyFrameRequestCount.load()
                  << " keyReqDropTotal=" << clientKeyFrameRequestDropped.load()
                  << " inputEvents=" << inputEvents.load()
                  << " inputIgnoredMove=" << inputIgnoredMove.load(std::memory_order_relaxed)
                  << " inputNoTarget=" << inputNoTarget.load(std::memory_order_relaxed)
                  << " inputUnsupported=" << inputUnsupported.load(std::memory_order_relaxed)
                  << " inputInjectFail=" << inputInjectFail.load(std::memory_order_relaxed)
                  << " capAgeAvgUs=" << capAgeAvgUs
                  << " capAgeMaxUs=" << captureAgeMaxUs
                  << " cb2eAvgUs=" << cb2eAvgUs
                  << " cb2eMaxUs=" << callbackToEncodeStartMaxUs
                  << " mbps=" << mbps
                  << " rawEquivMbps=" << rawEquivMbps
                  << " encRatioX100=" << encRatioX100
                  << " udpTxFrames=" << udpTxFrames
                  << " udpTxChunks=" << udpTxChunks
                  << " udpTxChunkPerFrameX100=" << udpTxChunkPerFrameX100
                  << " udpTxBytes=" << udpTxBytes
                  << " udpTxFail=" << udpTxFail
                  << " udpTxNoPeer=" << udpTxNoPeer
                  << " bitrateTarget=" << activeBitrate
                  << " fpsTarget=" << activeFps
                  << " keyintTarget=" << activeKeyint
                  << " size=" << activeEncodeW << "x" << activeEncodeH
                  << " gpuScaleReq=" << (gpuScalerRequested ? 1 : 0)
                  << " gpuScaleReady=" << (gpuScalerHealthy ? 1 : 0)
                  << " gpuScaleAttempts=" << gpuScaleAttempts
                  << " gpuScaleSuccess=" << gpuScaleSuccess
                  << " gpuScaleFail=" << gpuScaleFail
                  << " gpuScaleCpuFallback=" << gpuScaleCpuFallback
                  << " abrProfile=" << ((abrProfile == 0) ? "high" : ((abrProfile == 1) ? "mid" : "low"))
                  << " abrModSec=" << abrModeratePressureSeconds
                  << " abrSevSec=" << abrSeverePressureSeconds
                  << " abrGoodSec=" << abrGoodSeconds
                  << " abrOverride=" << (runtimeTuneManualOverride ? 1 : 0)
                  << " frameGatingMode=" << (frameGatingStaticMode ? "static" : "motion")
                  << " frameGatingSkips=" << frameGatingSkipCount
                  << " frameGatingStaticSkips=" << frameGatingStaticSkipCount
                  << " frameGatingChangePm=" << frameGatingChangePermilleLast
                  << " frameGatingChangeAvgPm=" << frameGatingChangeAvgPm
                  << "\n";

        const uint64_t metricsUpdatedUs = clientMetricsUpdatedUs.load();
        const bool metricsFresh =
            (metricsUpdatedUs > 0) && (t >= metricsUpdatedUs) && ((t - metricsUpdatedUs) <= 3000000ULL);
        const uint64_t clAvgLatencyUs = metricsFresh ? clientMetricsAvgLatencyUs.load() : 0;
        const uint64_t clAvgDecodeTailUs = metricsFresh ? clientMetricsAvgDecodeTailUs.load() : 0;
        const uint32_t clDecodedFpsX100 = metricsFresh ? clientMetricsDecodedFpsX100.load() : 0;
        const uint32_t clRecvMbpsX1000 = metricsFresh ? clientMetricsRecvMbpsX1000.load() : 0;
        const uint32_t clWidth = metricsFresh ? clientMetricsWidth.load() : 0;
        const uint32_t clHeight = metricsFresh ? clientMetricsHeight.load() : 0;
        const uint32_t clCongestionState = metricsFresh ? clientMetricsCongestionState.load() : 0;
        const uint32_t clCongestionTransitions = metricsFresh ? clientMetricsCongestionTransitions.load() : 0;
        const uint32_t clCongestionRecoveryCount = metricsFresh ? clientMetricsCongestionRecoveryCount.load() : 0;
        const uint32_t clCongestionRecoveryReq = metricsFresh ? clientMetricsCongestionRecoveryReq.load() : 0;
        const uint32_t clCongestionRecoveryMaxUs = metricsFresh ? clientMetricsCongestionRecoveryMaxUs.load() : 0;
        const uint32_t clQueueDepthMax = metricsFresh ? clientMetricsQueueDepthMax.load() : 0;
        const uint32_t clQueueDepthH4p = metricsFresh ? clientMetricsQueueDepthH4p.load() : 0;
        const uint32_t clUdpDropPm = metricsFresh ? clientMetricsUdpAssemblyDropPm.load() : 0;

        if (abrEnabled && !runtimeTuneManualOverride && !m9Apply) {
          const uint32_t minGoodFpsX100 = args.fps * (abrQualityFirst ? 95u : 93u);
          const uint32_t minOkayFpsX100 = args.fps * (abrQualityFirst ? 90u : 85u);
          const uint32_t minDegradeFpsX100 = args.fps * (abrQualityFirst ? 55u : 45u);
          const uint32_t minSevereFpsX100 = args.fps * (abrQualityFirst ? 45u : 35u);
          const bool abrWarmupDone = (t >= (startUs + 4000000ULL));

          const uint64_t severeLatencyUs = abrQualityFirst ? 170000ULL : 150000ULL;
          const uint64_t severeTailUs = abrQualityFirst ? 140000ULL : 110000ULL;
          const uint64_t moderateLatencyUs = abrQualityFirst ? 145000ULL : 125000ULL;
          const uint64_t moderateTailUs = abrQualityFirst ? 120000ULL : 90000ULL;
          const uint64_t emergencyLatencyUs = abrQualityFirst ? 260000ULL : 220000ULL;
          const uint64_t emergencyTailUs = abrQualityFirst ? 190000ULL : 160000ULL;

          const bool severeDownByClient =
              metricsFresh &&
              (clAvgLatencyUs > severeLatencyUs ||
               clAvgDecodeTailUs > severeTailUs ||
               (clDecodedFpsX100 < minSevereFpsX100 &&
                (clAvgLatencyUs > (severeLatencyUs - 30000ULL) || clAvgDecodeTailUs > (severeTailUs - 40000ULL))));
          const bool moderateDownByClient =
              metricsFresh &&
              (clAvgLatencyUs > moderateLatencyUs ||
               clAvgDecodeTailUs > moderateTailUs ||
               (clDecodedFpsX100 < minDegradeFpsX100 &&
                (clAvgLatencyUs > (moderateLatencyUs - 50000ULL) ||
                 clAvgDecodeTailUs > (moderateTailUs - 30000ULL))));
          const bool emergencyDownByClient =
              metricsFresh &&
              (clAvgLatencyUs > emergencyLatencyUs ||
               clAvgDecodeTailUs > emergencyTailUs);
          const bool severeDownByHost = (!metricsFresh && cb2eAvgUs > (abrQualityFirst ? 110000ULL : 90000ULL));
          const bool moderateDownByHost = (!metricsFresh && cb2eAvgUs > (abrQualityFirst ? 90000ULL : 70000ULL));
          const bool severeDown = abrWarmupDone && (severeDownByClient || severeDownByHost);
          const bool moderateDown = abrWarmupDone && (moderateDownByClient || moderateDownByHost);
          const bool emergencyDown = abrWarmupDone && emergencyDownByClient;

          if (severeDown) {
            ++abrSeverePressureSeconds;
          } else {
            abrSeverePressureSeconds = 0;
          }
          if (moderateDown) {
            ++abrModeratePressureSeconds;
          } else {
            abrModeratePressureSeconds = 0;
          }

          const bool goodForLowToMid =
              metricsFresh &&
              (clAvgLatencyUs < 90000ULL) &&
              (clAvgDecodeTailUs < 65000ULL) &&
              (clDecodedFpsX100 >= minOkayFpsX100);
          const bool goodForMidToHigh =
              metricsFresh &&
              (clAvgLatencyUs < 75000ULL) &&
              (clAvgDecodeTailUs < 50000ULL) &&
              (clDecodedFpsX100 >= minGoodFpsX100);

          int targetProfile = abrProfile;
          const char* abrReason = "none";
          if (t >= abrCooldownUntilUs) {
            const uint32_t highToMidSevereSec = abrQualityFirst ? 3u : 2u;
            const uint32_t highToMidModerateSec = abrQualityFirst ? 6u : 4u;
            const uint32_t midToLowSevereSec = abrQualityFirst ? 4u : 3u;
            const uint32_t midToLowModerateSec = abrQualityFirst ? 8u : 5u;
            const uint32_t lowToMidGoodSec = abrQualityFirst ? 8u : 5u;
            const uint32_t midToHighGoodSec = abrQualityFirst ? 12u : 8u;

            if (abrProfile == 0) {
              if (emergencyDown && abrHasLowProfile && abrSeverePressureSeconds >= 1) {
                targetProfile = 2;
                abrReason = "client_emergency";
              } else if ((abrSeverePressureSeconds >= highToMidSevereSec) || (abrModeratePressureSeconds >= highToMidModerateSec)) {
                if (abrHasMidProfile) {
                  targetProfile = 1;
                  abrReason = (abrSeverePressureSeconds >= highToMidSevereSec) ? "high_to_mid_severe" : "high_to_mid_moderate";
                } else if (abrHasLowProfile) {
                  targetProfile = 2;
                  abrReason = (abrSeverePressureSeconds >= highToMidSevereSec) ? "high_to_low_severe" : "high_to_low_moderate";
                }
              }
              abrGoodSeconds = 0;
            } else if (abrProfile == 1) {
              if (emergencyDown && abrHasLowProfile) {
                targetProfile = 2;
                abrReason = "client_emergency";
                abrGoodSeconds = 0;
              } else if ((abrSeverePressureSeconds >= midToLowSevereSec || abrModeratePressureSeconds >= midToLowModerateSec) && abrHasLowProfile) {
                targetProfile = 2;
                abrReason = (abrSeverePressureSeconds >= midToLowSevereSec) ? "mid_to_low_severe" : "mid_to_low_moderate";
                abrGoodSeconds = 0;
              } else {
                if (goodForMidToHigh) {
                  ++abrGoodSeconds;
                } else {
                  abrGoodSeconds = 0;
                }
                if (abrGoodSeconds >= midToHighGoodSec) {
                  targetProfile = 0;
                  abrReason = "client_stable_high";
                }
              }
            } else {  // abrProfile == 2
              if (goodForLowToMid) {
                ++abrGoodSeconds;
              } else {
                abrGoodSeconds = 0;
              }
              if (abrGoodSeconds >= lowToMidGoodSec) {
                targetProfile = abrHasMidProfile ? 1 : 0;
                abrReason = "client_stable_mid";
              }
            }
          }

          if (targetProfile != abrProfile) {
            uint32_t targetW = abrHighW;
            uint32_t targetH = abrHighH;
            uint32_t targetBitrate = abrHighBitrate;
            if (targetProfile == 1) {
              targetW = abrMidW;
              targetH = abrMidH;
              targetBitrate = abrMidBitrate;
            } else if (targetProfile == 2) {
              targetW = abrLowW;
              targetH = abrLowH;
              targetBitrate = abrLowBitrate;
            }

            if (!apply_encoder_target(targetW, targetH, activeFps, targetBitrate, activeKeyint)) {
              std::cerr << "[native-video-host][abr] encoder profile apply failed\n";
              break;
            }

            abrProfile = targetProfile;
            abrGoodSeconds = 0;
            abrModeratePressureSeconds = 0;
            abrSeverePressureSeconds = 0;
            abrCooldownUntilUs = t + 4000000ULL;
            forceKeyNext = true;

            std::cout << "[native-video-host][abr] profile="
                      << ((abrProfile == 0) ? "high" : ((abrProfile == 1) ? "mid" : "low"))
                      << " encode=" << activeEncodeW << "x" << activeEncodeH
                      << " bitrate=" << activeBitrate
                      << " reason=" << abrReason
                      << " clientSize=" << clWidth << "x" << clHeight
                      << " clientDecodedFps=" << (clDecodedFpsX100 / 100.0)
                      << " clientAvgLatUs=" << clAvgLatencyUs
                      << " clientAvgTailUs=" << clAvgDecodeTailUs
                      << " clientMbps=" << (clRecvMbpsX1000 / 1000.0)
                      << "\n";
          }
        }

        if (m9Enabled && !runtimeTuneManualOverride) {
          const bool downByClient =
              metricsFresh &&
              (clCongestionState == 2 ||
               clDecodedFpsX100 < m9DecodedFpsFloorX100 ||
               clQueueDepthMax >= m9QueueDepthHighFrames ||
               clUdpDropPm >= m9UdpDropPmHigh ||
               clAvgLatencyUs >= m9LatencyHighUs ||
               clAvgDecodeTailUs >= m9TailHighUs);
          const bool downByHostFallback =
              (!metricsFresh && cb2eAvgUs >= m9TailHighUs);
          const bool downPressure = downByClient || downByHostFallback;
          const bool upPressure =
              metricsFresh &&
              clCongestionState == 0 &&
              clDecodedFpsX100 >= m9DecodedFpsRecoverX100 &&
              clQueueDepthMax <= m9QueueDepthLowFrames &&
              clUdpDropPm <= m9UdpDropPmLow &&
              clAvgLatencyUs <= m9LatencyLowUs &&
              clAvgDecodeTailUs <= m9TailLowUs;

          if (downPressure) {
            ++m9DownPressureSeconds;
          } else {
            m9DownPressureSeconds = 0;
          }
          if (upPressure) {
            ++m9UpPressureSeconds;
          } else {
            m9UpPressureSeconds = 0;
          }

          int targetLevel = m9Level;
          const char* m9Reason = "hold";
          if (t >= m9CooldownUntilUs) {
            if (downPressure && m9DownPressureSeconds >= m9DownRequireSec && targetLevel < 3) {
              ++targetLevel;
              m9Reason = downByClient ? "client_pressure" : "host_fallback_pressure";
            } else if (upPressure && m9UpPressureSeconds >= m9UpRequireSec && targetLevel > 0) {
              --targetLevel;
              m9Reason = "client_recovered";
            }
          }

          auto m9_level_bitrate = [&](int level) -> uint32_t {
            if (level <= 0) return m9BitrateLevel0;
            if (level == 1) return m9BitrateLevel1;
            if (level == 2) return m9BitrateLevel2;
            return m9BitrateLevel3;
          };
          auto m9_level_fps = [&](int level) -> uint32_t {
            if (level <= 0) return m9FpsLevel0;
            if (level == 1) return m9FpsLevel1;
            if (level == 2) return m9FpsLevel2;
            return m9FpsLevel3;
          };
          auto m9_level_w = [&](int level) -> uint32_t {
            if (level <= 0) return m9WidthLevel0;
            if (level == 1) return m9WidthLevel1;
            if (level == 2) return m9WidthLevel2;
            return m9WidthLevel3;
          };
          auto m9_level_h = [&](int level) -> uint32_t {
            if (level <= 0) return m9HeightLevel0;
            if (level == 1) return m9HeightLevel1;
            if (level == 2) return m9HeightLevel2;
            return m9HeightLevel3;
          };

          if (targetLevel != m9Level) {
            const char* action = (targetLevel > m9Level) ? "down" : "up";
            const uint32_t targetBitrate = m9_level_bitrate(targetLevel);
            const uint32_t targetFps = m9_level_fps(targetLevel);
            const uint32_t targetW = m9_level_w(targetLevel);
            const uint32_t targetH = m9_level_h(targetLevel);
            std::cout << "[native-video-host][m9] action=" << action
                      << " mode=" << (m9Apply ? "apply" : "dryrun")
                      << " fromLevel=" << m9Level
                      << " toLevel=" << targetLevel
                      << " reason=" << m9Reason
                      << " targetBitrate=" << targetBitrate
                      << " targetFps=" << targetFps
                      << " targetSize=" << targetW << "x" << targetH
                      << " decodedFps=" << (clDecodedFpsX100 / 100.0)
                      << " avgLatUs=" << clAvgLatencyUs
                      << " avgTailUs=" << clAvgDecodeTailUs
                      << " queueDepthMax=" << clQueueDepthMax
                      << " queueDepthH4p=" << clQueueDepthH4p
                      << " udpDropPm=" << clUdpDropPm
                      << " congState=" << clCongestionState
                      << " congTrans=" << clCongestionTransitions
                      << " congRecCnt=" << clCongestionRecoveryCount
                      << " congRecReq=" << clCongestionRecoveryReq
                      << " congRecMaxUs=" << clCongestionRecoveryMaxUs
                      << "\n";
            if (m9Apply) {
              if (!apply_encoder_target(targetW, targetH, targetFps, targetBitrate, activeKeyint)) {
                std::cerr << "[native-video-host][m9] encoder target apply failed level=" << targetLevel << "\n";
                break;
              }
              forceKeyNext = true;
            }
            m9Level = targetLevel;
            m9CooldownUntilUs = t + static_cast<uint64_t>(m9CooldownSec) * 1000000ULL;
            m9DownPressureSeconds = 0;
            m9UpPressureSeconds = 0;
          }
        }
      }
      sentFrames = 0;
      encodedFrames = 0;
      sentBytes = 0;
      rawEquivalentBytes = 0;
      udpTxFrames = 0;
      udpTxChunks = 0;
      udpTxBytes = 0;
      udpTxFail = 0;
      udpTxNoPeer = 0;
      skippedByOverwrite = 0;
      stalePreEncodeDropCount = 0;
      staleEncodedDropCount = 0;
      encoderResetCount = 0;
      callbackFrames = 0;
      captureAgeSumUs = 0;
      captureAgeMaxUs = 0;
      callbackToEncodeStartSumUs = 0;
      callbackToEncodeStartMaxUs = 0;
      gpuScaleAttempts = 0;
      gpuScaleSuccess = 0;
      gpuScaleFail = 0;
      gpuScaleCpuFallback = 0;
      frameGatingSkipCount = 0;
      frameGatingStaticSkipCount = 0;
      frameGatingChangePermilleSum = 0;
      frameGatingChangePermilleCount = 0;
      statAtUs += 1000000ULL;
    }
  }

  stop = true;
  frame.cv.notify_all();
  windowSelectionTxn.cv.notify_all();
  {
    SOCKET ctlSock = controlClientSock.exchange(INVALID_SOCKET);
    if (ctlSock != INVALID_SOCKET) {
      shutdown(ctlSock, SD_BOTH);
      closesocket(ctlSock);
    }
  }
  if (controlListenSock != INVALID_SOCKET) {
    closesocket(controlListenSock);
    controlListenSock = INVALID_SOCKET;
  }
  if (controlThread.joinable()) controlThread.join();
  detach_capture_session();
  if (clientSock != INVALID_SOCKET) {
    closesocket(clientSock);
    clientSock = INVALID_SOCKET;
  }
  if (listenSock != INVALID_SOCKET) {
    closesocket(listenSock);
    listenSock = INVALID_SOCKET;
  }
  if (useH264) {
    encoder.shutdown();
    if (mfStarted) MFShutdown();
  }
  std::cout << "[native-video-host] done\n";
  return 0;
}
