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
#include <deque>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
#include "bind_port_candidates.hpp"
#include "capture_cadence_gate.hpp"
#include "d3d_capture_readback.hpp"
#include "directory_client.hpp"
#include "encode_resolution_ladder.hpp"
#include "gdi_capture_process.hpp"
#include "json_profile.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "secure_input_broker.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "capture_backend_dxgi.hpp"

namespace {

// Every host log line is prefixed with a wall-clock timestamp so a capture can be lined up against
// the client's own timestamped log (which uses the same MM-DD HH:MM:SS form). Rather than touch the
// hundreds of std::cout/std::cerr sites, this filtering streambuf is slipped under both streams: it
// buffers each line per-thread and, on the terminating newline, emits "timestamp + line" as one
// locked write so concurrent log threads can never split a line or interleave a stamp mid-line.
class TimestampPrefixBuf : public std::streambuf {
 public:
  explicit TimestampPrefixBuf(std::streambuf* dest) : dest_(dest) {}

 protected:
  int_type overflow(int_type ch) override {
    if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
    const char c = traits_type::to_char_type(ch);
    std::string& line = tls_line();
    line.push_back(c);
    if (c == '\n') flush_line(line);
    return ch;
  }
  std::streamsize xsputn(const char* s, std::streamsize n) override {
    std::string& line = tls_line();
    for (std::streamsize i = 0; i < n; ++i) {
      line.push_back(s[i]);
      if (s[i] == '\n') flush_line(line);
    }
    return n;
  }
  int sync() override {
    std::lock_guard<std::mutex> lk(mu_);
    return dest_->pubsync();
  }

 private:
  static std::string& tls_line() {
    static thread_local std::string line;
    return line;
  }
  static std::string timestamp_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d:%02d.%03d ", tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    return std::string(buf);
  }
  void flush_line(std::string& line) {
    const std::string ts = timestamp_now();
    std::lock_guard<std::mutex> lk(mu_);
    dest_->sputn(ts.data(), static_cast<std::streamsize>(ts.size()));
    dest_->sputn(line.data(), static_cast<std::streamsize>(line.size()));
    line.clear();
  }

  std::streambuf* dest_;
  std::mutex mu_;
};

// Phase of the host's main capture/encode loop, published for the liveness watchdog. A permanent
// hang inside a GPU/MFT/driver call stops the loop WITHOUT crashing, so neither the in-loop
// self-heal (which never runs) nor the supervisor (which only relaunches on a crash) recovers it.
// The watchdog reads the last phase + how long the loop has been stuck to decide when to terminate.
enum class MainLoopPhase : uint32_t {
  Startup = 0,
  Loop,            // between iterations / ordinary work -- 10s hang threshold
  CaptureRestart,  // restart_capture_session: legitimately slow (device/pool rebuild) -- 20s
  EncodeCall,      // MFT encode of a frame -- the prime suspect for a driver/MFT wedge -- 10s
};
// Terminate exit code the watchdog uses; the supervisor treats it as "wedged, relaunch fast" and
// keeps it out of the crash streak / nv12 auto-disable (it is a recovery, not a crash).
constexpr unsigned int kExitMainLoopWatchdog = 43;

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::native_poc::ControlLink;
using remote60::native_poc::TcpControlLink;
using remote60::native_poc::UdpControlChannel;
using remote60::native_poc::UdpControlLink;
using remote60::native_poc::ControlInputAckMessage;
using remote60::native_poc::ControlInputEventMessage;
using remote60::native_poc::ControlInputTextMessage;
using remote60::native_poc::ControlStreamStateMessage;
using remote60::native_poc::ControlClientMetricsMessage;
using remote60::native_poc::ControlRequestKeyFrameMessage;
using remote60::native_poc::ControlRuntimeEncoderConfigMessage;
using remote60::native_poc::ControlDesktopBackendRequestMessage;
using remote60::native_poc::ControlCaptureModeRequestMessage;
using remote60::native_poc::ControlWindowEntry;
using remote60::native_poc::ControlMonitorListMessage;
using remote60::native_poc::ControlMonitorListRequestMessage;
using remote60::native_poc::ControlMonitorSelectMessage;
using remote60::native_poc::ControlWindowListMessage;
using remote60::native_poc::ControlWindowListRequestMessage;
using remote60::native_poc::ControlWindowSelectMessage;
using remote60::native_poc::ControlWindowSelectedMessage;
using remote60::native_poc::ControlWindowThumbnailHeader;
using remote60::native_poc::ControlWindowThumbnailRequestMessage;
using remote60::native_poc::ControlPingMessage;
using remote60::native_poc::ControlPongMessage;
using remote60::native_poc::H264EncodeFrameStats;
using remote60::native_poc::EncodedFrameHeader;
using remote60::native_poc::H264AccessUnit;
using remote60::native_poc::H264Encoder;
using remote60::native_poc::GdiCaptureProcess;
using remote60::native_poc::GdiCaptureProcessConfig;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::RawFrameHeader;
using remote60::native_poc::UdpCodec;
using remote60::native_poc::UdpHelloPacket;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;
using remote60::native_poc::SecureInputBrokerClient;
using remote60::native_poc::VideoTransport;
using remote60::native_poc::bgra_to_nv12;
using remote60::native_poc::clamp_udp_mtu;
using remote60::native_poc::parse_video_transport;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::video_transport_name;
using remote60::host::DesktopCaptureBackend;
using remote60::host::DxgiDesktopCaptureConfig;
using remote60::host::DxgiDesktopCaptureSession;
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
// 10s, up from 1s: a static scene with frame gating on trips the send-interval detector on
// nearly every frame, and at one 2.5KB line per second that alone wrote ~9MB per streaming
// hour. One line per ten seconds still names the bottleneck while a user is feeling it.
constexpr uint64_t kHostUserFeedbackMinIntervalUs = 10000000;
constexpr uint64_t kCaptureStallKeepaliveIntervalUs = 1000000;  // 1s
constexpr uint64_t kCaptureCallbackStallRestartUs = 1200000;  // 1.2s
constexpr uint64_t kCaptureCallbackRestartCooldownUs = 3000000;  // 3s
// DXGI/WGC frozen-ring self-heal. These backends are change-driven, so the callback-stall
// watchdog above deliberately skips them -- silence on a static desktop is normal. But a ring
// that has frozen under GPU contention (submits stuck in GpuPending, their completion query never
// signalling) is distinguishable from an idle one by the age of its oldest pending submit: an idle
// ring enqueues nothing, so its oldest-pending age is 0. 250ms is telemetry only; past 2s over two
// consecutive polls the ring is dead and a same-device capture restart is due. If it refreezes
// within 60s the device itself is wedged, so we exit and let the supervisor rebuild the process.
constexpr uint64_t kCaptureFrozenWarnUs = 250000;                // 250ms
constexpr uint64_t kCaptureFrozenRestartUs = 2000000;            // 2s
constexpr uint32_t kCaptureFrozenPollStreakMin = 2;
constexpr uint64_t kCaptureFrozenEscalationWindowUs = 60000000;  // 60s
// Readback-throughput soft watchdog (DXGI/WGC). A GPU->CPU readback that drains slowly under GPU
// contention sits in the blind zone between the two hard self-heals above: the capture thread
// keeps ACQUIRING and the cadence gate keeps accepting frames (so the callback-stall/capture-dead
// watchdog stays silent), while the ring publishes almost nothing and its oldest-pending age peaks
// *below* the 2s frozen-ring threshold (so that watchdog never fires either). It is caught instead
// by watching per-1s windows where the gate accepted a real rate but the pipeline published
// almost none, corroborated by either an elevated (but sub-2s) pending age or a burst of
// staging-busy/superseded drops. First trip restarts capture+readback on the same device like the
// frozen-ring path; a recurrence inside the same 60s window escalates to a process restart for a
// fresh D3D device. These are intentionally softer than the frozen-ring thresholds -- the point is
// to cover the case the 2s hard threshold misses -- and the frozen-ring path is left untouched.
constexpr uint64_t kReadbackDrainWarmupUs = 4000000;            // 4s after start/restart/reattach
constexpr uint32_t kReadbackDrainConsecutiveSecMin = 3;         // consecutive 1s windows
constexpr uint64_t kReadbackDrainPendingAgeUs = 250000;         // 250ms window peak
constexpr uint32_t kReadbackDrainDropBurstMin = 3;             // busy+superseded delta / window
constexpr uint64_t kQueueWaitTimeoutUsDefault = 100000;  // 100ms
constexpr uint64_t kQueueWaitTimeoutUsMin = 5000;  // 5ms
constexpr uint32_t kCaptureInputMinPushPerSecDefault = 10;
constexpr uint32_t kCaptureInputStallConsecutiveSecDefault = 3;
constexpr uint32_t kCaptureInputStallWarmupSecDefault = 4;
constexpr uint32_t kFrameGatingStaticFpsDefault = 8;
constexpr uint32_t kFrameGatingStaticThresholdPermilleDefault = 6;
constexpr uint32_t kFrameGatingEnterFramesDefault = 10;
constexpr uint32_t kFrameGatingExitFramesDefault = 2;
constexpr uint32_t kFrameGatingSampleTargetDefault = 2048;
constexpr uint32_t kKeyReqMinIntervalUsDefault = 120000;  // 120ms
constexpr uint32_t kKeyReqTokenRefillUsDefault = 300000;  // 300ms / token
constexpr uint32_t kKeyReqTokenCapacityDefault = 3;

void wake_display_for_remote_session() {
  // ES_DISPLAY_REQUIRED resets the idle timer, but a monitor that has already powered down
  // is not guaranteed to light immediately on every display driver. Mirror a real local
  // wake without leaving the pointer displaced: the paired relative moves cancel out.
  INPUT wake[2]{};
  wake[0].type = INPUT_MOUSE;
  wake[0].mi.dx = 1;
  wake[0].mi.dwFlags = MOUSEEVENTF_MOVE;
  wake[1].type = INPUT_MOUSE;
  wake[1].mi.dx = -1;
  wake[1].mi.dwFlags = MOUSEEVENTF_MOVE;
  (void)SendInput(2, wake, sizeof(INPUT));
  (void)PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND,
                     static_cast<WPARAM>(SC_MONITORPOWER), static_cast<LPARAM>(-1));
}

class HostPowerKeepalive {
 public:
  HostPowerKeepalive() {
    Apply(false);
  }

  ~HostPowerKeepalive() {
    (void)SetThreadExecutionState(ES_CONTINUOUS);
  }

  void SetStreaming(bool streaming, bool wakeDisplay = false) {
    // Only a real not-streaming -> streaming edge may wake the display. The wake injects
    // actual mouse motion, and the previous condition re-ran it for any call that passed
    // wakeDisplay while already streaming -- including the capture-fallback retry loops,
    // which re-arm themselves every 100ms and so jittered the cursor continuously.
    const bool startedStreaming = streaming && !streaming_;
    if (streaming_ == streaming) return;
    streaming_ = streaming;
    Apply(streaming);
    if (startedStreaming && wakeDisplay) wake_display_for_remote_session();
  }

 private:
  static void Apply(bool streaming) {
    EXECUTION_STATE flags = ES_CONTINUOUS | ES_SYSTEM_REQUIRED;
    if (streaming) flags = static_cast<EXECUTION_STATE>(flags | ES_DISPLAY_REQUIRED);
    (void)SetThreadExecutionState(flags);
  }

  bool streaming_ = false;
};

struct HostBottleneckStage {
  uint32_t code = 0;
  uint64_t us = 0;
  const char* name = "none";
};

struct D3DReadbackTiming {
  uint64_t d3dWaitUs = 0;
  uint64_t copyMapUs = 0;
  uint64_t memcpyUs = 0;
  uint64_t unmapWaitUs = 0;
  uint64_t unmapUs = 0;
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
  // Not the wire: this is the interval between AUs being *enqueued* to the sender by the encode/main
  // thread. A large value means the host failed to supply AUs steadily (async MFT bursting, main
  // scheduling), which the client sees as a gap -- the actual wire timing is the "wire seq=" lines.
  update_host_bottleneck_stage(9, sendIntervalErrUs, "encode_au_enqueue_jitter", &stage);
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

std::string hr_hex(HRESULT hr) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
  return std::string(buf);
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

// True while the interactive desktop is the ordinary one the user works on.
//
// The elevated host can inject into that desktop itself, including elevated windows such as the
// taskbar. What it cannot reach is the secure desktop -- the lock screen, the UAC prompt,
// Ctrl+Alt+Del -- which is a separate desktop only a SYSTEM process can drive. Routing
// everything through the SYSTEM agent instead is worse than it sounds: the agent reports nothing
// back, so when its injection does not take effect the host still counts every event as
// delivered and the session looks alive while no click ever lands.
//
// Checked on a short cache because it runs per input event.
// Fresh (uncached) query of whether the input desktop is the ordinary "Default" one. A locked
// workstation, a UAC prompt, or any other secure desktop switches the input desktop to Winlogon,
// which this user-session process cannot open. Split out from the cached wrapper below so callers
// that must not trust a stale flag -- the static-screen bootstrap, which is about to paint pixels
// -- can force a live check.
bool interactive_desktop_is_default_uncached() {
  bool isDefault = false;
  HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (desktop) {
    wchar_t name[64]{};
    DWORD needed = 0;
    if (GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed)) {
      isDefault = _wcsicmp(name, L"Default") == 0;
    }
    CloseDesktop(desktop);
  }
  // A desktop this process cannot even open is the secure one.
  return isDefault;
}

bool interactive_desktop_is_default() {
  static std::atomic<uint64_t> lastCheckUs{0};
  static std::atomic<bool> cached{true};
  const uint64_t nowUs = qpc_now_us();
  const uint64_t last = lastCheckUs.load(std::memory_order_relaxed);
  if (last != 0 && nowUs - last < 250000ULL) return cached.load(std::memory_order_relaxed);

  const bool isDefault = interactive_desktop_is_default_uncached();
  cached.store(isDefault, std::memory_order_relaxed);
  lastCheckUs.store(nowUs, std::memory_order_relaxed);
  return isDefault;
}

bool send_desktop_mouse_input(DWORD flags, DWORD mouseData = 0) {
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = flags;
  in.mi.mouseData = mouseData;
  return SendInput(1, &in, sizeof(INPUT)) == 1;
}


// Real keyboard events for the focused window.
//
// Chrome, Electron apps and anything UWP-backed ignore a synthetic WM_CHAR/WM_KEYDOWN that
// was PostMessage'd at them: they route keyboard through their own focus manager and consult
// real key state, which a posted message never establishes. Desktop mode already drives the
// real cursor with SendInput, so the keyboard has to travel the same way to reach them.
bool send_desktop_unicode_char(wchar_t ch) {
  INPUT in[2]{};
  for (int i = 0; i < 2; ++i) {
    in[i].type = INPUT_KEYBOARD;
    in[i].ki.wVk = 0;
    in[i].ki.wScan = static_cast<WORD>(ch);
    in[i].ki.dwFlags = KEYEVENTF_UNICODE | (i == 1 ? KEYEVENTF_KEYUP : 0);
  }
  return SendInput(2, in, sizeof(INPUT)) == 2;
}

bool send_desktop_virtual_key(uint32_t vk, bool keyUp) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = static_cast<WORD>(vk);
  in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
  in.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
  // Extended keys need the flag or the target sees the numpad twin instead.
  switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE: case VK_RCONTROL: case VK_RMENU:
      in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
      break;
    default:
      break;
  }
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
      // Button-less moves used to be dropped, on the assumption that a finger not touching
      // cannot hover. The on-screen mouse breaks that assumption: it exists precisely to place
      // the pointer before pressing anything, and dropping those moves made it do nothing at
      // all. WM_MOUSEMOVE with no buttons is what a real mouse sends while hovering.
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
      // Same reasoning as the text path: a posted WM_KEYDOWN never reaches Chrome and
      // friends, and a modifier posted this way does not establish real key state, so
      // Ctrl+C could never work. Use real keystrokes whenever the target holds focus.
      const HWND foreground = GetForegroundWindow();
      const bool targetHasFocus =
          foreground && (foreground == keyTargetHwnd ||
                         GetAncestor(keyTargetHwnd, GA_ROOT) == foreground);
      bool ok = false;
      if (targetHasFocus) {
        ok = send_desktop_virtual_key(input.keyCode, keyUp);
      } else {
        const UINT msg = keyUp ? WM_KEYUP : WM_KEYDOWN;
        const LPARAM lp = key_event_lparam(input.keyCode, keyUp);
        ok = PostMessageW(keyTargetHwnd, msg, static_cast<WPARAM>(input.keyCode), lp) != 0;
      }
      if (!ok) return InputInjectResult::Failed;
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
    // A move with no button held is honoured here, unlike window mode. Desktop mode drives the
    // real cursor, and the client only sends these when it means to place the pointer: the
    // on-screen mouse positions before clicking, and scroll mode positions before scrolling.
    // Dropping them made both silently do nothing.
    return SetCursorPos(screenPt.x, screenPt.y) ? InputInjectResult::Injected : InputInjectResult::Failed;
  }
  if (input.kind == 2 || input.kind == 3) {
    const DWORD mouseFlag = mouse_vk_to_sendinput_flag(input.kind, input.keyCode);
    if (mouseFlag == 0) return InputInjectResult::Unsupported;
    if (!SetCursorPos(screenPt.x, screenPt.y)) return InputInjectResult::Failed;
    return send_desktop_mouse_input(mouseFlag) ? InputInjectResult::Injected
                                               : InputInjectResult::Failed;
  }
  if (input.kind == 4) {
    if (!SetCursorPos(screenPt.x, screenPt.y)) return InputInjectResult::Failed;
    return send_desktop_mouse_input(MOUSEEVENTF_WHEEL,
                                    static_cast<DWORD>(static_cast<SHORT>(input.wheelDelta)))
               ? InputInjectResult::Injected
               : InputInjectResult::Failed;
  }
  if (input.kind == 5 || input.kind == 6) {
    // Desktop mode drives the real cursor, so keyboard goes to the real focus too.
    const bool keyUp = (input.kind == 6);
    if (resolvedTargetOut) {
      HWND focusHwnd = GetForegroundWindow();
      *resolvedTargetOut = focusHwnd ? describe_input_target(focusHwnd) : std::string("sendinput");
    }
    if (!send_desktop_virtual_key(input.keyCode, keyUp)) return InputInjectResult::Failed;
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
  if (desktopMode) {
    // Real keystrokes to the focused window; WM_CHAR posted at a top-level window is ignored
    // by Chrome and other apps that own their own input routing.
    if (resolvedTargetOut) {
      HWND focusHwnd = GetForegroundWindow();
      *resolvedTargetOut = focusHwnd ? describe_input_target(focusHwnd) : std::string("sendinput");
    }
    for (uint16_t i = 0; i < text.utf16Count; ++i) {
      const uint16_t ch = text.utf16[i];
      if (ch == 0) continue;
      if (!send_desktop_unicode_char(static_cast<wchar_t>(ch))) return InputInjectResult::Failed;
    }
    return InputInjectResult::Injected;
  }

  HWND fallbackHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(
      captureTargetHwnd.load(std::memory_order_acquire)));
  HWND targetHwnd = choose_text_target_window(fallbackHwnd, desktopInputState);
  if (!targetHwnd || !IsWindow(targetHwnd)) return InputInjectResult::NoTarget;
  if (resolvedTargetOut) *resolvedTargetOut = describe_input_target(targetHwnd);
  // Window mode targets a specific window without stealing focus, so posted messages are the
  // deliberate mechanism. If that window already holds focus, real keystrokes reach far more
  // applications, so prefer them.
  const HWND foreground = GetForegroundWindow();
  const bool targetHasFocus =
      foreground && (foreground == targetHwnd || IsChild(foreground, targetHwnd) ||
                     GetAncestor(targetHwnd, GA_ROOT) == foreground);
  for (uint16_t i = 0; i < text.utf16Count; ++i) {
    const uint16_t ch = text.utf16[i];
    if (ch == 0) continue;
    if (targetHasFocus) {
      if (!send_desktop_unicode_char(static_cast<wchar_t>(ch))) return InputInjectResult::Failed;
    } else if (!PostMessageW(targetHwnd, WM_CHAR, static_cast<WPARAM>(ch), 1)) {
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
  // NV12 is 4:2:0, so an odd crop leaves the trailing chroma column/row unwritten and shows
  // as a coloured edge line. Round the extent down to even.
  cropW &= ~1;
  cropH &= ~1;
  *outX = static_cast<uint32_t>(cropX);
  *outY = static_cast<uint32_t>(cropY);
  *outW = static_cast<uint32_t>(cropW);
  *outH = static_cast<uint32_t>(cropH);
  return (*outW >= 2 && *outH >= 2);
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForPrimaryMonitor(
    HWND preferredWindow = nullptr, const char* preferredSource = nullptr,
    HMONITOR preferredMonitor = nullptr) {
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

  // A specific screen when one was chosen. Everything below is the primary-monitor path, which
  // stays the default: a client that never asks for a monitor sees exactly what it always did.
  if (preferredMonitor) {
    createForMonitor(preferredMonitor, "CreateForMonitor(selected)");
    if (item) return item;
    std::cerr << "[native-video-host] selected monitor unavailable; falling back to primary\n";
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

/** Network-order address for bind(); 0.0.0.0 when unset. A typo must not bind nowhere silently. */
ULONG resolve_bind_address(const std::string& bindAddress) {
  if (bindAddress.empty()) return htonl(INADDR_ANY);
  in_addr parsed{};
  if (inet_pton(AF_INET, bindAddress.c_str(), &parsed) == 1) return parsed.s_addr;
  std::cerr << "[native-video-host] invalid --bind-address " << bindAddress << ", using 0.0.0.0\n";
  return htonl(INADDR_ANY);
}

struct Args {
  uint16_t bindPort = 43000;
  // Ordered fallback list for the media socket; the first port that binds wins. Corporate
  // firewalls commonly permit outbound UDP only to a whitelist of destination ports, so a host
  // sitting on 43000 is unreachable from those networks however healthy the rest of the path is.
  // 443 carries QUIC and 3478 carries STUN, so both are open almost everywhere. Empty means
  // "just bindPort", which is what a config file or an explicit single --bind-port produces.
  std::vector<uint16_t> bindPortCandidates;
  // Empty binds every interface. Test harnesses pass 127.0.0.1: a loopback bind never
  // triggers the Windows Firewall consent dialog, which dims the whole screen and starves
  // WGC capture for as long as it is up -- every measurement taken behind it is garbage.
  std::string bindAddress;
  uint16_t controlPort = 0;
  uint32_t tcpSendBufKb = 0;
  uint32_t udpMtu = 1200;
  uint32_t traceEvery = 0;
  uint32_t traceMax = 0;
  uint32_t inputLogEvery = 120;
  bool enableInputInjection = true;
  std::string inputInjectionMode = "background_message";
  uint32_t inputTargetPid = 0;
  std::string inputTargetProcess;
  std::string inputTargetTitle;
  std::string transport;
  std::string codec = "raw";
  uint32_t fps = 30;
  uint32_t seconds = 0;  // 0: infinite
  // M7-confirmed 1080p defaults. The old 1.1 Mbps default also silently tripped the
  // <=1.5 Mbps auto-720p downscale in choose_h264_encode_size on any larger display.
  uint32_t bitrate = 8000000;
  uint32_t keyint = 30;
  uint32_t encodeWidth = 0;
  uint32_t encodeHeight = 0;
  uint32_t captureWindowPid = 0;
  std::string captureWindowProcess;
  std::string captureWindowTitle;
  bool captureWindowClientOnly = false;
  uint32_t captureWindowRebindIntervalMs = 1000;
  // Directory service. Empty url keeps the host on the current LAN-only behaviour: it simply
  // waits for a client that already knows its address.
  std::string directoryUrl;
  std::string directoryId;
  std::string directoryPw;
  std::string directoryHostName;
  uint16_t directoryObservePort = 0;
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

DesktopCaptureBackend desktop_capture_backend_from_env() {
  const char* raw = std::getenv("REMOTE60_DESKTOP_CAPTURE_BACKEND");
  if (!raw || !*raw) return DesktopCaptureBackend::Dxgi;
  std::string s(raw);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "wgc") return DesktopCaptureBackend::Wgc;
  if (s == "gdi") return DesktopCaptureBackend::Gdi;
  return DesktopCaptureBackend::Dxgi;
}

bool desktop_capture_backend_from_code(uint16_t code, DesktopCaptureBackend* out) {
  if (!out) return false;
  switch (code) {
    case 1:
      *out = DesktopCaptureBackend::Dxgi;
      return true;
    case 2:
      *out = DesktopCaptureBackend::Wgc;
      return true;
    case 3:
      *out = DesktopCaptureBackend::Gdi;
      return true;
    default:
      return false;
  }
}

uint16_t desktop_capture_backend_code(DesktopCaptureBackend backend) {
  switch (backend) {
    case DesktopCaptureBackend::Dxgi:
      return 1;
    case DesktopCaptureBackend::Wgc:
      return 2;
    case DesktopCaptureBackend::Gdi:
      return 3;
  }
  return 1;
}

const char* desktop_capture_backend_name(DesktopCaptureBackend backend) {
  switch (backend) {
    case DesktopCaptureBackend::Dxgi: return "dxgi";
    case DesktopCaptureBackend::Wgc: return "wgc";
    case DesktopCaptureBackend::Gdi: return "gdi";
  }
  return "unknown";
}

struct PrimaryMonitorInfo {
  HMONITOR monitor = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  // Desktop position of this monitor. Nonzero whenever it is not the top-left one, and negative
  // for a monitor placed left of or above the primary, which is why these are signed.
  int32_t originX = 0;
  int32_t originY = 0;
};

std::optional<PrimaryMonitorInfo> primary_monitor_info() {
  const HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  if (!monitor) return std::nullopt;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoA(monitor, &info)) return std::nullopt;
  const LONG width = info.rcMonitor.right - info.rcMonitor.left;
  const LONG height = info.rcMonitor.bottom - info.rcMonitor.top;
  if (width <= 0 || height <= 0) return std::nullopt;
  PrimaryMonitorInfo out;
  out.monitor = monitor;
  out.width = static_cast<uint32_t>(width);
  out.height = static_cast<uint32_t>(height);
  out.originX = info.rcMonitor.left;
  out.originY = info.rcMonitor.top;
  return out;
}

HRESULT create_d3d11_device_for_primary_monitor(Microsoft::WRL::ComPtr<ID3D11Device>* outDevice,
                                                Microsoft::WRL::ComPtr<ID3D11DeviceContext>* outContext,
                                                D3D_FEATURE_LEVEL* outLevel) {
  if (!outDevice || !outContext) return E_INVALIDARG;
  outDevice->Reset();
  outContext->Reset();

  Microsoft::WRL::ComPtr<IDXGIAdapter1> targetAdapter;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> fallbackAdapter;
  const HMONITOR targetMonitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  if (targetMonitor) {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && factory) {
      for (UINT adapterIndex = 0; !targetAdapter; ++adapterIndex) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) continue;
        for (UINT outputIndex = 0;; ++outputIndex) {
          Microsoft::WRL::ComPtr<IDXGIOutput> output;
          if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
          if (!output) continue;
          if (!fallbackAdapter) fallbackAdapter = adapter;
          DXGI_OUTPUT_DESC desc{};
          if (FAILED(output->GetDesc(&desc))) continue;
          if (desc.Monitor == targetMonitor) {
            targetAdapter = adapter;
            break;
          }
        }
      }
    }
  }
  if (!targetAdapter && fallbackAdapter) {
    targetAdapter = fallbackAdapter;
  }

  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  const HRESULT hr = D3D11CreateDevice(targetAdapter.Get(),
                                       targetAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       nullptr,
                                       0,
                                       D3D11_SDK_VERSION,
                                       outDevice->GetAddressOf(),
                                       &level,
                                       outContext->GetAddressOf());
  if (SUCCEEDED(hr) && outLevel) *outLevel = level;
  return hr;
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
      if (json_profile::json_get_string(jsonText, "directoryUrl", &s)) a.directoryUrl = s;
      if (json_profile::json_get_string(jsonText, "directoryId", &s)) a.directoryId = s;
      if (json_profile::json_get_string(jsonText, "directoryHostName", &s)) a.directoryHostName = s;
      if (json_profile::json_get_u32(jsonText, "directoryObservePort", &v)) {
        a.directoryObservePort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
      }
      // Deliberately no directoryPw here: profiles are committed, passwords are not.
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
      // Accepts one port or an ordered comma-separated fallback list.
      a.bindPortCandidates = remote60::native_poc::parse_bind_port_candidates(argv[++i]);
      if (!a.bindPortCandidates.empty()) a.bindPort = a.bindPortCandidates.front();
    } else if (k == "--bind-address" && i + 1 < argc) {
      a.bindAddress = argv[++i];
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
    } else if (k == "--directory-url" && i + 1 < argc) {
      a.directoryUrl = argv[++i];
    } else if (k == "--directory-id" && i + 1 < argc) {
      a.directoryId = argv[++i];
    } else if (k == "--directory-pw" && i + 1 < argc) {
      a.directoryPw = argv[++i];
    } else if (k == "--host-name" && i + 1 < argc) {
      a.directoryHostName = argv[++i];
    } else if (k == "--directory-observe-port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) {
        a.directoryObservePort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
      }
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

// Fit a source frame inside a target box without changing its aspect ratio. Encoding a
// 4:3 window into a 16:9 box (the shipped profiles) otherwise stretches the picture.
void fit_size_preserving_aspect(uint32_t srcW, uint32_t srcH, uint32_t boxW, uint32_t boxH,
                                uint32_t* outW, uint32_t* outH) {
  if (!outW || !outH) return;
  if (srcW == 0 || srcH == 0 || boxW == 0 || boxH == 0) {
    *outW = boxW;
    *outH = boxH;
    return;
  }
  const double scale = std::min({static_cast<double>(boxW) / static_cast<double>(srcW),
                                 static_cast<double>(boxH) / static_cast<double>(srcH), 1.0});
  *outW = clamp_even_dim(static_cast<uint32_t>(std::lround(srcW * scale)), 2, srcW);
  *outH = clamp_even_dim(static_cast<uint32_t>(std::lround(srcH * scale)), 2, srcH);
}

void choose_h264_encode_size(const Args& args, uint32_t captureW, uint32_t captureH,
                             uint32_t* outW, uint32_t* outH, bool* outAutoFallback720) {
  if (!outW || !outH || !outAutoFallback720) return;
  *outAutoFallback720 = false;
  uint32_t targetW = captureW;
  uint32_t targetH = captureH;
  if (args.encodeWidth > 0 && args.encodeHeight > 0) {
    // Treat the configured size as a bounding box and fit the capture inside it. Clamping
    // each axis on its own squashes the picture whenever the source is not the same aspect
    // as the profile (a 16:10 or 3:2 monitor against the shipped 1920x1080 profiles).
    const double sx = static_cast<double>(args.encodeWidth) / static_cast<double>(captureW);
    const double sy = static_cast<double>(args.encodeHeight) / static_cast<double>(captureH);
    const double scale = std::min({sx, sy, 1.0});
    if (scale > 0.0) {
      targetW = static_cast<uint32_t>(std::lround(captureW * scale));
      targetH = static_cast<uint32_t>(std::lround(captureH * scale));
    }
    targetW = clamp_even_dim(targetW, 2, captureW);
    targetH = clamp_even_dim(targetH, 2, captureH);
  } else {
    // What the bitrate can carry. The threshold used to sit at 1.5 Mbps, which only caught the
    // extremes; 3 Mbps at 1080p was left to spend a quarter of the bits per pixel and showed it
    // whenever the whole screen changed at once. See encode_resolution_ladder.hpp.
    const auto choice =
        remote60::native_poc::choose_encode_resolution(args.bitrate, captureW, captureH, false);
    if (choice.reduced) {
      targetW = choice.width;
      targetH = choice.height;
      *outAutoFallback720 = true;
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

// Fraction of the frame that differs from the previous one, in permille.
//
// Returns 0 if and only if the two frames are byte-identical, so callers can use "0" as an
// exact "nothing moved" test. Anything else reports at least 1.
//
// This deliberately does a full blockwise compare rather than sampling pixels. The previous
// version sampled a few thousand pixels and averaged their intensity delta, which reports 0
// for small localised edits: typing one character changes ~200 of 2 million pixels, so the
// average barely moves and the frame looks static. Gating on that throttles typing. memcmp is
// SIMD-optimised in the CRT and costs far less than the frame copy already being done.
uint32_t estimate_bgra_change_permille(const uint8_t* a, const uint8_t* b, size_t sizeBytes,
                                       uint32_t /*sampleTarget*/) {
  if (!a || !b || sizeBytes == 0) return 1000;
  constexpr size_t kBlockBytes = 4096;
  const size_t blockCount = (sizeBytes + kBlockBytes - 1) / kBlockBytes;
  size_t changedBlocks = 0;
  for (size_t i = 0; i < blockCount; ++i) {
    const size_t offset = i * kBlockBytes;
    const size_t len = std::min(kBlockBytes, sizeBytes - offset);
    if (std::memcmp(a + offset, b + offset, len) != 0) ++changedBlocks;
  }
  if (changedBlocks == 0) return 0;
  const uint64_t permille = (static_cast<uint64_t>(changedBlocks) * 1000ULL) / blockCount;
  return static_cast<uint32_t>(std::clamp<uint64_t>(permille, 1, 1000));
}

// Average 2x2 blocks into one pixel. Bilinear alone only samples 2 taps per axis, so a
// >2x downscale (4K->1080p, 1440p->720p) drops most source pixels and aliases hard on text.
void box_halve_bgra(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t srcStride,
                    std::vector<uint8_t>* out, uint32_t* outW, uint32_t* outH) {
  const uint32_t dstW = std::max<uint32_t>(1, srcW / 2);
  const uint32_t dstH = std::max<uint32_t>(1, srcH / 2);
  out->resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
  uint8_t* dst = out->data();
  for (uint32_t y = 0; y < dstH; ++y) {
    const uint8_t* row0 = src + static_cast<size_t>(std::min(srcH - 1, y * 2)) * srcStride;
    const uint8_t* row1 = src + static_cast<size_t>(std::min(srcH - 1, y * 2 + 1)) * srcStride;
    uint8_t* dstRow = dst + static_cast<size_t>(y) * dstW * 4;
    for (uint32_t x = 0; x < dstW; ++x) {
      const uint32_t x0 = std::min(srcW - 1, x * 2);
      const uint32_t x1 = std::min(srcW - 1, x * 2 + 1);
      const uint8_t* p00 = row0 + static_cast<size_t>(x0) * 4;
      const uint8_t* p10 = row0 + static_cast<size_t>(x1) * 4;
      const uint8_t* p01 = row1 + static_cast<size_t>(x0) * 4;
      const uint8_t* p11 = row1 + static_cast<size_t>(x1) * 4;
      uint8_t* outPx = dstRow + static_cast<size_t>(x) * 4;
      for (int c = 0; c < 4; ++c) {
        outPx[c] = static_cast<uint8_t>((p00[c] + p10[c] + p01[c] + p11[c] + 2) >> 2);
      }
    }
  }
  *outW = dstW;
  *outH = dstH;
}

bool resize_bgra_bilinear(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t srcStride,
                          uint32_t dstW, uint32_t dstH, std::vector<uint8_t>* outBgra) {
  if (!src || srcW == 0 || srcH == 0 || srcStride < (srcW * 4) || dstW == 0 || dstH == 0 || !outBgra) {
    return false;
  }

  std::vector<uint8_t> reduced;
  while (srcW >= dstW * 2 && srcH >= dstH * 2 && srcW > 1 && srcH > 1) {
    std::vector<uint8_t> next;
    uint32_t nextW = 0;
    uint32_t nextH = 0;
    box_halve_bgra(src, srcW, srcH, srcStride, &next, &nextW, &nextH);
    reduced.swap(next);
    src = reduced.data();
    srcW = nextW;
    srcH = nextH;
    srcStride = nextW * 4;
  }
  if (srcW == dstW && srcH == dstH) {
    outBgra->resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
    for (uint32_t y = 0; y < dstH; ++y) {
      std::memcpy(outBgra->data() + static_cast<size_t>(y) * dstW * 4,
                  src + static_cast<size_t>(y) * srcStride, static_cast<size_t>(dstW) * 4);
    }
    return true;
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

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

// Grab a still preview of one window for the target picker. The capture pipeline only ever
// streams a single target, so previews for the *other* listed windows have no pixel source --
// PrintWindow renders a window into our own DC even while it is occluded, which is what makes
// a thumbnail grid possible without spinning up a capture session per window.
bool capture_window_thumbnail(HWND hwnd, uint32_t maxW, uint32_t maxH,
                              std::vector<uint8_t>* outBgra, uint32_t* outW, uint32_t* outH) {
  if (!outBgra || !outW || !outH) return false;
  outBgra->clear();
  *outW = 0;
  *outH = 0;
  if (maxW == 0 || maxH == 0) return false;

  int srcW = 0;
  int srcH = 0;
  const bool desktop = (hwnd == nullptr);
  if (desktop) {
    srcW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    srcH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  } else {
    if (!IsWindow(hwnd) || IsIconic(hwnd)) return false;
    // PrintWindow delivers WM_PRINT with SendMessage and no timeout; one hung window (a
    // crashed-driver dialog, a stuck installer) wedges the whole control session behind it,
    // and with it every window-select and stream-state message.
    if (IsHungAppWindow(hwnd)) return false;
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return false;
    srcW = rc.right - rc.left;
    srcH = rc.bottom - rc.top;
    if (srcW <= 1 || srcH <= 1) {
      RECT wr{};
      if (!GetWindowRect(hwnd, &wr)) return false;
      srcW = wr.right - wr.left;
      srcH = wr.bottom - wr.top;
    }
  }
  if (srcW <= 1 || srcH <= 1) return false;

  HDC screenDc = GetDC(nullptr);
  if (!screenDc) return false;
  HDC memDc = CreateCompatibleDC(screenDc);
  if (!memDc) {
    ReleaseDC(nullptr, screenDc);
    return false;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = srcW;
  bmi.bmiHeader.biHeight = -srcH;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib || !bits) {
    if (dib) DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return false;
  }
  HGDIOBJ oldBmp = SelectObject(memDc, dib);

  bool captured = false;
  if (desktop) {
    captured = (BitBlt(memDc, 0, 0, srcW, srcH, screenDc, GetSystemMetrics(SM_XVIRTUALSCREEN),
                       GetSystemMetrics(SM_YVIRTUALSCREEN), SRCCOPY | CAPTUREBLT) != FALSE);
  } else {
    // PW_RENDERFULLCONTENT is needed for DirectComposition/UWP-backed windows; without it
    // those render blank. It is ignored on older systems.
    captured = (PrintWindow(hwnd, memDc, PW_CLIENTONLY | PW_RENDERFULLCONTENT) != FALSE);
    if (!captured) captured = (PrintWindow(hwnd, memDc, PW_RENDERFULLCONTENT) != FALSE);
  }

  std::vector<uint8_t> full;
  if (captured) {
    full.resize(static_cast<size_t>(srcW) * static_cast<size_t>(srcH) * 4u);
    std::memcpy(full.data(), bits, full.size());
    // PrintWindow leaves alpha at 0 for many windows; force opaque so clients can blit it.
    for (size_t i = 3; i < full.size(); i += 4) full[i] = 0xFF;
  }

  SelectObject(memDc, oldBmp);
  DeleteObject(dib);
  DeleteDC(memDc);
  ReleaseDC(nullptr, screenDc);
  if (!captured || full.empty()) return false;

  uint32_t dstW = 0;
  uint32_t dstH = 0;
  fit_size_preserving_aspect(static_cast<uint32_t>(srcW), static_cast<uint32_t>(srcH), maxW, maxH,
                             &dstW, &dstH);
  if (dstW == 0 || dstH == 0) return false;
  if (dstW == static_cast<uint32_t>(srcW) && dstH == static_cast<uint32_t>(srcH)) {
    *outBgra = std::move(full);
  } else if (!resize_bgra_bilinear(full.data(), static_cast<uint32_t>(srcW),
                                   static_cast<uint32_t>(srcH), static_cast<uint32_t>(srcW) * 4u,
                                   dstW, dstH, outBgra)) {
    return false;
  }
  *outW = dstW;
  *outH = dstH;
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
             uint32_t outW, uint32_t outH, std::vector<uint8_t>* outBgra,
             D3DReadbackTiming* outTiming = nullptr) {
    if (!src || !outBgra || srcStride < inW * 4) return false;
    D3DReadbackTiming localTiming{};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    {
      const uint64_t lockWaitStartUs = qpc_now_us();
      std::lock_guard<std::mutex> lk(*d3dMutex);
      const uint64_t lockAcquiredUs = qpc_now_us();
      localTiming.d3dWaitUs =
          (lockAcquiredUs >= lockWaitStartUs) ? (lockAcquiredUs - lockWaitStartUs) : 0;
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

      // This blt is BGRA->BGRA, so both ends must be declared full-range RGB. Leaving the
      // color spaces unset lets the driver assume studio range on one side and crush levels.
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace{};
      colorSpace.Usage = 0;             // playback (full precision)
      colorSpace.RGB_Range = 0;         // 0 = full range (0-255)
      colorSpace.YCbCr_Matrix = 1;      // BT.709, matches apply_video_colorimetry
      colorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
      videoContext->VideoProcessorSetStreamColorSpace(processor.Get(), 0, &colorSpace);
      videoContext->VideoProcessorSetOutputColorSpace(processor.Get(), &colorSpace);
      // Vendor auto-processing (edge enhancement / denoise) is tuned for video, not text,
      // and produces ringing around UI glyphs.
      videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.Get(), 0, FALSE);

      D3D11_VIDEO_PROCESSOR_STREAM stream{};
      stream.Enable = TRUE;
      stream.pInputSurface = inputView.Get();
      if (FAILED(videoContext->VideoProcessorBlt(processor.Get(), outputView.Get(), 0, 1, &stream))) {
        return false;
      }

      context->CopyResource(dstStaging.Get(), dstTexture.Get());
      if (FAILED(context->Map(dstStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
      const uint64_t copyMapDoneUs = qpc_now_us();
      localTiming.copyMapUs =
          (copyMapDoneUs >= lockAcquiredUs) ? (copyMapDoneUs - lockAcquiredUs) : 0;
    }
    outBgra->resize(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4);
    const uint32_t outStride = outW * 4;
    auto* dst = outBgra->data();
    const auto* mappedData = reinterpret_cast<const uint8_t*>(mapped.pData);
    const uint64_t memcpyStartUs = qpc_now_us();
    for (uint32_t row = 0; row < outH; ++row) {
      std::memcpy(dst + static_cast<size_t>(row) * outStride,
                  mappedData + static_cast<size_t>(row) * mapped.RowPitch, outStride);
    }
    const uint64_t memcpyDoneUs = qpc_now_us();
    localTiming.memcpyUs =
        (memcpyDoneUs >= memcpyStartUs) ? (memcpyDoneUs - memcpyStartUs) : 0;
    {
      const uint64_t unmapWaitStartUs = qpc_now_us();
      std::lock_guard<std::mutex> lk(*d3dMutex);
      const uint64_t unmapLockAcquiredUs = qpc_now_us();
      localTiming.unmapWaitUs =
          (unmapLockAcquiredUs >= unmapWaitStartUs) ? (unmapLockAcquiredUs - unmapWaitStartUs) : 0;
      context->Unmap(dstStaging.Get(), 0);
      const uint64_t unmapDoneUs = qpc_now_us();
      localTiming.unmapUs =
          (unmapDoneUs >= unmapLockAcquiredUs) ? (unmapDoneUs - unmapLockAcquiredUs) : 0;
    }
    if (outTiming) {
      *outTiming = localTiming;
    }
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

// Peak send rate used to spread one frame's datagrams over time, as a multiple of the
// configured average bitrate. Sending a whole keyframe as an unthrottled burst overruns the
// Wi-Fi buffer on the AP and on the phone, which is the usual cause of the picture breaking
// up on an otherwise healthy link.
// Must exceed the largest datagram the peer can send. A datagram that does not fit is dropped
// with WSAEMSGSIZE, which looked like a handshake failure the first time it happened.
constexpr size_t kUdpReceiveBufferBytes = 4096;

std::atomic<uint32_t> gUdpPacePeakBitrateBps{0};  // 0 disables intra-frame pacing
// Set from the viewer's hello. Older viewers do not advertise it and must keep receiving the
// consecutive layout they know how to repair.
std::atomic<bool> gUdpVideoFecInterleaved{false};
std::atomic<uint32_t> gUdpKeyframePacePeakBitrateBps{100000000};

void udp_pace_wait_until(uint64_t targetUs) {
  // A yield loop burns most of one logical core because a 1200-byte datagram at the normal
  // pacing rate is only a few hundred microseconds apart. A reusable high-resolution
  // waitable timer keeps the sender asleep without falling back to the ~15.6ms legacy timer
  // quantum. Retain a short yield tail because setting a kernel timer for a few dozen
  // microseconds costs more than it saves.
  struct ThreadWaitTimer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* high resolution */,
                                            TIMER_MODIFY_STATE | SYNCHRONIZE);
    ~ThreadWaitTimer() {
      if (handle) CloseHandle(handle);
    }
  };
  thread_local ThreadWaitTimer timer;
  for (;;) {
    const uint64_t nowUs = qpc_now_us();
    if (nowUs >= targetUs) return;
    const uint64_t remainUs = targetUs - nowUs;
    if (timer.handle && remainUs > 100) {
      LARGE_INTEGER due{};
      due.QuadPart = -static_cast<LONGLONG>(std::max<uint64_t>(1, remainUs - 50) * 10ULL);
      if (SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE)) {
        (void)WaitForSingleObject(timer.handle, INFINITE);
      } else {
        std::this_thread::yield();
      }
    } else {
      std::this_thread::yield();
    }
  }
}

// Returns the per-frame send budget in microseconds, or 0 when the frame should go out as
// fast as possible (small frames are not worth the pacing overhead).
uint64_t udp_pace_budget_us(size_t payloadSize, uint32_t chunkCount, bool keyFrame) {
  uint32_t peakBps = gUdpPacePeakBitrateBps.load(std::memory_order_relaxed);
  if (keyFrame && peakBps != 0) {
    // IDRs are much larger than delta frames. Pacing one at only a small multiple of the
    // average bitrate blocks the sender for several frame periods, fills the latest-wins
    // queue, and triggers another IDR -- a self-sustaining low-FPS loop. Keep pacing, but
    // give recovery frames enough wire rate to finish inside roughly one 60 Hz interval.
    peakBps = std::max(peakBps,
                       gUdpKeyframePacePeakBitrateBps.load(std::memory_order_relaxed));
  }
  if (peakBps == 0 || chunkCount <= 8) return 0;
  return (static_cast<uint64_t>(payloadSize) * 8ULL * 1000000ULL) / static_cast<uint64_t>(peakBps);
}

// Result of a chunked UDP video send. EpochChanged means a session rollover bumped the media epoch
// mid-frame, so the remaining chunks were aborted rather than interleaved into the new session --
// the caller must NOT treat this as a transport failure (the rollover already cleared the queue and
// re-armed the barrier). TransportError is a real sendto failure on the current epoch.
enum class UdpSendOutcome { Sent, TransportError, EpochChanged };

// liveEpoch/itemEpoch let a rollover abort a chunked send mid-frame: if the live media epoch no
// longer matches the epoch this frame was stamped for, the remaining data/parity packets are the
// old session's and must not reach a freshly attached decoder. nullptr liveEpoch disables the check.
UdpSendOutcome send_udp_chunks_impl(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                    size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                    uint32_t mtuBytes, SendPathStats* stats,
                                    const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch) {
  if (!payload || payloadSize == 0 || s == INVALID_SOCKET) return UdpSendOutcome::TransportError;
  if (payloadSize > std::numeric_limits<uint32_t>::max()) return UdpSendOutcome::TransportError;
  const uint64_t startUs = qpc_now_us();
  const uint32_t safeMtu = clamp_udp_mtu(mtuBytes);
  if (safeMtu <= sizeof(UdpVideoChunkHeader)) return UdpSendOutcome::TransportError;
  const uint32_t maxChunk = safeMtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  std::vector<uint8_t> datagram(safeMtu);
  const uint32_t chunkCount =
      static_cast<uint32_t>((payloadSize + maxChunk - 1) / maxChunk);
  if (chunkCount == 0 || chunkCount > std::numeric_limits<uint16_t>::max())
    return UdpSendOutcome::TransportError;
  const auto epoch_changed = [&]() {
    return liveEpoch && liveEpoch->load(std::memory_order_relaxed) != itemEpoch;
  };
  const uint32_t fecGroupCount =
      (chunkCount + remote60::native_poc::kUdpVideoFecGroupSize - 1u) /
      remote60::native_poc::kUdpVideoFecGroupSize;
  const uint32_t packetCount = chunkCount + fecGroupCount;
  const uint64_t pacedPayloadBytes =
      static_cast<uint64_t>(payloadSize) + static_cast<uint64_t>(fecGroupCount) * maxChunk;
  const uint64_t budgetUs =
      udp_pace_budget_us(static_cast<size_t>(pacedPayloadBytes), packetCount,
                         (baseHeader.flags & 0x1u) != 0);
  uint32_t packetOrdinal = 0;

  auto send_packet = [&](const UdpVideoChunkHeader& header, const uint8_t* bytes,
                         uint32_t byteCount) -> bool {
    if (budgetUs > 0 && packetOrdinal > 0) {
      udp_pace_wait_until(startUs + (budgetUs * packetOrdinal) / packetCount);
    }
    ++packetOrdinal;
    std::memcpy(datagram.data(), &header, sizeof(header));
    std::memcpy(datagram.data() + sizeof(header), bytes, byteCount);
    const uint64_t callStartUs = stats ? qpc_now_us() : 0;
    const int n = sendto(s, reinterpret_cast<const char*>(datagram.data()),
                         static_cast<int>(sizeof(header) + byteCount), 0,
                         reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    if (n <= 0) return false;
    if (stats) {
      const uint64_t callDoneUs = qpc_now_us();
      const uint64_t callUs = callDoneUs >= callStartUs ? callDoneUs - callStartUs : 0;
      ++stats->payloadChunkCount;
      ++stats->payloadCallCount;
      stats->payloadUs += callUs;
      stats->payloadChunkMaxUs = std::max(stats->payloadChunkMaxUs, callUs);
    }
    return true;
  };

  for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
    if (epoch_changed()) return UdpSendOutcome::EpochChanged;
    const size_t offset = static_cast<size_t>(chunkIndex) * maxChunk;
    const uint32_t chunkSize =
        static_cast<uint32_t>(std::min<size_t>(maxChunk, payloadSize - offset));
    UdpVideoChunkHeader h = baseHeader;
    h.chunkOffset = static_cast<uint32_t>(offset);
    h.chunkSize = chunkSize;
    h.chunkIndex = static_cast<uint16_t>(chunkIndex);
    h.chunkCount = static_cast<uint16_t>(chunkCount);
    h.chunkStride = maxChunk;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u | 0x10u));
    if (offset == 0) h.flags |= 0x2u;
    if (offset + chunkSize >= payloadSize) h.flags |= 0x4u;
    if (!send_packet(h, payload + offset, chunkSize)) return UdpSendOutcome::TransportError;
  }

  // One XOR parity datagram per eight data datagrams repairs one loss in every group. The
  // parity is sent after the frame data so a short Wi-Fi burst is less likely to erase a data
  // packet and its repair packet together.
  //
  // Which eight matters more than how many. Wi-Fi drops packets in bursts, so grouping eight
  // consecutive chunks puts the whole burst in one group, where a single parity repairs
  // nothing. Interleaving -- group g holds chunks g, g+G, g+2G ... -- spreads a burst of up
  // to G across G groups, one loss each, all recoverable, at exactly the same cost.
  const bool interleaved = gUdpVideoFecInterleaved.load(std::memory_order_relaxed);
  std::vector<uint8_t> parity(maxChunk, 0);
  for (uint32_t group = 0; group < fecGroupCount; ++group) {
    if (epoch_changed()) return UdpSendOutcome::EpochChanged;
    std::fill(parity.begin(), parity.end(), 0);
    const uint32_t firstChunk =
        interleaved ? group : (group * remote60::native_poc::kUdpVideoFecGroupSize);
    const uint32_t step = interleaved ? fecGroupCount : 1u;
    const uint32_t limit =
        interleaved ? chunkCount
                    : std::min<uint32_t>(chunkCount,
                                         firstChunk +
                                             remote60::native_poc::kUdpVideoFecGroupSize);
    for (uint32_t chunkIndex = firstChunk; chunkIndex < limit; chunkIndex += step) {
      const size_t offset = static_cast<size_t>(chunkIndex) * maxChunk;
      const uint32_t chunkSize =
          static_cast<uint32_t>(std::min<size_t>(maxChunk, payloadSize - offset));
      for (uint32_t i = 0; i < chunkSize; ++i) parity[i] ^= payload[offset + i];
    }
    UdpVideoChunkHeader h = baseHeader;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u));
    h.flags |= 0x10u;
    if (interleaved) h.flags |= 0x20u;
    h.chunkOffset = firstChunk * maxChunk;
    h.chunkSize = maxChunk;
    h.chunkIndex = static_cast<uint16_t>(firstChunk);
    h.chunkCount = static_cast<uint16_t>(chunkCount);
    h.chunkStride = maxChunk;
    if (!send_packet(h, parity.data(), maxChunk)) return UdpSendOutcome::TransportError;
  }

  if (stats) {
    const uint64_t doneUs = qpc_now_us();
    stats->payloadUs = doneUs >= startUs ? doneUs - startUs : stats->payloadUs;
  }
  return UdpSendOutcome::Sent;
}

bool send_udp_chunks(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                     uint32_t mtuBytes) {
  return send_udp_chunks_impl(s, peer, payload, payloadSize, baseHeader, mtuBytes, nullptr,
                              nullptr, 0) == UdpSendOutcome::Sent;
}

UdpSendOutcome send_udp_chunks_timed(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                     uint32_t mtuBytes, SendPathStats* stats,
                                     const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch) {
  return send_udp_chunks_impl(s, peer, payload, payloadSize, baseHeader, mtuBytes, stats, liveEpoch,
                              itemEpoch);
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
  uint64_t captureD3DWaitUs = 0;
  uint64_t captureCopyMapUs = 0;
  uint64_t captureMemcpyUs = 0;
  uint64_t captureUnmapWaitUs = 0;
  uint64_t captureUnmapUs = 0;
  // GPU NV12 conversion of this frame for the zero-copy encode path; -1 when absent.
  // Whoever pops the frame claims the slot and must release it.
  int32_t nv12Slot = -1;
  uint64_t nv12Generation = 0;
  uint32_t nv12W = 0;
  uint32_t nv12H = 0;
  std::shared_ptr<std::vector<uint8_t>> payload;
};

}  // namespace

int main(int argc, char** argv) {
  // A remote host cannot wake itself after Windows enters S3. Keep the machine reachable
  // while the host is running; the display requirement is enabled only for an active stream.
  HostPowerKeepalive powerKeepalive;
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);
  // Prefix every host log line with a wall-clock timestamp so it aligns with the client log.
  // Static so the filtering buffers outlive every logging thread for the life of the process.
  static TimestampPrefixBuf coutTsBuf(std::cout.rdbuf());
  std::cout.rdbuf(&coutTsBuf);
  static TimestampPrefixBuf cerrTsBuf(std::cerr.rdbuf());
  std::cerr.rdbuf(&cerrTsBuf);

  // The host normally runs behind a tray app with no foreground boost. Keep capture,
  // conversion, and encode deadlines above ordinary UI/background work; opt out for A/B or
  // constrained systems with REMOTE60_NATIVE_NORMAL_PRIORITY=1.
  if (!env_truthy("REMOTE60_NATIVE_NORMAL_PRIORITY")) {
    const BOOL processPriorityOk =
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    const BOOL threadPriorityOk =
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    std::cout << "[native-video-host] latency-priority processAboveNormal="
              << (processPriorityOk ? 1 : 0)
              << " mainThreadAboveNormal=" << (threadPriorityOk ? 1 : 0) << "\n";
  }

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
  // Spread each frame's datagrams over the wire instead of bursting them. Expressed as a
  // percentage of the average bitrate: 500 means a frame may leave at up to 5x the
  // average rate. 0 restores the old unthrottled burst.
  const uint32_t udpPacePeakPercent =
      env_u32_clamped("REMOTE60_NATIVE_UDP_PACE_PEAK_PERCENT", 500, 0, 2000);
  // A percentage alone is too slow at low user bitrates: 4 Mbps * 5 can take longer than
  // one 30 fps period to deliver a normal motion frame plus FEC. Keep packets paced, but
  // finish ordinary frames within the frame budget.
  const uint32_t udpPacePeakFloorBps = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_PACE_PEAK_FLOOR_BPS", 40000000, 0, 1000000000);
  const uint32_t udpKeyframePacePeakBps = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_KEYFRAME_PACE_PEAK_BPS", 100000000, 0, 1000000000);
  // Holding an encoded frame back to enforce even send spacing costs exactly what it holds:
  // measured end-to-end latency p95 went 4ms -> 31ms at 30fps when this was enabled
  // unconditionally, and rose further when the hold also pushed the next frame's deadline.
  // The H4 sender queue is already capped at two frames with keyframe supersede, so a
  // catch-up burst can only ever be a couple of frames; smoothing it is not worth a frame
  // period of latency. Off by default; the cap below re-enables bounded smoothing.
  const uint32_t senderMaxCadenceHoldUs =
      env_u32_clamped("REMOTE60_NATIVE_SENDER_MAX_CADENCE_HOLD_US", 0, 0, 33000);
  const bool senderCadenceSmoothing = senderMaxCadenceHoldUs > 0;
  // The capture-submit limiter keeps 60Hz callbacks from flooding a 30fps encode, but it
  // rejects rather than defers, and a rejected desktop-duplication frame is lost for good.
  // Widening the early tolerance lets a slightly-early callback through instead of leaving a
  // double-length gap on screen; the disable switch exists to measure the limiter's cost.
  const bool captureSubmitLimitEnabled =
      !env_truthy("REMOTE60_NATIVE_CAPTURE_SUBMIT_LIMIT_DISABLE");
  const uint32_t captureSubmitEarlyTolerancePercent = env_u32_clamped(
      "REMOTE60_NATIVE_CAPTURE_SUBMIT_EARLY_TOLERANCE_PCT", 25, 0, 90);
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

  // Print every candidate, not just the first: with a fallback list the port this line names is
  // a request, and "udp bound port=" below is what actually happened.
  std::cout << "[native-video-host] waiting client bindPort=";
  if (args.bindPortCandidates.size() > 1) {
    for (size_t i = 0; i < args.bindPortCandidates.size(); ++i) {
      if (i) std::cout << ",";
      std::cout << args.bindPortCandidates[i];
    }
  } else {
    std::cout << args.bindPort;
  }
  std::cout << " transport=" << video_transport_name(transport)
            << " fps=" << args.fps;
  if (useH264) std::cout << " bitrate=" << args.bitrate;
  std::cout << " seconds=" << args.seconds << "\n";
  if (useH264) {
    const uint64_t pacePeakBps = noPacingH264
                                     ? 0ULL
                                     : std::max<uint64_t>(
                                           udpPacePeakFloorBps,
                                           (static_cast<uint64_t>(args.bitrate) *
                                            udpPacePeakPercent) /
                                               100ULL);
    gUdpPacePeakBitrateBps.store(
        static_cast<uint32_t>(std::min<uint64_t>(pacePeakBps, 4000000000ULL)),
        std::memory_order_relaxed);
    gUdpKeyframePacePeakBitrateBps.store(udpKeyframePacePeakBps,
                                        std::memory_order_relaxed);
    std::cout << "[native-video-host] h264 pacing=" << (noPacingH264 ? "off" : "on")
              << " udpPacePeakPercent=" << udpPacePeakPercent
              << " udpPacePeakBps=" << gUdpPacePeakBitrateBps.load(std::memory_order_relaxed)
              << " udpPacePeakFloorBps=" << udpPacePeakFloorBps
              << " udpKeyframePacePeakBps="
              << gUdpKeyframePacePeakBitrateBps.load(std::memory_order_relaxed)
              << " stalePreEncodeGuard=" << (guardStalePreEncode ? 1 : 0)
              << " capturePoolBuffers=" << captureFramePoolBuffers
              << " encoderTuneMode=" << encoderTuneMode
              << " abr=" << (abrEnabled ? "on" : "off")
              << " abrMode=" << (abrQualityFirst ? "quality-first" : "default")
              << " frameGating=" << (frameGatingEnabled ? "on" : "off")
              << " staticSceneFps=" << frameGatingStaticFps
              << " gatingStaticPm=" << frameGatingStaticThresholdPermille
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

  // Credentials may come from the command line or the environment. The environment is the
  // better place for the password: a command line is readable by any process on the machine.
  auto arg_or_env = [](const std::string& fromArgs, const char* envKey) -> std::string {
    if (!fromArgs.empty()) return fromArgs;
    const char* v = std::getenv(envKey);
    return v ? std::string(v) : std::string();
  };
  const std::string directoryUrl = arg_or_env(args.directoryUrl, "REMOTE60_DIRECTORY_URL");
  const std::string directoryId = arg_or_env(args.directoryId, "REMOTE60_DIRECTORY_ID");
  const std::string directoryPw = arg_or_env(args.directoryPw, "REMOTE60_DIRECTORY_PW");
  remote60::native_poc::directory::HostAgent directoryAgent;
  if (!directoryUrl.empty() && transport != VideoTransport::Udp) {
    std::cerr << "[native-video-host] directory requires transport=udp; ignoring directory url\n";
  }

  SOCKET listenSock = INVALID_SOCKET;
  SOCKET clientSock = INVALID_SOCKET;
  // A second listener on the legacy port for clients that dial this PC by address; see the bind
  // site. `retiredSock` holds whichever socket the handshake did not choose but that still has an
  // owner -- the directory agent captured the primary socket and keeps heartbeating on it even
  // when a LAN client wins the handshake.
  SOCKET lanSock = INVALID_SOCKET;
  SOCKET retiredSock = INVALID_SOCKET;
  // main() returns from many places; a destructor is the only way to close these on every path.
  struct SocketCloser {
    SOCKET* handle;
    ~SocketCloser() {
      if (handle && *handle != INVALID_SOCKET) {
        closesocket(*handle);
        *handle = INVALID_SOCKET;
      }
    }
  };
  SocketCloser lanCloser{&lanSock};
  SocketCloser retiredCloser{&retiredSock};
  // The port the media socket actually landed on. It only differs from args.bindPort when a
  // fallback candidate was used, and the directory must publish this one rather than the request.
  uint16_t mediaBindPort = args.bindPort;
  sockaddr_in udpPeer{};
  bool udpPeerReady = false;
  std::atomic<bool> sessionDirectoryAuthenticated{false};
  std::mutex directorySessionAuthMu;
  std::string directorySessionToken;
  uint32_t directorySessionIpNet = 0;
  // Which of the three things a Hello can be. The caller needs the distinction because a first
  // Hello and its retransmissions are indistinguishable at the endpoint level -- and, behind a
  // relay, so are two entirely different clients.
  enum class DirectoryHello { Rejected, Retransmit, NewSession };
  auto classify_directory_hello = [&](const std::string& token,
                                      const sockaddr_in& peer) -> DirectoryHello {
    if (token.empty()) return DirectoryHello::Rejected;
    {
      std::lock_guard<std::mutex> lock(directorySessionAuthMu);
      if (!directorySessionToken.empty() && token == directorySessionToken &&
          peer.sin_addr.s_addr == directorySessionIpNet) {
        // A controller reconnect creates a new UDP socket/port. The already-proven opaque
        // capability remains the session credential, while the first authenticated source IP
        // (which can differ from the directory-observed endpoint under hairpin NAT) stays bound.
        // This is also what makes retransmission safe: the capability itself is single-use, so
        // without the cache the client's second Hello would be refused.
        return DirectoryHello::Retransmit;
      }
    }
    if (!directoryAgent.AuthorizePeer(token, peer)) return DirectoryHello::Rejected;
    {
      std::lock_guard<std::mutex> lock(directorySessionAuthMu);
      directorySessionToken = token;
      directorySessionIpNet = peer.sin_addr.s_addr;
    }
    return DirectoryHello::NewSession;
  };
  auto authorize_directory_session = [&](const std::string& token,
                                         const sockaddr_in& peer) -> bool {
    return classify_directory_hello(token, peer) != DirectoryHello::Rejected;
  };

  SecureInputBrokerClient secureInputBroker;

  // H4: the encode thread hands encoded frames to this sender instead of pacing the wire
  // inline. Pacing a 60ms keyframe used to stall the next frame's encode start directly.
  // Depth is 2: an arriving keyframe supersedes the whole backlog, and a delta that would
  // overflow the queue drops the backlog and requests a fresh keyframe -- an encoded delta
  // must never be skipped silently or the reference chain corrupts until the next IDR.
  struct EncodedSendItem {
    std::vector<uint8_t> bytes;
    UdpVideoChunkHeader udpHdr{};
    bool keyFrame = false;
    uint64_t frameIntervalUs = 0;
    // qpc time the encode/main thread handed this AU to the sender queue. The sender subtracts it
    // from its actual wire-start to expose queueWaitUs -- the gap between "AU ready" and "bytes on
    // the wire" -- so a stutter can be pinned to AU supply vs the sender/wire, not guessed.
    uint64_t enqueueUs = 0;
    // Media epoch live when this item was handed to the sender. streamGeneration is a
    // target-selection id that does NOT change on a session rollover, so it cannot fence a delta
    // encoded for the previous client. The sender drops any dequeued item whose mediaEpoch no
    // longer matches the current one (see mediaSessionEpoch).
    uint64_t mediaEpoch = 0;
  };
  std::mutex senderMu;
  std::condition_variable senderCv;
  std::deque<EncodedSendItem> senderQueue;
  sockaddr_in senderPeer{};
  bool senderPeerReady = false;
  // After a backlog drop every delta references frames that never went out; shipping them
  // paints macroblock corruption on the client until the next IDR. They are held back here
  // until the requested keyframe actually passes through.
  bool senderWaitingForKey = false;
  // Session media barrier. Bumped (under senderMu) by the rollover transaction in pump_udp_hello;
  // read by the sender at dequeue to fence any item stamped for a previous session. Every item is
  // stamped with this value when enqueued. Starts at 1 to match sessionEpoch.
  std::atomic<uint64_t> mediaSessionEpoch{1};
  std::atomic<bool> senderStop{false};
  std::atomic<bool> senderSendFailed{false};
  std::atomic<bool> senderRequestKey{false};
  // Set by the sender thread when a same-epoch transport error re-armed the barrier. The main loop
  // consumes it at its top -> forceKeyNext + arm_trailing_kick, because senderRequestKey is only
  // consumed after a real frame is popped: on a static desktop no new frame arrives to carry it, so
  // the recovery IDR would never be produced. This is the only barrier-recovery signal that works
  // when the screen is not changing. forceKeyNext must never be written from the sender thread.
  std::atomic<bool> senderRecoveryPending{false};
  std::atomic<uint64_t> barrierRearmCount{0};  // same-epoch send-failure barrier re-arms (telemetry)
  std::atomic<uint64_t> senderDropCount{0};
  std::atomic<uint64_t> senderTxFrames{0};
  std::atomic<uint64_t> senderTxChunks{0};
  std::atomic<uint64_t> senderTxBytes{0};
  std::atomic<uint64_t> senderTxNoPeer{0};
  std::atomic<uint64_t> senderLastSendStartUs{0};
  std::atomic<uint64_t> senderSendDurSumUs{0};
  std::atomic<uint64_t> senderSendDurMaxUs{0};
  std::atomic<uint64_t> senderSendCount{0};
  // IDR telemetry written by the sender thread (per current media epoch): when the first key AU of
  // this session hit the wire, and the size/chunk count of the last key AU sent. Reset by the
  // rollover transaction so they describe the current session, not the previous one. Diagnostic
  // only -- never wired into ABR evidence.
  std::atomic<uint64_t> senderFirstKeyWireUs{0};
  std::atomic<uint64_t> senderLastKeyAuBytes{0};
  std::atomic<uint64_t> senderLastKeyAuChunks{0};
  std::thread senderThread;
  // The reader thread owns the peer address; the render loop picks up changes through these.
  std::atomic<uint32_t> udpPeerIpNet{0};
  std::atomic<uint16_t> udpPeerPortNet{0};
  std::atomic<bool> udpPeerChanged{false};
  if (transport == VideoTransport::Tcp) {
    listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] listen socket create failed\n";
      return 2;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(args.bindPort);
    local.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
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
    local.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
    // Walk the candidates in order and keep the first that binds. A failed bind leaves the
    // socket unbound, so the next attempt can reuse it.
    std::vector<uint16_t> portCandidates = args.bindPortCandidates;
    if (portCandidates.empty()) portCandidates.push_back(args.bindPort);
    bool udpBound = false;
    for (const uint16_t candidate : portCandidates) {
      local.sin_port = htons(candidate);
      if (bind(clientSock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0) {
        mediaBindPort = candidate;
        udpBound = true;
        break;
      }
      std::cerr << "[native-video-host] udp bind failed port=" << candidate << "; trying next\n";
    }
    if (!udpBound) {
      std::cerr << "[native-video-host] udp bind failed on every candidate port\n";
      closesocket(clientSock);
      return 3;
    }
    std::cout << "[native-video-host] udp bound port=" << mediaBindPort << "\n";

    // Keep the last candidate listening as well, so dialling this PC by address still works.
    //
    // The candidate list exists to move the host onto a port restrictive networks allow, and
    // moving it is exactly what breaks the other way in: both clients default to the legacy port
    // when someone types an address by hand. The directory path is unaffected -- it dials
    // hostPublicUdpPort, which follows whatever the primary socket was given -- but a LAN user
    // has nothing telling them the port changed.
    //
    // Only the handshake watches both. Whichever socket the Hello arrives on becomes the media
    // socket and everything downstream is unchanged, so a session that never uses this listener
    // behaves exactly as it did before.
    const uint16_t lanPort =
        portCandidates.size() > 1 ? portCandidates.back() : 0;
    if (lanPort != 0 && lanPort != mediaBindPort) {
      lanSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (lanSock != INVALID_SOCKET) {
        sockaddr_in lanAddr{};
        lanAddr.sin_family = AF_INET;
        lanAddr.sin_port = htons(lanPort);
        lanAddr.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
        if (bind(lanSock, reinterpret_cast<const sockaddr*>(&lanAddr), sizeof(lanAddr)) == 0) {
          std::cout << "[native-video-host] lan direct-dial listener port=" << lanPort << "\n";
        } else {
          // Not fatal: the primary socket is the one that matters, and the usual reason this
          // fails is another GNLink host already holding the legacy port.
          std::cout << "[native-video-host] lan direct-dial listener unavailable port=" << lanPort
                    << "\n";
          closesocket(lanSock);
          lanSock = INVALID_SOCKET;
        }
      }
    }

    // The directory agent shares this socket on purpose: the public address it publishes has
    // to be the one NAT maps for the media stream, and that is a property of this socket.
    if (!directoryUrl.empty()) {
      remote60::native_poc::directory::HostAgentConfig dirCfg;
      dirCfg.url = directoryUrl;
      dirCfg.accountId = directoryId;
      dirCfg.password = directoryPw;
      dirCfg.hostName = args.directoryHostName;
      dirCfg.observeUdpPort = args.directoryObservePort;
      dirCfg.localUdpPort = mediaBindPort;
      // The legacy/alternate listener from N6. Publishing it is what lets a client whose network
      // filters the primary port have something else to dial.
      dirCfg.alternateUdpPort = lanPort;
      dirCfg.heartbeatSeconds = env_u32_clamped("REMOTE60_DIRECTORY_HEARTBEAT_SEC", 25, 5, 300);
      std::string dirError;
      const bool started = directoryAgent.Start(
          dirCfg,
          [clientSock](const void* data, size_t len, const sockaddr_in& to) {
            (void)sendto(clientSock, static_cast<const char*>(data), static_cast<int>(len), 0,
                         reinterpret_cast<const sockaddr*>(&to), sizeof(to));
          },
          &dirError);
      if (!started) {
        // Not fatal: direct LAN connections still work, so say why and carry on.
        std::cerr << "[native-video-host] directory disabled: " << dirError << "\n";
      } else {
        std::cout << "[native-video-host] directory agent started url=" << directoryUrl << "\n";
      }
    }

    for (;;) {
      // Wait on the primary and, when present, the legacy direct-dial listener. Reading only the
      // primary would leave a LAN client's Hello sitting unanswered forever.
      SOCKET readySock = clientSock;
      if (lanSock != INVALID_SOCKET) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(clientSock, &readSet);
        FD_SET(lanSock, &readSet);
        timeval wait{};
        wait.tv_sec = 1;
        const int ready = select(0, &readSet, nullptr, nullptr, &wait);
        if (ready == 0) continue;
        if (ready == SOCKET_ERROR) {
          std::cerr << "[native-video-host] udp handshake select failed err=" << WSAGetLastError()
                    << "\n";
          closesocket(clientSock);
          return 5;
        }
        // The primary wins a tie: it is the one the directory published.
        readySock = FD_ISSET(clientSock, &readSet) ? clientSock : lanSock;
      }

      // Big enough for the directory's observation reply; a datagram larger than the buffer
      // would be dropped with WSAEMSGSIZE and taken for a handshake failure.
      uint8_t rx[kUdpReceiveBufferBytes];
      sockaddr_in peer{};
      int peerLen = sizeof(peer);
      const int n = recvfrom(readySock, reinterpret_cast<char*>(rx), sizeof(rx), 0,
                             reinterpret_cast<sockaddr*>(&peer), &peerLen);
      // Zero-length datagrams are legal (NAT keepalives, scanners) and must not end the
      // process while it waits for a real client.
      if (n == 0) continue;
      if (n < 0) {
        const int err = WSAGetLastError();
        if (err == WSAEMSGSIZE || err == WSAECONNRESET) continue;
        std::cerr << "[native-video-host] udp handshake recv failed err=" << err << "\n";
        closesocket(clientSock);
        return 5;
      }
      UdpHelloPacket hello{};
      bool isHello = n >= static_cast<int>(sizeof(UdpHelloPacket));
      if (isHello) {
        std::memcpy(&hello, rx, sizeof(hello));
        isHello = hello.magic == remote60::native_poc::kMagic &&
                  hello.kind == static_cast<uint16_t>(UdpPacketKind::Hello) &&
                  hello.version == remote60::native_poc::kUdpProtocolVersion &&
                  (hello.features & remote60::native_poc::kUdpFeatureVideoFec) != 0;
      }
      if (!isHello) {
        // Only the primary socket carries directory traffic; the legacy listener never had a
        // punch or an observation sent to it, and feeding it in would let unrelated LAN noise
        // interrupt the heartbeat.
        if (readySock == clientSock) {
          (void)directoryAgent.ConsumeUdpPacket(rx, static_cast<size_t>(n), peer);
        }
        continue;
      }

      gUdpVideoFecInterleaved.store(
          (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved) != 0,
          std::memory_order_relaxed);

      UdpHelloPacket ack{};
      ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
      ack.features = remote60::native_poc::kUdpFeatureVideoFec |
                     (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved);
      size_t tokenLen = 0;
      while (tokenLen < sizeof(hello.authToken) && hello.authToken[tokenLen] != '\0') ++tokenLen;
      if (tokenLen > 0) {
        const std::string authToken(hello.authToken, hello.authToken + tokenLen);
        if (!authorize_directory_session(authToken, peer)) {
          std::cerr << "[native-video-host] rejected udp hello with invalid directory capability\n";
          continue;
        }
        sessionDirectoryAuthenticated.store(true, std::memory_order_release);
        ack.features |= remote60::native_poc::kUdpFeatureDirectoryAuth;
      }
      (void)sendto(readySock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                   reinterpret_cast<const sockaddr*>(&peer), peerLen);

      // The socket that answered becomes the media socket for the rest of the session, so
      // everything downstream keeps using clientSock exactly as before.
      if (readySock != clientSock) {
        std::cout << "[native-video-host] client arrived on the lan direct-dial listener; "
                     "media moves to port "
                  << lanPort << "\n";
        // The directory agent captured the primary socket and must keep heartbeating on it, so
        // it is retired rather than closed -- otherwise the host drops off the directory the
        // moment someone connects over the LAN.
        retiredSock = clientSock;
        clientSock = lanSock;
        lanSock = INVALID_SOCKET;
      } else if (lanSock != INVALID_SOCKET) {
        closesocket(lanSock);
        lanSock = INVALID_SOCKET;
      }

      udpPeer = peer;
      udpPeerReady = true;
      {
        std::lock_guard<std::mutex> lk(senderMu);
        senderPeer = peer;
        senderPeerReady = true;
      }
      udpPeerIpNet.store(peer.sin_addr.s_addr, std::memory_order_release);
      udpPeerPortNet.store(peer.sin_port, std::memory_order_release);
      break;
    }
    // Stays blocking: a dedicated reader thread now owns receives, and control messages must
    // not wait for the next render-loop iteration. The timeout only exists so that thread can
    // notice shutdown.
    (void)remote60::native_poc::set_recv_timeout(clientSock, 200);
  }

  if (sessionDirectoryAuthenticated.load(std::memory_order_acquire)) {
    std::string secureInputStatus;
    const std::wstring servicePath = remote60::native_poc::sibling_executable_path(
        L"GNLinkInputService.exe");
    const bool secureInputReady =
        secureInputBroker.EnsureInstalledAndConnected(servicePath, &secureInputStatus);
    std::cout << "[native-video-host] secure-input ready=" << (secureInputReady ? 1 : 0)
              << " status=" << secureInputStatus << "\n";
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
  // Split of what happened while a security prompt or the lock screen was in front. inputEvents
  // alone cannot answer it: the fallback path reports success whether or not the click reached
  // anything, so a dead session and a working one produce identical numbers.
  std::atomic<uint64_t> secureInputAttempts{0};       // events seen while the secure desktop was up
  std::atomic<uint64_t> secureInputDelivered{0};      // handed to the SYSTEM agent
  std::atomic<uint64_t> secureInputBrokerFailed{0};   // agent unreachable; fell back, cannot land
  std::atomic<uint64_t> secureInputSkipWindowMode{0}; // window mode never routes to the agent
  std::atomic<uint64_t> secureInputSkipUnauthenticated{0};  // no directory capability to act on
  // P2 desktop-backend promotion (WGC -> requested DXGI climb-back). Lifetime totals, never
  // per-second reset: a session-shape summary is more useful than a rate for a rare transition.
  std::atomic<uint64_t> desktopPromotionAttempts{0};
  std::atomic<uint64_t> desktopPromotionSuccess{0};
  std::atomic<uint64_t> desktopPromotionFail{0};
  std::atomic<uint64_t> desktopPromotionDeferredSecureTotal{0};  // deadlines held off by the secure gate (per episode)
  std::atomic<uint64_t> desktopSecureProbeFalseTotal{0};         // uncached probes that saw a secure desktop
  std::atomic<uint64_t> lastPromotionWaitUs{0};                  // demotion -> successful promotion
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
  // Which screen desktop mode shows. Zero is the primary, which is what it always was, so a
  // client that never selects one behaves exactly as before.
  std::atomic<uint32_t> selectedMonitorIdState{0};
  std::atomic<uint32_t> monitorSelectRequested{0};
  std::atomic<bool> monitorSelectPending{false};
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
  std::atomic<bool> desktopBackendReqPending{false};
  std::atomic<uint32_t> desktopBackendReqSeq{0};
  std::atomic<uint16_t> desktopBackendReqValue{
      desktop_capture_backend_code(desktop_capture_backend_from_env())};
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
  // Input desktop is routed on a cached (~250ms) default/secure check; when a UAC prompt or lock
  // rises between refreshes an event lands on ordinary SendInput and fails. These split that
  // failure by its real cause instead of piling every miss into inputInjectFail:
  //   inputFreshProbeSecure  -- cached-default event failed, an uncached re-probe found the desktop
  //                             actually secure (the stale-cache case)
  //   inputFreshProbeReroute -- of those, the ones the SYSTEM broker then landed on the retry
  //   inputInjectFailDefault -- cached-default event failed AND an uncached re-probe still says
  //                             default: a genuine failure on the interactive desktop
  std::atomic<uint64_t> inputFreshProbeSecure{0};
  std::atomic<uint64_t> inputFreshProbeReroute{0};
  std::atomic<uint64_t> inputInjectFailDefault{0};
  // One control conversation, independent of how the bytes travel. TCP works on a LAN;
  // a host behind NAT is only reachable over the punched UDP socket, so the same dispatch
  // has to serve both.
  // Closes the outbound message when the dispatch arm returns, however it returns.
  struct FlushControlMessageOnExit {
    ControlLink* link = nullptr;
    ~FlushControlMessageOnExit() {
      if (link) (void)link->EndMessage();
    }
  };

  auto serve_control_session = [&](ControlLink& link) {
    // A new session starts with the stream on, exactly like the first client of a fresh
    // process. The previous session's disconnect turned it off, and a client that never
    // sends stream-state (the Windows client) would otherwise stare at a black screen
    // forever after any reconnect. Clients that manage the state explicitly still can.
    if (!streamControlActive.exchange(true, std::memory_order_acq_rel)) {
      std::cout << "[native-video-host][control] stream restored for new session\n";
    }
    auto send_window_list = [&](uint32_t seq) -> bool {
      ControlWindowListMessage rsp{};
      rsp.header.magic = remote60::native_poc::kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowList);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      rsp.seq = seq;
      if (windowSelectionLocked.load(std::memory_order_relaxed)) {
        rsp.flags |= remote60::native_poc::kControlWindowListFlagSelectionLocked;
      }
      // Tells the client it may ask for previews; older hosts leave this clear and
      // older clients ignore the bit, so both directions stay compatible.
      rsp.flags |= remote60::native_poc::kControlWindowListFlagThumbnails;
      // Says the monitor messages exist here. A client that asked an older host would wait for a
      // reply that never comes, since unknown opcodes are drained silently.
      rsp.flags |= remote60::native_poc::kControlWindowListFlagMonitors;
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
      std::cout << "[native-video-host][control] window-list seq=" << seq
                << " count=" << rsp.itemCount
                << " selectedId=" << rsp.selectedWindowId
                << "\n";
      return link.Write(&rsp, sizeof(rsp));
    };
    auto send_monitor_list = [&](uint32_t seq) -> bool {
      ControlMonitorListMessage rsp{};
      rsp.header.magic = remote60::native_poc::kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlMonitorList);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      rsp.seq = seq;
      rsp.selectedMonitorId = selectedMonitorIdState.load(std::memory_order_acquire);
      const auto monitors = enumerate_monitors();
      rsp.itemCount = std::min<uint32_t>(static_cast<uint32_t>(monitors.size()),
                                         remote60::native_poc::kControlMonitorListMaxEntries);
      for (uint32_t i = 0; i < rsp.itemCount; ++i) {
        const auto& src = monitors[i];
        auto& dst = rsp.items[i];
        dst.id = i;
        dst.x = src.x;
        dst.y = src.y;
        dst.width = src.width;
        dst.height = src.height;
        if (src.primary) dst.flags |= remote60::native_poc::kControlMonitorFlagPrimary;
        std::snprintf(dst.name, sizeof(dst.name), "%s", src.name.c_str());
      }
      std::cout << "[native-video-host][control] monitor-list seq=" << seq
                << " count=" << rsp.itemCount << " selectedId=" << rsp.selectedMonitorId << "\n";
      return link.Write(&rsp, sizeof(rsp));
    };
    auto send_window_thumbnail =
        [&](const ControlWindowThumbnailRequestMessage& req) -> bool {
      const uint32_t maxW = std::clamp<uint32_t>(
          req.maxWidth == 0 ? 256u : req.maxWidth, 16u,
          remote60::native_poc::kWindowThumbnailMaxWidth);
      const uint32_t maxH = std::clamp<uint32_t>(
          req.maxHeight == 0 ? 160u : req.maxHeight, 16u,
          remote60::native_poc::kWindowThumbnailMaxHeight);

      std::vector<uint8_t> bgra;
      uint32_t tw = 0;
      uint32_t th = 0;
      bool ok = false;
      if (req.windowId == 0) {
        ok = capture_window_thumbnail(nullptr, maxW, maxH, &bgra, &tw, &th);
      } else {
        HWND hwnd = window_id_to_hwnd(req.windowId);
        if (should_include_window(hwnd)) {
          ok = capture_window_thumbnail(hwnd, maxW, maxH, &bgra, &tw, &th);
        }
      }
      if (bgra.size() > remote60::native_poc::kWindowThumbnailMaxPayloadBytes) {
        ok = false;
      }

      ControlWindowThumbnailHeader rsp{};
      rsp.header.magic = remote60::native_poc::kMagic;
      rsp.header.type = static_cast<uint16_t>(MessageType::ControlWindowThumbnail);
      rsp.header.size = static_cast<uint16_t>(sizeof(rsp));
      rsp.seq = req.seq;
      rsp.windowId = req.windowId;
      if (ok) {
        rsp.flags |= 0x1u;
        rsp.width = tw;
        rsp.height = th;
        rsp.stride = tw * 4u;
        rsp.payloadSize = static_cast<uint32_t>(bgra.size());
        rsp.version = qpc_now_us();
      }
      if (!link.Write(&rsp, sizeof(rsp))) return false;
      if (rsp.payloadSize == 0) return true;
      return link.Write(bgra.data(), bgra.size());
    };
    auto send_input_ack = [&](uint32_t seq) -> bool {
      ControlInputAckMessage ack{};
      ack.header.magic = remote60::native_poc::kMagic;
      ack.header.type = static_cast<uint16_t>(MessageType::ControlInputAck);
      ack.header.size = static_cast<uint16_t>(sizeof(ack));
      ack.seq = seq;
      ack.hostRecvQpcUs = qpc_now_us();
      ack.hostSendQpcUs = qpc_now_us();
      return link.Write(&ack, sizeof(ack));
    };

    while (!stop.load()) {
      MessageHeader header{};
      if (!link.Read(&header, sizeof(header))) break;
      // Marks the response boundary the UDP transport needs; a no-op over TCP.
      const FlushControlMessageOnExit flushResponse{&link};
      if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
      const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
      const auto type = static_cast<MessageType>(header.type);

      if (type == MessageType::ControlPing && header.size == sizeof(ControlPingMessage)) {
        ControlPingMessage ping{};
        ping.header = header;
        if (!link.Read(&ping.seq, sizeof(ping) - sizeof(MessageHeader))) break;
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
        // The probe is cached for 250ms, so asking it per ping is cheap. Telling the viewer that
        // a security prompt is up is the difference between an explained pause and an apparent
        // freeze, and it costs one bit in a word that is already on the wire.
        if (!interactive_desktop_is_default()) {
          pong.captureTargetFlags |= remote60::native_poc::kCaptureFlagSecureDesktopActive;
        }
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
        if (!link.Write(&pong, sizeof(pong))) break;
        continue;
      }

      if (type == MessageType::ControlInputEvent && header.size == sizeof(ControlInputEventMessage)) {
        ControlInputEventMessage input{};
        input.header = header;
        if (!link.Read(&input.seq, sizeof(input) - sizeof(MessageHeader))) break;
        std::string resolvedTarget;
        if (inputInjectionEnabled) {
          const bool desktopMode =
              !inputTargetCriteria.enabled() &&
              (selectedWindowIdState.load(std::memory_order_acquire) == 0);
          const uint32_t domainW = inputDomainW.load(std::memory_order_acquire);
          const uint32_t domainH = inputDomainH.load(std::memory_order_acquire);
          InputInjectResult injectResult = InputInjectResult::Failed;
          // Prefer the SYSTEM agent, which is the only way into elevated windows and the lock
          // screen -- but fall back when it is unavailable. The service is registered by the
          // installer, so a host running from a build tree (or before installation) has no
          // broker at all, and treating that as a hard failure left the session with no input
          // whatsoever instead of the ordinary desktop injection that still works fine.
          //
          // The conjuncts are evaluated separately so a failure can name itself. Clicks that go
          // nowhere on a consent prompt look identical to clicks that work: the fallback path
          // below reports Injected either way, because SendInput on the Default desktop succeeds
          // whether or not anything is there to receive it. Counting which branch ran, and why,
          // is the difference between "input is broken" and a specific cause.
          const bool secureDesktopActive = !interactive_desktop_is_default();
          bool routedToAgent = false;
          if (secureDesktopActive) {
            secureInputAttempts.fetch_add(1, std::memory_order_relaxed);
            if (!desktopMode) {
              secureInputSkipWindowMode.fetch_add(1, std::memory_order_relaxed);
            } else if (!sessionDirectoryAuthenticated.load(std::memory_order_acquire)) {
              // A plain-LAN session has no capability token, and the agent will not act without
              // one. Nothing about the click is wrong; it simply cannot be authorised.
              secureInputSkipUnauthenticated.fetch_add(1, std::memory_order_relaxed);
            } else if (!secureInputBroker.SendInputEvent(input, domainW, domainH)) {
              secureInputBrokerFailed.fetch_add(1, std::memory_order_relaxed);
            } else {
              secureInputDelivered.fetch_add(1, std::memory_order_relaxed);
              routedToAgent = true;
            }
          }
          // Set once the outcome has already been tallied (the secure-desktop path), so the result
          // switch below does not also count it -- e.g. as an inject failure.
          bool injectAccounted = false;
          if (routedToAgent) {
            injectResult = InputInjectResult::Injected;
            resolvedTarget = " secure-system-agent";
          } else if (secureDesktopActive) {
            // The cached check says the desktop is secure and the broker path did not route (window
            // mode, unauthenticated, or broker failure -- all already tallied). Ordinary SendInput on
            // a secure desktop would just fail, so do not fall through to it, and do not double-count
            // the miss as an inject failure.
            injectAccounted = true;
          } else {
            injectResult =
                inject_background_input_event(input, inputTargetCriteria, hostCaptureTargetHwnd,
                                              desktopMode, domainW, domainH,
                                              &desktopInputState, &resolvedTarget);
            if (injectResult == InputInjectResult::Failed) {
              // The cached check said default but injection failed: the 250ms cache may be stale
              // because a UAC prompt or lock screen rose since the last refresh. Pay for ONE uncached
              // probe on this specific failing event (never per event -- that would be far too costly
              // on a 100+/s pointer stream) to find out which it is.
              if (!interactive_desktop_is_default_uncached()) {
                inputFreshProbeSecure.fetch_add(1, std::memory_order_relaxed);
                // Actually secure now. Retry THIS event through the SYSTEM broker exactly once.
                if (desktopMode &&
                    sessionDirectoryAuthenticated.load(std::memory_order_acquire) &&
                    secureInputBroker.SendInputEvent(input, domainW, domainH)) {
                  injectResult = InputInjectResult::Injected;
                  resolvedTarget = " secure-system-agent(reprobe)";
                  inputFreshProbeReroute.fetch_add(1, std::memory_order_relaxed);
                  secureInputDelivered.fetch_add(1, std::memory_order_relaxed);
                }
                // else: genuinely secure but not broker-eligible (window mode / unauthenticated) or
                // the broker failed -- stays Failed, but inputFreshProbeSecure distinguishes it from
                // a real default-desktop failure below.
              } else {
                // Uncached re-probe still says default: a genuine failure on the interactive desktop
                // (UIPI, no target, a transient block). Counted separately so it is not confused
                // with the stale-cache case.
                inputInjectFailDefault.fetch_add(1, std::memory_order_relaxed);
              }
            }
          }
          if (injectAccounted) {
            // Already tallied on the secure path; nothing more to record.
          } else if (injectResult == InputInjectResult::Injected) {
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
        if (!link.Read(&text.seq, sizeof(text) - sizeof(MessageHeader))) break;
        std::string resolvedTarget;
        if (inputInjectionEnabled) {
          const bool desktopMode =
              !inputTargetCriteria.enabled() &&
              (selectedWindowIdState.load(std::memory_order_acquire) == 0);
          InputInjectResult injectResult = InputInjectResult::Failed;
          if (desktopMode && sessionDirectoryAuthenticated.load(std::memory_order_acquire) &&
              !interactive_desktop_is_default() &&
              secureInputBroker.SendInputText(text,
                                              inputDomainW.load(std::memory_order_acquire),
                                              inputDomainH.load(std::memory_order_acquire))) {
            injectResult = InputInjectResult::Injected;
            resolvedTarget = " secure-system-agent";
          } else {
            injectResult = apply_input_text_message(text, hostCaptureTargetHwnd, desktopMode,
                                                    &desktopInputState, &resolvedTarget);
          }
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
        if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
        if (!send_window_list(req.seq)) break;
        continue;
      }

      if (type == MessageType::ControlMonitorListRequest &&
          header.size == sizeof(ControlMonitorListRequestMessage)) {
        ControlMonitorListRequestMessage req{};
        req.header = header;
        if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
        if (!send_monitor_list(req.seq)) break;
        continue;
      }

      if (type == MessageType::ControlMonitorSelect &&
          header.size == sizeof(ControlMonitorSelectMessage)) {
        ControlMonitorSelectMessage req{};
        req.header = header;
        if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
        // Applied by the render loop, which owns the capture item; answered with the list so the
        // client sees the selection that actually took effect rather than the one it asked for.
        monitorSelectRequested.store(req.monitorId, std::memory_order_release);
        monitorSelectPending.store(true, std::memory_order_release);
        if (!send_monitor_list(req.seq)) break;
        continue;
      }

      if (type == MessageType::ControlWindowThumbnailRequest &&
          header.size == sizeof(ControlWindowThumbnailRequestMessage)) {
        ControlWindowThumbnailRequestMessage req{};
        req.header = header;
        if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
        if (!send_window_thumbnail(req)) break;
        continue;
      }

      if (type == MessageType::ControlWindowSelect &&
          header.size == sizeof(ControlWindowSelectMessage)) {
        ControlWindowSelectMessage req{};
        req.header = header;
        if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;

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
          // Also give up when the link dies. The client that asked for this selection may be the
          // one that just went away, and the next client cannot be served until this returns --
          // so waiting only for completion would hold the whole session handover behind a reply
          // nobody is left to read. Polled, because a rollover closes the channel rather than
          // touching this transaction.
          while (!stop.load() && !windowSelectionTxn.completed && link.Alive()) {
            windowSelectionTxn.cv.wait_for(lk, std::chrono::milliseconds(100));
          }
          rsp.flags = windowSelectionTxn.responseFlags;
          rsp.windowId = windowSelectionTxn.responseWindowId;
          rsp.streamGeneration = windowSelectionTxn.responseStreamGeneration;
          rsp.hostSendQpcUs = qpc_now_us();
          std::snprintf(rsp.reason, sizeof(rsp.reason), "%s", windowSelectionTxn.responseReason.c_str());
          std::snprintf(rsp.title, sizeof(rsp.title), "%s", windowSelectionTxn.responseTitle.c_str());
        }

        if (!link.Write(&rsp, sizeof(rsp))) break;
        continue;
      }

      if (type == MessageType::ControlClientMetrics &&
          header.size == sizeof(ControlClientMetricsMessage)) {
        ControlClientMetricsMessage metrics{};
        metrics.header = header;
        if (!link.Read(&metrics.seq, sizeof(metrics) - sizeof(MessageHeader))) break;
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
        // Logged as it arrives rather than folded into the per-second stat line: this is the
        // only view the host gets of what the remote display is actually doing, and a viewer
        // reporting stutter needs it visible without attaching to the device.
        if (metrics.presentSampleCount > 0) {
          std::cout << "[native-video-host][client-present]"
                    << " fps=" << (metrics.presentFpsX100 / 100.0)
                    << " targetUs=" << metrics.presentTargetIntervalUs
                    << " gapP50Us=" << metrics.presentGapP50Us
                    << " gapP95Us=" << metrics.presentGapP95Us
                    << " gapMaxUs=" << metrics.presentGapMaxUs
                    << " over1_5x=" << metrics.presentOver1_5xCount
                    << " over2x=" << metrics.presentOver2xCount
                    << " samples=" << metrics.presentSampleCount
                    << " sched=" << metrics.presentScheduledCount
                    << " immediate=" << metrics.presentImmediateCount
                    << " reanchor=" << metrics.presentReanchorCount
                    << " displayed=" << metrics.presentDisplayedCount
                    << "\n";
        }
        continue;
      }

      if (type == MessageType::ControlRequestKeyFrame &&
          header.size == sizeof(ControlRequestKeyFrameMessage)) {
        ControlRequestKeyFrameMessage req{};
        req.header = header;
        if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
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
        if (!link.Read(&tune.seq, sizeof(tune) - sizeof(MessageHeader))) break;
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

      if (type == MessageType::ControlDesktopBackendRequest &&
          header.size == sizeof(ControlDesktopBackendRequestMessage)) {
        ControlDesktopBackendRequestMessage req{};
        req.header = header;
        if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
        if (req.backend == 1 || req.backend == 2 || req.backend == 3) {
          desktopBackendReqSeq.store(req.seq, std::memory_order_release);
          desktopBackendReqValue.store(req.backend, std::memory_order_release);
          desktopBackendReqPending.store(true, std::memory_order_release);
          std::cout << "[native-video-host][control] desktop-backend-request seq=" << req.seq
                    << " backend="
                    << (req.backend == 2 ? "wgc" : (req.backend == 3 ? "gdi" : "dxgi"))
                    << "\n";
        }
        continue;
      }

      if (type == MessageType::ControlStreamState &&
          header.size == sizeof(ControlStreamStateMessage)) {
        ControlStreamStateMessage req{};
        req.header = header;
        if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
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
        if (!link.Read(&req.seq, sizeof(req) - sizeof(MessageHeader))) break;
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

      if (bodySize > 0 && !link.Discard(bodySize)) break;
    }
    streamControlActive.store(false, std::memory_order_release);
  };

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
      ctlLocal.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
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
            {
              TcpControlLink link(acceptedSock);
              serve_control_session(link);
            }
            if (acceptedSock != INVALID_SOCKET) {
              shutdown(acceptedSock, SD_BOTH);
              closesocket(acceptedSock);
            }
            {
              SOCKET expected = acceptedSock;
              controlClientSock.compare_exchange_strong(expected, INVALID_SOCKET);
            }
            std::cout << "[native-video-host][control] tcp client disconnected\n";
          }
        });
      }
    }
  }

  // Control over the media socket. A client that arrived through the directory service has no
  // way to open a TCP connection back to us, so the same dispatch is also served here; a LAN
  // client that prefers TCP simply never sends control datagrams and this stays idle.
  UdpControlChannel udpControlChannel;
  std::thread udpControlThread;
  std::thread udpReaderThread;

  // ---------------------------------------------------------------- session epoch
  //
  // A session begins when a Hello presents a capability we have not seen before, and that is the
  // only reliable signal there is. The endpoint is not one: through a relay every client reaches
  // us from the same address and port, so "the peer changed" stays false forever and the second
  // client inherits the first one's control channel -- where its messages are acknowledged and
  // then dropped, because their sequence numbers look like ones already delivered.
  //
  // The epoch serialises the handover. The reader raises it and waits; the dispatcher resets the
  // channel, re-enters its session loop (which is also what turns the stream back on) and
  // publishes that it is ready; only then does the reader answer the Hello. Since the client
  // repeats its Hello until it sees an Ack, nothing it sends can arrive before the reset.
  // Starts at one, not zero: control is only wired up after the handshake loop above has already
  // accepted a Hello, so by the time the dispatcher starts there is a session waiting for it.
  std::atomic<uint64_t> sessionEpoch{1};
  std::atomic<uint64_t> controlReadyEpoch{0};
  std::mutex sessionEpochMu;
  std::condition_variable sessionEpochCv;
  auto begin_session_epoch = [&]() -> uint64_t {
    const uint64_t epoch = sessionEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    // Wakes the dispatcher out of its blocking read so it can pick the new epoch up. Reset is
    // deliberately left to that thread: doing it here would clear the queues underneath a
    // session still being served.
    udpControlChannel.Close(remote60::native_poc::ControlCloseReason::SessionRollover);
    sessionEpochCv.notify_all();
    return epoch;
  };
  auto await_control_ready = [&](uint64_t epoch) {
    std::unique_lock<std::mutex> lock(sessionEpochMu);
    // Bounded: if the dispatcher cannot come back we answer the client anyway, because a session
    // with video and no window list still beats one that never starts.
    sessionEpochCv.wait_for(lock, std::chrono::milliseconds(1500), [&] {
      return controlReadyEpoch.load(std::memory_order_acquire) >= epoch ||
             sessionEpoch.load(std::memory_order_acquire) > epoch;
    });
  };

  if (transport == VideoTransport::Udp) {
    udpControlChannel.Configure(
        [&](const void* data, size_t len) -> bool {
          const uint32_t ip = udpPeerIpNet.load(std::memory_order_acquire);
          const uint16_t port = udpPeerPortNet.load(std::memory_order_acquire);
          if (ip == 0 || port == 0) return false;
          sockaddr_in to{};
          to.sin_family = AF_INET;
          to.sin_addr.s_addr = ip;
          to.sin_port = port;
          return sendto(clientSock, static_cast<const char*>(data), static_cast<int>(len), 0,
                        reinterpret_cast<const sockaddr*>(&to), sizeof(to)) > 0;
        },
        remote60::native_poc::kUdpControlStreamHostToClient,
        remote60::native_poc::kUdpControlStreamClientToHost, args.udpMtu);

    udpReaderThread = std::thread([&]() {
      // Startup barrier. The dispatcher's first Reset races this thread: if the client's first
      // ControlData lands here first, OnPacket ACKs it into rxReady_, then the dispatcher's
      // Reset wipes rxReady_ -- and the client, holding an ACK, never retransmits. The serve
      // loop then starves for its full 10s read timeout ("ended reason=none") with a 40-70%
      // field hit rate. Hold this thread off the socket until the dispatcher has published
      // controlReadyEpoch for the current epoch; datagrams meanwhile wait, unharmed, in the
      // kernel socket buffer. wait_for (not wait) so shutdown cannot strand us if no one
      // signals the cv after stop.
      {
        std::unique_lock<std::mutex> lock(sessionEpochMu);
        while (!stop.load() &&
               controlReadyEpoch.load(std::memory_order_acquire) <
                   sessionEpoch.load(std::memory_order_acquire)) {
          sessionEpochCv.wait_for(lock, std::chrono::milliseconds(50));
        }
      }
      int lastLoggedRecvError = 0;
      while (!stop.load()) {
        uint8_t rx[kUdpReceiveBufferBytes];
        sockaddr_in peer{};
        int peerLen = sizeof(peer);
        const int n = recvfrom(clientSock, reinterpret_cast<char*>(rx), sizeof(rx), 0,
                               reinterpret_cast<sockaddr*>(&peer), &peerLen);
        // A zero-length datagram is legal and arrives from NAT keepalives and port scanners.
        // It used to fall into the error path below and end this thread, after which no Hello
        // was ever read again: video kept streaming to the previous peer while every new
        // client connected its control channel and then watched nothing arrive.
        if (n == 0) continue;
        if (n < 0) {
          const int err = WSAGetLastError();
          if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK || err == WSAEMSGSIZE ||
              err == WSAECONNRESET) {
            // Nothing arrived, or one datagram was malformed. Keep the retransmit timers moving
            // so a stalled transfer still recovers while the link is quiet.
            udpControlChannel.Tick();
            continue;
          }
          // This thread is the only reader of hellos; while the process lives it must too.
          // Whatever went wrong with one receive, the socket itself outlives it.
          if (err != lastLoggedRecvError) {
            lastLoggedRecvError = err;
            std::cout << "[native-video-host] udp reader recv error err=" << err
                      << " (continuing)\n";
          }
          udpControlChannel.Tick();
          Sleep(50);
          continue;
        }
        const size_t len = static_cast<size_t>(n);

        UdpHelloPacket hello{};
        if (len >= sizeof(UdpHelloPacket)) {
          std::memcpy(&hello, rx, sizeof(hello));
          if (hello.magic == remote60::native_poc::kMagic &&
              hello.kind == static_cast<uint16_t>(UdpPacketKind::Hello) &&
              hello.version == remote60::native_poc::kUdpProtocolVersion &&
              (hello.features & remote60::native_poc::kUdpFeatureVideoFec) != 0) {
            gUdpVideoFecInterleaved.store(
                (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved) != 0,
                std::memory_order_relaxed);

            UdpHelloPacket ack{};
            ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
            ack.features =
                remote60::native_poc::kUdpFeatureVideoFec |
                (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved);
            size_t tokenLen = 0;
            while (tokenLen < sizeof(hello.authToken) && hello.authToken[tokenLen] != '\0') {
              ++tokenLen;
            }
            bool directoryAuthenticated = false;
            bool newSession = false;
            if (tokenLen > 0) {
              const std::string authToken(hello.authToken, hello.authToken + tokenLen);
              const auto kind = classify_directory_hello(authToken, peer);
              if (kind == DirectoryHello::Rejected) {
                std::cerr << "[native-video-host] rejected reconnect hello with invalid directory capability\n";
                continue;
              }
              newSession = (kind == DirectoryHello::NewSession);
              directoryAuthenticated = true;
              ack.features |= remote60::native_poc::kUdpFeatureDirectoryAuth;
              std::string secureInputStatus;
              (void)secureInputBroker.EnsureInstalledAndConnected(
                  remote60::native_poc::sibling_executable_path(
                      L"GNLinkInputService.exe"),
                  &secureInputStatus);
            } else if (sessionDirectoryAuthenticated.load(std::memory_order_acquire)) {
              // Do not let an unauthenticated LAN Hello take over or de-authorize an active
              // directory session. Direct-LAN mode remains available before authentication.
              std::cerr << "[native-video-host] rejected unauthenticated reconnect during directory session\n";
              continue;
            }
            sessionDirectoryAuthenticated.store(directoryAuthenticated,
                                                std::memory_order_release);
            const bool changed =
                udpPeerIpNet.load(std::memory_order_acquire) != peer.sin_addr.s_addr ||
                udpPeerPortNet.load(std::memory_order_acquire) != peer.sin_port;
            // An unauthenticated LAN client has no capability to compare, so the endpoint is all
            // there is to go on. It is a weaker signal -- an app restart that lands on the same
            // port is invisible -- but the relay, which is what makes endpoints ambiguous, only
            // ever carries authenticated sessions.
            const bool startsSession = directoryAuthenticated ? newSession : changed;
            if (changed) {
              udpPeerIpNet.store(peer.sin_addr.s_addr, std::memory_order_release);
              udpPeerPortNet.store(peer.sin_port, std::memory_order_release);
            }
            if (startsSession) {
              // Even when the endpoint is unchanged: a new client has a new decoder, and sending
              // it deltas against frames it never saw leaves it grey until the next keyframe.
              udpPeerChanged.store(true, std::memory_order_release);
              const uint64_t epoch = begin_session_epoch();
              std::cout << "[native-video-host][control] session epoch=" << epoch
                        << (changed ? " peer=new" : " peer=same") << "\n";
              await_control_ready(epoch);
            }
            // Answered last, so that by the time the client believes it is connected the control
            // channel behind this endpoint is already the new session's.
            (void)sendto(clientSock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                         reinterpret_cast<const sockaddr*>(&peer), peerLen);
            continue;
          }
        }

        if (udpControlChannel.OnPacket(rx, len)) continue;
        (void)directoryAgent.ConsumeUdpPacket(rx, len, peer);
      }
      udpControlChannel.Close(remote60::native_poc::ControlCloseReason::Shutdown);
      sessionEpochCv.notify_all();
    });

    // One dispatcher for the life of the process, serving one session after another. It used to
    // serve exactly one: any failed read returned from serve_control_session and the thread
    // exited for good, taking the stream with it (the session teardown clears
    // streamControlActive, and only re-entry restores it). A client that merely walked out of
    // Wi-Fi range was enough to leave the host answering handshakes and nothing else.
    udpControlThread = std::thread([&]() {
      uint64_t servedEpoch = 0;
      for (;;) {
        {
          std::unique_lock<std::mutex> lock(sessionEpochMu);
          sessionEpochCv.wait(lock, [&] {
            return stop.load() || sessionEpoch.load(std::memory_order_acquire) > servedEpoch;
          });
        }
        if (stop.load()) break;
        servedEpoch = sessionEpoch.load(std::memory_order_acquire);
        // Reset belongs here rather than in the reader: this is the thread that owns the
        // channel's read side, so nothing is being consumed while the queues are cleared.
        udpControlChannel.Reset();
        {
          std::lock_guard<std::mutex> lock(sessionEpochMu);
          controlReadyEpoch.store(servedEpoch, std::memory_order_release);
        }
        sessionEpochCv.notify_all();

        // The read timeout is what lets the host notice a client that simply vanished. The
        // channel only declares peer-lost while it has something to retransmit; a client that
        // dies between requests leaves nothing outstanding, and a blocking read sat here for
        // the rest of the process with the stream still marked active -- capturing, encoding,
        // and sending to nobody. The client pings about once a second, so ten silent seconds
        // is a client that is gone, not one that is slow.
        UdpControlLink link(&udpControlChannel, 10000);
        serve_control_session(link);
        // Closed is not finished. Retransmits running out means this client is gone, which is
        // the ordinary end of a session and the reason to wait for the next one.
        std::cout << "[native-video-host][control] udp control session ended epoch=" << servedEpoch
                  << " reason=" << remote60::native_poc::to_string(udpControlChannel.CloseReason())
                  << "\n";
      }
    });
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
  HRESULT hr = create_d3d11_device_for_primary_monitor(&d3d, &ctx, &fl);
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
  DesktopCaptureBackend requestedDesktopBackend = desktop_capture_backend_from_env();
  DesktopCaptureBackend activeDesktopBackend = requestedDesktopBackend;
  // A demotion away from the requested backend is temporary until proven otherwise; these pace
  // the attempts to get back to it. First retry is quick because the usual causes -- a UAC prompt
  // being answered, RDP disconnecting -- clear in seconds; the ceiling keeps a machine that
  // genuinely cannot use the requested backend from restarting capture forever.
  constexpr uint64_t kDesktopBackendRetryMinUs = 3'000'000;
  constexpr uint64_t kDesktopBackendRetryMaxUs = 30'000'000;
  uint64_t desktopBackendRetryAtUs = 0;
  uint64_t desktopBackendRetryDelayUs = kDesktopBackendRetryMinUs;
  // P2 secure-desktop stable gate. A demotion to WGC (UAC prompt, lock screen, RDP) used to be
  // climbed back on a bare 3s timer, so every retry deadline that fired while the secure desktop
  // was still up spent a restart_capture_session (pipeline flush + forced IDR) that failed at once
  // -- E_ACCESSDENIED churn. Now the promotion is additionally gated on the interactive DEFAULT
  // desktop having been up continuously for kDesktopDefaultStableUs, probed uncached at a bounded
  // cadence, with one final uncached check the instant before the restart to close the
  // probe->restart TOCTOU window. A genuine promotion failure (e.g. RDP: default desktop stable but
  // primary duplication still unavailable) is NOT deferred here -- it falls through to the existing
  // exponential backoff, which is the right owner for a backend that truly cannot start.
  constexpr uint64_t kDesktopDefaultStableUs = 1'000'000;       // continuous default settle before promote
  constexpr uint64_t kDesktopDefaultProbeIntervalUs = 200'000;  // OpenInputDesktop probe cadence
  uint64_t desktopDefaultStableSinceUs = 0;  // when the default desktop last became continuously up (0=not)
  uint64_t desktopDefaultProbeAtUs = 0;      // next uncached secure-desktop probe
  uint64_t desktopDemotionSinceUs = 0;       // when this WGC demotion began (for the promotion-wait metric)
  bool promotionDeferredForCurrentDeadline = false;  // episode latch so the deferred counter can't per-loop spin
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

  auto monitorInfo = primary_monitor_info();
  if (!monitorInfo.has_value()) {
    std::cerr << "[native-video-host] primary monitor query failed\n";
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    if (mfStarted) MFShutdown();
    return 8;
  }
  if (!captureWindowModeActive && requestedDesktopBackend == DesktopCaptureBackend::Dxgi &&
      monitorInfo->width < monitorInfo->height) {
    activeDesktopBackend = DesktopCaptureBackend::Wgc;
    std::cout << "[native-video-host] rotation_unsupported fallback_reason=rotation_unsupported\n";
  }
  // Tell the SYSTEM agent where the captured pixels live. Without it the agent can only assume,
  // and its old assumption -- the primary monitor -- put every click on the wrong screen when the
  // prompt opened somewhere else.
  secureInputBroker.SetTargetRect(monitorInfo->originX, monitorInfo->originY, monitorInfo->width,
                                  monitorInfo->height);

  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  uint32_t captureWidth = 0;
  uint32_t captureHeight = 0;
  winrt::Windows::Graphics::SizeInt32 captureSize{};
  if (captureWindowModeActive || activeDesktopBackend == DesktopCaptureBackend::Wgc) {
    item = captureWindowModeActive
               ? CreateItemForPrimaryMonitor(captureWindowInfo.hwnd, "CreateForWindow(target-window)")
               : CreateItemForPrimaryMonitor();
    if (!item) {
      std::cerr << "[native-video-host] capture item create failed\n";
      closesocket(clientSock);
      if (listenSock != INVALID_SOCKET) closesocket(listenSock);
      if (mfStarted) MFShutdown();
      return 8;
    }
    captureSize = item.Size();
    captureWidth = static_cast<uint32_t>(captureSize.Width);
    captureHeight = static_cast<uint32_t>(captureSize.Height);
  } else {
    captureWidth = monitorInfo->width;
    captureHeight = monitorInfo->height;
    captureSize.Width = static_cast<int32_t>(captureWidth);
    captureSize.Height = static_cast<int32_t>(captureHeight);
  }
  if (captureWidth < 2 || captureHeight < 2) {
    std::cerr << "[native-video-host] invalid capture size\n";
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    if (mfStarted) MFShutdown();
    return 9;
  }
  std::cout << "[native-video-host] desktop_backend="
            << (captureWindowModeActive ? "wgc_window" : desktop_capture_backend_name(activeDesktopBackend))
            << " capture=" << captureWidth << "x" << captureHeight << "\n";

  uint32_t encodeW = captureWidth;
  uint32_t encodeH = captureHeight;
  bool autoFallback720 = false;
  if (useH264) {
    choose_h264_encode_size(args, captureWidth, captureHeight, &encodeW, &encodeH, &autoFallback720);
  }

  // Whether the ladder, rather than the source size, is currently deciding the resolution. Held
  // across runtime changes so the band between the two thresholds can return the previous answer.
  bool encodeLadderReduced = autoFallback720;
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
  uint32_t abrHighBitrate = args.bitrate;
  uint32_t abrMidBitrate = std::min<uint32_t>(
      abrHighBitrate, std::max<uint32_t>(2000000u, (abrHighBitrate * 75u) / 100u));
  uint32_t abrLowBitrate = std::min<uint32_t>(
      abrHighBitrate, std::max<uint32_t>(1500000u, (abrHighBitrate * 55u) / 100u));
  bool abrHasMidProfile = (abrMidBitrate < abrHighBitrate);
  bool abrHasLowProfile = abrHasLowerResolution || (abrLowBitrate < abrMidBitrate);
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
  // Nominal (pre-aspect-fit) encode box of the current quality level, and the source size
  // the active encode dimensions were fitted against.
  uint32_t nominalEncodeW = abrHighW;
  uint32_t nominalEncodeH = abrHighH;
  uint32_t encodeSourceW = captureWidth;
  uint32_t encodeSourceH = captureHeight;
  // Refit debounce: candidate geometry and how long it has been stable.
  uint32_t pendingRefitW = 0;
  uint32_t pendingRefitH = 0;
  uint64_t pendingRefitSinceUs = 0;
  constexpr uint64_t kEncodeRefitSettleUs = 400000;  // 0.4 s of stable size before re-init
  uint32_t activeFps = args.fps;
  // What the user asked for, as distinct from whatever the encoder is running at this
  // moment: overview mode lowers the active values on purpose, and restoring focus from
  // "whatever is active" would restore the lowered ones. Only an explicit runtime tune of
  // the same field moves a ceiling -- a bitrate-only tune falls back to active values for
  // its fps/keyint arguments, and those must not leak in here.
  uint32_t userFpsCeiling = args.fps;
  uint32_t userKeyintCeiling = args.keyint;
  uint32_t activeBitrate = abrHighBitrate;
  // Field A/B override for the keyframe interval (0 = off). Every ~1s a 120-160KB IDR was
  // measured holding the previous frame an extra tick on 75% of key presents (the user's
  // "periodically shows the previous frame"); this pins keyint (e.g. 120) without touching the
  // client, winning over both the CLI default and runtime tunes.
  const uint32_t keyintOverride = env_u32_clamped("REMOTE60_NATIVE_KEYINT_OVERRIDE", 0, 0, 600);
  uint32_t activeKeyint = keyintOverride != 0 ? keyintOverride : args.keyint;
  uint64_t activeFrameIntervalUs =
      std::max<uint64_t>(1, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, activeFps)));
  uint64_t activePacingFrameIntervalUs = activeFrameIntervalUs;
  std::atomic<uint64_t> captureSubmitMinIntervalUs{activeFrameIntervalUs};
  std::atomic<uint64_t> nextCaptureSubmitUs{0};
  // Picks which offered frames reach the encoder, and how evenly. Guarded by its own mutex
  // because capture callbacks can arrive on more than one thread across backends.
  remote60::native_poc::CaptureCadenceGate captureCadenceGate;
  std::mutex captureCadenceMu;
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
  // Submit latch for forceKeyNext. The async MFT can hold the key output for a few inputs, and
  // forcing EVERY input in the meantime produced trains of 4-5 consecutive 40-160KB IDRs per
  // request (measured at 17:24:26/32/52 in the field log). One forced input per request: stamped
  // on submit, cleared when a key is accepted into the send path (on UDP that is the send-queue
  // enqueue, not the wire; a failed send re-forces via barrier recovery), and timing out (300ms)
  // so a lost key retries.
  uint64_t forceKeySubmittedAtUs = 0;
  int64_t captureTimelineOriginUs = -1;
  int64_t auTimelineOriginUs = -1;
  auto resetHostTimelineAnchors = [&]() {
    captureTimelineOriginUs = -1;
    auTimelineOriginUs = -1;
  };
  auto refresh_frame_intervals = [&]() {
    activeFrameIntervalUs =
        std::max<uint64_t>(1, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, activeFps)));
    // Encoded capture is callback-clocked below. Raw mode uses the main tick at the exact
    // requested cadence.
    activePacingFrameIntervalUs = activeFrameIntervalUs;
    captureSubmitMinIntervalUs.store(activeFrameIntervalUs, std::memory_order_release);
    frameGatingStaticIntervalUs =
        std::max<uint64_t>(activeFrameIntervalUs, std::max<uint64_t>(1, 1000000ULL / frameGatingStaticFps));
  };
  refresh_frame_intervals();
  // Declared before every lambda that references them. FrameState precedes the pipeline so
  // the worker's publish callback never outlives what it writes into.
  FrameState frame;
  // Asynchronous readback ring: the capture callback only submits a GPU copy; a worker maps
  // finished copies and publishes them. The publish function is assigned below, before the
  // first create_staging call.
  remote60::native_poc::D3dCaptureReadbackPipeline captureReadback;
  remote60::native_poc::D3dCaptureReadbackPipeline::PublishFn capturePublishFn;
  // Zero-copy encode bookkeeping. A surface handed to the MFT stays reserved until the
  // encoder's total output count passes the frame's submission -- only then is its texture
  // provably no longer being read.
  struct Nv12PendingRelease {
    int32_t slot = -1;
    uint64_t generation = 0;
    uint64_t requiredOutputs = 0;
  };
  std::deque<Nv12PendingRelease> nv12PendingReleases;
  bool surfaceEncodeHealthy = true;
  uint64_t encoderOutputSamplesTotal = 0;
  // Encoder OUTPUT-liveness heartbeat. The main-loop liveness watchdog only tracks loop iteration
  // progress (mainLoopProgressUs), which keeps advancing even when the async hardware MFT accepts
  // input every call but emits no output access unit -- an output-starvation wedge that freezes the
  // video while the loop still spins and the watchdog stays green. These track real encoder output
  // progress so that stall is observable (and, later, recoverable). Diagnostic-only in this commit.
  uint64_t encoderInputAcceptedTotal = 0;      // encode calls that handed a frame to the MFT
  uint64_t encoderRealInputAccepted = 0;       // ... of which carried a real captured frame
  uint64_t encoderSyntheticInputAccepted = 0;  // ... trailing-edge/bootstrap synthetic kicks
  uint64_t encoderOutputAuTotal = 0;           // cumulative output access units produced
  uint64_t lastEncoderOutputUs = 0;            // qpc of the last produced output (0 = never yet)
  uint64_t encoderNoOutputSinceUs = 0;         // qpc the current no-output streak began (catches
                                               // a from-startup encoder that never emits one AU)
  uint32_t encoderAcceptedNoOutputStreak = 0;  // consecutive accepted-input calls with no output
  uint64_t lastEncoderStarvationLogUs = 0;     // rate-limits the anomaly line to <=1/s
  // Async-event counters accumulated ACROSS the current no-output streak (reset when output
  // resumes) so one anomaly line can tell a host event-driving bug (NeedInput accrues, HaveOutput
  // stays 0) from a genuine vendor/hardware stall, rather than showing only the last call's counts.
  uint64_t starveNeedInputAccum = 0;
  uint64_t starveHaveOutputAccum = 0;
  uint64_t starveNoEventAccum = 0;
  uint64_t starveNotAcceptingAccum = 0;
  uint64_t starveNeedMoreAccum = 0;
  uint64_t starveNeedInputOnlyCalls = 0;
  // Clears the CURRENT starvation episode (not the lifetime totals). Must run whenever the encoder
  // is shut down + reinitialized or the stream (re)activates, otherwise a no-output streak left over
  // from the previous encoder -- or a long stream-inactive gap -- would inflate noOutputAgeUs and
  // fire a false starvation log on the fresh encoder's first inputs.
  auto reset_encoder_starvation_episode = [&]() {
    encoderNoOutputSinceUs = 0;
    encoderAcceptedNoOutputStreak = 0;
    lastEncoderStarvationLogUs = 0;
    starveNeedInputAccum = starveHaveOutputAccum = starveNoEventAccum = 0;
    starveNotAcceptingAccum = starveNeedMoreAccum = starveNeedInputOnlyCalls = 0;
  };
  uint64_t nv12SurfaceEncodeCount = 0;
  uint32_t surfaceEncodeProbeCount = 0;
  uint64_t surfaceEncodeProbeSumUs = 0;
  int32_t poppedNv12Slot = -1;
  uint64_t poppedNv12Generation = 0;

  auto apply_encoder_target = [&](uint32_t targetW, uint32_t targetH, uint32_t targetFps,
                                  uint32_t targetBitrate, uint32_t targetKeyint) -> bool {
    // The keyint A/B env override is enforced HERE, the single choke point every caller passes
    // (runtime tune, capture-UI overview/focus, ABR/M9 refit) -- pinning it in just one caller
    // let another quietly revert the override with its own cached keyint. Ceiling bookkeeping
    // upstream stays based on what the CLIENT actually requested.
    if (keyintOverride != 0) targetKeyint = keyintOverride;
    // Callers pass the nominal box for the current ABR/M9 level. Remember it so a later
    // source-size change can be re-fitted against the same budget instead of ratcheting down.
    nominalEncodeW = targetW;
    nominalEncodeH = targetH;
    fit_size_preserving_aspect(encodeSourceW, encodeSourceH, targetW, targetH, &targetW, &targetH);

    const bool keyintChanged = (targetKeyint != activeKeyint);
    const bool fpsChanged = (targetFps != activeFps);
    const bool resizeChanged = (targetW != activeEncodeW || targetH != activeEncodeH);
    const bool bitrateChanged = (targetBitrate != activeBitrate);

    if (keyintChanged || fpsChanged || resizeChanged) {
      encoder.shutdown();
      // The shutdown flushed the MFT, so every in-flight surface is released.
      for (const auto& pending : nv12PendingReleases) {
        captureReadback.ReleaseNv12Slot(pending.slot, pending.generation);
      }
      nv12PendingReleases.clear();
      surfaceEncodeHealthy = true;
      if (!encoder.initialize(targetW, targetH, targetFps, targetBitrate, targetKeyint)) {
        return false;
      }
      resetHostTimelineAnchors();
      reset_encoder_starvation_episode();
      // shutdown+initialize discarded any pending key input; a stale latch here would delay the
      // fresh encoder's needed IDR by up to the 300ms retry window.
      forceKeySubmittedAtUs = 0;
    } else if (bitrateChanged) {
      if (!encoder.reconfigure_bitrate(targetBitrate)) {
        encoder.shutdown();
        if (!encoder.initialize(targetW, targetH, targetFps, targetBitrate, targetKeyint)) {
          return false;
        }
        resetHostTimelineAnchors();
        reset_encoder_starvation_episode();
        // Same contract as the other reinit sites: shutdown discarded any pending key input.
        forceKeySubmittedAtUs = 0;
      }
    }

    activeEncodeW = targetW;
    activeEncodeH = targetH;
    activeFps = targetFps;
    activeBitrate = targetBitrate;
    activeKeyint = targetKeyint;
    inputDomainW.store(activeEncodeW, std::memory_order_release);
    inputDomainH.store(activeEncodeH, std::memory_order_release);
    // The pacing budget follows the active bitrate. It used to be computed once at startup,
    // so after an ABR downshift frames kept leaving at the launch rate (bursts the network
    // just asked us to stop), and after an upshift sends were throttled below the new rate.
    const uint64_t pacePeakBps = noPacingH264
                                     ? 0ULL
                                     : std::max<uint64_t>(
                                           udpPacePeakFloorBps,
                                           (static_cast<uint64_t>(activeBitrate) *
                                            udpPacePeakPercent) /
                                               100ULL);
    const uint32_t pacePeakBpsClamped =
        static_cast<uint32_t>(std::min<uint64_t>(pacePeakBps, 4000000000ULL));
    if (gUdpPacePeakBitrateBps.load(std::memory_order_relaxed) != pacePeakBpsClamped) {
      gUdpPacePeakBitrateBps.store(pacePeakBpsClamped, std::memory_order_relaxed);
      std::cout << "[native-video-host] pacing update udpPacePeakBps=" << pacePeakBpsClamped
                << " bitrate=" << activeBitrate << "\n";
    }
    captureReadback.SetOutputSize(activeEncodeW, activeEncodeH);
    refresh_frame_intervals();
    return true;
  };

  auto apply_confirmed_capture_geometry = [&](uint32_t newW, uint32_t newH, const char* reason,
                                              bool allowWindowOverride = false) {
    // An interactive window DRAG keeps the 0.4s settle path (per-frame MFT re-init would thrash),
    // so it bails here. A CONFIRMED window selection passes allowWindowOverride=true so the encode
    // target is re-fit to the final window geometry immediately -- otherwise the first IDR goes out
    // at the pre-selection encode size and a second, new-size IDR follows a frame later, forcing the
    // client to reconfigure twice and fire a keyframe-request storm.
    if (captureWindowModeActive && !allowWindowOverride) return;
    if (newW < 2 || newH < 2) return;
    if (newW == encodeSourceW && newH == encodeSourceH) return;  // already fit to this source
    encodeSourceW = newW;
    encodeSourceH = newH;
    pendingRefitW = 0;
    pendingRefitH = 0;
    pendingRefitSinceUs = 0;
    const uint32_t prevEncW = activeEncodeW;
    const uint32_t prevEncH = activeEncodeH;
    // Confirmed change: no aspectClose skip. A smaller same-aspect source must still shrink
    // activeEncode to avoid upscaling. Passing the current nominal box re-fits activeEncode from
    // the new encodeSource aspect and rebuilds the MFT immediately, instead of after the 0.4s settle.
    if (apply_encoder_target(nominalEncodeW, nominalEncodeH, activeFps, activeBitrate, activeKeyint)) {
      forceKeyNext = true;
      resetHostTimelineAnchors();
      std::cout << "[native-video-host] capture-geometry-confirmed reason=" << reason
                << " source=" << newW << "x" << newH
                << " encode=" << prevEncW << "x" << prevEncH
                << "->" << activeEncodeW << "x" << activeEncodeH << "\n";
    }
  };

  auto apply_capture_ui_quality_mode = [&](bool overviewMode, uint64_t nowUs) -> bool {
    if (!useH264) return true;
    // Derived from the live ceiling, not from the m9 level constants: those are frozen at
    // encoder initialization, so a host born at 3 Mbps regressed to its birth bitrate and
    // size every time the client left overview mode -- and set the manual override, which
    // kept ABR from ever repairing it. Same freeze as the ABR profiles, one more door in.
    // (The m9 adaptive levels themselves are still the frozen constants; that ladder is off
    // by default and needs its own pass before it can be trusted with live values.)
    const uint32_t focusBitrate = abrHighBitrate;
    const uint32_t targetBitrate =
        overviewMode
            ? std::min<uint32_t>(focusBitrate,
                                 std::max<uint32_t>(900000u, (focusBitrate * 50u) / 100u))
            : focusBitrate;
    const uint32_t targetFps =
        overviewMode ? std::max<uint32_t>(15u, (userFpsCeiling * 67u) / 100u) : userFpsCeiling;
    const auto sizeChoice = remote60::native_poc::choose_abr_profile_size(
        overviewMode ? 2 : 0, targetBitrate, captureWidth, captureHeight, encodeLadderReduced);
    const uint32_t targetKeyint =
        overviewMode ? std::max<uint32_t>(userKeyintCeiling, 60u) : userKeyintCeiling;
    if (!apply_encoder_target(sizeChoice.width, sizeChoice.height, targetFps, targetBitrate,
                              targetKeyint)) {
      return false;
    }
    encodeLadderReduced = sizeChoice.reduced;
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
  DxgiDesktopCaptureSession dxgiCaptureSession;
  GdiCaptureProcess gdiCaptureProcess;
  std::atomic<bool> captureSessionReady{false};
  std::atomic<bool> dxgiFallbackRequested{false};
  std::atomic<bool> gdiFallbackRequested{false};
  uint64_t captureSessionStartedUs = 0;
  uint64_t captureRestartCount = 0;
  uint64_t lastCaptureRestartUs = 0;
  // Frozen-ring self-heal state (DXGI/WGC). Streak guards against a single slow poll; the last
  // restart timestamp lets a refreeze inside the window escalate to a full process restart.
  uint32_t captureFrozenPollStreak = 0;
  uint64_t captureFrozenWarnedAtUs = 0;
  uint64_t lastFrozenRestartUs = 0;
  // Telemetry for the frozen-ring self-heal, so a real-GPU run can tell whether B-1 is actually the
  // fix (oldest-pending age climbs to the 2s restart threshold) or whether the age keeps clearing at
  // 50-100ms and the starvation lives in the readback path itself (the surface-only bypass, B-2).
  // The age is a per-interval peak because a once-per-second sample of the instantaneous age would
  // miss a spike that the restart logic (which polls every loop) does see.
  uint64_t oldestGpuPendingPeakUs = 0;
  // Peak GpuPending count over the interval, next to the age peak: a frozen ring pins this at the
  // ring size while the age climbs, whereas a merely busy ring churns it low. The instantaneous
  // age/count are also emitted, so a print catches both the interval's worst and the current state.
  uint32_t gpuPendingCountPeak = 0;
  // Restarts driven specifically by the frozen ring, kept apart from captureDeadRestartCount, which
  // also counts the GDI callback-stall watchdog -- mixing them would blur which path actually fired.
  // (A refreeze inside the escalation window is a distinct outcome, but it exits the process, so it
  // shows up in the refroze log line rather than a counter that no later stats print would carry.)
  uint64_t frozenRingRestartCount = 0;
  // Readback-throughput soft-watchdog state (see kReadbackDrainWarmupUs). The trigger is over
  // per-1s-window deltas, so the cumulative sources (cadence accepts, staging-busy drops,
  // superseded drops) are diffed against the previous tick's snapshot every tick -- not every
  // print. The oldest-pending peak is accumulated at loop frequency next to the frozen-ring peak
  // (a once-per-second sample would miss a spike the loop-rate poll sees) and reset each tick.
  // The consecutive-second counter debounces a single slow window; the last drain-restart
  // timestamp lets a recurrence inside the frozen-ring escalation window escalate to a process
  // restart. streamActiveSinceUs anchors a warmup after a client (re)attaches.
  uint64_t readbackDrainPrevAccepted = 0;
  uint64_t readbackDrainPrevBusyDrops = 0;
  uint64_t readbackDrainPrevSuperseded = 0;
  uint32_t readbackDrainConsecutiveSec = 0;
  uint64_t readbackDrainOldestPendingPeakUs = 0;  // per-1s window, reset every stats tick
  uint64_t lastReadbackDrainRestartUs = 0;
  uint64_t readbackDrainRestartCount = 0;
  uint64_t streamActiveSinceUs = 0;
  bool readbackDrainPrevStreamActive = false;
  bool dxgiCaptureStarted = false;
  bool gdiCaptureStarted = false;
  std::mutex captureFallbackReasonMu;
  std::string dxgiFallbackReason;
  std::string gdiFallbackReason;
  auto set_dxgi_fallback_reason = [&](const std::string& reason) {
    std::lock_guard<std::mutex> lock(captureFallbackReasonMu);
    dxgiFallbackReason = reason;
  };
  auto set_gdi_fallback_reason = [&](const std::string& reason) {
    std::lock_guard<std::mutex> lock(captureFallbackReasonMu);
    gdiFallbackReason = reason;
  };
  auto copy_dxgi_fallback_reason = [&]() {
    std::lock_guard<std::mutex> lock(captureFallbackReasonMu);
    return dxgiFallbackReason;
  };
  auto copy_gdi_fallback_reason = [&]() {
    std::lock_guard<std::mutex> lock(captureFallbackReasonMu);
    return gdiFallbackReason;
  };

  std::mutex captureResourceMu;
  std::atomic<uint32_t> captureSizeChangePending{0};
  // Capture attachment (session) cookie. Bumped by detach_capture_session() on the main thread
  // before any pool recreate; a capture callback or readback completion that began under the
  // previous attachment sees the change and drops its frame instead of stamping it with the
  // post-recreate target/generation. Hardens the recreate transition race.
  std::atomic<uint64_t> captureAttachmentCookie{1};
  // WGC ContentSize gate. A WGC frame-pool surface is a FIXED buffer size (captureWidth x
  // captureHeight, chosen at pool creation); frame.ContentSize() is the actual content region and
  // shrinks/grows with the window. The callback records a mismatching content size here and drops
  // the frame; the main thread settles then recreates the pool at the new size (the callback thread
  // must never recreate capture resources itself).
  std::atomic<uint32_t> wgcContentSizeMismatchPending{0};
  std::atomic<uint32_t> wgcPendingContentW{0};
  std::atomic<uint32_t> wgcPendingContentH{0};
  std::atomic<uint64_t> wgcContentSizeMismatchDrops{0};
  // Main-thread-only settle tracking + recreate telemetry for the WGC ContentSize gate.
  uint32_t wgcSettleTrackW = 0;
  uint32_t wgcSettleTrackH = 0;
  uint64_t wgcSettleSinceUs = 0;
  uint64_t wgcPoolRecreates = 0;
  constexpr uint64_t kWgcContentSettleUs = 100000;  // 0.1s of a stable content size before recreate
  const uint32_t captureStagingSlotCount =
      std::max<uint32_t>(3u, static_cast<uint32_t>(captureFramePoolBuffers + 1));
  auto create_staging = [&](uint32_t srcW, uint32_t srcH) -> bool {
    captureReadback.Shutdown();
    if (!captureReadback.Initialize(d3d.Get(), ctx.Get(), &d3dContextMu, srcW, srcH,
                                    captureStagingSlotCount, capturePublishFn)) {
      std::cerr << "[native-video-host] recreating D3D device after readback init failure size="
                << srcW << "x" << srcH << "\n";
      d3d.Reset();
      ctx.Reset();
      const HRESULT recreateHr = create_d3d11_device_for_primary_monitor(&d3d, &ctx, &fl);
      if (FAILED(recreateHr) || !d3d || !ctx) {
        std::cerr << "[native-video-host] D3D11 device recreate failed hr="
                  << hr_hex(recreateHr) << "\n";
        return false;
      }
      if (useH264) {
        (void)encoder.set_d3d11_device(d3d.Get());
      }
      gpuScaler = GpuBgraScaler();
      gpuScalerHealthy = false;
      if (gpuScalerRequested) {
        gpuScalerHealthy = gpuScaler.initialize(d3d.Get(), ctx.Get(), &d3dContextMu);
        std::cout << "[native-video-host] gpu scaler reinit after device recreate ready="
                  << (gpuScalerHealthy ? 1 : 0) << "\n";
      }
      if (!captureReadback.Initialize(d3d.Get(), ctx.Get(), &d3dContextMu, srcW, srcH,
                                      captureStagingSlotCount, capturePublishFn)) {
        std::cerr << "[native-video-host] readback init retry failed size="
                  << srcW << "x" << srcH << "\n";
        return false;
      }
    }
    if (useH264) {
      captureReadback.SetOutputSize(activeEncodeW, activeEncodeH);
      // Opt-in until a healthy-driver A/B lands: the path is functionally verified (color,
      // e2e), but on the bring-up machine the driver threw internal errors mid-run and an
      // H3-triggered cause could not be ruled out. The product path stays the H1/H2 one.
      captureReadback.SetNv12Enabled(
          encoder.using_hardware() && env_truthy("REMOTE60_NATIVE_NV12_SURFACE"));
    }
    return true;
  };

  const auto update_u64_max = [](std::atomic<uint64_t>& target, const uint64_t value) {
    auto old = target.load(std::memory_order_relaxed);
    while (value > old && !target.compare_exchange_weak(old, value, std::memory_order_release, std::memory_order_relaxed)) {
    }
  };
  std::atomic<uint64_t> callbackFrames{0};
  // Hardware-cursor state from the DXGI backend (pointer-only frames are dropped by the content
  // pipeline, so without this side channel the remote cursor freezes on a still screen). Written
  // by the capture thread, drained by the main loop's ~30Hz latest-wins UDP cursor sender.
  std::atomic<int32_t> dxgiPointerX{0};
  std::atomic<int32_t> dxgiPointerY{0};
  std::atomic<bool> dxgiPointerVisible{false};
  std::atomic<uint64_t> dxgiPointerGeneration{0};  // stream generation the sample belongs to
  std::atomic<uint64_t> dxgiPointerUpdateUs{0};
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
  // Timestamp (qpc) of the last frame actually published to the encoder ring, set in
  // capturePublishFn on a valid payload -- distinct from lastCallbackUs, which is the capture time.
  // The stats line reports this as lastPublishAgeUs (diagnostic only). Deliberately not reset on a
  // restart: the age then honestly shows the publish gap and snaps back on the first new publish,
  // which is exactly the recovery signal we want to see after a frozen-ring restart.
  std::atomic<uint64_t> lastPublishUs{0};
  std::atomic<uint64_t> lastCaptureUsForInterval{0};
  std::atomic<uint64_t> firstCallbackLoggedGeneration{0};
  // Static-screen bootstrap cache: a memory-only copy of the last raw frame actually published,
  // plus the identity of the capture that produced it. On a static desktop DXGI AcquireNextFrame
  // just times out after a (re)start, so the forced keyframe has nothing to encode and a fresh
  // viewer sits black for seconds. Keeping the last-good frame lets the main loop re-encode it once
  // as an IDR so the picture paints immediately. Written ONLY from a real capture publish
  // (capturePublishFn) and deliberately NOT touched by flush_capture_pipeline_state, so it survives
  // a flush and a reattach can still use it.
  struct BootstrapFrameCache {
    std::shared_ptr<std::vector<uint8_t>> payload;  // BGRA pixels, post-crop
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint64_t captureQpcUs = 0;      // real capture time, for replay-age telemetry only
    uint64_t streamGeneration = 0;  // capture generation that produced these pixels
    bool windowMode = false;
    uint64_t selectedWindowId = 0;
    uint64_t targetHwnd = 0;
    uint32_t targetPid = 0;
    uint32_t srcCaptureWidth = 0;   // pre-crop capture source dims (meta.width/height)
    uint32_t srcCaptureHeight = 0;
    uint32_t consoleSessionId = 0;  // WTS active console session at capture time
  };
  std::mutex bootstrapCacheMu;
  BootstrapFrameCache bootstrapCache;
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

  // Worker-thread side: a finished readback becomes the latest frame. Timing fields keep
  // their FrameState names so downstream logs stay parseable; their meaning under the async
  // pipeline is documented at each assignment.
  capturePublishFn = [&](std::shared_ptr<std::vector<uint8_t>> payload, uint32_t frameW,
                         uint32_t frameH, uint32_t stride,
                         const remote60::native_poc::CaptureFrameMeta& meta,
                         uint64_t gpuPendingUs, uint64_t workerMapUs, uint64_t workerMemcpyUs) {
    if (!payload || payload->empty() || frameW < 2 || frameH < 2) return;
    // Drop a readback completion whose Submit happened under a previous capture attachment: a pool
    // recreate bumped the cookie in between, so these pixels belong to the old target/geometry. The
    // stream-generation check downstream does not catch a same-generation size-change recreate (the
    // WGC ContentSize path and captureSizeChangePending keep the generation), so the cookie is what
    // makes that case safe. Release the NV12 slot first or the zero-copy ring leaks.
    if (meta.attachmentCookie != 0 &&
        meta.attachmentCookie != captureAttachmentCookie.load(std::memory_order_acquire)) {
      if (meta.nv12Slot >= 0) {
        captureReadback.ReleaseNv12Slot(meta.nv12Slot, meta.nv12Generation);
      }
      return;
    }
    const uint64_t queuePushUs = qpc_now_us();
    lastPublishUs.store(queuePushUs, std::memory_order_release);
    const uint64_t prevCallbackUs = lastCallbackUs.load(std::memory_order_acquire);
    const uint64_t prevCaptureUs = lastCaptureUsForInterval.load(std::memory_order_acquire);
    uint64_t callbackIntervalUs = 0;
    uint64_t captureIntervalUs = 0;
    if (prevCallbackUs > 0 && meta.callbackUs >= prevCallbackUs) {
      callbackIntervalUs = meta.callbackUs - prevCallbackUs;
    }
    if (prevCaptureUs > 0 && meta.captureUs >= prevCaptureUs) {
      captureIntervalUs = meta.captureUs - prevCaptureUs;
    }
    lastCallbackUs.store(meta.callbackUs, std::memory_order_release);
    lastCaptureUsForInterval.store(meta.captureUs, std::memory_order_release);
    // Update the static-screen bootstrap cache from this real publish -- the ONLY writer. Copy the
    // payload shared_ptr (do NOT move: `frame` still takes ownership below). The buffer pool
    // recycles a payload only once its LAST holder releases, so holding this copy keeps the pixels
    // alive and immutable until the next publish replaces it. meta.width/height are the pre-crop
    // capture source dims; frameW/frameH are the post-crop payload dims we must encode.
    {
      std::lock_guard<std::mutex> lk(bootstrapCacheMu);
      bootstrapCache.payload = payload;
      bootstrapCache.width = frameW;
      bootstrapCache.height = frameH;
      bootstrapCache.stride = stride;
      bootstrapCache.captureQpcUs = meta.captureUs;
      bootstrapCache.streamGeneration = meta.streamGeneration;
      bootstrapCache.windowMode = captureWindowModeActive.load(std::memory_order_acquire);
      bootstrapCache.selectedWindowId = selectedWindowIdState.load(std::memory_order_acquire);
      bootstrapCache.targetHwnd = hostCaptureTargetHwnd.load(std::memory_order_acquire);
      bootstrapCache.targetPid = hostCaptureTargetPid.load(std::memory_order_acquire);
      bootstrapCache.srcCaptureWidth = meta.width;
      bootstrapCache.srcCaptureHeight = meta.height;
      bootstrapCache.consoleSessionId = WTSGetActiveConsoleSessionId();
    }
    uint64_t currentVersion = 0;
    {
      std::lock_guard<std::mutex> lk(frame.mu);
      if (frame.nv12Slot >= 0) {
        // The consumer never claimed the previous frame's conversion (latest-wins overwrite);
        // give the slot back or the ring drains to nothing.
        captureReadback.ReleaseNv12Slot(frame.nv12Slot, frame.nv12Generation);
      }
      frame.nv12Slot = meta.nv12Slot;
      frame.nv12Generation = meta.nv12Generation;
      frame.nv12W = meta.nv12W;
      frame.nv12H = meta.nv12H;
      frame.payload = std::move(payload);
      frame.width = frameW;
      frame.height = frameH;
      frame.stride = stride;
      frame.streamGeneration = meta.streamGeneration;
      frame.captureUs = meta.captureUs;
      frame.callbackUs = meta.callbackUs;
      frame.captureAgeAtCallbackUs = meta.captureAgeAtCallbackUs;
      frame.captureClockSkewUs = meta.captureClockSkewUs;
      frame.queuePushUs = queuePushUs;
      frame.callbackIntervalUs = callbackIntervalUs;
      frame.captureIntervalUs = captureIntervalUs;
      frame.captureD3DWaitUs = meta.d3dWaitUs;       // callback wait on d3dContextMu
      frame.captureCopyMapUs = meta.submitCopyUs;    // callback CopyResource + query End
      frame.captureMemcpyUs = workerMemcpyUs;        // worker memcpy incl. crop
      frame.captureUnmapWaitUs = gpuPendingUs;       // submit -> GPU copy finished
      frame.captureUnmapUs = workerMapUs;            // worker Map of the finished copy
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
    if (meta.streamGeneration != 0 && loggedGeneration != meta.streamGeneration &&
        firstCallbackLoggedGeneration.compare_exchange_strong(
            loggedGeneration, meta.streamGeneration,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      std::cout << "[native-video-host] capture-switch first-callback"
                << describe_active_capture_target()
                << " callbackUs=" << meta.callbackUs
                << " captureUs=" << meta.captureUs
                << "\n";
    }
    frame.cv.notify_one();
  };

  // Capture-callback side: size check, a cheap crop-rect query, then a single GPU copy
  // submit. No Map, no memcpy, no allocation -- the DXGI duplication frame is released the
  // moment this returns instead of being held across a synchronous readback.
  auto publish_captured_texture = [&](ID3D11Texture2D* src,
                                      uint64_t callbackUs,
                                      uint64_t sourceCaptureUs,
                                      uint64_t captureAgeAtCallbackUs,
                                      uint64_t captureClockSkewUs,
                                      bool hasNewContent) {
    if (!src) return;
    // WGC/DXGI commonly callback at the monitor refresh rate even when the encoder target is
    // 30fps. Submitting all 60 copies made the staging ring and GPU fight over obsolete
    // frames; query completion then oscillated between 16 and 50ms. Limit before the copy,
    // using a phase-preserving deadline so the accepted frames stay evenly spaced.
    {
      std::lock_guard<std::mutex> lk(captureCadenceMu);
      captureCadenceGate.SetEnabled(captureSubmitLimitEnabled);
      captureCadenceGate.SetEarlyTolerancePercent(captureSubmitEarlyTolerancePercent);
      captureCadenceGate.SetRequestedIntervalUs(
          std::max<uint64_t>(1, captureSubmitMinIntervalUs.load(std::memory_order_acquire)));
      if (!captureCadenceGate.ShouldAccept(callbackUs, hasNewContent)) return;
    }
    uint32_t frameW = 0;
    uint32_t frameH = 0;
    {
      std::lock_guard<std::mutex> lk(captureResourceMu);
      frameW = captureWidth;
      frameH = captureHeight;
    }
    if (frameW < 2 || frameH < 2) return;
    D3D11_TEXTURE2D_DESC srcDesc{};
    src->GetDesc(&srcDesc);
    if (srcDesc.Width != frameW || srcDesc.Height != frameH) {
      captureSizeChangePending.store(1, std::memory_order_release);
      return;
    }
    remote60::native_poc::CaptureFrameMeta meta{};
    meta.width = frameW;
    meta.height = frameH;
    meta.callbackUs = callbackUs;
    meta.captureUs = sourceCaptureUs;
    meta.captureAgeAtCallbackUs = captureAgeAtCallbackUs;
    meta.captureClockSkewUs = captureClockSkewUs;
    meta.streamGeneration = captureStreamGenerationState.load(std::memory_order_acquire);
    meta.attachmentCookie = captureAttachmentCookie.load(std::memory_order_acquire);
    if (captureWindowModeActive && captureWindowClientOnlyActive) {
      const HWND cropHwnd = reinterpret_cast<HWND>(
          static_cast<uintptr_t>(hostCaptureTargetHwnd.load(std::memory_order_acquire)));
      uint32_t cropX = 0;
      uint32_t cropY = 0;
      uint32_t cropW = 0;
      uint32_t cropH = 0;
      if (cropHwnd && compute_window_client_crop(cropHwnd, frameW, frameH, &cropX, &cropY, &cropW, &cropH)) {
        meta.cropActive = true;
        meta.cropX = cropX;
        meta.cropY = cropY;
        meta.cropW = cropW;
        meta.cropH = cropH;
      }
    }
    (void)captureReadback.Submit(src, meta);
  };

  if (!create_staging(captureWidth, captureHeight)) {
    std::cerr << "[native-video-host] capture readback pipeline create failed\n";
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    if (mfStarted) MFShutdown();
    return 10;
  }

  auto attach_frame_arrived = [&]() {
    token = pool.FrameArrived([&](Direct3D11CaptureFramePool const& sender,
                                  winrt::Windows::Foundation::IInspectable const&) {
      if (stop.load()) return;
      // Snapshot the capture attachment cookie on entry, before reading any capture geometry or
      // generation. If a main-thread recreate bumps it while this callback runs, the pre-publish
      // recheck below drops the frame instead of stamping it with the new target/generation.
      const uint64_t myAttachmentCookie = captureAttachmentCookie.load(std::memory_order_acquire);
      try {
        auto latest = sender.TryGetNextFrame();
        if (!latest) return;
        // Drain queued frames and keep only the newest one to avoid stale-frame backlog.
        while (auto newer = sender.TryGetNextFrame()) {
          latest = newer;
        }
        if (!streamControlActive.load(std::memory_order_acquire)) return;

        // A WGC frame-pool surface is a FIXED buffer size (captureWidth x captureHeight, chosen when
        // the pool was created); frame.ContentSize() is the actual content region and shrinks/grows
        // with the window. Copying the whole surface would fold the stale size-delta band (undefined
        // pixels beyond ContentSize) into the encoded frame -- that reads as "an old frame mixed into
        // the current one". Microsoft's own sample gates on ContentSize and recreates the pool when
        // it changes. Here the callback NEVER recreates capture resources: it records the pending
        // content size + a flag and drops the frame, and the main thread settles then recreates.
        const auto contentSize = latest.ContentSize();
        const uint32_t contentW = contentSize.Width > 0 ? static_cast<uint32_t>(contentSize.Width) : 0;
        const uint32_t contentH = contentSize.Height > 0 ? static_cast<uint32_t>(contentSize.Height) : 0;
        uint32_t poolW = 0;
        uint32_t poolH = 0;
        {
          std::lock_guard<std::mutex> lk(captureResourceMu);
          poolW = captureWidth;
          poolH = captureHeight;
        }
        if (contentW >= 2 && contentH >= 2 && (contentW != poolW || contentH != poolH)) {
          wgcContentSizeMismatchDrops.fetch_add(1, std::memory_order_relaxed);
          wgcPendingContentW.store(contentW, std::memory_order_release);
          wgcPendingContentH.store(contentH, std::memory_order_release);
          wgcContentSizeMismatchPending.store(1, std::memory_order_release);
          return;  // drop; the main thread will settle then recreate the pool at the new size
        }

        auto src = SurfaceToTexture(latest.Surface());
        if (!src) return;
        const uint64_t callbackUs = qpc_now_us();
        uint64_t sourceCaptureUs = callbackUs;
        uint64_t captureAgeAtCallbackUs = 0;
        uint64_t captureClockSkewUs = 0;
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
        // A recreate may have started while this callback was running. If the attachment cookie
        // moved, this frame belongs to the previous attachment -- drop it rather than publish it
        // under the new target/generation.
        if (captureAttachmentCookie.load(std::memory_order_acquire) != myAttachmentCookie) return;
        publish_captured_texture(src.Get(), callbackUs, sourceCaptureUs, captureAgeAtCallbackUs,
                                 captureClockSkewUs, true);
      } catch (...) {
      }
    });
  };

  auto detach_capture_session = [&]() {
    // Invalidate any capture callback or readback completion that began under the current
    // attachment before we tear the pool down: bumping the cookie makes that in-flight work drop
    // instead of being published under the post-recreate target/geometry/generation.
    captureAttachmentCookie.fetch_add(1, std::memory_order_acq_rel);
    captureSessionReady.store(false, std::memory_order_release);
    if (dxgiCaptureStarted) {
      dxgiCaptureSession.Stop();
      dxgiCaptureStarted = false;
    }
    if (gdiCaptureStarted) {
      gdiCaptureProcess.Stop();
      gdiCaptureStarted = false;
    }
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

  auto restart_capture_session_impl = [&]() -> bool {
    detach_capture_session();
    try {
      if (!captureWindowModeActive && activeDesktopBackend == DesktopCaptureBackend::Dxgi) {
        monitorInfo = primary_monitor_info();
        if (!monitorInfo.has_value()) {
          std::cerr << "[native-video-host] primary monitor query failed on restart\n";
          return false;
        }
        if (monitorInfo->width < monitorInfo->height) {
          activeDesktopBackend = DesktopCaptureBackend::Wgc;
          set_dxgi_fallback_reason("rotation_unsupported");
          std::cout << "[native-video-host] rotation_unsupported fallback_reason=rotation_unsupported\n";
        }
      }
      if (captureWindowModeActive) {
        const uintptr_t hwndRaw = static_cast<uintptr_t>(hostCaptureTargetHwnd.load(std::memory_order_relaxed));
        HWND targetHwnd = reinterpret_cast<HWND>(hwndRaw);
        if (targetHwnd && IsWindow(targetHwnd)) {
          auto refreshedItem = CreateItemForPrimaryMonitor(targetHwnd, "CreateForWindow(restart-refresh)");
          if (refreshedItem) {
            item = refreshedItem;
          }
        }
      } else if (activeDesktopBackend == DesktopCaptureBackend::Wgc) {
        auto refreshedItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(restart-refresh)");
        if (refreshedItem) {
          item = refreshedItem;
        }
      } else {
        item = nullptr;
      }
      winrt::Windows::Graphics::SizeInt32 newSize{};
      uint32_t newW = 0;
      uint32_t newH = 0;
      if (item) {
        newSize = item.Size();
        newW = static_cast<uint32_t>(newSize.Width);
        newH = static_cast<uint32_t>(newSize.Height);
      } else if (monitorInfo.has_value()) {
        newW = monitorInfo->width;
        newH = monitorInfo->height;
        newSize.Width = static_cast<int32_t>(newW);
        newSize.Height = static_cast<int32_t>(newH);
      }
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
      if (!captureWindowModeActive && activeDesktopBackend == DesktopCaptureBackend::Dxgi) {
        DxgiDesktopCaptureConfig config;
        config.d3dDevice = d3d.Get();
        config.monitor = monitorInfo->monitor;
        config.landscapeOnly = true;
        // Capture-thread side of the cursor forwarder: just stores the latest sample; the main
        // loop's pump_cursor_forward() throttles and sends. No lock, no send from this thread.
        config.onPointer = [&](int32_t px, int32_t py, bool visible) {
          dxgiPointerX.store(px, std::memory_order_relaxed);
          dxgiPointerY.store(py, std::memory_order_relaxed);
          dxgiPointerVisible.store(visible, std::memory_order_relaxed);
          dxgiPointerGeneration.store(
              captureStreamGenerationState.load(std::memory_order_acquire),
              std::memory_order_relaxed);
          dxgiPointerUpdateUs.store(qpc_now_us(), std::memory_order_release);
        };
        std::string dxgiDetail;
        const bool started = dxgiCaptureSession.Start(
            config,
            [&](ID3D11Texture2D* texture, uint32_t width, uint32_t height,
                uint32_t accumulatedFrames) {
              if (stop.load()) return;
              if (!streamControlActive.load(std::memory_order_acquire)) return;
              const uint64_t callbackUs = qpc_now_us();
              publish_captured_texture(texture, callbackUs, callbackUs, 0, 0,
                                       accumulatedFrames > 0);
            },
            [&](const std::string&, const std::string& message) {
              std::cout << "[native-video-host] " << message << "\n";
            },
            [&](const std::string& reason) {
              set_dxgi_fallback_reason(reason);
              dxgiFallbackRequested.store(true, std::memory_order_release);
            },
            &dxgiDetail);
        if (!started) {
          std::cout << "[native-video-host] fallback_reason=" << dxgiDetail << "\n";
          activeDesktopBackend = DesktopCaptureBackend::Wgc;
          auto refreshedItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(dxgi-fallback)");
          if (!refreshedItem) return false;
          item = refreshedItem;
          newSize = item.Size();
          newW = static_cast<uint32_t>(newSize.Width);
          newH = static_cast<uint32_t>(newSize.Height);
          if (newW < 2 || newH < 2) return false;
          if (!create_staging(newW, newH)) return false;
          {
            std::lock_guard<std::mutex> lk(captureResourceMu);
            captureSize = newSize;
            captureWidth = newW;
            captureHeight = newH;
          }
        } else {
          dxgiCaptureStarted = true;
          captureSessionStartedUs = qpc_now_us();
          captureSessionReady.store(true, std::memory_order_release);
          captureSizeChangePending.store(0, std::memory_order_release);
          std::cout << "[native-video-host] desktop_backend=dxgi capture-started=1\n";
          return true;
        }
      }
      if (!captureWindowModeActive && activeDesktopBackend == DesktopCaptureBackend::Gdi) {
        GdiCaptureProcessConfig config;
        config.width = newW;
        config.height = newH;
        const uint32_t gdiDefaultFps =
            activeFps >= 50 ? std::min<uint32_t>(120u, activeFps + 4u) : activeFps;
        config.fps = env_u32_clamped("REMOTE60_GDI_CAPTURE_FPS",
                                     gdiDefaultFps, 1, 120);
        config.captureLayeredWindows = env_truthy("REMOTE60_GDI_CAPTURE_LAYERED");
        std::string gdiDetail;
        const bool started = gdiCaptureProcess.Start(
            config,
            [&](std::shared_ptr<std::vector<uint8_t>> pixels, uint32_t width,
                uint32_t height, uint32_t stride, uint64_t captureQpcUs,
                uint64_t captureCopyUs, uint64_t parentCopyUs) {
              if (stop.load() || !streamControlActive.load(std::memory_order_acquire)) return;
              const uint64_t callbackUs = qpc_now_us();
              remote60::native_poc::CaptureFrameMeta meta{};
              meta.width = width;
              meta.height = height;
              meta.callbackUs = callbackUs;
              meta.captureUs = captureQpcUs;
              meta.captureAgeAtCallbackUs =
                  callbackUs >= captureQpcUs ? callbackUs - captureQpcUs : 0;
              meta.submitCopyUs = captureCopyUs;
              meta.streamGeneration =
                  captureStreamGenerationState.load(std::memory_order_acquire);
              capturePublishFn(std::move(pixels), width, height, stride, meta,
                               0, 0, parentCopyUs);
            },
            [&](const std::string&, const std::string& message) {
              std::cout << "[native-video-host] " << message << "\n";
            },
            [&](const std::string& reason) {
              set_gdi_fallback_reason(reason);
              gdiFallbackRequested.store(true, std::memory_order_release);
            },
            &gdiDetail);
        if (!started) {
          std::cout << "[native-video-host] fallback_reason=" << gdiDetail << "\n";
          activeDesktopBackend = DesktopCaptureBackend::Wgc;
          auto refreshedItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(gdi-fallback)");
          if (!refreshedItem) return false;
          item = refreshedItem;
          newSize = item.Size();
          newW = static_cast<uint32_t>(newSize.Width);
          newH = static_cast<uint32_t>(newSize.Height);
          if (newW < 2 || newH < 2) return false;
          if (!create_staging(newW, newH)) return false;
          {
            std::lock_guard<std::mutex> lk(captureResourceMu);
            captureSize = newSize;
            captureWidth = newW;
            captureHeight = newH;
          }
        } else {
          gdiCaptureStarted = true;
          captureSessionStartedUs = qpc_now_us();
          captureSessionReady.store(true, std::memory_order_release);
          captureSizeChangePending.store(0, std::memory_order_release);
          std::cout << "[native-video-host] desktop_backend=gdi capture-started=1 processIsolated=1\n";
          return true;
        }
      }
      pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
          d3dDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
          captureFramePoolBuffers, captureSize);
      session = pool.CreateCaptureSession(item);
      // Windows draws a yellow "being captured" border on the session by default; it lands
      // inside the encoded frame and reads as a rendering artifact on the viewer.
      try {
        session.IsBorderRequired(false);
      } catch (...) {
        std::cout << "[native-video-host] wgc_border_hide=unsupported\n";
      }
      // A remote-control viewer needs to see the pointer to aim clicks, so keep the cursor
      // composited unless it is explicitly turned off.
      try {
        session.IsCursorCaptureEnabled(!env_truthy("REMOTE60_NATIVE_HIDE_CURSOR"));
      } catch (...) {
        std::cout << "[native-video-host] wgc_cursor_toggle=unsupported\n";
      }
      attach_frame_arrived();
      session.StartCapture();
      captureSessionStartedUs = qpc_now_us();
      captureSessionReady.store(true, std::memory_order_release);
      captureSizeChangePending.store(0, std::memory_order_release);
      std::cout << "[native-video-host] desktop_backend="
                << (captureWindowModeActive ? "wgc_window" : desktop_capture_backend_name(activeDesktopBackend))
                << " capture-started=1\n";
      return true;
    } catch (...) {
      detach_capture_session();
      return false;
    }
  };

  // Liveness state for the main-loop watchdog (declared before restart_capture_session so it can
  // flag its own slow phase). mainLoopProgressUs is bumped each loop iteration; the watchdog reads
  // it plus the current phase and never touches a lock or the GPU.
  std::atomic<uint32_t> mainLoopPhase{static_cast<uint32_t>(MainLoopPhase::Startup)};
  std::atomic<uint64_t> mainLoopProgressUs{qpc_now_us()};
  std::atomic<uint64_t> mainLoopLastSeq{0};
  auto enter_main_phase = [&](MainLoopPhase p) {
    mainLoopPhase.store(static_cast<uint32_t>(p), std::memory_order_release);
  };
  auto mark_main_progress = [&](MainLoopPhase p) {
    mainLoopProgressUs.store(qpc_now_us(), std::memory_order_release);
    mainLoopPhase.store(static_cast<uint32_t>(p), std::memory_order_release);
  };

  auto restart_capture_session = [&]() -> bool {
    enter_main_phase(MainLoopPhase::CaptureRestart);
    // A restarted session invalidates the held pointer sample even when the stream generation
    // survives (some size-changes keep it): a stale position against the new capture geometry
    // would misplace the remote cursor until the next real mouse update.
    dxgiPointerUpdateUs.store(0, std::memory_order_release);
    if (!restart_capture_session_impl()) return false;
    uint32_t finalW = 0, finalH = 0;
    {
      std::lock_guard<std::mutex> lk(captureResourceMu);
      finalW = captureWidth;
      finalH = captureHeight;
    }
    apply_confirmed_capture_geometry(finalW, finalH, "capture-restart");
    return true;
  };

  if (!restart_capture_session()) {
    std::cerr << "[native-video-host] capture session start failed\n";
    captureReadback.Shutdown();
    closesocket(clientSock);
    if (listenSock != INVALID_SOCKET) closesocket(listenSock);
    if (mfStarted) MFShutdown();
    return 10;
  }
  powerKeepalive.SetStreaming(streamControlActive.load(std::memory_order_acquire), true);

  const uint64_t startUs = qpc_now_us();
  uint64_t nextTickUs = startUs;
  // For encoded path, latency is prioritized over strict send pacing.
  // Raw path keeps legacy pacing to avoid excessive CPU/bandwidth burst.
  // Encoded capture callbacks are already phase-limited to activeFps before GPU readback.
  // A second independent main-loop clock periodically woke just before the callback, waited
  // only a quarter-frame, then slept to its next tick; the meanwhile-arriving frame was
  // overwritten by the following callback. Consume encoded frames directly from the CV so
  // every accepted 30 Hz capture reaches the encoder. Raw mode still needs its own clock.
  const bool paceByTick = useRaw;
  const uint64_t captureWindowRebindIntervalUs =
      static_cast<uint64_t>(std::max<uint32_t>(200, args.captureWindowRebindIntervalMs)) * 1000ULL;
  uint64_t nextCaptureWindowCheckUs = startUs + captureWindowRebindIntervalUs;
  uint64_t statAtUs = startUs + 1000000ULL;
  // Every rate in the stats line is computed over a one-second window, so the window is not
  // widened -- only the printing is decimated. Each printed line still describes a true
  // second; there are just fewer of them. At the old every-second cadence a streaming day
  // wrote hundreds of megabytes through the host log; set 1 to watch a session closely.
  const uint32_t statsPrintEverySec =
      env_u32_clamped("REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC", 30, 1, 3600);
  uint64_t statTicks = 0;
  uint64_t sentFrames = 0;
  uint64_t encodedFrames = 0;
  // Encoded frames the sender queue policy discarded (backlog resync or waiting for the
  // forced IDR). These are the frames a viewer experiences as a freeze.
  uint64_t senderHeldFrames = 0;
  // Session media barrier / IDR telemetry (encode-thread side). forceKeyInputCount and
  // nonKeyAuWhileWaiting reset per print interval; firstKeyEnqueuedUs is per media epoch (reset by
  // the rollover transaction). Goal: tell "encoder never produced a key" apart from "key produced
  // but lost in UDP assembly". Diagnostic only -- never fed to ABR.
  uint64_t forceKeyInputCount = 0;    // key inputs handed to the encoder
  uint64_t nonKeyAuWhileWaiting = 0;  // delta AUs seen while the barrier was closed
  uint64_t firstKeyEnqueuedUs = 0;    // wire-time stamp of the first key enqueued this media epoch
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
  uint64_t captureReadbackSamples = 0;
  uint64_t captureD3DWaitSumUs = 0;
  uint64_t captureD3DWaitMaxUs = 0;
  uint64_t captureCopyMapSumUs = 0;
  uint64_t captureCopyMapMaxUs = 0;
  uint64_t captureMemcpySumUs = 0;
  uint64_t captureMemcpyMaxUs = 0;
  uint64_t captureUnmapWaitSumUs = 0;
  uint64_t captureUnmapWaitMaxUs = 0;
  uint64_t captureUnmapSumUs = 0;
  uint64_t captureUnmapMaxUs = 0;
  uint64_t gpuScaleTimedCount = 0;
  uint64_t gpuScaleD3DWaitSumUs = 0;
  uint64_t gpuScaleD3DWaitMaxUs = 0;
  uint64_t gpuScaleCopyMapSumUs = 0;
  uint64_t gpuScaleCopyMapMaxUs = 0;
  uint64_t gpuScaleMemcpySumUs = 0;
  uint64_t gpuScaleMemcpyMaxUs = 0;
  uint64_t gpuScaleUnmapWaitSumUs = 0;
  uint64_t gpuScaleUnmapWaitMaxUs = 0;
  uint64_t gpuScaleUnmapSumUs = 0;
  uint64_t gpuScaleUnmapMaxUs = 0;
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
  uint64_t selectionFirstKeyframePendingGeneration = 0;
  uint64_t selectionFirstKeyframeDropCount = 0;
  bool streamActiveApplied = true;
  // The capture lifecycle used to be "start once, stop at exit". Everything between -- a client
  // disconnecting, another connecting an hour later -- left DXGI duplication (or WGC after a
  // fallback) acquiring frames at full desktop rate for nobody, which is what starved RDP
  // sessions into single-digit frame rates until the process was killed. Capture now detaches
  // after the stream has been inactive for a grace period, and reattaches on the active edge.
  // The grace period exists because the picker also parks the stream: tearing down DXGI for a
  // two-second visit to the target list would make every return visibly slow.
  bool captureIdleDetached = false;
  uint64_t captureIdleDetachAtUs = 0;
  constexpr uint64_t kCaptureIdleDetachDelayUs = 5'000'000;
  constexpr uint64_t kCaptureReattachRetryMinUs = 250'000;
  constexpr uint64_t kCaptureReattachRetryMaxUs = 5'000'000;
  uint64_t captureReattachRetryAtUs = 0;
  uint64_t captureReattachRetryDelayUs = kCaptureReattachRetryMinUs;
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
  // Receives now happen on their own thread so a control message never waits for the next
  // frame; this just adopts a peer change the reader has already handled.
  // ~30Hz latest-wins cursor forwarder (UdpCursorPosPacket). Desktop-DXGI only: WGC composites
  // the cursor into the frames themselves, and a window target has its own coordinate space.
  // Sends on movement/visibility change, plus a 250ms heartbeat while visible so the viewer's
  // stale-hide timeout does not blank a stationary cursor. Unreliable by design; no resend.
  uint64_t cursorSendLastUs = 0;
  int32_t cursorSentX = INT32_MIN;
  int32_t cursorSentY = INT32_MIN;
  bool cursorSentVisible = false;
  auto pump_cursor_forward = [&](uint64_t nowUs) {
    // Field verdict: the remote-cursor marker reads as clutter, not signal -- the user asked for
    // it gone. Default OFF on both ends; the reviewed machinery (generation fence, overlay) stays
    // dormant behind this env for future reconsideration.
    static const bool remoteCursorEnabled = env_truthy("REMOTE60_NATIVE_REMOTE_CURSOR");
    if (!remoteCursorEnabled) return;
    if (transport != VideoTransport::Udp || !udpPeerReady) return;
    if (!streamControlActive.load(std::memory_order_acquire)) return;
    if (captureWindowModeActive.load(std::memory_order_acquire)) return;
    if (activeDesktopBackend != DesktopCaptureBackend::Dxgi) return;
    if (dxgiPointerUpdateUs.load(std::memory_order_acquire) == 0) return;
    if (nowUs < cursorSendLastUs + 33'000) return;  // <=30Hz
    // Generation fence: a sample captured under the previous target/attachment must never be
    // sent as if it belonged to the current one (stale desktop cursor over a fresh window).
    const uint64_t sampleGen = dxgiPointerGeneration.load(std::memory_order_relaxed);
    if (sampleGen != captureStreamGenerationState.load(std::memory_order_acquire)) return;
    const int32_t px = dxgiPointerX.load(std::memory_order_acquire);
    const int32_t py = dxgiPointerY.load(std::memory_order_acquire);
    const bool visible = dxgiPointerVisible.load(std::memory_order_acquire);
    const bool changed = px != cursorSentX || py != cursorSentY || visible != cursorSentVisible;
    if (!changed && (!visible || nowUs < cursorSendLastUs + 250'000)) return;
    remote60::native_poc::UdpCursorPosPacket pkt{};
    if (visible) pkt.flags |= 0x1u;
    pkt.x = px;
    pkt.y = py;
    pkt.streamGeneration = sampleGen;
    {
      std::lock_guard<std::mutex> lk(captureResourceMu);
      pkt.captureW = captureWidth;
      pkt.captureH = captureHeight;
    }
    pkt.hostQpcUs = nowUs;
    (void)sendto(clientSock, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
                 reinterpret_cast<const sockaddr*>(&udpPeer), sizeof(udpPeer));
    cursorSendLastUs = nowUs;
    cursorSentX = px;
    cursorSentY = py;
    cursorSentVisible = visible;
  };

  auto pump_udp_hello = [&]() {
    if (transport != VideoTransport::Udp) return;
    if (!udpPeerChanged.exchange(false, std::memory_order_acq_rel)) return;
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = udpPeerIpNet.load(std::memory_order_acquire);
    peer.sin_port = udpPeerPortNet.load(std::memory_order_acquire);
    udpPeer = peer;
    udpPeerReady = true;
    {
      // Session media barrier: the whole rollover is one transaction under the same lock the
      // sender thread dequeues on. Dropping the queue discards every delta still bound for the old
      // session; senderWaitingForKey holds new deltas until a real IDR; bumping the media epoch
      // fences even an item the sender has already popped for the old peer. Without this, a delta
      // queued before the swap goes out to the *new* peer as a P-frame its decoder can never use.
      std::lock_guard<std::mutex> lk(senderMu);
      senderDropCount.fetch_add(senderQueue.size(), std::memory_order_relaxed);
      senderHeldFrames += senderQueue.size();
      sentFrames -= std::min<uint64_t>(sentFrames, senderQueue.size());
      senderQueue.clear();
      senderWaitingForKey = true;
      senderPeer = peer;
      senderPeerReady = true;
      mediaSessionEpoch.fetch_add(1, std::memory_order_acq_rel);
      senderFirstKeyWireUs.store(0, std::memory_order_relaxed);
      senderLastKeyAuBytes.store(0, std::memory_order_relaxed);
      senderLastKeyAuChunks.store(0, std::memory_order_relaxed);
    }
    forceKeyNext = true;
    firstKeyEnqueuedUs = 0;  // re-anchor the per-epoch IDR telemetry on the new session
    std::cout << "[native-video-host] udp peer updated; media barrier armed epoch="
              << mediaSessionEpoch.load(std::memory_order_acquire) << " forcing keyframe\n";
  };

  auto start_encoded_sender = [&]() {
    if (transport != VideoTransport::Udp || !useH264) return;
    senderThread = std::thread([&]() {
      (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
      uint64_t cadenceScheduledUs = 0;
      uint64_t cadenceGeneration = 0;
      uint64_t cadenceMediaEpoch = 0;
      // Same-epoch barrier-recovery rate tracking. First implementation logs only: a persistent
      // local send failure that keeps re-arming and re-requesting IDRs would otherwise spin. A
      // policy (drop peer / wait re-Hello) is deferred until real-use shows whether it recurs.
      uint64_t recoveryWindowStartUs = 0;
      uint32_t recoveryAttemptsInWindow = 0;
      // Actual-wire telemetry: interval between consecutive wire-starts on THIS (sender) thread, so
      // an uneven picture can be pinned to the wire vs the encode/main AU supply (whose enqueue
      // interval is the separate encode_au_enqueue jitter metric). 0 = no previous send yet.
      uint64_t prevWireStartUs = 0;
      while (true) {
        EncodedSendItem item;
        sockaddr_in peer{};
        bool peerReady = false;
        size_t queueDepthAtDequeue = 0;
        {
          std::unique_lock<std::mutex> lk(senderMu);
          senderCv.wait(lk, [&] { return senderStop.load(std::memory_order_acquire) ||
                                         !senderQueue.empty(); });
          if (senderQueue.empty()) {
            if (senderStop.load(std::memory_order_acquire)) return;
            continue;
          }
          item = std::move(senderQueue.front());
          senderQueue.pop_front();
          queueDepthAtDequeue = senderQueue.size();
          peer = senderPeer;
          peerReady = senderPeerReady;
        }
        // Session media barrier: an item stamped for a previous session -- queued before the
        // rollover, or popped in the instant before the swap -- must never reach the new peer.
        // Drop it here so a stale P-frame cannot land on the new decoder.
        if (item.mediaEpoch != mediaSessionEpoch.load(std::memory_order_acquire)) {
          senderDropCount.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        if (!peerReady) {
          senderTxNoPeer.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        const uint64_t frameIntervalUs =
            std::clamp<uint64_t>(item.frameIntervalUs, 8333ULL, 200000ULL);
        const uint64_t nowUs = qpc_now_us();
        // Optional catch-up smoothing. The schedule is carried in its own variable and
        // advanced as max(now, scheduled + interval): deriving the next deadline from the
        // *actual* send time instead would fold each hold into the following frame's deadline
        // and ratchet the stream progressively further behind live.
        const bool freshCadence = cadenceScheduledUs == 0 ||
                                  cadenceGeneration != item.udpHdr.streamGeneration ||
                                  cadenceMediaEpoch != item.mediaEpoch ||
                                  nowUs > cadenceScheduledUs + frameIntervalUs * 2ULL;
        cadenceGeneration = item.udpHdr.streamGeneration;
        // Re-anchor the pacing clock on a media rollover so a new session's first frame is not
        // held against a deadline inherited from the previous client.
        cadenceMediaEpoch = item.mediaEpoch;
        if (freshCadence) {
          cadenceScheduledUs = nowUs;
        } else if (senderCadenceSmoothing) {
          const uint64_t earliestSendUs = cadenceScheduledUs + frameIntervalUs;
          if (nowUs < earliestSendUs) {
            udp_pace_wait_until(std::min<uint64_t>(earliestSendUs, nowUs + senderMaxCadenceHoldUs));
          }
          cadenceScheduledUs = std::max<uint64_t>(nowUs, earliestSendUs);
        } else {
          cadenceScheduledUs = nowUs;
        }
        const uint64_t sendStartUs = qpc_now_us();
        item.udpHdr.sendQpcUs = sendStartUs;
        SendPathStats pathStats{};
        const UdpSendOutcome outcome =
            send_udp_chunks_timed(clientSock, peer, item.bytes.data(), item.bytes.size(),
                                  item.udpHdr, args.udpMtu, &pathStats, &mediaSessionEpoch,
                                  item.mediaEpoch);
        const uint64_t sendDoneUs = qpc_now_us();
        if (outcome == UdpSendOutcome::Sent) {
          const uint64_t durUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
          senderLastSendStartUs.store(sendStartUs, std::memory_order_relaxed);
          senderTxFrames.fetch_add(1, std::memory_order_relaxed);
          senderTxChunks.fetch_add(pathStats.payloadChunkCount, std::memory_order_relaxed);
          senderTxBytes.fetch_add(item.bytes.size(), std::memory_order_relaxed);
          senderSendDurSumUs.fetch_add(durUs, std::memory_order_relaxed);
          senderSendCount.fetch_add(1, std::memory_order_relaxed);
          uint64_t prevMax = senderSendDurMaxUs.load(std::memory_order_relaxed);
          while (durUs > prevMax &&
                 !senderSendDurMaxUs.compare_exchange_weak(prevMax, durUs,
                                                           std::memory_order_relaxed)) {
          }
          if (item.keyFrame) {
            // Record when the first key AU of this media epoch reached the wire and the size of
            // the last key sent -- distinguishes "key never produced" from "key lost in assembly".
            uint64_t expectedFirst = 0;
            senderFirstKeyWireUs.compare_exchange_strong(expectedFirst, sendStartUs,
                                                         std::memory_order_relaxed);
            senderLastKeyAuBytes.store(item.bytes.size(), std::memory_order_relaxed);
            senderLastKeyAuChunks.store(pathStats.payloadChunkCount, std::memory_order_relaxed);
          }
          // Actual-wire per-frame telemetry. A key frame always logs (end-to-end anchor); a normal
          // frame logs only when its wire-start interval ran >1.5x the target (an actual hitch), so
          // steady 60fps play stays quiet. queueWaitUs = "AU ready" -> "bytes on wire"; wireIntUs =
          // gap since the previous send's wire-start; join to the client by udpHdr.seq, not clocks.
          const uint64_t wireIntUs = prevWireStartUs > 0 && sendStartUs >= prevWireStartUs
                                         ? sendStartUs - prevWireStartUs
                                         : 0;
          const uint64_t queueWaitUs =
              item.enqueueUs > 0 && sendStartUs >= item.enqueueUs ? sendStartUs - item.enqueueUs : 0;
          prevWireStartUs = sendStartUs;
          if (item.keyFrame || (wireIntUs > (frameIntervalUs * 3ULL) / 2ULL)) {
            std::cout << "[native-video-host] wire seq=" << item.udpHdr.seq
                      << " key=" << (item.keyFrame ? 1 : 0) << " bytes=" << item.bytes.size()
                      << " chunks=" << pathStats.payloadChunkCount << " wireIntUs=" << wireIntUs
                      << " targetIntUs=" << frameIntervalUs << " queueWaitUs=" << queueWaitUs
                      << " sendDurUs=" << durUs << " queueDepth=" << queueDepthAtDequeue
                      << " epoch=" << item.mediaEpoch << "\n";
          }
        } else if (outcome == UdpSendOutcome::EpochChanged) {
          // A rollover bumped the media epoch mid-frame; the remaining chunks were aborted so old-
          // epoch data cannot interleave into the new session. The rollover already cleared the
          // queue and re-armed the barrier under senderMu, so this is NOT a transport failure --
          // just account the aborted item and re-anchor pacing for the new epoch's first frame.
          senderDropCount.fetch_add(1, std::memory_order_relaxed);
          cadenceScheduledUs = 0;
          cadenceGeneration = 0;
          cadenceMediaEpoch = 0;
        } else {
          // Real transport error on the current epoch. Any H264 frame -- key OR delta -- that failed
          // to reach the wire breaks the client's reference chain (a delta references a picture the
          // client never fully received), and a barrier that was opened by this frame's key would
          // leave the decoder stuck. Clear the queue and re-arm the barrier, then ask the MAIN loop
          // for a fresh IDR via senderRecoveryPending: senderRequestKey alone is only consumed after
          // a real frame is popped, so on a static desktop the recovery IDR would never be produced.
          senderSendFailed.store(true, std::memory_order_release);
          bool rearmed = false;
          {
            std::lock_guard<std::mutex> lk(senderMu);
            if (mediaSessionEpoch.load(std::memory_order_acquire) == item.mediaEpoch) {
              // senderDropCount is the authoritative drop tally; senderHeldFrames/sentFrames are
              // owned by the encode thread and must not be touched here (that would be a data race).
              senderDropCount.fetch_add(senderQueue.size() + 1, std::memory_order_relaxed);
              senderQueue.clear();
              senderWaitingForKey = true;
              rearmed = true;
            } else {
              // A rollover landed between the send and here; it already re-armed. Just drop.
              senderDropCount.fetch_add(1, std::memory_order_relaxed);
            }
          }
          if (rearmed) {
            senderFirstKeyWireUs.store(0, std::memory_order_relaxed);  // retry epoch's first key reappears
            barrierRearmCount.fetch_add(1, std::memory_order_relaxed);
            senderRequestKey.store(true, std::memory_order_release);
            senderRecoveryPending.store(true, std::memory_order_release);
            // Re-anchor pacing: a partial frame consumed part of the schedule and the epoch is
            // unchanged, so freshCadence would not otherwise trip for the recovery IDR.
            cadenceScheduledUs = 0;
            cadenceGeneration = 0;
            cadenceMediaEpoch = 0;
            const uint64_t failUs = qpc_now_us();
            if (recoveryWindowStartUs == 0 || failUs - recoveryWindowStartUs > 5'000'000ULL) {
              recoveryWindowStartUs = failUs;
              recoveryAttemptsInWindow = 0;
            }
            ++recoveryAttemptsInWindow;
            std::cout << "[native-video-host] send-failed barrier re-armed epoch=" << item.mediaEpoch
                      << " keyFrame=" << (item.keyFrame ? 1 : 0)
                      << " attemptsIn5s=" << recoveryAttemptsInWindow << "\n";
            if (recoveryAttemptsInWindow > 3) {
              std::cerr << "[native-video-host] WARN repeated same-epoch send failures ("
                        << recoveryAttemptsInWindow << " in <=5s) -- link may be down\n";
            }
          }
        }
      }
    });
  };
  start_encoded_sender();
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
      selectionFirstKeyframeDropCount = 0;
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
    nextCaptureSubmitUs.store(0, std::memory_order_release);
    {
      // The measured offer rate describes the old target and the old content; carrying it
      // into a restart would pace the first second against something no longer true.
      std::lock_guard<std::mutex> lk(captureCadenceMu);
      captureCadenceGate.Reset();
    }

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
      frame.captureD3DWaitUs = 0;
      frame.captureCopyMapUs = 0;
      frame.captureMemcpyUs = 0;
      frame.captureUnmapWaitUs = 0;
      frame.captureUnmapUs = 0;
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
      if (requestedDesktopBackend == DesktopCaptureBackend::Wgc ||
          activeDesktopBackend == DesktopCaptureBackend::Wgc) {
        nextItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(window-select-desktop)");
        if (!nextItem) {
          if (outReason) *outReason = "desktop_capture_item_failed";
          if (outTitle) *outTitle = "desktop";
          return false;
        }
      } else {
        nextItem = nullptr;
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
    // Confirmed window selection: re-fit the encoder to the FINAL window geometry now (before the
    // selection first-frame gate opens), so the first IDR is already at the final size. Without this,
    // apply_confirmed_capture_geometry (called inside restart_capture_session) bails for window mode
    // and the encoder stays at the pre-selection size -- the client would get an old-size IDR, then a
    // new-size IDR a frame later, and reconfigure twice. A window DRAG still returns early there and
    // keeps the 0.4s settle. The desktop selection already re-fit through the non-window path.
    if (nextCaptureWindowModeActive && useH264) {
      uint32_t finalW = 0;
      uint32_t finalH = 0;
      {
        std::lock_guard<std::mutex> lk(captureResourceMu);
        finalW = captureWidth;
        finalH = captureHeight;
      }
      apply_confirmed_capture_geometry(finalW, finalH, "window-select", /*allowWindowOverride=*/true);
    }
    forceKeyNext = true;
    selectionFirstKeyframePendingGeneration = nextCaptureStreamGeneration;
    selectionFirstKeyframeDropCount = 0;
    ++captureRestartCount;
    flush_capture_pipeline_state("window-select");

    if (outFlags) *outFlags = 0x1u;
    if (outWindowId) *outWindowId = nextSelectedWindowId;
    if (outStreamGeneration) *outStreamGeneration = nextCaptureStreamGeneration;
    if (outReason) *outReason = nextReason;
    if (outTitle) *outTitle = nextTitle;
    return true;
  };

  // --- Trailing-edge encoder kick (host main/encode thread only) ----------------------------
  // The async H.264 MFT holds the most recent input frame until the NEXT input arrives, so on a
  // still screen the last real capture (the state after a drag-release, a right-click menu, the
  // first frame after connect) stays stuck inside the encoder and never reaches the wire. This kick
  // supplies exactly one "next input" on a trailing edge: every real frame (re)arms a 150ms timer,
  // so continuous motion just pushes the deadline out (zero synthetic frames); only when changes
  // stop does the timer fire and resubmit the cached last raw frame once, flushing the held frame
  // out. A kick is cancelled the moment the latest real input is observed coming out of the encoder
  // (its capture timestamp on an emitted AU) or -- on a fresh media barrier -- the epoch's first key
  // AU reaches the wire. Kicks are kept out of ABR/rate evidence: a single sparse frame is not a
  // congestion signal. This is NOT a periodic keepalive -- nothing is sent while the screen is quiet.
  constexpr uint64_t kTrailingKickDelayUs = 150000;  // 150ms trailing edge
  bool trailingKickPending = false;
  uint64_t trailingKickDueAtUs = 0;
  uint64_t lastSeenBootstrapEpoch = 0;       // session epoch we last armed for
  uint64_t lastSeenStreamGeneration = 0;     // capture generation last armed for (window-select/reattach)
  uint64_t lastRealInputCaptureUs = 0;       // capture ts of the most recent real frame fed to the MFT
  uint64_t lastEmittedAuCaptureUs = 0;       // capture ts seen on the most recent emitted AU (encoder output)
  uint64_t lastKickedForInputCaptureUs = 0;  // one-kick-per-held-input guard
  uint64_t trailingKickCount = 0;            // telemetry: total trailing-edge kicks served
  uint64_t lastKickSourceAgeUs = 0;          // telemetry: cached-frame age at the last kick
  // Periodic static refresh cadence (0 = off). On a genuinely still screen the pipeline sends
  // nothing at all, so the viewer's picture silently ages and looks dead; this re-serves the
  // cached frame as a cheap P-frame at a low rate. Milliseconds via env for field tuning.
  const uint64_t staticRefreshIntervalUs =
      static_cast<uint64_t>(env_u32_clamped("REMOTE60_NATIVE_STATIC_REFRESH_MS", 1000, 0, 10000)) *
      1000ULL;
  uint64_t staticRefreshCount = 0;           // telemetry: refresh SUBMITS (not wire AUs)
  uint64_t lastStaticRefreshAttemptUs = 0;   // attempt-side cadence anchor; see the refresh block
  auto arm_trailing_kick = [&](uint64_t atUs) {
    if (!useH264) return;
    trailingKickPending = true;
    trailingKickDueAtUs = atUs + kTrailingKickDelayUs;
  };
  auto cancel_trailing_kick = [&]() {
    trailingKickPending = false;
    trailingKickDueAtUs = 0;
  };
  // Validate the cache against the live capture identity and the CURRENT secure-desktop state, then
  // fill the loop's frame locals from it. Returns false (leaving the screen black) if anything is
  // stale, mismatched, or the desktop is locked/secure -- better black than a wrong picture.
  auto kick_try_fill = [&](std::shared_ptr<std::vector<uint8_t>>& outPayload, uint32_t& outW,
                           uint32_t& outH, uint32_t& outStride, uint64_t nowUs) -> bool {
    BootstrapFrameCache snap;
    {
      std::lock_guard<std::mutex> lk(bootstrapCacheMu);
      snap = bootstrapCache;  // copies the shared_ptr (keeps pixels alive) + identity fields
    }
    if (!snap.payload || snap.payload->empty() || snap.width < 2 || snap.height < 2) return false;
    // Identity: the cached pixels must belong to the target the session is watching now.
    if (snap.windowMode != captureWindowModeActive.load(std::memory_order_acquire)) return false;
    if (snap.selectedWindowId != selectedWindowIdState.load(std::memory_order_acquire)) return false;
    if (snap.targetHwnd != hostCaptureTargetHwnd.load(std::memory_order_acquire)) return false;
    if (snap.targetPid != hostCaptureTargetPid.load(std::memory_order_acquire)) return false;
    if (snap.streamGeneration != captureStreamGenerationState.load(std::memory_order_acquire))
      return false;
    if (snap.consoleSessionId != WTSGetActiveConsoleSessionId()) return false;
    {
      std::lock_guard<std::mutex> lk(captureResourceMu);
      if (snap.srcCaptureWidth != captureWidth || snap.srcCaptureHeight != captureHeight)
        return false;
    }
    // Re-check secure/lock state live (the shared query caches ~250ms; do not trust it here).
    if (!interactive_desktop_is_default_uncached()) return false;
    outPayload = snap.payload;
    outW = snap.width;
    outH = snap.height;
    outStride = snap.stride;
    ++trailingKickCount;
    lastKickSourceAgeUs = (nowUs > snap.captureQpcUs) ? (nowUs - snap.captureQpcUs) : 0;
    std::cout << "[native-video-host] trailing-edge kick epoch="
              << sessionEpoch.load(std::memory_order_acquire)
              << " ageUs=" << lastKickSourceAgeUs << " size=" << outW << "x" << outH
              << " gen=" << snap.streamGeneration << "\n";
    return true;
  };

  // Dedicated liveness watchdog. It shares no lock or GPU with the capture/encode/send threads, so
  // it stays responsive when they wedge inside a driver/MFT call (the failure seen in the field:
  // the whole main loop stopped, control threads kept running, and nothing recovered because the
  // in-loop self-heal never ran and the supervisor only relaunches on a crash). It never calls into
  // D3D (a device-wide hang could block that too) -- it only reads the atomics the main loop last
  // stored, writes one raw record via WriteFile (not iostream, whose lock a hung main may hold), and
  // TerminateProcess()es so the supervisor relaunches a fresh child. ExitProcess/normal return are
  // avoided: they run DLL detach / join the hung threads and would re-hang.
  std::thread mainLoopWatchdog([&]() {
    constexpr uint64_t kHangNormalUs = 10'000'000;   // Loop / EncodeCall
    constexpr uint64_t kHangSlowUs = 20'000'000;     // CaptureRestart / Startup (legit slow)
    constexpr uint64_t kStartupGraceUs = 30'000'000;  // device/encoder bring-up before arming
    const uint64_t watchdogStartUs = qpc_now_us();
    while (!stop.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      if (stop.load(std::memory_order_acquire)) break;
      const uint64_t now = qpc_now_us();
      if (now - watchdogStartUs < kStartupGraceUs) continue;
      const uint32_t phase = mainLoopPhase.load(std::memory_order_acquire);
      const uint64_t progressUs = mainLoopProgressUs.load(std::memory_order_acquire);
      const uint64_t ageUs = now > progressUs ? now - progressUs : 0;
      const uint64_t threshold =
          (phase == static_cast<uint32_t>(MainLoopPhase::CaptureRestart) ||
           phase == static_cast<uint32_t>(MainLoopPhase::Startup))
              ? kHangSlowUs
              : kHangNormalUs;
      if (ageUs >= threshold) {
        char rec[192];
        const int n = std::snprintf(
            rec, sizeof(rec),
            "[native-video-host][watchdog] main-loop hang phase=%u ageUs=%llu lastSeq=%llu; "
            "terminating (exit 43) for supervisor relaunch\n",
            phase, static_cast<unsigned long long>(ageUs),
            static_cast<unsigned long long>(mainLoopLastSeq.load(std::memory_order_acquire)));
        HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
        if (herr && herr != INVALID_HANDLE_VALUE && n > 0) {
          DWORD wrote = 0;
          WriteFile(herr, rec, static_cast<DWORD>(n), &wrote, nullptr);
        }
        TerminateProcess(GetCurrentProcess(), kExitMainLoopWatchdog);
      }
    }
  });
  mainLoopWatchdog.detach();

  while (!stop.load()) {
    mark_main_progress(MainLoopPhase::Loop);
    const uint64_t nowUs = qpc_now_us();
    uint64_t tickWaitUs = 0;
    if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
      break;
    }
    if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
      break;
    }
    pump_udp_hello();
    pump_cursor_forward(nowUs);
    // Arm the trailing-edge kick on a fresh viewer/decoder (bumped session epoch) and on a capture
    // identity change (a new stream generation -- window select, reattach, backend change). The kick
    // fires 150ms later only if no real frame has arrived and been flushed out; a real callback
    // fills the cold cache first, so even the first kick has pixels to resubmit.
    {
      const uint64_t curEpoch = sessionEpoch.load(std::memory_order_acquire);
      if (curEpoch != lastSeenBootstrapEpoch) {
        lastSeenBootstrapEpoch = curEpoch;
        arm_trailing_kick(nowUs);
      }
      const uint64_t curGen = captureStreamGenerationState.load(std::memory_order_acquire);
      if (curGen != lastSeenStreamGeneration) {
        lastSeenStreamGeneration = curGen;
        arm_trailing_kick(nowUs);
      }
    }
    // Barrier recovery: the sender thread re-armed senderWaitingForKey after a same-epoch send
    // failure and cannot itself produce an IDR (forceKeyNext is main-thread-owned, and on a static
    // desktop no new frame arrives to carry senderRequestKey). Consume the flag here, before the
    // frame wait, and both force the next encode to be a key AND arm the trailing kick so the kick
    // resubmits the cached raw frame when the screen is not changing -- otherwise a re-armed barrier
    // on a still desktop would never open.
    if (senderRecoveryPending.exchange(false, std::memory_order_acq_rel)) {
      forceKeyNext = true;
      arm_trailing_kick(nowUs);
    }
    if (desktopBackendReqPending.exchange(false, std::memory_order_acq_rel)) {
      const uint32_t reqSeq = desktopBackendReqSeq.load(std::memory_order_acquire);
      DesktopCaptureBackend nextRequested = requestedDesktopBackend;
      const uint16_t requestedCode = desktopBackendReqValue.load(std::memory_order_acquire);
      if (desktop_capture_backend_from_code(requestedCode, &nextRequested)) {
        requestedDesktopBackend = nextRequested;
        const bool desktopActive = !captureWindowModeActive.load(std::memory_order_acquire);
        const bool restartNeeded = desktopActive && activeDesktopBackend != requestedDesktopBackend;
        if (restartNeeded) {
          const DesktopCaptureBackend prevActiveBackend = activeDesktopBackend;
          activeDesktopBackend = requestedDesktopBackend;
          if (!restart_capture_session()) {
            activeDesktopBackend = prevActiveBackend;
            std::cerr << "[native-video-host][control] desktop-backend-apply failed seq=" << reqSeq
                      << " requested=" << desktop_capture_backend_name(requestedDesktopBackend)
                      << " active=" << desktop_capture_backend_name(activeDesktopBackend)
                      << "\n";
          } else {
            ++captureRestartCount;
            captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
            lastCaptureUsForInterval.store(0, std::memory_order_release);
            lastCallbackUs.store(0, std::memory_order_release);
            resetHostTimelineAnchors();
            forceKeyNext = true;
            flush_capture_pipeline_state("desktop-backend-switch");
            std::cout << "[native-video-host][control] desktop-backend-applied seq=" << reqSeq
                      << " requested=" << desktop_capture_backend_name(requestedDesktopBackend)
                      << " active=" << desktop_capture_backend_name(activeDesktopBackend)
                      << " desktopActive=1\n";
          }
        } else {
          std::cout << "[native-video-host][control] desktop-backend-stored seq=" << reqSeq
                    << " requested=" << desktop_capture_backend_name(requestedDesktopBackend)
                    << " active=" << desktop_capture_backend_name(activeDesktopBackend)
                    << " desktopActive=" << (desktopActive ? 1 : 0)
                    << "\n";
        }
      }
    }
    // A DXGI worker can lose duplication during a fullscreen/desktop transition after the
    // viewer has already marked the stream inactive. Process recovery before the inactive
    // early-return; otherwise the request remains stuck and the next selection intermittently
    // times out with DXGI_ERROR_ACCESS_LOST/E_ACCESSDENIED.
    // The active check comes before the exchange so an inactive stream does not consume the
    // request: with no client watching, restarting capture here would be exactly the leak this
    // lifecycle exists to close -- an RDP connect moves the desktop, the fallback fires, and a
    // clientless host starts capturing the RDP session at full rate. While the stream is
    // inactive the request either survives until the client returns (processed then, one
    // iteration after the active edge) or is cleared by the idle detach, whose reattach
    // re-resolves the backend from scratch anyway.
    if (streamControlActive.load(std::memory_order_acquire) &&
        dxgiFallbackRequested.exchange(false, std::memory_order_acq_rel) &&
        !captureWindowModeActive.load(std::memory_order_acquire)) {
      powerKeepalive.SetStreaming(true, true);
      if (dxgiCaptureStarted) {
        dxgiCaptureSession.Stop();
        dxgiCaptureStarted = false;
      }
      activeDesktopBackend = DesktopCaptureBackend::Wgc;
      const std::string fallbackReason = copy_dxgi_fallback_reason();
      std::cout << "[native-video-host] fallback_reason="
                << (fallbackReason.empty() ? "dxgi_runtime_fallback" : fallbackReason)
                << "\n";
      if (!restart_capture_session()) {
        std::cerr << "[native-video-host] capture fallback restart failed; retrying\n";
        dxgiFallbackRequested.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      ++captureRestartCount;
      captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
      lastCaptureUsForInterval.store(0, std::memory_order_release);
      lastCallbackUs.store(0, std::memory_order_release);
      resetHostTimelineAnchors();
      forceKeyNext = true;
      flush_capture_pipeline_state("dxgi-runtime-fallback");
    }
    if (streamControlActive.load(std::memory_order_acquire) &&
        gdiFallbackRequested.exchange(false, std::memory_order_acq_rel) &&
        !captureWindowModeActive.load(std::memory_order_acquire)) {
      powerKeepalive.SetStreaming(true, true);
      if (gdiCaptureStarted) {
        gdiCaptureProcess.Stop();
        gdiCaptureStarted = false;
      }
      activeDesktopBackend = DesktopCaptureBackend::Wgc;
      const std::string fallbackReason = copy_gdi_fallback_reason();
      std::cout << "[native-video-host] fallback_reason="
                << (fallbackReason.empty() ? "gdi_runtime_fallback" : fallbackReason)
                << "\n";
      if (!restart_capture_session()) {
        std::cerr << "[native-video-host] GDI capture fallback restart failed; retrying\n";
        gdiFallbackRequested.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      ++captureRestartCount;
      captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
      lastCaptureUsForInterval.store(0, std::memory_order_release);
      lastCallbackUs.store(0, std::memory_order_release);
      resetHostTimelineAnchors();
      forceKeyNext = true;
      flush_capture_pipeline_state("gdi-runtime-fallback");
    }

    // Climb back to the requested backend once whatever forced the demotion has passed.
    //
    // A demotion used to be permanent: activeDesktopBackend was set to Wgc and the only way back
    // was an explicit request from the client, which then failed again for the same reason. So a
    // single UAC prompt or RDP connect left the session on WGC for good, and the picture stayed
    // degraded long after the cause was gone. That is the "everything is slower after a UAC
    // prompt" report.
    //
    // Both causes are temporary by nature. The secure desktop goes away when the prompt is
    // answered, and the desktop returns to the physical adapter when RDP disconnects, so simply
    // trying again is what was missing.
    if (activeDesktopBackend != requestedDesktopBackend &&
        !captureWindowModeActive.load(std::memory_order_acquire) &&
        streamControlActive.load(std::memory_order_acquire)) {
      const uint64_t nowUs = qpc_now_us();
      if (desktopDemotionSinceUs == 0) desktopDemotionSinceUs = nowUs;
      // Probe the interactive-desktop state at a bounded cadence (OpenInputDesktop is a syscall).
      // A secure desktop resets the stability clock; the default desktop starts or continues it.
      // The uncached query is deliberate: the shared cached one is refreshed by input/pong callers
      // and can hand a stale "default" reading to a promotion decision the moment a UAC prompt rose.
      if (nowUs >= desktopDefaultProbeAtUs) {
        desktopDefaultProbeAtUs = nowUs + kDesktopDefaultProbeIntervalUs;
        if (interactive_desktop_is_default_uncached()) {
          if (desktopDefaultStableSinceUs == 0) desktopDefaultStableSinceUs = nowUs;
        } else {
          desktopDefaultStableSinceUs = 0;
          desktopSecureProbeFalseTotal.fetch_add(1, std::memory_order_relaxed);
        }
      }
      const bool defaultStable =
          desktopDefaultStableSinceUs != 0 &&
          (nowUs - desktopDefaultStableSinceUs) >= kDesktopDefaultStableUs;
      if (desktopBackendRetryAtUs == 0) {
        desktopBackendRetryAtUs = nowUs + kDesktopBackendRetryMinUs;
      } else if (nowUs >= desktopBackendRetryAtUs) {
        // The retry deadline is due. Promote only if the default desktop has been up for the whole
        // settle window AND one final uncached check confirms it is still up right now -- otherwise
        // a UAC prompt that reappeared since the last cadence probe would still eat a restart+IDR.
        bool finalDefault = defaultStable;
        if (finalDefault && !interactive_desktop_is_default_uncached()) {
          finalDefault = false;
          desktopDefaultStableSinceUs = 0;  // secure again: restart the settle clock
          desktopSecureProbeFalseTotal.fetch_add(1, std::memory_order_relaxed);
        }
        if (!finalDefault) {
          // Deferred by the secure gate. Latch so this counts once per deadline episode, not once
          // per main-loop iteration -- the deadline stays due until we actually attempt.
          if (!promotionDeferredForCurrentDeadline) {
            promotionDeferredForCurrentDeadline = true;
            desktopPromotionDeferredSecureTotal.fetch_add(1, std::memory_order_relaxed);
            std::cout << "[native-video-host] desktop-promotion-deferred reason=secure-desktop\n";
          }
        } else {
          promotionDeferredForCurrentDeadline = false;
          desktopPromotionAttempts.fetch_add(1, std::memory_order_relaxed);
          const DesktopCaptureBackend demoted = activeDesktopBackend;
          activeDesktopBackend = requestedDesktopBackend;
          const bool restarted = restart_capture_session();
          // restart_capture_session() reports that *a* session started, not that it started on the
          // backend we asked for. When the requested one is still unavailable it falls back
          // internally, puts activeDesktopBackend back where it was, and returns success anyway.
          // The backend the restart actually left behind is the only honest test.
          const bool promoted = restarted && activeDesktopBackend == requestedDesktopBackend;
          if (restarted) {
            // A restart replaces the capture session whether or not the backend moved, so the
            // timeline still has to be re-anchored and the next frame still has to be a keyframe.
            ++captureRestartCount;
            captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(),
                                       std::memory_order_release);
            lastCaptureUsForInterval.store(0, std::memory_order_release);
            lastCallbackUs.store(0, std::memory_order_release);
            resetHostTimelineAnchors();
            forceKeyNext = true;
            flush_capture_pipeline_state(promoted ? "desktop-backend-restored"
                                                  : "desktop-backend-retry-failed");
          }
          if (promoted) {
            desktopPromotionSuccess.fetch_add(1, std::memory_order_relaxed);
            if (desktopDemotionSinceUs != 0 && nowUs >= desktopDemotionSinceUs) {
              lastPromotionWaitUs.store(nowUs - desktopDemotionSinceUs, std::memory_order_relaxed);
            }
            std::cout << "[native-video-host] desktop-backend-restored from="
                      << desktop_capture_backend_name(demoted)
                      << " to=" << desktop_capture_backend_name(activeDesktopBackend) << "\n";
            desktopBackendRetryAtUs = 0;
            desktopBackendRetryDelayUs = kDesktopBackendRetryMinUs;
            desktopDemotionSinceUs = 0;
          } else {
            // A real promotion failure with the default desktop up (e.g. RDP: primary duplication
            // still unavailable). This is not the secure-desktop case, so back off -- a machine
            // that genuinely cannot use the requested backend must not restart every few seconds.
            desktopPromotionFail.fetch_add(1, std::memory_order_relaxed);
            activeDesktopBackend = demoted;
            desktopBackendRetryDelayUs =
                std::min<uint64_t>(desktopBackendRetryDelayUs * 2, kDesktopBackendRetryMaxUs);
            desktopBackendRetryAtUs = nowUs + desktopBackendRetryDelayUs;
          }
          // Any attempt consumes the current stability evidence; the next one must gather fresh
          // proof that the default desktop is up before it may fire.
          desktopDefaultStableSinceUs = 0;
          desktopDefaultProbeAtUs = 0;
        }
      }
    } else {
      desktopBackendRetryAtUs = 0;
      desktopBackendRetryDelayUs = kDesktopBackendRetryMinUs;
      desktopDefaultStableSinceUs = 0;
      desktopDefaultProbeAtUs = 0;
      desktopDemotionSinceUs = 0;
      promotionDeferredForCurrentDeadline = false;
    }

    const bool streamActive = streamControlActive.load(std::memory_order_acquire);
    if (!streamActive) {
      if (streamActiveApplied) {
        flush_capture_pipeline_state("stream-inactive");
        streamActiveApplied = false;
        powerKeepalive.SetStreaming(false);
        captureIdleDetachAtUs = qpc_now_us() + kCaptureIdleDetachDelayUs;
        std::cout << "[native-video-host] stream inactive\n";
      }
      if (!captureIdleDetached && qpc_now_us() >= captureIdleDetachAtUs) {
        detach_capture_session();
        // Stale by construction: whatever forced a fallback while nobody was watching is
        // re-evaluated from scratch when the reattach picks its backend.
        dxgiFallbackRequested.store(false, std::memory_order_release);
        gdiFallbackRequested.store(false, std::memory_order_release);
        captureIdleDetached = true;
        std::cout << "[native-video-host] capture detached (idle)\n";
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    if (!streamActiveApplied) {
      if (captureIdleDetached) {
        // Reattach before declaring the stream applied, and only declare it on success:
        // marking it applied with no capture running would serve black frames with no path
        // back. Failure retries with backoff -- the desktop may still be mid-transition
        // (RDP disconnecting, a secure desktop closing) when the client returns.
        const uint64_t nowUs = qpc_now_us();
        if (nowUs < captureReattachRetryAtUs) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        if (!captureWindowModeActive.load(std::memory_order_acquire)) {
          // Fresh resolution, not the backend the last session was demoted to. This is also
          // what frees a host parked on WGC by an RDP visit: the desktop is back on the real
          // adapter by now, and starting from the requested backend finds it.
          activeDesktopBackend = requestedDesktopBackend;
          // Reattach bypasses the climb-back secure gate, so honour the same rule here: if the
          // requested backend is DXGI but the desktop is currently secure (UAC/lock), attaching
          // DXGI would just take an immediate E_ACCESSDENIED and demote. Start on WGC instead so
          // the returning viewer gets a picture now, and let the climb-back promote to DXGI once
          // the default desktop settles. A requested WGC/GDI backend is respected as-is.
          if (requestedDesktopBackend == DesktopCaptureBackend::Dxgi &&
              !interactive_desktop_is_default_uncached()) {
            activeDesktopBackend = DesktopCaptureBackend::Wgc;
          }
        }
        if (!restart_capture_session()) {
          captureReattachRetryDelayUs =
              std::min<uint64_t>(captureReattachRetryDelayUs * 2, kCaptureReattachRetryMaxUs);
          captureReattachRetryAtUs = nowUs + captureReattachRetryDelayUs;
          std::cerr << "[native-video-host] capture reattach failed; retrying in "
                    << (captureReattachRetryDelayUs / 1000) << "ms\n";
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        captureIdleDetached = false;
        captureReattachRetryAtUs = 0;
        captureReattachRetryDelayUs = kCaptureReattachRetryMinUs;
        ++captureRestartCount;
        captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
        lastCaptureUsForInterval.store(0, std::memory_order_release);
        lastCallbackUs.store(0, std::memory_order_release);
        resetHostTimelineAnchors();
        flush_capture_pipeline_state("capture-reattached");
        std::cout << "[native-video-host] capture reattached backend="
                  << desktop_capture_backend_name(activeDesktopBackend) << "\n";
      }
      streamActiveApplied = true;
      forceKeyNext = true;
      // A returning viewer on a still desktop needs a picture too; arm the trailing-edge kick for
      // the current epoch (coalesces with any arm from the epoch/generation edges above). This also
      // covers the stream-inactive->active edge and a capture reattach, which both land here.
      arm_trailing_kick(qpc_now_us());
      powerKeepalive.SetStreaming(true, true);
      // A stream-inactive->active edge starts a fresh streaming episode; drop any no-output streak
      // left from before so the inactive gap is not mistaken for encoder starvation.
      reset_encoder_starvation_episode();
      std::cout << "[native-video-host] stream active; forcing keyframe\n";
    }
    if (useH264 && runtimeTunePending.exchange(false, std::memory_order_acq_rel)) {
      const uint32_t reqSeq = runtimeTuneSeq.load(std::memory_order_acquire);
      const uint32_t requestedBitrate = runtimeTuneBitrate.load(std::memory_order_acquire);
      const bool bitrateExplicit = requestedBitrate >= 100000;
      uint32_t targetBitrate = requestedBitrate;
      uint32_t targetKeyint = runtimeTuneKeyint.load(std::memory_order_acquire);
      uint32_t targetFps = runtimeTuneFps.load(std::memory_order_acquire);
      // Explicitness is recorded before the fallbacks fill the gaps: the fallbacks are the
      // CURRENT values, and only what the user actually asked for may move a ceiling. A
      // bitrate-only tune sent while overview mode has activeFps lowered would otherwise
      // write that lowered value into userFpsCeiling -- the exact contamination the ceiling
      // exists to prevent, back in through a side door.
      const bool fpsExplicit = targetFps >= 1;
      const bool keyintExplicit = targetKeyint >= 1;
      if (targetBitrate < 100000) targetBitrate = activeBitrate;
      if (targetKeyint < 1) targetKeyint = activeKeyint;
      if (targetFps < 1) targetFps = activeFps;
      const bool bitrateChanged = (targetBitrate != activeBitrate);
      const bool keyintChanged = (targetKeyint != activeKeyint);
      const bool fpsChanged = (targetFps != activeFps);
      // A request can match the ACTIVE value while changing the CEILING: with ABR sitting on
      // its low profile at 6.6 Mbps, a user lowering the ceiling from 12M to exactly 6.6M
      // changes nothing active -- and used to be dropped whole, leaving the profiles, the
      // ladder, and the manual-override reset all unrun. The ceiling comparisons catch what
      // the active comparisons cannot; apply_encoder_target is a no-op for identical targets,
      // so entering the block for a ceiling-only change costs no encoder restart.
      const bool bitrateCeilingChanged = bitrateExplicit && (targetBitrate != abrHighBitrate);
      const bool fpsCeilingChanged = fpsExplicit && (targetFps != userFpsCeiling);
      const bool keyintCeilingChanged = keyintExplicit && (targetKeyint != userKeyintCeiling);
      if (bitrateChanged || keyintChanged || fpsChanged || bitrateCeilingChanged ||
          fpsCeilingChanged || keyintCeilingChanged) {
        if (bitrateExplicit) {
          // The UI bitrate is the top quality ceiling, not an instruction to disable
          // adaptation. A 20 Mbps request may start there, but the host must still step down
          // when the client's decoded FPS/latency says the Wi-Fi path cannot sustain it.
          abrHighBitrate = targetBitrate;
          abrMidBitrate = std::min<uint32_t>(
              abrHighBitrate,
              std::max<uint32_t>(2000000u, (abrHighBitrate * 75u) / 100u));
          abrLowBitrate = std::min<uint32_t>(
              abrHighBitrate,
              std::max<uint32_t>(1500000u, (abrHighBitrate * 55u) / 100u));
          abrHasMidProfile = abrMidBitrate < abrHighBitrate;
          abrHasLowProfile = abrHasLowerResolution || abrLowBitrate < abrMidBitrate;
          abrProfile = 0;
        }
        // The resolution follows the bitrate, because the bitrate is a budget for the whole
        // frame: the same 3 Mbps buys four times as much per pixel at 720p. Switching to mobile
        // has to take the picture size down with it, or the encoder spends the difference
        // predicting badly every time the screen changes at once.
        uint32_t ladderW = nominalEncodeW;
        uint32_t ladderH = nominalEncodeH;
        bool ladderReducedNext = encodeLadderReduced;
        if (bitrateExplicit) {
          const auto choice = remote60::native_poc::choose_encode_resolution(
              targetBitrate, captureWidth, captureHeight, encodeLadderReduced);
          ladderReducedNext = choice.reduced;
          ladderW = choice.width;
          ladderH = choice.height;
          if (ladderW != nominalEncodeW || ladderH != nominalEncodeH) {
            std::cout << "[native-video-host][control] encode ladder " << nominalEncodeW << "x"
                      << nominalEncodeH << " -> " << ladderW << "x" << ladderH
                      << " for " << (targetBitrate / 1000) << "kbps\n";
          }
        }
        // Pass the nominal box, not the fitted activeEncode size: apply_encoder_target
        // records its width/height arguments as the new nominal budget, and feeding the
        // already-fitted size back in would permanently shrink the box for every later
        // target switch.
        if (!apply_encoder_target(ladderW, ladderH, targetFps, targetBitrate, targetKeyint)) {
          std::cerr << "[native-video-host][control] runtime-config apply failed seq=" << reqSeq << "\n";
          break;
        }
        encodeLadderReduced = ladderReducedNext;
        if (fpsExplicit) userFpsCeiling = targetFps;
        if (keyintExplicit) userKeyintCeiling = targetKeyint;
        runtimeTuneManualOverride = false;
        abrCooldownUntilUs = nowUs + 3000000ULL;
        abrGoodSeconds = 0;
        abrModeratePressureSeconds = 0;
        abrSeverePressureSeconds = 0;
        forceKeyNext = true;
        if (fpsChanged && !captureWindowModeActive.load(std::memory_order_acquire) &&
            activeDesktopBackend == DesktopCaptureBackend::Gdi) {
          if (!restart_capture_session()) {
            std::cerr << "[native-video-host][control] GDI fps restart failed seq="
                      << reqSeq << "\n";
            break;
          }
          ++captureRestartCount;
          flush_capture_pipeline_state("gdi-fps-change");
        }
        std::cout << "[native-video-host][control] runtime-config-applied seq=" << reqSeq
                  << " bitrate=" << activeBitrate
                  << " keyint=" << activeKeyint
                  << " fps=" << activeFps
                  // Was hardcoded "abrOverride=1", which misreported the ABR ladder as pinned --
                  // the actual flag is cleared just above, so print the real state.
                  << " abrOverride=" << (runtimeTuneManualOverride ? 1 : 0) << "\n";
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
    // Switching screens is the same operation as switching to desktop mode, aimed at a particular
    // monitor. Done here rather than on the control thread because the capture item belongs to
    // this loop, exactly like the window and capture-mode selections above.
    if (monitorSelectPending.exchange(false, std::memory_order_acq_rel)) {
      const uint32_t requestedId = monitorSelectRequested.load(std::memory_order_acquire);
      const auto monitors = enumerate_monitors();
      if (requestedId >= monitors.size()) {
        std::cerr << "[native-video-host][control] monitor-select out of range id=" << requestedId
                  << " count=" << monitors.size() << "\n";
      } else {
        const auto& target = monitors[requestedId];
        auto nextItem = CreateItemForPrimaryMonitor(nullptr, nullptr, target.handle);
        if (!nextItem) {
          std::cerr << "[native-video-host][control] monitor-select capture failed id="
                    << requestedId << "\n";
        } else {
          item = nextItem;
          selectedMonitorIdState.store(requestedId, std::memory_order_release);
          // A monitor is a desktop target, so any window selection it replaces has to go.
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
            hostCaptureTargetTitle = target.name;
          }
          lastCaptureRestartUs = nowUs;
          if (restart_capture_session()) {
            ++captureRestartCount;
            captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
            lastCaptureUsForInterval.store(0, std::memory_order_release);
            lastCallbackUs.store(0, std::memory_order_release);
            resetHostTimelineAnchors();
            forceKeyNext = true;
            std::cout << "[native-video-host][control] monitor-select applied id=" << requestedId
                      << " " << target.width << "x" << target.height
                      << " at " << target.x << "," << target.y << "\n";
          } else {
            std::cerr << "[native-video-host][control] monitor-select restart failed id="
                      << requestedId << "\n";
          }
        }
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
    // WGC ContentSize settle + main-thread pool recreate. The capture callback dropped frames whose
    // ContentSize != the pool geometry and recorded the pending content size here; during an
    // interactive window drag that size churns every frame. Wait for it to hold steady for a short
    // settle window, then recreate the pool + readback at the new size on THIS (main) thread --
    // the callback thread must never recreate capture resources. restart_capture_session() rebuilds
    // the WGC pool at item.Size() (the settled window size) and create_staging at the new geometry.
    if (wgcContentSizeMismatchPending.load(std::memory_order_acquire) != 0) {
      const uint32_t pendW = wgcPendingContentW.load(std::memory_order_acquire);
      const uint32_t pendH = wgcPendingContentH.load(std::memory_order_acquire);
      uint32_t curCapW = 0;
      uint32_t curCapH = 0;
      {
        std::lock_guard<std::mutex> lk(captureResourceMu);
        curCapW = captureWidth;
        curCapH = captureHeight;
      }
      if (pendW < 2 || pendH < 2 || (pendW == curCapW && pendH == curCapH)) {
        // Content settled back to the current pool geometry -- nothing to recreate.
        wgcContentSizeMismatchPending.store(0, std::memory_order_release);
        wgcSettleTrackW = 0;
        wgcSettleTrackH = 0;
        wgcSettleSinceUs = 0;
      } else if (pendW != wgcSettleTrackW || pendH != wgcSettleTrackH) {
        // Size still moving: (re)arm the settle timer on the newest candidate.
        wgcSettleTrackW = pendW;
        wgcSettleTrackH = pendH;
        wgcSettleSinceUs = nowUs;
      } else if (nowUs - wgcSettleSinceUs >= kWgcContentSettleUs) {
        // Stable for the settle window: recreate the pool/readback at the new size on the main thread.
        wgcContentSizeMismatchPending.store(0, std::memory_order_release);
        wgcSettleTrackW = 0;
        wgcSettleTrackH = 0;
        wgcSettleSinceUs = 0;
        lastCaptureRestartUs = nowUs;
        flush_capture_pipeline_state("wgc-content-size");
        if (restart_capture_session()) {
          ++captureRestartCount;
          ++wgcPoolRecreates;
          captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
          lastCaptureUsForInterval.store(0, std::memory_order_release);
          lastCallbackUs.store(0, std::memory_order_release);
          resetHostTimelineAnchors();
          // Force an IDR at the (now correct) geometry. An interactive drag still lets the encode
          // size catch up on the 0.4s refit path; only the capture pool was resized here.
          forceKeyNext = true;
          uint32_t newCapW = 0;
          uint32_t newCapH = 0;
          {
            std::lock_guard<std::mutex> lk(captureResourceMu);
            newCapW = captureWidth;
            newCapH = captureHeight;
          }
          std::cout << "[native-video-host] wgc-content-size pool recreated content="
                    << pendW << "x" << pendH << " capture=" << newCapW << "x" << newCapH
                    << " poolRecreates=" << wgcPoolRecreates
                    << " restartCount=" << captureRestartCount << "\n";
        } else {
          std::cerr << "[native-video-host] wgc-content-size pool recreate failed content="
                    << pendW << "x" << pendH << "\n";
        }
      }
    }
    if (captureSizeChangePending.exchange(0, std::memory_order_acq_rel) != 0) {
      lastCaptureRestartUs = nowUs;
      flush_capture_pipeline_state("size-change");
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
    if (captureSessionReady.load(std::memory_order_acquire) &&
        streamControlActive.load(std::memory_order_acquire) &&
        !captureWindowModeActive.load(std::memory_order_acquire) &&
        activeDesktopBackend == DesktopCaptureBackend::Gdi) {
      // GDI is clocked and must publish continuously. WGC/DXGI are change-driven and can
      // legitimately stay silent on a static desktop, so callback silence is not a stall for
      // those backends and must never trigger a restart loop.
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
    // DXGI/WGC frozen-ring self-heal. The callback-stall watchdog above is GDI-only on purpose:
    // change-driven backends are silent on a static desktop, so silence there is not a stall. A
    // ring that has frozen under GPU contention is a different thing and it has a distinct signal
    // -- its oldest submit sits in GpuPending because the completion query never fires, while an
    // idle ring holds nothing pending at all. Restart on that age, over two consecutive polls so a
    // single slow readback does not trip it. This is the "it went dark and reconnecting shows
    // nothing" report from a host pinned by a GPU-heavy game; before this, DXGI/WGC had no path
    // back short of the user restarting the host.
    if (captureSessionReady.load(std::memory_order_acquire) &&
        streamControlActive.load(std::memory_order_acquire) &&
        !captureWindowModeActive.load(std::memory_order_acquire) &&
        activeDesktopBackend != DesktopCaptureBackend::Gdi) {
      const uint64_t oldestPendingUs = captureReadback.OldestGpuPendingAgeUs();
      oldestGpuPendingPeakUs = std::max(oldestGpuPendingPeakUs, oldestPendingUs);
      // Same loop-rate sample feeds the readback-drain watchdog's per-1s-window peak; unlike the
      // frozen-ring peak above (reset per print interval) this one is reset every stats tick.
      readbackDrainOldestPendingPeakUs = std::max(readbackDrainOldestPendingPeakUs, oldestPendingUs);
      gpuPendingCountPeak = std::max(gpuPendingCountPeak, captureReadback.GpuPendingCount());
      if (oldestPendingUs >= kCaptureFrozenWarnUs && captureFrozenWarnedAtUs == 0) {
        captureFrozenWarnedAtUs = nowUs;
        std::cout << "[native-video-host] capture readback slow oldestPendingUs=" << oldestPendingUs
                  << "\n";
      }
      if (oldestPendingUs >= kCaptureFrozenRestartUs) {
        ++captureFrozenPollStreak;
      } else {
        captureFrozenPollStreak = 0;
        captureFrozenWarnedAtUs = 0;
      }
      const bool restartCooldownDone =
          (lastCaptureRestartUs == 0 ||
           nowUs >= (lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
      if (captureFrozenPollStreak >= kCaptureFrozenPollStreakMin && restartCooldownDone) {
        captureFrozenPollStreak = 0;
        captureFrozenWarnedAtUs = 0;
        // First freeze: a same-device capture restart clears a wedged duplication/WGC session.
        // A refreeze inside the window means the device itself is stuck -- restarting capture on
        // the same device will not clear it -- so exit and let the supervisor rebuild the process
        // with a fresh D3D device. main() runs under that supervisor; a non-zero return long after
        // startup reads to it as a restartable exit (ranMs >= 15s, so not counted as a crash-loop),
        // and it relaunches without the startup backoff.
        const bool refroze =
            lastFrozenRestartUs != 0 &&
            nowUs < (lastFrozenRestartUs + kCaptureFrozenEscalationWindowUs);
        lastFrozenRestartUs = nowUs;
        if (refroze) {
          std::cerr << "[native-video-host] capture ring refroze within "
                    << (kCaptureFrozenEscalationWindowUs / 1000000)
                    << "s oldestPendingUs=" << oldestPendingUs
                    << "; exiting for a full process restart\n";
          // A machine-readable twin of the line above: an in-process escalation counter would be
          // pointless (the process exits before another stats print), so this single record carries
          // the last state the frozen ring reached and pairs with host_app.log's exit-code-3 line
          // to reconstruct a cross-process recovery across the restart. (Codex.)
          const uint64_t refreezeLastPubUs = lastPublishUs.load(std::memory_order_acquire);
          const uint64_t refreezeLastPubAgeUs =
              (refreezeLastPubUs > 0 && nowUs > refreezeLastPubUs) ? nowUs - refreezeLastPubUs : 0;
          std::cout << "[native-video-host] capture-recovery reason=frozen-ring-refreeze"
                    << " action=process-restart exitCode=3"
                    << " oldestPendingUs=" << oldestPendingUs
                    << " gpuPendingCount=" << captureReadback.GpuPendingCount()
                    << " frozenRingRestarts=" << frozenRingRestartCount
                    << " captureRestarts=" << captureRestartCount
                    << " lastPublishAgeUs=" << refreezeLastPubAgeUs
                    << " backend=" << desktop_capture_backend_name(activeDesktopBackend)
                    << " windowSec=" << (kCaptureFrozenEscalationWindowUs / 1000000)
                    << "\n";
          std::cout.flush();
          std::cerr.flush();
          return 3;
        }
        lastCaptureRestartUs = nowUs;
        const bool restarted = restart_capture_session();
        if (restarted) {
          ++captureRestartCount;
          captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(),
                                     std::memory_order_release);
          lastCaptureUsForInterval.store(0, std::memory_order_release);
          lastCallbackUs.store(0, std::memory_order_release);
          resetHostTimelineAnchors();
          forceKeyNext = true;
          ++captureDeadRestartCount;
          ++frozenRingRestartCount;
          std::cout << "[native-video-host] capture session restarted reason=frozen-ring count="
                    << captureRestartCount << " captureDeadRestartCount=" << captureDeadRestartCount
                    << " frozenRingRestarts=" << frozenRingRestartCount
                    << " oldestPendingUs=" << oldestPendingUs << "\n";
        } else {
          std::cerr << "[native-video-host] frozen-ring restart failed oldestPendingUs="
                    << oldestPendingUs << "\n";
        }
      }
    }
    if (paceByTick) {
      if (nowUs < nextTickUs) {
        const uint64_t paceWaitStartUs = qpc_now_us();
        // Reuse the high-resolution sender timer. sleep_for commonly overshoots a 60 Hz
        // deadline by 1-3ms on Windows; resetting the clock to that late wakeup on every
        // frame turned a requested 60fps into a stable 48-54fps.
        udp_pace_wait_until(nextTickUs);
        const uint64_t paceWaitDoneUs = qpc_now_us();
        tickWaitUs = (paceWaitDoneUs >= paceWaitStartUs) ? (paceWaitDoneUs - paceWaitStartUs) : 0;
        continue;
      }
      // Preserve the target phase after a normal sub-frame timer overshoot. Re-anchor only
      // when processing actually missed a whole frame, avoiding both drift and catch-up bursts.
      if (nowUs > nextTickUs + activePacingFrameIntervalUs) {
        nextTickUs = nowUs;
      }
      nextTickUs += activePacingFrameIntervalUs;
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
    uint64_t captureD3DWaitUs = 0;
    uint64_t captureCopyMapUs = 0;
    uint64_t captureMemcpyUs = 0;
    uint64_t captureUnmapWaitUs = 0;
    uint64_t captureUnmapUs = 0;
    uint64_t version = 0;
    int32_t nv12Slot = -1;
    uint64_t nv12Generation = 0;
    uint32_t nv12W = 0;
    uint32_t nv12H = 0;
    uint32_t queueWaitReason = 0;  // 0: normal, 1: timeout, 2: no-work
    const uint64_t queueSelectStartUs = qpc_now_us();
    bool servedBootstrap = false;
    bool kickForcedKey = false;  // true only when this kick must open a closed media barrier (IDR)
    if (trailingKickPending && nowUs >= trailingKickDueAtUs) {
      // A real frame already waiting in the ring is always better than a kick; fall through to the
      // normal pop (the encode below re-arms and records it). Otherwise decide whether the last real
      // input still needs flushing out of the MFT.
      bool realWaiting = false;
      {
        std::lock_guard<std::mutex> lk(frame.mu);
        realWaiting = (frame.version != lastVersionSent) && frame.payload && !frame.payload->empty();
      }
      if (!realWaiting) {
        // Media barrier (UDP): a closed barrier means the epoch's first key AU has not reached the
        // wire, so a fresh/returning viewer still has no picture. TCP has no barrier (always open).
        bool barrierClosed = false;
        if (transport == VideoTransport::Udp) {
          std::lock_guard<std::mutex> lk(senderMu);
          barrierClosed = senderWaitingForKey;
        }
        // The latest real input is "stuck" until its capture timestamp is observed on an emitted AU;
        // on the async MFT it sits there until the next input, which on a still screen never comes.
        const bool latestInputStuck = (lastRealInputCaptureUs > lastEmittedAuCaptureUs);
        // One kick per distinct held input: never resubmit the same held frame twice on a P-frame
        // trailing edge. A closed barrier overrides this -- it must keep kicking until an IDR lands.
        const bool alreadyKickedThisInput =
            (lastRealInputCaptureUs != 0 && lastKickedForInputCaptureUs == lastRealInputCaptureUs);
        const bool needKick = barrierClosed || (latestInputStuck && !alreadyKickedThisInput);
        bool rearm = false;
        if (needKick && kick_try_fill(payload, w, h, stride, nowUs)) {
          servedBootstrap = true;
          // A closed barrier needs a real IDR; an ordinary trailing edge on an open stream can ride
          // the held frame as-is (a P-frame is fine). Leave any pre-existing forceKeyNext untouched.
          if (barrierClosed) {
            forceKeyNext = true;
            kickForcedKey = true;
          }
          seq = 0;
          version = lastVersionSent;  // no real version consumed (keeps queue bookkeeping stable)
          streamGeneration = captureStreamGenerationState.load(std::memory_order_acquire);
          captureUs = nowUs;     // fresh monotonic stamps: never reuse the stale capture time
          callbackUs = nowUs;
          queuePushUs = nowUs;
          lastKickedForInputCaptureUs = lastRealInputCaptureUs;  // one-shot per held input
          // Keep kicking on a still-closed barrier: each kick feeds a forced IDR, so the held frame
          // becomes an IDR within a couple of flushes and the cancel comes when it reaches the wire.
          rearm = barrierClosed;
        }
        // Otherwise one-shot: a failed fill (locked/secure/identity mismatch) leaves the screen black
        // rather than painting a wrong or stale picture, and a satisfied trailing edge stays quiet.
        if (rearm) {
          arm_trailing_kick(nowUs);
        } else {
          cancel_trailing_kick();
        }
      }
    }
    // Periodic static refresh (user requirement): on a still screen duplication offers no content
    // and the trailing kick is one-shot, so NOTHING is sent and the session looks frozen (the
    // field case: a static game map, revived only by dragging it). Re-serve the cached frame at a
    // low cadence (default 1Hz, REMOTE60_NATIVE_STATIC_REFRESH_MS, 0=off) as an ordinary P-frame.
    // The cadence anchors on BOTH the last emitted AU and the last refresh ATTEMPT: the async MFT
    // may legally return no output for a submitted input, and an emitted-only clock would then
    // retry on every loop iteration -- a tight input flood, the opposite of an idle 1Hz refresh.
    // The barrier must be open (a closed barrier is the kick's job and needs an IDR) and the
    // sender queue empty (stacking a synthetic frame onto a backlog helps nobody; the queue drains
    // within a few loop ticks). kick_try_fill re-validates identity/secure/size, so a lock screen
    // or a mid-switch target stays black rather than repainting a stale picture; a failed fill
    // also stamps the attempt clock so the (uncached) secure probe is not repeated every tick.
    if (!servedBootstrap && staticRefreshIntervalUs > 0 && useH264 &&
        streamControlActive.load(std::memory_order_acquire) && !trailingKickPending &&
        lastEmittedAuCaptureUs != 0 &&
        nowUs >= lastEmittedAuCaptureUs + staticRefreshIntervalUs &&
        nowUs >= lastStaticRefreshAttemptUs + staticRefreshIntervalUs) {
      bool refreshBlocked = false;
      if (transport == VideoTransport::Udp) {
        std::lock_guard<std::mutex> lk(senderMu);
        refreshBlocked = senderWaitingForKey || !senderQueue.empty();
      }
      if (!refreshBlocked) {
        // Stamped on the ATTEMPT, before the encode result is known -- see the cadence note.
        lastStaticRefreshAttemptUs = nowUs;
        if (kick_try_fill(payload, w, h, stride, nowUs)) {
          servedBootstrap = true;
          seq = 0;
          version = lastVersionSent;  // no real version consumed (keeps queue bookkeeping stable)
          streamGeneration = captureStreamGenerationState.load(std::memory_order_acquire);
          captureUs = nowUs;
          callbackUs = nowUs;
          queuePushUs = nowUs;
          ++staticRefreshCount;
        }
      }
    }
    bool queueReady = false;
    if (!servedBootstrap) {
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
      captureD3DWaitUs = frame.captureD3DWaitUs;
      captureCopyMapUs = frame.captureCopyMapUs;
      captureMemcpyUs = frame.captureMemcpyUs;
      captureUnmapWaitUs = frame.captureUnmapWaitUs;
      captureUnmapUs = frame.captureUnmapUs;
      nv12Slot = frame.nv12Slot;
      nv12Generation = frame.nv12Generation;
      nv12W = frame.nv12W;
      nv12H = frame.nv12H;
      frame.nv12Slot = -1;  // claimed; this loop now owns the release
    }
    // NB: a real frame pop deliberately does NOT cancel the kick. The pending timer is (re)armed and
    // lastRealInputCaptureUs recorded once the frame is actually fed to the MFT (see below), so the
    // deadline trails the LAST real input; the kick then cancels only when that input is observed
    // coming out of the encoder, not merely because a frame was popped.
    if (poppedNv12Slot >= 0) {
      // The previous iteration bailed out before encoding (gating skip, stale drop);
      // release its claimed conversion now.
      captureReadback.ReleaseNv12Slot(poppedNv12Slot, poppedNv12Generation);
    }
    poppedNv12Slot = nv12Slot;
    poppedNv12Generation = nv12Generation;
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
    ++captureReadbackSamples;
    captureD3DWaitSumUs += captureD3DWaitUs;
    captureD3DWaitMaxUs = std::max(captureD3DWaitMaxUs, captureD3DWaitUs);
    captureCopyMapSumUs += captureCopyMapUs;
    captureCopyMapMaxUs = std::max(captureCopyMapMaxUs, captureCopyMapUs);
    captureMemcpySumUs += captureMemcpyUs;
    captureMemcpyMaxUs = std::max(captureMemcpyMaxUs, captureMemcpyUs);
    captureUnmapWaitSumUs += captureUnmapWaitUs;
    captureUnmapWaitMaxUs = std::max(captureUnmapWaitMaxUs, captureUnmapWaitUs);
    captureUnmapSumUs += captureUnmapUs;
    captureUnmapMaxUs = std::max(captureUnmapMaxUs, captureUnmapUs);
    const uint64_t queueWaitUs =
        (queuePopUs > 0 && queuePushUs > 0 && queuePopUs >= queuePushUs) ? (queuePopUs - queuePushUs) : 0;
    const uint64_t queueGapFrames =
        (lastVersionSent > 0 && version > lastVersionSent) ? (version - lastVersionSent - 1) : 0;
    ++queuePopCount;
    const uint64_t lastPopVersionAtRead = lastPopFrameVersion.load(std::memory_order_acquire);
    const uint64_t queueDepthAtPop = (version > lastPopVersionAtRead) ? (version - lastPopVersionAtRead) : 0;
    update_u64_max(queueDepthMax, queueDepthAtPop);
    lastPopFrameVersion.store(version, std::memory_order_release);
    if (!servedBootstrap && frameGatingEnabled && useH264 && payload && !payload->empty()) {
      if (frameGatingRefPayload && !frameGatingRefPayload->empty() &&
          frameGatingRefW == w && frameGatingRefH == h && frameGatingRefStride == stride) {
        frameGatingChangePermilleLast = estimate_bgra_change_permille(
            payload->data(), frameGatingRefPayload->data(), payload->size(), frameGatingSampleTarget);
        frameGatingChangePermilleSum += frameGatingChangePermilleLast;
        ++frameGatingChangePermilleCount;

        if (frameGatingChangePermilleLast == 0) {
          frameGatingStaticStreak = std::min<uint32_t>(frameGatingStaticStreak + 1, 60000);
          frameGatingMotionStreak = 0;
        } else {
          frameGatingMotionStreak = std::min<uint32_t>(frameGatingMotionStreak + 1, 60000);
          frameGatingStaticStreak = 0;
        }
      } else {
        frameGatingStaticStreak = 0;
        frameGatingMotionStreak = 0;
        frameGatingChangePermilleLast = 1000;
      }

      const bool prevStaticMode = frameGatingStaticMode;
      // Any difference at all counts as motion. estimate_bgra_change_permille returns 0 only
      // for a byte-identical frame, so this both leaves static mode on the first changed
      // frame and never throttles an edit that is too small to move a percentage threshold.
      const bool motionNow = frameGatingChangePermilleLast > 0;
      if (!frameGatingStaticMode && frameGatingStaticStreak >= frameGatingEnterFrames) {
        frameGatingStaticMode = true;
      } else if (frameGatingStaticMode &&
                 (motionNow || frameGatingMotionStreak >= frameGatingExitFrames)) {
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
      // The static interval throttles idle scenes; it must never hold back a frame that
      // actually changed, or the first interaction after idle arrives late.
      // In paced motion mode the main tick already enforces activeFrameIntervalUs. Applying
      // the same interval here a second time makes a slightly-early capture timestamp skip
      // the entire tick (measured 1-6 lost frames/s at 60fps). Keep this limiter only for
      // static throttling or the explicitly unpaced throughput path.
      const bool needsGatingRateLimit = frameGatingStaticMode || !paceByTick;
      if (needsGatingRateLimit && !keyReqPending && !motionNow &&
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
    if (!servedBootstrap) {
      if (lastVersionSent > 0 && version > lastVersionSent + 1) {
        skippedByOverwrite += (version - lastVersionSent - 1);
      }
      lastVersionSent = version;
    }
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
                    << " captureD3DWaitUs=" << captureD3DWaitUs
                    << " captureCopyMapUs=" << captureCopyMapUs
                    << " captureMemcpyUs=" << captureMemcpyUs
                    << " captureUnmapWaitUs=" << captureUnmapWaitUs
                    << " captureUnmapUs=" << captureUnmapUs
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
                  << " captureD3DWaitUs=" << captureD3DWaitUs
                  << " captureCopyMapUs=" << captureCopyMapUs
                  << " captureMemcpyUs=" << captureMemcpyUs
                  << " captureUnmapWaitUs=" << captureUnmapWaitUs
                  << " captureUnmapUs=" << captureUnmapUs
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
      D3DReadbackTiming scaleReadbackTiming{};
      uint64_t preEncodePrepUs = 0;
      uint64_t scaleUs = 0;
      uint64_t nv12Us = 0;
      const uint64_t preEncodeStartUs = qpc_now_us();
      // A window selection or a resize changes the source geometry; re-fit the encode size
      // to the new aspect so the scaler never has to stretch. The source size changes on
      // EVERY frame of an interactive window drag, and apply_encoder_target tears the MFT
      // down, so two guards keep this from thrashing: the geometry must hold steady for a
      // settle period, and near-identical aspect (letterboxing under 2%) is left alone.
      if (!servedBootstrap && w > 0 && h > 0 && (w != encodeSourceW || h != encodeSourceH)) {
        const uint64_t nowRefitUs = qpc_now_us();
        if (w != pendingRefitW || h != pendingRefitH) {
          pendingRefitW = w;
          pendingRefitH = h;
          pendingRefitSinceUs = nowRefitUs;
        } else if (nowRefitUs - pendingRefitSinceUs >= kEncodeRefitSettleUs) {
          uint32_t refitW = activeEncodeW;
          uint32_t refitH = activeEncodeH;
          fit_size_preserving_aspect(w, h, nominalEncodeW, nominalEncodeH, &refitW, &refitH);
          const double activeAspect =
              static_cast<double>(activeEncodeW) / static_cast<double>(std::max(1u, activeEncodeH));
          const double refitAspect =
              static_cast<double>(refitW) / static_cast<double>(std::max(1u, refitH));
          const bool aspectClose =
              std::abs(refitAspect - activeAspect) <= activeAspect * 0.02;
          encodeSourceW = w;
          encodeSourceH = h;
          if ((refitW != activeEncodeW || refitH != activeEncodeH) && !aspectClose) {
            const uint32_t prevW = activeEncodeW;
            const uint32_t prevH = activeEncodeH;
            const uint32_t keepNominalW = nominalEncodeW;
            const uint32_t keepNominalH = nominalEncodeH;
            if (apply_encoder_target(keepNominalW, keepNominalH, activeFps, activeBitrate,
                                     activeKeyint)) {
              forceKeyNext = true;
              std::cout << "[native-video-host] encode-refit source=" << w << "x" << h
                        << " encode=" << prevW << "x" << prevH << " -> " << activeEncodeW << "x"
                        << activeEncodeH << "\n";
            } else {
              // apply_encoder_target already shut the encoder down; without a working encoder
              // every later frame fails silently, so treat this like the other callers do.
              std::cerr << "[native-video-host] encode-refit failed source=" << w << "x" << h
                        << "; stopping stream\n";
              break;
            }
          }
        }
      } else {
        pendingRefitW = 0;
        pendingRefitH = 0;
      }
      const bool wantSurfaceEncode = useH264 && nv12Slot >= 0 && surfaceEncodeHealthy &&
                                     nv12W == activeEncodeW && nv12H == activeEncodeH;
      if (!wantSurfaceEncode && (activeEncodeW != w || activeEncodeH != h)) {
        const uint64_t scaleStartUs = qpc_now_us();
        bool scaleOk = false;
        if (gpuScalerHealthy) {
          ++gpuScaleAttempts;
          scaleOk = gpuScaler.scale(payload->data(), w, h, stride, activeEncodeW, activeEncodeH,
                                    &scaledBgra, &scaleReadbackTiming);
          if (scaleOk) {
            ++gpuScaleSuccess;
            ++gpuScaleTimedCount;
            gpuScaleD3DWaitSumUs += scaleReadbackTiming.d3dWaitUs;
            gpuScaleD3DWaitMaxUs = std::max(gpuScaleD3DWaitMaxUs, scaleReadbackTiming.d3dWaitUs);
            gpuScaleCopyMapSumUs += scaleReadbackTiming.copyMapUs;
            gpuScaleCopyMapMaxUs = std::max(gpuScaleCopyMapMaxUs, scaleReadbackTiming.copyMapUs);
            gpuScaleMemcpySumUs += scaleReadbackTiming.memcpyUs;
            gpuScaleMemcpyMaxUs = std::max(gpuScaleMemcpyMaxUs, scaleReadbackTiming.memcpyUs);
            gpuScaleUnmapWaitSumUs += scaleReadbackTiming.unmapWaitUs;
            gpuScaleUnmapWaitMaxUs = std::max(gpuScaleUnmapWaitMaxUs, scaleReadbackTiming.unmapWaitUs);
            gpuScaleUnmapSumUs += scaleReadbackTiming.unmapUs;
            gpuScaleUnmapMaxUs = std::max(gpuScaleUnmapMaxUs, scaleReadbackTiming.unmapUs);
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

      const uint64_t prepDoneUs = qpc_now_us();
      preEncodePrepUs = (prepDoneUs >= preEncodeStartUs) ? (prepDoneUs - preEncodeStartUs) : 0;

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
      if (senderRequestKey.exchange(false, std::memory_order_acq_rel)) {
        // The sender dropped a backlog; the stream needs an IDR to resynchronize.
        forceKeyNext = true;
      }
       // The keyint schedule applies to REAL frames only. A kick/refresh-served synthetic frame
       // carries seq=0, and 0 % keyint == 0 made every one of them an IDR -- defeating the open-
       // barrier design of riding the held frame as a cheap P-frame (a 40-160KB IDR instead of a
       // few-KB P, once per kick/refresh). A closed barrier still gets its IDR via forceKeyNext.
       // A single submit latch (forceKeySubmittedAtUs) covers ALL key reasons -- request,
       // first-frame (encodedSeq==0), and the keyint schedule: one key input pending inside the
       // async MFT satisfies every one of them, so none may re-force while it is in flight. The
       // measured 4-5 consecutive-IDR trains came from forcing every input until the key finally
       // surfaced. The latch is stamped only after the encoder ACCEPTS the input (below), and
       // times out after 300ms so a lost key is retried.
        const uint64_t encodeStartUs = qpc_now_us();
       const bool forceKeyInFlight =
           forceKeySubmittedAtUs != 0 && encodeStartUs < forceKeySubmittedAtUs + 300'000;
       const bool scheduledKey =
           !servedBootstrap && (activeKeyint > 0) && ((seq % activeKeyint) == 0);
       const bool keyWanted = forceKeyNext || (encodedSeq == 0) || scheduledKey;
       const bool forceKeyFrame = keyWanted && !forceKeyInFlight;
        const uint64_t encodeInputUs = captureStampUs;
        if (captureTimelineOriginUs < 0) {
          captureTimelineOriginUs = static_cast<int64_t>(encodeInputUs);
        }
        const uint64_t queueToEncodeUs = (encodeStartUs >= queuePopUs) ? (encodeStartUs - queuePopUs) : 0;
       const uint64_t callbackToEncodeStartUs =
            (encodeStartUs >= callbackUs) ? (encodeStartUs - callbackUs) : 0;
        std::vector<H264AccessUnit> units;
        H264EncodeFrameStats encodeStats{};
        bool surfaceEncoded = false;
        // The MFT encode is the prime suspect for a driver/GPU wedge that stops the whole loop
        // without returning; mark the phase so the watchdog attributes a hang here correctly.
        enter_main_phase(MainLoopPhase::EncodeCall);
        if (wantSurfaceEncode) {
          auto nv12Tex = captureReadback.Nv12SlotTexture(nv12Slot, nv12Generation);
          if (nv12Tex &&
              encoder.encode_frame_surface(nv12Tex.Get(), forceKeyFrame,
                                           static_cast<int64_t>(encodeInputUs) * 10, &units,
                                           &encodeStats)) {
            surfaceEncoded = true;
            ++nv12SurfaceEncodeCount;
            Nv12PendingRelease pending;
            pending.slot = nv12Slot;
            pending.generation = nv12Generation;
            pending.requiredOutputs = encoderOutputSamplesTotal + 1;
            nv12PendingReleases.push_back(pending);
            poppedNv12Slot = -1;  // ownership moved to the deferred-release queue
            // Accepting a DXGI sample is no proof the vendor path is fast: AMF accepts them
            // and then takes ~68ms a frame on internal synchronization (measured; the CPU
            // path runs 4.5ms). Probe the first frames and drop back for the session when
            // the surface path costs more than half the 33ms frame budget on average.
            surfaceEncodeProbeSumUs += encodeStats.encodeCallUs;
            if (++surfaceEncodeProbeCount == 30) {
              const uint64_t avgUs = surfaceEncodeProbeSumUs / surfaceEncodeProbeCount;
              if (avgUs > 16000) {
                surfaceEncodeHealthy = false;
                captureReadback.SetNv12Enabled(false);
                std::cout << "[native-video-host] nv12 surface encode too slow avgUs=" << avgUs
                          << " backend=" << encoder.backend_name()
                          << "; reverting to cpu nv12\n";
              } else {
                std::cout << "[native-video-host] nv12 surface encode probe ok avgUs=" << avgUs
                          << " backend=" << encoder.backend_name() << "\n";
              }
              surfaceEncodeProbeCount = 0;
              surfaceEncodeProbeSumUs = 0;
            }
          } else {
            // One rejection turns the path off for the session; this frame is dropped and
            // the next one takes the CPU route. Its slot is released at the next loop top.
            surfaceEncodeHealthy = false;
            captureReadback.SetNv12Enabled(false);
            std::cout << "[native-video-host] nv12 surface encode rejected backend="
                      << encoder.backend_name() << "; falling back to cpu nv12\n";
            continue;
          }
        }
       if (!surfaceEncoded &&
           !encoder.encode_frame_bgra(encodeSrc, encodeSrcW, encodeSrcH, encodeSrcStride,
                                      forceKeyFrame, static_cast<int64_t>(encodeInputUs) * 10,
                                      &units, &encodeStats)) {
        ++encodeFailCount;
        if ((encodeFailCount % 60) == 1) {
          std::cout << "[native-video-host] encode failed count=" << encodeFailCount << "\n";
        }
        continue;
      }
      // Encode returned; back to ordinary work for the watchdog's threshold.
      enter_main_phase(MainLoopPhase::Loop);
      if (forceKeyFrame) {
        // Latch/count only for inputs the encoder actually ACCEPTED: a failed encode never
        // reached the MFT, and arming the latch for it would suppress the retry for 300ms.
        ++forceKeyInputCount;
        forceKeySubmittedAtUs = encodeStartUs;
      }
      if (!surfaceEncoded) {
        nv12Us = encodeStats.colorConvertUs;
        preEncodePrepUs += nv12Us;
      }
      encoderOutputSamplesTotal += encodeStats.processOutputSamples;
      if (!servedBootstrap) {
        // A real frame was just handed to the async MFT; it becomes the encoder's held input until
        // the next frame arrives. Record its capture timestamp and (re)arm the trailing kick so the
        // deadline always trails the LAST real input -- continuous motion keeps pushing it out and
        // adds zero synthetic frames; only a genuine pause lets the kick fire to flush this frame.
        lastRealInputCaptureUs = encodeInputUs;
        arm_trailing_kick(qpc_now_us());
      }
      while (!nv12PendingReleases.empty() &&
             nv12PendingReleases.front().requiredOutputs <= encoderOutputSamplesTotal) {
        captureReadback.ReleaseNv12Slot(nv12PendingReleases.front().slot,
                                        nv12PendingReleases.front().generation);
        nv12PendingReleases.pop_front();
      }
      const uint64_t encodeEndUs = qpc_now_us();

      // Encoder output-liveness heartbeat. Placed BEFORE the units.empty() early-out below so a
      // starved encoder -- which returns empty on every call -- is still observed here; the old
      // `continue` skipped the whole 1s stats / self-heal tail, so a wedge produced no telemetry at
      // all. A frame was just handed to the MFT this call, so input is advancing; only the OUTPUT is
      // in question. This block changes no control flow (diagnostic only).
      ++encoderInputAcceptedTotal;
      if (servedBootstrap) {
        ++encoderSyntheticInputAccepted;
      } else {
        ++encoderRealInputAccepted;
      }
      if (encodeStats.processOutputSamples > 0) {
        encoderOutputAuTotal += encodeStats.processOutputSamples;
        lastEncoderOutputUs = encodeEndUs;
        encoderNoOutputSinceUs = 0;
        encoderAcceptedNoOutputStreak = 0;
        // Reset the episode so the next starvation logs its first line immediately, and clear the
        // per-streak async accumulators.
        lastEncoderStarvationLogUs = 0;
        starveNeedInputAccum = starveHaveOutputAccum = starveNoEventAccum = 0;
        starveNotAcceptingAccum = starveNeedMoreAccum = starveNeedInputOnlyCalls = 0;
        // Revive mainLoopLastSeq (previously declared but never stored, so the watchdog record read
        // a constant 0): publish real encoder-output progress, not loop iterations. A follow-up can
        // make the watchdog fire on this age while input is still being accepted.
        mainLoopLastSeq.store(encoderOutputSamplesTotal, std::memory_order_release);
      } else {
        ++encoderAcceptedNoOutputStreak;
        if (encoderNoOutputSinceUs == 0) encoderNoOutputSinceUs = encodeEndUs;
        starveNeedInputAccum += encodeStats.asyncPollNeedInputCount;
        starveHaveOutputAccum += encodeStats.asyncPollHaveOutputCount;
        starveNoEventAccum += encodeStats.asyncPollNoEventCount;
        starveNotAcceptingAccum += encodeStats.processInputNotAcceptingCount;
        starveNeedMoreAccum += encodeStats.processOutputNeedMoreInputCount;
        starveNeedInputOnlyCalls += encodeStats.asyncNeedInputOnlyCall;
        // Age is measured from when the streak began, NOT from lastEncoderOutputUs, so an encoder
        // that never emitted a single AU since startup (lastEncoderOutputUs==0) is still detected.
        const uint64_t noOutputAgeUs =
            (encoderNoOutputSinceUs > 0 && encodeEndUs > encoderNoOutputSinceUs)
                ? (encodeEndUs - encoderNoOutputSinceUs)
                : 0;
        // Stream active + encoder keeps accepting input but produces no output for a while = the
        // async-MFT output-starvation wedge (video frozen, main loop spinning, liveness watchdog
        // green). Emit one rate-limited anomaly line with the streak-accumulated async counters so a
        // field recurrence tells a host event-driving bug (NeedInput accrues, HaveOutput stays 0)
        // from a genuine vendor/hardware stall. Recovery is a separate follow-up; diagnostic only.
        if (streamControlActive.load(std::memory_order_acquire) &&
            encoderAcceptedNoOutputStreak >= 8 && noOutputAgeUs >= 1000000ULL &&
            (lastEncoderStarvationLogUs == 0 ||
             encodeEndUs >= lastEncoderStarvationLogUs + 1000000ULL)) {
          lastEncoderStarvationLogUs = encodeEndUs;
          std::cout << "[native-video-host] encoder-output-starvation"
                    << " acceptedNoOutputStreak=" << encoderAcceptedNoOutputStreak
                    << " noOutputAgeUs=" << noOutputAgeUs
                    << " everOutput=" << (lastEncoderOutputUs > 0 ? 1 : 0)
                    << " realIn=" << encoderRealInputAccepted
                    << " synthIn=" << encoderSyntheticInputAccepted
                    << " outAu=" << encoderOutputAuTotal
                    << " asyncEnabled=" << static_cast<unsigned>(encodeStats.asyncEnabled)
                    << " streakNeedInput=" << starveNeedInputAccum
                    << " streakHaveOutput=" << starveHaveOutputAccum
                    << " streakNeedInputOnlyCalls=" << starveNeedInputOnlyCalls
                    << " streakNoEvent=" << starveNoEventAccum
                    << " streakNotAccepting=" << starveNotAcceptingAccum
                    << " streakNeedMore=" << starveNeedMoreAccum
                    << " pendingDepth=" << encodeStats.pendingInputDepth
                    << " pendingOverflow=" << encodeStats.pendingInputOverflowTotal
                    << "\n";
        }
      }

      if (units.empty()) continue;

      captureAgeSumUs += captureAgeAtCallbackUs;
      captureAgeMaxUs = std::max(captureAgeMaxUs, captureAgeAtCallbackUs);
      callbackToEncodeStartSumUs += callbackToEncodeStartUs;
      callbackToEncodeStartMaxUs = std::max(callbackToEncodeStartMaxUs, callbackToEncodeStartUs);

      bool encoderResetTriggered = false;
      bool sessionReconnectTriggered = false;
      bool countedRawForInput = false;
      if (senderSendFailed.exchange(false, std::memory_order_acq_rel)) {
        // Same policy the inline path had: a UDP send failure on an endless session waits
        // for the peer to re-Hello rather than exiting.
        ++udpTxFail;
        if (args.seconds == 0) {
          continue;
        }
      }
        // An async MFT can release several access units from one encode call. They are pushed
        // microseconds apart, so the sender thread has usually not been scheduled between them
        // and the queue depth reflects the burst rather than a backlogged wire. Counting that
        // as congestion discarded the whole GOP and forced an IDR on a perfectly healthy link.
        //
        // Judge congestion once, on the backlog that existed *before* this batch: that is the
        // only part of the queue the sender has genuinely failed to drain. Sizing the limit
        // from the batch instead would still overflow on the last unit whenever a frame was
        // already queued, and a large drain would authorise an equally large queue -- seconds
        // of latency -- so the absolute cap below bounds it regardless.
        constexpr size_t kSenderQueueMaxFrames = 6;
        size_t senderBacklogBeforeBatch = 0;
        {
          std::lock_guard<std::mutex> lk(senderMu);
          senderBacklogBeforeBatch = senderQueue.size();
        }
        const bool senderBacklogged = senderBacklogBeforeBatch >= 2;
        for (const auto& au : units) {
          if (au.bytes.empty()) continue;
          const int64_t auCaptureUs = (au.sampleTimeHns > 0) ? (au.sampleTimeHns / 10) : static_cast<int64_t>(encodeInputUs);
          // This AU carries the capture timestamp of the input frame it was produced from (the async
          // MFT preserves input sample times FIFO). Observing it is the proof a given real input has
          // finally come OUT of the encoder -- the cancel signal for the trailing kick. Track the
          // newest we have seen so a pending kick disarms once the latest real input has emerged.
          if (auCaptureUs > 0 && static_cast<uint64_t>(auCaptureUs) > lastEmittedAuCaptureUs) {
            lastEmittedAuCaptureUs = static_cast<uint64_t>(auCaptureUs);
          }
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
            reset_encoder_starvation_episode();
            // Same contract as the reinit sites above: the reset discarded any pending key input.
            forceKeySubmittedAtUs = 0;
            ++encoderResetCount;
            consecutiveStaleEncodedFrames = 0;
            forceKeyNext = true;
            encoderResetTriggered = true;
            break;
          }
          continue;
        }
        consecutiveStaleEncodedFrames = 0;

        // The requested IDR can be delayed behind older async MFT output. Only the AU's
        // actual CleanPoint/IDR state is safe to advertise as a keyframe.
        const bool encodedKeyFrame = au.keyFrame;
        // A barrier-opening kick (fresh viewer, no reference frames) must deliver a real IDR: a
        // non-IDR AU would decode into garbage. Drop anything but an IDR in that case. An ordinary
        // trailing-edge kick on an OPEN stream, however, is flushing out the last real held frame,
        // whose P-frame references the decoder already has -- so let it through.
        if (servedBootstrap && kickForcedKey && !encodedKeyFrame) {
          continue;
        }
        if (selectionFirstKeyframePendingGeneration != 0 &&
            streamGeneration == selectionFirstKeyframePendingGeneration &&
            !encodedKeyFrame) {
          ++selectionFirstKeyframeDropCount;
          if ((selectionFirstKeyframeDropCount % 30ULL) == 1ULL) {
            std::cout << "[native-video-host] selection generation waiting keyframe streamGen="
                      << streamGeneration
                      << " droppedAu=" << selectionFirstKeyframeDropCount
                      << " forceKeyNext=" << (forceKeyNext ? 1 : 0)
                      << "\n";
          }
          continue;
        }

        EncodedFrameHeader hdr{};
        hdr.header.magic = remote60::native_poc::kMagic;
        hdr.header.type = static_cast<uint16_t>(MessageType::EncodedFrameH264);
        hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
        hdr.seq = ++encodedSeq;
        hdr.width = activeEncodeW;
        hdr.height = activeEncodeH;
        hdr.payloadSize = static_cast<uint32_t>(au.bytes.size());
        hdr.flags = encodedKeyFrame ? 1u : 0u;
        hdr.streamGeneration = streamGeneration;
        hdr.captureQpcUs =
            static_cast<uint64_t>(std::max<int64_t>(0, auCaptureUs));
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
        bool enqueuedForSend = false;
        if (transport == VideoTransport::Tcp) {
          enqueuedForSend = true;
          sentOk = send_all_timed(clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                                  &sendPathStats.headerCallCount) &&
                   send_all_timed(clientSock, au.bytes.data(), au.bytes.size(), &sendPathStats.payloadUs,
                                 &sendPathStats.payloadCallCount);
        } else {
          if (!udpPeerReady) {
            ++udpTxNoPeer;
            sentOk = false;
          } else {
            EncodedSendItem item;
            item.keyFrame = (hdr.flags & 1u) != 0;
            item.frameIntervalUs = activeFrameIntervalUs;
            item.udpHdr.magic = remote60::native_poc::kMagic;
            item.udpHdr.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
            item.udpHdr.size = static_cast<uint16_t>(sizeof(item.udpHdr));
            item.udpHdr.seq = hdr.seq;
            item.udpHdr.codec = static_cast<uint16_t>(UdpCodec::H264);
            item.udpHdr.flags = (hdr.flags & 1u) ? 0x1u : 0u;
            item.udpHdr.width = hdr.width;
            item.udpHdr.height = hdr.height;
            item.udpHdr.stride = 0;
            item.udpHdr.payloadSize = hdr.payloadSize;
            item.udpHdr.streamGeneration = hdr.streamGeneration;
            item.udpHdr.captureQpcUs = hdr.captureQpcUs;
            item.udpHdr.encodeStartQpcUs = hdr.encodeStartQpcUs;
            item.udpHdr.encodeEndQpcUs = hdr.encodeEndQpcUs;
            item.udpHdr.sendQpcUs = hdr.sendQpcUs;  // sender restamps at wire time
            item.bytes = std::move(au.bytes);
            {
              std::lock_guard<std::mutex> lk(senderMu);
              // Stamp under the same lock the rollover bumps the epoch under, so the stamp is
              // consistent with the queue-clear: a delta stamped just after a rollover carries the
              // new epoch (and rides the fresh barrier); one stamped just before is dropped at
              // dequeue. This is also how the static bootstrap IDR gets tagged for the new epoch --
              // it flows through this same enqueue path and needs no special case.
              item.mediaEpoch = mediaSessionEpoch.load(std::memory_order_acquire);
              item.enqueueUs = qpc_now_us();  // AU handed to sender; sender derives queueWaitUs
              if (item.keyFrame) {
                // A new IDR makes every queued frame irrelevant and re-anchors the stream. This is
                // also the barrier-open point: a real (or bootstrap) key AU for the current epoch
                // clears senderWaitingForKey so deltas may flow again.
                senderDropCount.fetch_add(senderQueue.size(), std::memory_order_relaxed);
                senderHeldFrames += senderQueue.size();
                sentFrames -= std::min<uint64_t>(sentFrames, senderQueue.size());
                senderQueue.clear();
                senderWaitingForKey = false;
                if (firstKeyEnqueuedUs == 0) firstKeyEnqueuedUs = sendStartUs;
                senderQueue.push_back(std::move(item));
                enqueuedForSend = true;
              } else if (senderWaitingForKey) {
                // This delta references dropped frames; sending it would decode into
                // block garbage. Hold everything until the forced keyframe arrives.
                ++nonKeyAuWhileWaiting;
                senderDropCount.fetch_add(1, std::memory_order_relaxed);
                senderRequestKey.store(true, std::memory_order_release);
              } else if (senderBacklogged || senderQueue.size() >= kSenderQueueMaxFrames) {
                // Backlogged: drop the stale frames AND this delta -- it references what
                // was just dropped -- then resync with a fresh IDR.
                senderDropCount.fetch_add(senderQueue.size() + 1, std::memory_order_relaxed);
                // Frames already counted as sent are being erased here; move them to the held
                // tally so the reported wire rate does not include what never left.
                senderHeldFrames += senderQueue.size();
                sentFrames -= std::min<uint64_t>(sentFrames, senderQueue.size());
                senderQueue.clear();
                senderWaitingForKey = true;
                senderRequestKey.store(true, std::memory_order_release);
              } else {
                senderQueue.push_back(std::move(item));
                enqueuedForSend = true;
              }
            }
            if (enqueuedForSend) senderCv.notify_one();
            // Handing the frame off succeeded even when the queue policy discarded it; this
            // flag means "no transport failure", and clearing it here would tear the session
            // down. Whether the frame really went out is tracked by enqueuedForSend below.
            sentOk = true;
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
          if (selectionFirstKeyframePendingGeneration != 0 &&
              streamGeneration == selectionFirstKeyframePendingGeneration &&
              (hdr.flags & 1u) != 0) {
            std::cout << "[native-video-host] selection first keyframe sent streamGen="
                      << streamGeneration
                      << " captureQpcUs=" << hdr.captureQpcUs
                      << " sendQpcUs=" << hdr.sendQpcUs
                      << " key=1"
                      << "\n";
            selectionFirstKeyframePendingGeneration = 0;
            selectionFirstKeyframeDropCount = 0;
          }
          // UDP tx counters are owned by the sender thread now; nothing to count here.
          if (!servedBootstrap && frameGatingEnabled && enqueuedForSend && payload &&
              !payload->empty()) {
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

        // A frame the sender queue discarded never reaches the wire. Counting it kept fps and
        // bitrate reporting a healthy stream straight through a cutout, which is precisely the
        // window that is visible to the user as a freeze -- so count only what was handed on.
        if (transport == VideoTransport::Udp && !enqueuedForSend) {
          ++senderHeldFrames;
          continue;
        }
        // A trailing-edge kick is a single sparse frame; keep it out of the fps/bitrate and ABR
        // evidence (it is counted separately as trailingKickCount). It still consumes the forced
        // keyframe below so the normal path does not re-force one on the next real frame.
        if (!servedBootstrap) {
          ++sentFrames;
          ++encodedFrames;
          sentBytes += hdr.payloadSize;
          if (!countedRawForInput) {
            rawEquivalentBytes +=
                static_cast<uint64_t>(activeEncodeW) * static_cast<uint64_t>(activeEncodeH) * 3 / 2;
            countedRawForInput = true;
          }
        }
        if ((hdr.flags & 1u) != 0) {
          forceKeyNext = false;
          forceKeySubmittedAtUs = 0;
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
          const uint64_t encUs = (encodeSpanUs >= nv12Us) ? (encodeSpanUs - nv12Us) : 0;
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
                    << " captureD3DWaitUs=" << captureD3DWaitUs
                    << " captureCopyMapUs=" << captureCopyMapUs
                    << " captureMemcpyUs=" << captureMemcpyUs
                    << " captureUnmapWaitUs=" << captureUnmapWaitUs
                    << " captureUnmapUs=" << captureUnmapUs
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
                     << " scaleD3DWaitUs=" << scaleReadbackTiming.d3dWaitUs
                     << " scaleCopyMapUs=" << scaleReadbackTiming.copyMapUs
                     << " scaleMemcpyUs=" << scaleReadbackTiming.memcpyUs
                     << " scaleUnmapWaitUs=" << scaleReadbackTiming.unmapWaitUs
                     << " scaleUnmapUs=" << scaleReadbackTiming.unmapUs
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
        const uint64_t encUs = (encodeSpanUs >= nv12Us) ? (encodeSpanUs - nv12Us) : 0;
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
                    << " captureD3DWaitUs=" << captureD3DWaitUs
                    << " captureCopyMapUs=" << captureCopyMapUs
                    << " captureMemcpyUs=" << captureMemcpyUs
                    << " captureUnmapWaitUs=" << captureUnmapWaitUs
                    << " captureUnmapUs=" << captureUnmapUs
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
                     << " scaleD3DWaitUs=" << scaleReadbackTiming.d3dWaitUs
                     << " scaleCopyMapUs=" << scaleReadbackTiming.copyMapUs
                     << " scaleMemcpyUs=" << scaleReadbackTiming.memcpyUs
                     << " scaleUnmapWaitUs=" << scaleReadbackTiming.unmapWaitUs
                     << " scaleUnmapUs=" << scaleReadbackTiming.unmapUs
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
      ++statTicks;
      const bool statsPrintDue = (statTicks % statsPrintEverySec) == 0;
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
          (useH264 &&
           captureSessionReady.load(std::memory_order_acquire) &&
           streamControlActive.load(std::memory_order_acquire) &&
           callbackFramesPerSec == 0) ? 1ULL : 0ULL;
      idleHoldTotal += idleHoldPerSec;
      const bool gdiLowPushFallbackEnabled =
          !captureWindowModeActive && activeDesktopBackend == DesktopCaptureBackend::Gdi;
      if (useH264 &&
          captureSessionReady.load(std::memory_order_acquire) &&
          streamControlActive.load(std::memory_order_acquire) &&
          gdiLowPushFallbackEnabled) {
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
            const bool fallbackFromGdi =
                !captureWindowModeActive && activeDesktopBackend == DesktopCaptureBackend::Gdi;
            if (fallbackFromGdi) {
              activeDesktopBackend = DesktopCaptureBackend::Wgc;
              set_gdi_fallback_reason("gdi_low_capture_rate");
              std::cout << "[native-video-host] fallback_reason=gdi_low_capture_rate"
                        << " callbackFramesPerSec=" << callbackFramesPerSec
                        << " minPushPerSec=" << captureInputMinPushPerSec << "\n";
            }
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
              std::cout << "[native-video-host] capture session restarted reason="
                        << (fallbackFromGdi ? "gdi-low-push-fallback" : "capture-input-stall")
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
      // Readback-throughput soft watchdog (DXGI/WGC). Runs every stats tick on per-1s-window
      // deltas: the frozen-ring block above already accumulated this window's oldest-pending peak
      // at loop frequency. Gated to the same live desktop-capture surface the frozen-ring watchdog
      // uses, plus a warmup and a secure-desktop check, so a legitimately static desktop, a
      // just-restarted session, or a lock screen cannot trip it. This is the ONLY new rebuild
      // trigger; the frozen-ring 2s hard path and session rollover behavior are unchanged.
      {
        const bool drainStreamActive = streamControlActive.load(std::memory_order_acquire);
        if (drainStreamActive && !readbackDrainPrevStreamActive) {
          streamActiveSinceUs = t;  // client (re)attach edge; anchors the warmup below
        }
        readbackDrainPrevStreamActive = drainStreamActive;

        // Per-1s-window deltas. AcceptContentCount / BusyDrops / SupersededDrops are all lifetime
        // cumulative (superseded especially -- it is never reset), so diff, never read absolute.
        // The snapshots are advanced every tick regardless of whether the watchdog is eligible, so
        // an eligible second always sees exactly that second's increment.
        // AcceptContentCount is a plain uint64 the capture-callback thread mutates under
        // captureCadenceMu; snapshot it under the same lock. The watchdog now restarts capture on
        // this value, so an unlocked read is a real data race, not just a stale display. (BusyDrops
        // and SupersededDrops are std::atomic, so they need no lock.)
        uint64_t acceptedNow;
        {
          std::lock_guard<std::mutex> lk(captureCadenceMu);
          acceptedNow = captureCadenceGate.AcceptContentCount();
        }
        const uint64_t busyNow = captureReadback.BusyDrops();
        const uint64_t supersededNow = captureReadback.SupersededDrops();
        const uint64_t acceptedDelta =
            (acceptedNow >= readbackDrainPrevAccepted) ? (acceptedNow - readbackDrainPrevAccepted) : 0;
        const uint64_t busyDelta =
            (busyNow >= readbackDrainPrevBusyDrops) ? (busyNow - readbackDrainPrevBusyDrops) : 0;
        const uint64_t supersededDelta =
            (supersededNow >= readbackDrainPrevSuperseded) ? (supersededNow - readbackDrainPrevSuperseded) : 0;
        readbackDrainPrevAccepted = acceptedNow;
        readbackDrainPrevBusyDrops = busyNow;
        readbackDrainPrevSuperseded = supersededNow;
        // published = callbackFramesPerSec: the readback worker's publish count for this second,
        // already reset each tick, so it is a true per-window delta as-is.
        const uint64_t drainPendingPeakUs = readbackDrainOldestPendingPeakUs;
        readbackDrainOldestPendingPeakUs = 0;  // window closes here

        const bool drainSurfaceEligible =
            useH264 &&
            captureSessionReady.load(std::memory_order_acquire) &&
            drainStreamActive &&
            !captureWindowModeActive.load(std::memory_order_acquire) &&
            activeDesktopBackend != DesktopCaptureBackend::Gdi;
        // Warmup after the latest of: capture session start, any capture restart, or client
        // reattach -- so the first seconds of a fresh pipeline (encoder spin-up, first IDR) never
        // read as a drain.
        uint64_t drainWarmupAnchorUs = captureSessionStartedUs;
        if (lastCaptureRestartUs > drainWarmupAnchorUs) drainWarmupAnchorUs = lastCaptureRestartUs;
        if (streamActiveSinceUs > drainWarmupAnchorUs) drainWarmupAnchorUs = streamActiveSinceUs;
        const bool drainWarmupDone = (t >= drainWarmupAnchorUs + kReadbackDrainWarmupUs);
        // accepted >= max(5, fps/4): a static/quiet desktop accepts almost nothing (pointer-only
        // offers never advance this count), so it stays well below the floor and cannot trip.
        const uint32_t drainAcceptFloor =
            std::max<uint32_t>(5u, std::max<uint32_t>(1u, activeFps) / 4u);
        const uint64_t drainPublishCeil = std::max<uint64_t>(1u, acceptedDelta / 10u);
        // Cheap arithmetic first; the uncached secure-desktop syscall runs only when a stall is
        // already indicated, so the healthy path pays no per-second OpenInputDesktop cost.
        const bool drainMetricsStalled =
            drainSurfaceEligible && drainWarmupDone &&
            acceptedDelta >= drainAcceptFloor &&
            callbackFramesPerSec <= drainPublishCeil &&
            (drainPendingPeakUs >= kReadbackDrainPendingAgeUs ||
             (busyDelta + supersededDelta) >= kReadbackDrainDropBurstMin);
        const bool drainStarved =
            drainMetricsStalled && interactive_desktop_is_default_uncached();

        if (drainStarved) {
          ++readbackDrainConsecutiveSec;
        } else {
          readbackDrainConsecutiveSec = 0;
        }

        const bool drainRestartCooldownDone =
            (lastCaptureRestartUs == 0 ||
             t >= (lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
        if (readbackDrainConsecutiveSec >= kReadbackDrainConsecutiveSecMin && drainRestartCooldownDone) {
          readbackDrainConsecutiveSec = 0;
          // First trip: restart_capture_session() runs create_staging -> captureReadback
          // Shutdown/Initialize, rebuilding the capture backend and the readback ring on the same
          // device. A recurrence inside the same 60s window the frozen-ring refreeze uses means the
          // device itself is wedged; match that path and exit code 3 so the supervisor rebuilds the
          // process with a fresh D3D device.
          const bool drainRecurred =
              lastReadbackDrainRestartUs != 0 &&
              t < (lastReadbackDrainRestartUs + kCaptureFrozenEscalationWindowUs);
          lastReadbackDrainRestartUs = t;
          if (drainRecurred) {
            const uint64_t drainLastPubUs = lastPublishUs.load(std::memory_order_acquire);
            const uint64_t drainLastPubAgeUs =
                (drainLastPubUs > 0 && t > drainLastPubUs) ? t - drainLastPubUs : 0;
            std::cerr << "[native-video-host] capture readback drain recurred within "
                      << (kCaptureFrozenEscalationWindowUs / 1000000)
                      << "s acceptedDelta=" << acceptedDelta
                      << " published=" << callbackFramesPerSec
                      << "; exiting for a full process restart\n";
            std::cout << "[native-video-host] capture-recovery reason=readback-drain-recurrence"
                      << " action=process-restart exitCode=3"
                      << " acceptedDelta=" << acceptedDelta
                      << " published=" << callbackFramesPerSec
                      << " oldestPendingPeakUs=" << drainPendingPeakUs
                      << " busyDelta=" << busyDelta
                      << " supersededDelta=" << supersededDelta
                      << " readbackDrainRestarts=" << readbackDrainRestartCount
                      << " captureRestarts=" << captureRestartCount
                      << " lastPublishAgeUs=" << drainLastPubAgeUs
                      << " backend=" << desktop_capture_backend_name(activeDesktopBackend)
                      << " windowSec=" << (kCaptureFrozenEscalationWindowUs / 1000000)
                      << "\n";
            std::cout.flush();
            std::cerr.flush();
            return 3;
          }
          lastCaptureRestartUs = t;
          const bool restarted = restart_capture_session();
          if (restarted) {
            ++captureRestartCount;
            captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
            lastCaptureUsForInterval.store(0, std::memory_order_release);
            lastCallbackUs.store(0, std::memory_order_release);
            resetHostTimelineAnchors();
            forceKeyNext = true;
            ++captureDeadRestartCount;
            ++readbackDrainRestartCount;
            std::cout << "[native-video-host] capture session restarted reason=readback-drain count="
                      << captureRestartCount
                      << " captureDeadRestartCount=" << captureDeadRestartCount
                      << " readbackDrainRestarts=" << readbackDrainRestartCount
                      << " acceptedDelta=" << acceptedDelta
                      << " published=" << callbackFramesPerSec
                      << " oldestPendingPeakUs=" << drainPendingPeakUs
                      << " busyDelta=" << busyDelta
                      << " supersededDelta=" << supersededDelta
                      << "\n";
          } else {
            std::cerr << "[native-video-host] readback-drain restart failed acceptedDelta="
                      << acceptedDelta << " published=" << callbackFramesPerSec << "\n";
          }
        }
      }
      if (useRaw) {
        if (statsPrintDue) {
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
                  << " wgcContentSizeMismatchDrops=" << wgcContentSizeMismatchDrops.load(std::memory_order_relaxed)
                  << " wgcPoolRecreates=" << wgcPoolRecreates
                  << " captureWindowRebindCount=" << hostCaptureRebindCount.load(std::memory_order_relaxed)
                  << " captureTargetPid=" << hostCaptureTargetPid.load(std::memory_order_relaxed)
                  << " captureTargetProc=" << targetProcessName
                  << " captureTargetHwnd=0x" << std::hex
                  << hostCaptureTargetHwnd.load(std::memory_order_relaxed) << std::dec
                  << " inputEvents=" << inputEvents.load()
                  << " secureInputAttempts=" << secureInputAttempts.load()
                  << " secureInputDelivered=" << secureInputDelivered.load()
                  << " secureInputBrokerFailed=" << secureInputBrokerFailed.load()
                  << " secureInputSkipWindowMode=" << secureInputSkipWindowMode.load()
                  << " secureInputSkipUnauth=" << secureInputSkipUnauthenticated.load()
                  << " desktopPromo=" << desktopPromotionAttempts.load() << "/"
                  << desktopPromotionSuccess.load() << "/" << desktopPromotionFail.load()
                  << " desktopPromoDeferSecure=" << desktopPromotionDeferredSecureTotal.load()
                  << " desktopSecureProbeFalse=" << desktopSecureProbeFalseTotal.load()
                  << " lastPromoWaitUs=" << lastPromotionWaitUs.load()
                  << " inputIgnoredMove=" << inputIgnoredMove.load(std::memory_order_relaxed)
                  << " inputNoTarget=" << inputNoTarget.load(std::memory_order_relaxed)
                  << " inputUnsupported=" << inputUnsupported.load(std::memory_order_relaxed)
                  << " inputInjectFail=" << inputInjectFail.load(std::memory_order_relaxed)
                  << " inputFreshProbeSecure=" << inputFreshProbeSecure.load(std::memory_order_relaxed)
                  << " inputFreshProbeReroute=" << inputFreshProbeReroute.load(std::memory_order_relaxed)
                  << " inputInjectFailDefault=" << inputInjectFailDefault.load(std::memory_order_relaxed)
                  << " keyReqDropTotal=" << clientKeyFrameRequestDropped.load()
                  << " callbackFrames=" << callbackFramesPerSec
                  << " skippedByOverwrite=" << skippedByOverwrite
                  << " frameGatingMode=" << (frameGatingStaticMode ? "static" : "motion")
                  << " frameGatingSkips=" << frameGatingSkipCount
                  << " frameGatingStaticSkips=" << frameGatingStaticSkipCount
                  << " mbps=" << mbps
                  << " size=" << w << "x" << h
                  << "\n";
        }
      } else {
        const uint64_t capAgeAvgUs = (encodedFrames > 0) ? (captureAgeSumUs / encodedFrames) : 0;
        const uint64_t cb2eAvgUs = (encodedFrames > 0) ? (callbackToEncodeStartSumUs / encodedFrames) : 0;
        const uint64_t captureD3DWaitAvgUs =
            (captureReadbackSamples > 0) ? (captureD3DWaitSumUs / captureReadbackSamples) : 0;
        const uint64_t captureCopyMapAvgUs =
            (captureReadbackSamples > 0) ? (captureCopyMapSumUs / captureReadbackSamples) : 0;
        const uint64_t captureMemcpyAvgUs =
            (captureReadbackSamples > 0) ? (captureMemcpySumUs / captureReadbackSamples) : 0;
        const uint64_t captureUnmapWaitAvgUs =
            (captureReadbackSamples > 0) ? (captureUnmapWaitSumUs / captureReadbackSamples) : 0;
        const uint64_t captureUnmapAvgUs =
            (captureReadbackSamples > 0) ? (captureUnmapSumUs / captureReadbackSamples) : 0;
        const uint64_t gpuScaleD3DWaitAvgUs =
            (gpuScaleTimedCount > 0) ? (gpuScaleD3DWaitSumUs / gpuScaleTimedCount) : 0;
        const uint64_t gpuScaleCopyMapAvgUs =
            (gpuScaleTimedCount > 0) ? (gpuScaleCopyMapSumUs / gpuScaleTimedCount) : 0;
        const uint64_t gpuScaleMemcpyAvgUs =
            (gpuScaleTimedCount > 0) ? (gpuScaleMemcpySumUs / gpuScaleTimedCount) : 0;
        const uint64_t gpuScaleUnmapWaitAvgUs =
            (gpuScaleTimedCount > 0) ? (gpuScaleUnmapWaitSumUs / gpuScaleTimedCount) : 0;
        const uint64_t gpuScaleUnmapAvgUs =
            (gpuScaleTimedCount > 0) ? (gpuScaleUnmapSumUs / gpuScaleTimedCount) : 0;
        const uint64_t frameGatingChangeAvgPm =
            (frameGatingChangePermilleCount > 0)
                ? (frameGatingChangePermilleSum / frameGatingChangePermilleCount)
                : frameGatingChangePermilleLast;
        const double rawEquivMbps = (rawEquivalentBytes * 8.0) / (1000.0 * 1000.0);
        const uint64_t encRatioX100 =
            (sentBytes > 0) ? ((rawEquivalentBytes * 100ULL) / sentBytes) : 0;
        // The sender thread owns the UDP wire counters now.
        if (transport == VideoTransport::Udp) {
          udpTxFrames = senderTxFrames.load(std::memory_order_relaxed);
          udpTxChunks = senderTxChunks.load(std::memory_order_relaxed);
          udpTxBytes = senderTxBytes.load(std::memory_order_relaxed);
          udpTxNoPeer += senderTxNoPeer.exchange(0, std::memory_order_relaxed);
        }
        const uint64_t udpTxChunkPerFrameX100 =
            (udpTxFrames > 0) ? ((udpTxChunks * 100ULL) / udpTxFrames) : 0;
        const uint64_t senderSendCountNow = senderSendCount.load(std::memory_order_relaxed);
        const uint64_t senderSendDurAvgUs =
            (senderSendCountNow > 0)
                ? (senderSendDurSumUs.load(std::memory_order_relaxed) / senderSendCountNow)
                : 0;
        if (statsPrintDue) {
        // Age of the last frame published to the encoder -- diagnostic only. A frozen ring shows
        // this climbing in lockstep with oldestGpuPendingPeakUs. Per Codex: report it, but never
        // drive the watchdog off it, since a static change-driven desktop is legitimately silent.
        const uint64_t statsNowUs = qpc_now_us();
        const uint64_t lastPublishAtUs = lastPublishUs.load(std::memory_order_acquire);
        const uint64_t lastPublishAgeUs =
            (lastPublishAtUs > 0 && statsNowUs > lastPublishAtUs) ? statsNowUs - lastPublishAtUs : 0;
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
                  << " wgcContentSizeMismatchDrops=" << wgcContentSizeMismatchDrops.load(std::memory_order_relaxed)
                  << " wgcPoolRecreates=" << wgcPoolRecreates
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
                  << " secureInputAttempts=" << secureInputAttempts.load()
                  << " secureInputDelivered=" << secureInputDelivered.load()
                  << " secureInputBrokerFailed=" << secureInputBrokerFailed.load()
                  << " secureInputSkipWindowMode=" << secureInputSkipWindowMode.load()
                  << " secureInputSkipUnauth=" << secureInputSkipUnauthenticated.load()
                  << " desktopPromo=" << desktopPromotionAttempts.load() << "/"
                  << desktopPromotionSuccess.load() << "/" << desktopPromotionFail.load()
                  << " desktopPromoDeferSecure=" << desktopPromotionDeferredSecureTotal.load()
                  << " desktopSecureProbeFalse=" << desktopSecureProbeFalseTotal.load()
                  << " lastPromoWaitUs=" << lastPromotionWaitUs.load()
                  << " inputIgnoredMove=" << inputIgnoredMove.load(std::memory_order_relaxed)
                  << " inputNoTarget=" << inputNoTarget.load(std::memory_order_relaxed)
                  << " inputUnsupported=" << inputUnsupported.load(std::memory_order_relaxed)
                  << " inputInjectFail=" << inputInjectFail.load(std::memory_order_relaxed)
                  << " inputFreshProbeSecure=" << inputFreshProbeSecure.load(std::memory_order_relaxed)
                  << " inputFreshProbeReroute=" << inputFreshProbeReroute.load(std::memory_order_relaxed)
                  << " inputInjectFailDefault=" << inputInjectFailDefault.load(std::memory_order_relaxed)
                  << " capAgeAvgUs=" << capAgeAvgUs
                  << " capAgeMaxUs=" << captureAgeMaxUs
                  << " cb2eAvgUs=" << cb2eAvgUs
                  << " cb2eMaxUs=" << callbackToEncodeStartMaxUs
                  << " captureReadbackSamples=" << captureReadbackSamples
                  << " captureStagingBusyDrops=" << captureReadback.BusyDrops()
                  << " captureSupersededDrops=" << captureReadback.SupersededDrops()
                  << " captureCpuBufferReuse=" << captureReadback.BufferReuseCount()
                  << " capturePreprocessed=" << captureReadback.PreprocessCount()
                  << " capturePreprocessFallbacks=" << captureReadback.PreprocessFallbacks()
                  << " nv12Converted=" << captureReadback.Nv12Converted()
                  << " nv12RingBusy=" << captureReadback.Nv12RingBusy()
                  << " nv12SurfaceFrames=" << nv12SurfaceEncodeCount
                  << " captureD3DWaitAvgUs=" << captureD3DWaitAvgUs
                  << " captureD3DWaitMaxUs=" << captureD3DWaitMaxUs
                  << " captureCopyMapAvgUs=" << captureCopyMapAvgUs
                  << " captureCopyMapMaxUs=" << captureCopyMapMaxUs
                  << " captureMemcpyAvgUs=" << captureMemcpyAvgUs
                  << " captureMemcpyMaxUs=" << captureMemcpyMaxUs
                  << " captureUnmapWaitAvgUs=" << captureUnmapWaitAvgUs
                  << " captureUnmapWaitMaxUs=" << captureUnmapWaitMaxUs
                  << " oldestGpuPendingPeakUs=" << oldestGpuPendingPeakUs
                  << " oldestGpuPendingNowUs=" << captureReadback.OldestGpuPendingAgeUs()
                  << " gpuPendingCount=" << captureReadback.GpuPendingCount()
                  << " gpuPendingCountPeak=" << gpuPendingCountPeak
                  << " frozenRingRestarts=" << frozenRingRestartCount
                  << " readbackDrainRestarts=" << readbackDrainRestartCount
                  << " readbackDrainSec=" << readbackDrainConsecutiveSec
                  << " lastPublishAgeUs=" << lastPublishAgeUs
                  << " captureUnmapAvgUs=" << captureUnmapAvgUs
                  << " captureUnmapMaxUs=" << captureUnmapMaxUs
                  << " mbps=" << mbps
                  << " rawEquivMbps=" << rawEquivMbps
                  << " encRatioX100=" << encRatioX100
                  << " udpTxFrames=" << udpTxFrames
                  << " udpTxChunks=" << udpTxChunks
                  << " udpTxChunkPerFrameX100=" << udpTxChunkPerFrameX100
                  << " udpTxBytes=" << udpTxBytes
                  << " udpTxFail=" << udpTxFail
                  << " udpTxNoPeer=" << udpTxNoPeer
                  << " senderQueueDrops=" << senderDropCount.load(std::memory_order_relaxed)
                  // Frames the queue policy withheld: the direct measure of how long a viewer
                  // was looking at a frozen picture.
                  << " senderHeldFrames=" << senderHeldFrames
                  << " senderSendDurAvgUs=" << senderSendDurAvgUs
                  << " senderSendDurMaxUs=" << senderSendDurMaxUs.load(std::memory_order_relaxed)
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
                  << " gpuScaleTimedCount=" << gpuScaleTimedCount
                  << " gpuScaleD3DWaitAvgUs=" << gpuScaleD3DWaitAvgUs
                  << " gpuScaleD3DWaitMaxUs=" << gpuScaleD3DWaitMaxUs
                  << " gpuScaleCopyMapAvgUs=" << gpuScaleCopyMapAvgUs
                  << " gpuScaleCopyMapMaxUs=" << gpuScaleCopyMapMaxUs
                  << " gpuScaleMemcpyAvgUs=" << gpuScaleMemcpyAvgUs
                  << " gpuScaleMemcpyMaxUs=" << gpuScaleMemcpyMaxUs
                  << " gpuScaleUnmapWaitAvgUs=" << gpuScaleUnmapWaitAvgUs
                  << " gpuScaleUnmapWaitMaxUs=" << gpuScaleUnmapWaitMaxUs
                  << " gpuScaleUnmapAvgUs=" << gpuScaleUnmapAvgUs
                  << " gpuScaleUnmapMaxUs=" << gpuScaleUnmapMaxUs
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
                  << " captureOfferContent=" << captureCadenceGate.OfferContentCount()
                  << " captureOfferPointer=" << captureCadenceGate.OfferPointerCount()
                  << " captureGateDropContent=" << captureCadenceGate.GateDropContentCount()
                  << " captureGateDropPointer=" << captureCadenceGate.GateDropPointerCount()
                  << " trailingKickCount=" << trailingKickCount
                  << " staticRefreshCount=" << staticRefreshCount
                  << " lastKickSourceAgeUs=" << lastKickSourceAgeUs
                  << " mediaEpoch=" << mediaSessionEpoch.load(std::memory_order_acquire)
                  << " forceKeyInputCount=" << forceKeyInputCount
                  << " nonKeyAuWhileWaiting=" << nonKeyAuWhileWaiting
                  << " barrierRearm=" << barrierRearmCount.load(std::memory_order_relaxed)
                  << " firstKeyEnqueuedUs=" << firstKeyEnqueuedUs
                  << " firstKeyWireUs=" << senderFirstKeyWireUs.load(std::memory_order_relaxed)
                  << " lastKeyAuBytes=" << senderLastKeyAuBytes.load(std::memory_order_relaxed)
                  << " lastKeyAuChunks=" << senderLastKeyAuChunks.load(std::memory_order_relaxed)
                  << "\n";
        }

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
          // The current target, not the one the process started with. A runtime FPS tune moves
          // activeFps, and judging against the startup args.fps would compare the client's rate
          // to a target that no longer exists -- after a tune to 20, a healthy 20 fps reads as
          // 66% of 30 and trips a demote; after a tune to 60, a struggling 20 fps reads as fine.
          // ABR only runs when no manual override or M9 is lowering activeFps, so here it is the
          // authoritative target. All four thresholds and the sparse floor share it.
          const uint32_t abrExpectedFps = std::max<uint32_t>(1, activeFps);
          const uint32_t minGoodFpsX100 = abrExpectedFps * (abrQualityFirst ? 95u : 93u);
          const uint32_t minOkayFpsX100 = abrExpectedFps * (abrQualityFirst ? 90u : 85u);
          const uint32_t minDegradeFpsX100 = abrExpectedFps * (abrQualityFirst ? 55u : 45u);
          const uint32_t minSevereFpsX100 = abrExpectedFps * (abrQualityFirst ? 45u : 35u);
          const bool abrWarmupDone = (t >= (startUs + 4000000ULL));

          // A second in which the host offered almost no frames carries no usable evidence
          // either way. The client's relative-lag metric is a delay-variation estimate over
          // that second's samples, and 2-4 samples let a single outlier -- or the decoder
          // holding output across a sparse cadence -- read as latency the network never had.
          // A static desktop (frame gating) is the common case: the picture was still, the
          // client decoded a handful of frames, and the old code took that for congestion and
          // demoted, then recovered on motion, then demoted again -- the quality seen flapping
          // between sharp and soft while simply reading the screen. sentFrames is this tick's
          // real send cadence (reset each stats second), which is what the discarded
          // queuePushPerSec never was. When evidence is this thin, hold the profile and let a
          // second with real motion decide against the unchanged thresholds.
          const bool hostOfferSparse =
              (sentFrames < std::max<uint64_t>(2, static_cast<uint64_t>(abrExpectedFps) / 2)) ||
              frameGatingStaticMode;

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
          // !hostOfferSparse on every up/down verdict: a sparse second neither degrades nor
          // recovers the profile. The pressure and good counters below fall to their else
          // branch and reset, so the profile holds until a second with real cadence arrives.
          const bool severeDown =
              abrWarmupDone && !hostOfferSparse && (severeDownByClient || severeDownByHost);
          const bool moderateDown =
              abrWarmupDone && !hostOfferSparse && (moderateDownByClient || moderateDownByHost);
          const bool emergencyDown = abrWarmupDone && !hostOfferSparse && emergencyDownByClient;

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
              metricsFresh && !hostOfferSparse &&
              (clAvgLatencyUs < 90000ULL) &&
              (clAvgDecodeTailUs < 65000ULL) &&
              (clDecodedFpsX100 >= minOkayFpsX100);
          const bool goodForMidToHigh =
              metricsFresh && !hostOfferSparse &&
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
            uint32_t targetBitrate = abrHighBitrate;
            if (targetProfile == 1) {
              targetBitrate = abrMidBitrate;
            } else if (targetProfile == 2) {
              targetBitrate = abrLowBitrate;
            }
            // Derived at transition time, never read from the profile: frozen profile sizes
            // are the bug that put "profile=high encode=1256x706 bitrate=12000000" in a live
            // log. Deriving from the ladder also tracks capture-size changes (monitor
            // switches, RDP) that a frozen value never could. Runtime tuning does the same
            // already, and the hysteresis state is shared so the two cannot fight.
            const auto ladderChoice = remote60::native_poc::choose_abr_profile_size(
                targetProfile, targetBitrate, captureWidth, captureHeight, encodeLadderReduced);
            uint32_t targetW = ladderChoice.width;
            uint32_t targetH = ladderChoice.height;

            if (!apply_encoder_target(targetW, targetH, activeFps, targetBitrate, activeKeyint)) {
              std::cerr << "[native-video-host][abr] encoder profile apply failed\n";
              break;
            }
            // Committed only once the encoder accepted the target, so a failed reinit cannot
            // leave the hysteresis state describing an encoder that does not exist.
            encodeLadderReduced = ladderChoice.reduced;

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
      captureReadbackSamples = 0;
      captureD3DWaitSumUs = 0;
      captureD3DWaitMaxUs = 0;
      captureCopyMapSumUs = 0;
      captureCopyMapMaxUs = 0;
      captureMemcpySumUs = 0;
      captureMemcpyMaxUs = 0;
      captureUnmapWaitSumUs = 0;
      captureUnmapWaitMaxUs = 0;
      // The frozen-ring peaks must span the whole print interval, not a single tick. Everything
      // else here resets every second and is sampled once per print, but a freeze can spike in any
      // of the ~30 ticks between prints (statsPrintEverySec defaults to 30), so a per-second reset
      // would throw those windows away and the peak would only ever show the last second before a
      // print. Reset them only once the value has actually been printed. (Codex.)
      if (statsPrintDue) {
        oldestGpuPendingPeakUs = 0;
        gpuPendingCountPeak = 0;
        // Per print-interval rates: reset only once printed so they span the whole interval
        // (matching the peak resets above). firstKey*/lastKeyAu* are per media epoch and are
        // reset by the rollover transaction instead, so they persist across prints.
        forceKeyInputCount = 0;
        nonKeyAuWhileWaiting = 0;
      }
      captureUnmapSumUs = 0;
      captureUnmapMaxUs = 0;
      gpuScaleTimedCount = 0;
      gpuScaleD3DWaitSumUs = 0;
      gpuScaleD3DWaitMaxUs = 0;
      gpuScaleCopyMapSumUs = 0;
      gpuScaleCopyMapMaxUs = 0;
      gpuScaleMemcpySumUs = 0;
      gpuScaleMemcpyMaxUs = 0;
      gpuScaleUnmapWaitSumUs = 0;
      gpuScaleUnmapWaitMaxUs = 0;
      gpuScaleUnmapSumUs = 0;
      gpuScaleUnmapMaxUs = 0;
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
  // Close before joining: the control session is parked in a blocking read, and the reader
  // thread is parked in recvfrom until its receive timeout expires. The dispatcher now outlives
  // any one session, so it also has to be woken from the wait it parks in between them.
  udpControlChannel.Close(remote60::native_poc::ControlCloseReason::Shutdown);
  sessionEpochCv.notify_all();
  if (udpControlThread.joinable()) udpControlThread.join();
  if (udpReaderThread.joinable()) udpReaderThread.join();
  detach_capture_session();
  // Stop the readback worker while everything its publish callback touches is still alive;
  // relying on destructor order would tear down FrameState first.
  captureReadback.Shutdown();
  // The sender still holds clientSock; stop it before the socket closes.
  senderStop.store(true, std::memory_order_release);
  senderCv.notify_all();
  if (senderThread.joinable()) senderThread.join();
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
