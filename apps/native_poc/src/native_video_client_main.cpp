#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <imm.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <mfapi.h>
#include <wrl/client.h>

#include "client_macro_window.hpp"
#include "input_macro.hpp"
#include "mf_h264_codec.hpp"
#include "json_profile.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "Imm32.lib")

namespace {

using remote60::native_poc::ControlInputAckMessage;
using remote60::native_poc::ControlInputEventMessage;
using remote60::native_poc::ControlInputTextMessage;
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
using remote60::native_poc::ClientInputQueue;
using remote60::native_poc::CaptureModeRequestState;
using remote60::native_poc::ClientControlMetricsSnapshot;
using remote60::native_poc::ClientControlScheduler;
using remote60::native_poc::DecodedFrameNv12;
using remote60::native_poc::EncodedFrameHeader;
using remote60::native_poc::H264Decoder;
using remote60::native_poc::KeyframeRequestState;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::ControlOutboundAction;
using remote60::native_poc::ControlOutboundActionKind;
using remote60::native_poc::RawFrameHeader;
using remote60::native_poc::QueuedControlInputMessage;
using remote60::native_poc::RuntimeTuneState;
using remote60::native_poc::TcpControlResponse;
using remote60::native_poc::TcpControlResponseKind;
using remote60::native_poc::UdpH264AssemblyDisposition;
using remote60::native_poc::UdpH264FrameAssembler;
using remote60::native_poc::UdpCodec;
using remote60::native_poc::UdpHelloPacket;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;
using remote60::native_poc::VideoTransport;
using remote60::native_poc::WindowPanelSnapshot;
using remote60::native_poc::WindowPanelStateModel;
using remote60::native_poc::WindowTargetUiEntry;
using remote60::native_poc::nv12_to_bgra;
using remote60::native_poc::clamp_udp_mtu;
using remote60::native_poc::parse_video_transport;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::video_transport_name;
namespace json_profile = remote60::native_poc::json_profile;

#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT
#define REMOTE60_NATIVE_ENCODED_EXPERIMENT 0
#endif

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
  std::string host = "127.0.0.1";
  uint16_t port = 43000;
  uint16_t controlPort = 0;
  uint32_t controlIntervalMs = 1000;
  uint32_t tcpRecvBufKb = 0;
  uint32_t tcpSendBufKb = 0;
  uint32_t udpMtu = 1200;
  uint32_t traceEvery = 0;
  uint32_t traceMax = 0;
  std::string transport;
  std::string codec = "raw";
  uint32_t seconds = 0;  // 0: infinite
  uint32_t fpsHint = 30;
  bool enableInputChannel = false;
  uint32_t inputLogEvery = 120;
  uint32_t runtimeBitrate = 0;
  uint32_t runtimeKeyint = 0;
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

std::string fixed_cstr_to_string(const char* buf, size_t cap) {
  if (!buf || cap == 0) return std::string{};
  size_t n = 0;
  while (n < cap && buf[n] != '\0') ++n;
  return std::string(buf, buf + n);
}

std::wstring utf8_to_wide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring{};
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (n <= 1) return std::wstring{};
  std::wstring out(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
  return out;
}

// GDI defaults to the legacy System bitmap font, which is unscalable and cannot render
// non-Latin window titles. Everything drawn through draw_text_utf8 selects this instead.
HFONT gUiFont = nullptr;
HFONT gUiTitleFont = nullptr;
int gUiDpi = 96;

int dpi_scale(int value) { return MulDiv(value, gUiDpi, 96); }

void ensure_ui_font(HWND hwnd) {
  int dpi = 96;
  if (hwnd) {
    const UINT windowDpi = GetDpiForWindow(hwnd);
    if (windowDpi > 0) dpi = static_cast<int>(windowDpi);
  }
  if (gUiFont && dpi == gUiDpi) return;
  if (gUiFont) {
    DeleteObject(gUiFont);
    gUiFont = nullptr;
  }
  gUiDpi = dpi;
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(9, dpi, 72);
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
  std::wcscpy(lf.lfFaceName, L"Segoe UI");
  gUiFont = CreateFontIndirectW(&lf);
  if (gUiTitleFont) {
    DeleteObject(gUiTitleFont);
    gUiTitleFont = nullptr;
  }
  lf.lfHeight = -MulDiv(15, dpi, 72);
  lf.lfWeight = FW_SEMIBOLD;
  gUiTitleFont = CreateFontIndirectW(&lf);
}

// Paint-time solid brushes, cached by color. Cards used to create and destroy several
// brushes per paint, and the picker repaints on every thumbnail arrival. UI thread only.
std::unordered_map<COLORREF, HBRUSH>& brush_cache() {
  static std::unordered_map<COLORREF, HBRUSH> cache;
  return cache;
}

HBRUSH cached_brush(COLORREF color) {
  auto& cache = brush_cache();
  const auto it = cache.find(color);
  if (it != cache.end()) return it->second;
  HBRUSH brush = CreateSolidBrush(color);
  cache.emplace(color, brush);
  return brush;
}

void destroy_cached_gdi_objects() {
  for (auto& entry : brush_cache()) {
    DeleteObject(entry.second);
  }
  brush_cache().clear();
  if (gUiTitleFont) {
    DeleteObject(gUiTitleFont);
    gUiTitleFont = nullptr;
  }
}

void draw_text_utf8(HDC hdc, const std::string& text, RECT* rect, UINT format) {
  if (!rect) return;
  const std::wstring wide = utf8_to_wide(text);
  HGDIOBJ oldFont = gUiFont ? SelectObject(hdc, gUiFont) : nullptr;
  DrawTextW(hdc, wide.c_str(), static_cast<int>(wide.size()), rect, format);
  if (oldFont) SelectObject(hdc, oldFont);
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
      std::cerr << "[native-video-client] failed to load --config file: " << configPath
                << " (" << errorText << ")\n";
    } else {
      uint32_t v = 0;
      std::string s;
      bool b = false;
      if (json_profile::json_get_string(jsonText, "remoteHost", &s)) a.host = s;
      if (json_profile::json_get_string(jsonText, "host", &s)) a.host = s;
      if (json_profile::json_get_u32(jsonText, "port", &v)) {
        a.port = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
      }
      if (json_profile::json_get_u32(jsonText, "controlPort", &v)) {
        a.controlPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
      }
      if (json_profile::json_get_u32(jsonText, "controlIntervalMs", &v)) {
        a.controlIntervalMs = std::clamp<uint32_t>(v, 20, 10000);
      }
      if (json_profile::json_get_u32(jsonText, "tcpRecvBufKb", &v)) a.tcpRecvBufKb = v;
      if (json_profile::json_get_u32(jsonText, "tcpSendBufKb", &v)) a.tcpSendBufKb = v;
      if (json_profile::json_get_u32(jsonText, "udpMtu", &v)) a.udpMtu = clamp_udp_mtu(v);
      if (json_profile::json_get_u32(jsonText, "traceEvery", &v)) a.traceEvery = v;
      if (json_profile::json_get_u32(jsonText, "traceMax", &v)) a.traceMax = v;
      if (json_profile::json_get_string(jsonText, "codec", &s)) a.codec = s;
      if (json_profile::json_get_string(jsonText, "transport", &s)) a.transport = s;
      if (json_profile::json_get_u32(jsonText, "seconds", &v)) a.seconds = v;
      if (json_profile::json_get_u32(jsonText, "fpsHint", &v)) a.fpsHint = std::clamp<uint32_t>(v, 1, 120);
      if (json_profile::json_get_bool(jsonText, "noInputChannel", &b)) a.enableInputChannel = !b;
      if (json_profile::json_get_bool(jsonText, "enableInputChannel", &b)) a.enableInputChannel = b;
      if (json_profile::json_get_bool(jsonText, "enableInputInjection", &b)) a.enableInputChannel = b;
      if (json_profile::json_get_u32(jsonText, "inputLogEvery", &v)) {
        a.inputLogEvery = std::max<uint32_t>(1, v);
      }
      if (json_profile::json_get_u32(jsonText, "runtimeBitrate", &v)) {
        a.runtimeBitrate = std::max<uint32_t>(100000, v);
      }
      if (json_profile::json_get_u32(jsonText, "runtimeKeyint", &v)) {
        a.runtimeKeyint = std::max<uint32_t>(1, v);
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
    if (k == "--host" && i + 1 < argc) {
      a.host = argv[++i];
    } else if (k == "--port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.port = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--control-port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.controlPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--control-interval-ms" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.controlIntervalMs = std::clamp<uint32_t>(v, 20, 10000);
    } else if (k == "--tcp-recvbuf-kb" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.tcpRecvBufKb = v;
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
    } else if (k == "--codec" && i + 1 < argc) {
      a.codec = argv[++i];
    } else if (k == "--transport" && i + 1 < argc) {
      a.transport = argv[++i];
    } else if (k == "--seconds" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.seconds = v;
    } else if (k == "--fps-hint" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.fpsHint = std::clamp<uint32_t>(v, 1, 120);
    } else if (k == "--no-input-channel") {
      a.enableInputChannel = false;
    } else if (k == "--enable-input-channel") {
      a.enableInputChannel = true;
    } else if (k == "--enable-input-injection") {
      a.enableInputChannel = true;
    } else if (k == "--input-log-every" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.inputLogEvery = std::max<uint32_t>(1, v);
    } else if (k == "--runtime-bitrate" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.runtimeBitrate = std::max<uint32_t>(100000, v);
    } else if (k == "--runtime-keyint" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.runtimeKeyint = std::max<uint32_t>(1, v);
    }
  }
  return a;
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

struct SharedFrame {
  enum class PixelFormat : uint8_t {
    Unknown = 0,
    Bgra32 = 1,
    Nv12 = 2,
  };
  std::mutex mu;
  PixelFormat format = PixelFormat::Unknown;
  // Visible content size -- what aspect fit, input mapping, and rendering treat as the
  // picture. For H.264 this is the display aperture (1080), not the coded plane (1088).
  uint32_t width = 0;
  uint32_t height = 0;
  // Coded plane the byte buffer is actually laid out in, plus where the visible rect starts.
  uint32_t codedWidth = 0;
  uint32_t codedHeight = 0;
  uint32_t visibleLeft = 0;
  uint32_t visibleTop = 0;
  uint32_t stride = 0;
  uint32_t seq = 0;
  uint64_t captureUs = 0;
  uint64_t encodeStartUs = 0;
  uint64_t encodeEndUs = 0;
  uint64_t sendUs = 0;
  uint64_t recvUs = 0;
  uint64_t decodeStartUs = 0;
  uint64_t decodeEndUs = 0;
  uint64_t queueSetUs = 0;
  uint64_t decodeToQueueUs = 0;
  uint64_t streamGeneration = 0;
  uint64_t version = 0;
  std::shared_ptr<std::vector<uint8_t>> bytes;
  Microsoft::WRL::ComPtr<IMFSample> surfaceSample;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> surfaceTexture;
  uint32_t surfaceSubresource = 0;
};

SharedFrame gFrame;
std::atomic<bool> gRunning{true};
SOCKET gSock = INVALID_SOCKET;
HWND gHwnd = nullptr;
uint32_t gWindowW = 1600;
uint32_t gWindowH = 900;
std::atomic<bool> gPaintQueued{false};
std::atomic<uint32_t> gTraceEvery{0};
std::atomic<uint32_t> gTraceMax{0};
std::atomic<uint64_t> gTracePresentPrinted{0};
std::atomic<uint64_t> gTraceRecvPrinted{0};
constexpr bool kInputPolicyForceBlock = false;
// Catch-up defaults tuned for software codec path: avoid runaway multi-second lag,
// but still clamp perceived latency quickly for interactive remote use.
constexpr uint64_t kCatchupLagDropUs = 450000;       // 0.45s
constexpr uint64_t kCatchupResumeKeyLagUs = 500000;  // 0.5s
constexpr uint64_t kDecodeQueueLagDropUs = 300000;   // 0.3s
constexpr uint64_t kDecodeQueueLagResumeUs = 400000; // 0.4s
constexpr uint64_t kStaleCaptureDropUs = 50000;      // 50ms
constexpr uint64_t kUserFeedbackLagWarnUs = 90000;   // 90ms
constexpr uint64_t kUserFeedbackGapWarnUs = 50000;   // 50ms
constexpr uint64_t kUserFeedbackMinIntervalUs = 1000000;  // 1s
constexpr uint64_t kKeyframeRequestMinIntervalUsDefault = 120000;  // 120ms
constexpr uint64_t kKeyframeRequestTokenRefillUsDefault = 300000;  // 300ms / token
constexpr uint32_t kKeyframeRequestTokenCapacityDefault = 3;
constexpr uint64_t kCatchupReenterMinIntervalUsDefault = 600000;  // 600ms
constexpr uint64_t kCongestionRecoverMinUsDefault = 250000;  // 250ms
constexpr uint64_t kCongestionRecoveryTimeoutUsDefault = 1500000;  // 1.5s

enum class ClientCongestionState : uint8_t {
  Normal = 0,
  Recovering = 1,
  Congested = 2,
};

const char* congestion_state_name(ClientCongestionState state) {
  switch (state) {
    case ClientCongestionState::Normal:
      return "normal";
    case ClientCongestionState::Recovering:
      return "recovering";
    case ClientCongestionState::Congested:
      return "congested";
    default:
      return "unknown";
  }
}

ClientInputQueue gInputQueueState;
std::atomic<bool> gInputEnabled{false};
std::atomic<uint16_t> gMouseButtons{0};
std::atomic<int32_t> gLastInputVideoX{0};
std::atomic<int32_t> gLastInputVideoY{0};
std::atomic<uint32_t> gSuppressedImeCharCount{0};

struct ClientRuntimeMetrics {
  std::atomic<uint32_t> seq{0};
  std::atomic<uint32_t> width{0};
  std::atomic<uint32_t> height{0};
  std::atomic<uint32_t> recvFpsX100{0};
  std::atomic<uint32_t> decodedFpsX100{0};
  std::atomic<uint32_t> recvMbpsX1000{0};
  std::atomic<uint32_t> skippedFrames{0};
  std::atomic<uint64_t> avgLatencyUs{0};
  std::atomic<uint64_t> maxLatencyUs{0};
  std::atomic<uint64_t> avgDecodeTailUs{0};
  std::atomic<uint64_t> maxDecodeTailUs{0};
  std::atomic<uint32_t> congestionState{0};
  std::atomic<uint32_t> congestionTransitions{0};
  std::atomic<uint32_t> congestionRecoveryCount{0};
  std::atomic<uint32_t> congestionRecoveryReq{0};
  std::atomic<uint32_t> congestionRecoveryMaxUs{0};
  std::atomic<uint32_t> queueDepthMax{0};
  std::atomic<uint32_t> queueDepthH4p{0};
  std::atomic<uint32_t> udpAssemblyDropPm{0};
  std::atomic<uint64_t> updatedQpcUs{0};
};

ClientRuntimeMetrics gClientMetrics;
KeyframeRequestState gKeyframeRequests{
    kKeyframeRequestMinIntervalUsDefault,
    kKeyframeRequestTokenRefillUsDefault,
    kKeyframeRequestTokenCapacityDefault};
std::atomic<uint64_t> gLastPresentedVersion{0};
std::atomic<uint64_t> gLastPresentedCaptureUs{0};  // updated after actual present, not at queue time
std::atomic<uint64_t> gPaintCoalescedCount{0};
std::atomic<uint64_t> gOverwriteBeforePresentCount{0};
std::atomic<uint64_t> gD3dPresentSuccessCount{0};
std::atomic<uint64_t> gD3dPresentFailCount{0};
std::atomic<uint64_t> gGdiFallbackPresentedCount{0};
std::atomic<uint64_t> gFallbackInitFailCount{0};
std::atomic<uint64_t> gFallbackRenderFailCount{0};
std::atomic<uint64_t> gFallbackNv12ConvertFailCount{0};
std::mutex gLogMu;

struct OverlayConfigSnapshot {
  std::string host = "127.0.0.1";
  uint16_t port = 43000;
  uint16_t controlPort = 0;
  std::string transport = "tcp";
  std::string codec = "raw";
  uint32_t fpsHint = 30;
  uint32_t controlIntervalMs = 1000;
  uint32_t tcpRecvBufKb = 0;
  uint32_t tcpSendBufKb = 0;
  uint32_t udpMtu = 1200;
  uint32_t udpSimDropPm = 0;
  uint64_t keyReqMinIntervalUs = kKeyframeRequestMinIntervalUsDefault;
  uint64_t keyReqTokenRefillUs = kKeyframeRequestTokenRefillUsDefault;
  uint32_t keyReqTokenCapacity = kKeyframeRequestTokenCapacityDefault;
};

OverlayConfigSnapshot gOverlayConfig;
std::atomic<bool> gControlConnected{false};
std::atomic<uint32_t> gHostCaptureTargetPid{0};
std::atomic<uint32_t> gHostCaptureTargetFlags{0};
std::atomic<uint32_t> gHostCaptureRebindCount{0};
std::atomic<uint64_t> gHostCaptureTargetHwnd{0};
std::atomic<uint64_t> gHostCaptureMetaUpdatedUs{0};
std::mutex gHostCaptureMetaMu;
std::string gHostCaptureTargetProcess = "monitor";
std::string gHostCaptureTargetTitle;
RuntimeTuneState gRuntimeTuneState{
    300000,
    30000000,
    250000,
    1,
    240};
std::atomic<bool> gCaptureOverviewMode{false};
remote60::native_poc::StreamStateControl gStreamStateControl;

// Browsing targets must not keep the host encoding (F1). The request rides the control
// scheduler, which orders stream state ahead of window selection. Sent only on explicit
// picker transitions: startup leaves the host's default-active stream alone, so headless
// harness clients that never open the picker keep receiving video unchanged.
void set_picker_visible_and_sync_stream(bool visible);
CaptureModeRequestState gCaptureModeRequests;
ClientControlScheduler gControlScheduler;

ClientControlMetricsSnapshot capture_client_control_metrics_snapshot() {
  ClientControlMetricsSnapshot snapshot{};
  snapshot.updatedQpcUs = gClientMetrics.updatedQpcUs.load(std::memory_order_relaxed);
  snapshot.message.width = gClientMetrics.width.load(std::memory_order_relaxed);
  snapshot.message.height = gClientMetrics.height.load(std::memory_order_relaxed);
  snapshot.message.recvFpsX100 = gClientMetrics.recvFpsX100.load(std::memory_order_relaxed);
  snapshot.message.decodedFpsX100 = gClientMetrics.decodedFpsX100.load(std::memory_order_relaxed);
  snapshot.message.recvMbpsX1000 = gClientMetrics.recvMbpsX1000.load(std::memory_order_relaxed);
  snapshot.message.skippedFrames = gClientMetrics.skippedFrames.load(std::memory_order_relaxed);
  snapshot.message.avgLatencyUs = gClientMetrics.avgLatencyUs.load(std::memory_order_relaxed);
  snapshot.message.maxLatencyUs = gClientMetrics.maxLatencyUs.load(std::memory_order_relaxed);
  snapshot.message.avgDecodeTailUs = gClientMetrics.avgDecodeTailUs.load(std::memory_order_relaxed);
  snapshot.message.maxDecodeTailUs = gClientMetrics.maxDecodeTailUs.load(std::memory_order_relaxed);
  snapshot.message.congestionState = gClientMetrics.congestionState.load(std::memory_order_relaxed);
  snapshot.message.congestionTransitions = gClientMetrics.congestionTransitions.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryCount =
      gClientMetrics.congestionRecoveryCount.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryReq =
      gClientMetrics.congestionRecoveryReq.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryMaxUs =
      gClientMetrics.congestionRecoveryMaxUs.load(std::memory_order_relaxed);
  snapshot.message.queueDepthMax = gClientMetrics.queueDepthMax.load(std::memory_order_relaxed);
  snapshot.message.queueDepthH4p = gClientMetrics.queueDepthH4p.load(std::memory_order_relaxed);
  snapshot.message.udpAssemblyDropPm = gClientMetrics.udpAssemblyDropPm.load(std::memory_order_relaxed);
  return snapshot;
}

struct OverlayMetricSample {
  uint64_t tsUs = 0;
  uint32_t recvFpsX100 = 0;
  uint32_t decodedFpsX100 = 0;
  uint32_t recvMbpsX1000 = 0;
  uint64_t avgLatencyUs = 0;
};

struct OverlayMetricAverages {
  uint32_t recvFpsX100 = 0;
  uint32_t decodedFpsX100 = 0;
  uint32_t recvMbpsX1000 = 0;
  uint64_t avgLatencyUs = 0;
  uint32_t sampleCount = 0;
};

std::mutex gOverlayMetricsMu;
std::deque<OverlayMetricSample> gOverlayMetrics;
void log_client_line(const std::string& line);

WindowPanelStateModel gWindowPanelState;
std::atomic<bool> gWindowPickerVisible{true};
std::atomic<bool> gWindowPickerToggleDown{false};
std::atomic<int> gGridScrollRow{0};  // card grid scroll, in whole rows

// Preview thumbnails for the target picker, fetched over the control channel when the host
// advertises kControlWindowListFlagThumbnails. Keyed by window id; id 0 is the desktop.
struct WindowThumb {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> bgra;
  uint64_t fetchedUs = 0;
};
std::mutex gThumbMu;
std::unordered_map<uint64_t, std::shared_ptr<const WindowThumb>> gThumbs;
std::deque<uint64_t> gThumbFetchQueue;
std::atomic<bool> gHostSupportsThumbnails{false};
constexpr uint64_t kThumbRefreshUs = 5000000;  // refresh a preview after 5 s

void queue_thumbnail_fetches_from_panel() {
  if (!gHostSupportsThumbnails.load(std::memory_order_relaxed)) return;
  const WindowPanelSnapshot snap = gWindowPanelState.Snapshot();
  const uint64_t nowUs = qpc_now_us();
  std::lock_guard<std::mutex> lk(gThumbMu);
  auto want = [&](uint64_t id) {
    const auto it = gThumbs.find(id);
    if (it != gThumbs.end() && it->second && nowUs - it->second->fetchedUs < kThumbRefreshUs) return;
    if (std::find(gThumbFetchQueue.begin(), gThumbFetchQueue.end(), id) != gThumbFetchQueue.end()) {
      return;
    }
    gThumbFetchQueue.push_back(id);
  };
  want(0);
  for (const auto& item : snap.items) want(item.id);
}
std::atomic<uint64_t> gSuppressMouseUntilUs{0};
std::atomic<uint32_t> gActiveTouchPointerId{0};
std::atomic<bool> gActiveTouchDown{false};

// Panel metrics are authored at 96 DPI and scaled per monitor; the process is
// per-monitor DPI aware, so raw pixel constants would render tiny on a scaled display.
inline int kPickerPanelPreferredWidth() { return dpi_scale(560); }
inline int kPickerPanelMinWidth() { return dpi_scale(420); }
inline int kPanelMargin() { return dpi_scale(12); }
inline int kPanelButtonHeight() { return dpi_scale(30); }
inline int kPanelButtonGap() { return dpi_scale(8); }
inline int kPanelSectionGap() { return dpi_scale(12); }
inline int kPanelInfoHeight() { return dpi_scale(64); }
inline int kPanelStatsHeight() { return dpi_scale(128); }
inline int kPanelItemHeight() { return dpi_scale(28); }
inline int kPanelItemGap() { return dpi_scale(4); }
constexpr uint32_t kRuntimeBitrateMin = 300000;
constexpr uint32_t kRuntimeBitrateMax = 30000000;
constexpr uint32_t kRuntimeBitrateStep = 250000;
constexpr uint32_t kRuntimeKeyintMin = 1;
constexpr uint32_t kRuntimeKeyintMax = 240;

struct ClientLayout {
  RECT clientRect{};
  RECT toggleButtonRect{};
  RECT macroButtonRect{};
  RECT panelRect{};
  RECT videoRect{};
  RECT refreshButtonRect{};
  RECT desktopButtonRect{};
  RECT selectedInfoRect{};
  RECT listRect{};
  RECT statsRect{};
};

RECT make_rect(int x, int y, int w, int h) {
  RECT r{};
  r.left = x;
  r.top = y;
  r.right = x + w;
  r.bottom = y + h;
  return r;
}

bool point_in_rect(const RECT& r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// Geometry of the card grid inside ClientLayout::listRect. Cards hold a 16:10 preview and a
// one-line caption, laid out left-to-right then top-to-bottom.
struct CardGridMetrics {
  int cols = 1;
  int cardW = 0;
  int cardH = 0;
  int thumbH = 0;
  int gap = 0;
  int visibleRows = 1;
  int visibleCards = 1;
};

CardGridMetrics compute_card_grid(const RECT& gridRect) {
  CardGridMetrics m;
  m.gap = dpi_scale(14);
  const int gridW = std::max<int>(1, gridRect.right - gridRect.left);
  const int gridH = std::max<int>(1, gridRect.bottom - gridRect.top);
  const int preferredCardW = dpi_scale(232);
  m.cols = std::max<int>(1, (gridW + m.gap) / (preferredCardW + m.gap));
  m.cardW = std::max<int>(dpi_scale(140), (gridW - (m.cols - 1) * m.gap) / m.cols);
  m.thumbH = (m.cardW * 10) / 16;
  m.cardH = m.thumbH + dpi_scale(30);
  m.visibleRows = std::max<int>(1, (gridH + m.gap) / (m.cardH + m.gap));
  m.visibleCards = m.visibleRows * m.cols;
  return m;
}

RECT card_rect_for_slot(const RECT& gridRect, const CardGridMetrics& m, int slot) {
  const int row = slot / m.cols;
  const int col = slot % m.cols;
  return make_rect(gridRect.left + col * (m.cardW + m.gap),
                   gridRect.top + row * (m.cardH + m.gap), m.cardW, m.cardH);
}


RECT aspect_fit_rect(const RECT& containerRect, uint32_t contentWidth, uint32_t contentHeight) {
  const int containerWidth =
      std::max<int>(1, static_cast<int>(containerRect.right - containerRect.left));
  const int containerHeight =
      std::max<int>(1, static_cast<int>(containerRect.bottom - containerRect.top));
  if (contentWidth == 0 || contentHeight == 0) {
    return containerRect;
  }

  const double containerAspect =
      static_cast<double>(containerWidth) / static_cast<double>(containerHeight);
  const double contentAspect =
      static_cast<double>(contentWidth) / static_cast<double>(contentHeight);
  int drawWidth = containerWidth;
  int drawHeight = containerHeight;
  if (contentAspect > containerAspect) {
    drawHeight = std::max<int>(1, static_cast<int>(std::lround(
        static_cast<double>(containerWidth) / contentAspect)));
  } else {
    drawWidth = std::max<int>(1, static_cast<int>(std::lround(
        static_cast<double>(containerHeight) * contentAspect)));
  }

  const int offsetX = (containerWidth - drawWidth) / 2;
  const int offsetY = (containerHeight - drawHeight) / 2;
  return make_rect(containerRect.left + offsetX, containerRect.top + offsetY, drawWidth, drawHeight);
}

bool resolve_active_video_content_size(uint32_t* outWidth, uint32_t* outHeight) {
  if (!outWidth || !outHeight) return false;
  *outWidth = 0;
  *outHeight = 0;

  const WindowPanelSnapshot panelSnapshot = gWindowPanelState.Snapshot();
  const uint32_t selectedWidth = panelSnapshot.selectedWidth;
  const uint32_t selectedHeight = panelSnapshot.selectedHeight;
  const uint64_t selectedStreamGeneration = panelSnapshot.lastSelectStreamGeneration;

  uint32_t frameWidth = 0;
  uint32_t frameHeight = 0;
  uint64_t frameStreamGeneration = 0;
  {
    std::lock_guard<std::mutex> lk(gFrame.mu);
    frameWidth = gFrame.width;
    frameHeight = gFrame.height;
    frameStreamGeneration = gFrame.streamGeneration;
  }

  if (selectedWidth > 0 && selectedHeight > 0) {
    if (frameWidth > 0 && frameHeight > 0 &&
        (selectedStreamGeneration == 0 || frameStreamGeneration == selectedStreamGeneration)) {
      *outWidth = frameWidth;
      *outHeight = frameHeight;
    } else {
      *outWidth = selectedWidth;
      *outHeight = selectedHeight;
    }
    return true;
  }

  const uint32_t metricWidth = gClientMetrics.width.load(std::memory_order_relaxed);
  const uint32_t metricHeight = gClientMetrics.height.load(std::memory_order_relaxed);
  if (metricWidth > 0 && metricHeight > 0) {
    *outWidth = metricWidth;
    *outHeight = metricHeight;
    return true;
  }

  if (frameWidth > 0 && frameHeight > 0) {
    *outWidth = frameWidth;
    *outHeight = frameHeight;
    return true;
  }
  return false;
}

RECT resolve_video_content_rect(HWND hwnd, const RECT& containerRect) {
  (void)hwnd;
  uint32_t contentWidth = 0;
  uint32_t contentHeight = 0;
  if (!resolve_active_video_content_size(&contentWidth, &contentHeight)) {
    return containerRect;
  }
  return aspect_fit_rect(containerRect, contentWidth, contentHeight);
}

ClientLayout compute_client_layout(HWND hwnd) {
  ClientLayout layout{};
  if (hwnd && IsWindow(hwnd)) {
    GetClientRect(hwnd, &layout.clientRect);
  } else {
    layout.clientRect = make_rect(0, 0, static_cast<int>(gWindowW), static_cast<int>(gWindowH));
  }
  const int clientW =
      std::max<int>(1, static_cast<int>(layout.clientRect.right - layout.clientRect.left));
  const int clientH =
      std::max<int>(1, static_cast<int>(layout.clientRect.bottom - layout.clientRect.top));
  layout.videoRect = make_rect(0, 0, clientW, clientH);

  if (!gWindowPickerVisible.load(std::memory_order_relaxed)) {
    layout.toggleButtonRect = make_rect(kPanelMargin(), kPanelMargin(), dpi_scale(120), kPanelButtonHeight());
    layout.macroButtonRect =
        make_rect(kPanelMargin() + dpi_scale(120) + kPanelButtonGap(), kPanelMargin(),
                  dpi_scale(90), kPanelButtonHeight());
    layout.panelRect = make_rect(0, 0, 0, 0);
    layout.refreshButtonRect = make_rect(0, 0, 0, 0);
    layout.desktopButtonRect = make_rect(0, 0, 0, 0);
    layout.selectedInfoRect = make_rect(0, 0, 0, 0);
    layout.listRect = make_rect(0, 0, 0, 0);
    layout.statsRect = make_rect(0, 0, 0, 0);
    return layout;
  }

  // The home screen owns the whole window: a header band with the actions, a card grid of
  // capture targets, and a one-line status footer.
  layout.panelRect = layout.clientRect;
  layout.toggleButtonRect = make_rect(0, 0, 0, 0);
  layout.macroButtonRect = make_rect(0, 0, 0, 0);

  const int margin = dpi_scale(24);
  const int headerH = dpi_scale(56);
  const int footerH = dpi_scale(36);
  const int buttonW = dpi_scale(130);

  layout.desktopButtonRect =
      make_rect(clientW - margin - buttonW, margin / 2 + (headerH - kPanelButtonHeight()) / 2,
                buttonW, kPanelButtonHeight());
  layout.refreshButtonRect =
      make_rect(layout.desktopButtonRect.left - kPanelButtonGap() - dpi_scale(96),
                layout.desktopButtonRect.top, dpi_scale(96), kPanelButtonHeight());
  layout.selectedInfoRect = make_rect(margin, margin / 2,
                                      std::max<int>(1, layout.refreshButtonRect.left - margin * 2),
                                      headerH);

  const int gridY = margin / 2 + headerH + dpi_scale(10);
  layout.listRect = make_rect(margin, gridY, std::max<int>(1, clientW - margin * 2),
                              std::max<int>(dpi_scale(120), clientH - gridY - footerH - dpi_scale(10)));
  layout.statsRect = make_rect(margin, layout.listRect.bottom + dpi_scale(6),
                               std::max<int>(1, clientW - margin * 2), footerH);
  return layout;
}

bool point_in_toggle_button(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.toggleButtonRect, x, y);
}

bool point_in_macro_button(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.macroButtonRect, x, y);
}

bool point_in_panel_ui(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.panelRect, x, y);
}

bool point_in_video_rect(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.videoRect, x, y);
}

bool map_client_point_to_video_coords(HWND hwnd, int x, int y, int32_t* outVideoX, int32_t* outVideoY) {
  if (!outVideoX || !outVideoY) return false;
  const ClientLayout layout = compute_client_layout(hwnd);
  const RECT contentRect = resolve_video_content_rect(hwnd, layout.videoRect);
  if (!point_in_rect(contentRect, x, y)) return false;
  uint32_t frameW = 0;
  uint32_t frameH = 0;
  if (!resolve_active_video_content_size(&frameW, &frameH)) return false;
  const int relX =
      std::clamp<int>(x - contentRect.left, 0,
                      std::max<int>(0, static_cast<int>(contentRect.right - contentRect.left - 1)));
  const int relY =
      std::clamp<int>(y - contentRect.top, 0,
                      std::max<int>(0, static_cast<int>(contentRect.bottom - contentRect.top - 1)));
  const int videoW = std::max<int>(1, static_cast<int>(contentRect.right - contentRect.left));
  const int videoH = std::max<int>(1, static_cast<int>(contentRect.bottom - contentRect.top));
  *outVideoX = static_cast<int32_t>((static_cast<uint64_t>(relX) * static_cast<uint64_t>(frameW - 1) +
                                     static_cast<uint64_t>(videoW / 2)) /
                                    static_cast<uint64_t>(videoW));
  *outVideoY = static_cast<int32_t>((static_cast<uint64_t>(relY) * static_cast<uint64_t>(frameH - 1) +
                                     static_cast<uint64_t>(videoH / 2)) /
                                    static_cast<uint64_t>(videoH));
  gLastInputVideoX.store(*outVideoX, std::memory_order_relaxed);
  gLastInputVideoY.store(*outVideoY, std::memory_order_relaxed);
  return true;
}

void enqueue_input_event(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode);
void enqueue_input_text_units(const uint16_t* text, size_t count);

void enqueue_control_input_message(const QueuedControlInputMessage& msg) {
  gInputQueueState.Enqueue(msg);
}

void enqueue_input_text_units(const uint16_t* text, size_t count) {
  if (kInputPolicyForceBlock) return;
  if (!gInputEnabled.load()) return;
  if (!text || count == 0) return;
  size_t offset = 0;
  while (offset < count) {
    const size_t remaining = count - offset;
    const size_t chunk = std::min<size_t>(remaining, remote60::native_poc::kControlInputTextMaxUtf16);
    QueuedControlInputMessage msg{};
    msg.type = MessageType::ControlInputText;
    msg.inputText.header.magic = remote60::native_poc::kMagic;
    msg.inputText.header.type = static_cast<uint16_t>(MessageType::ControlInputText);
    msg.inputText.header.size = static_cast<uint16_t>(sizeof(msg.inputText));
    msg.inputText.seq = gInputQueueState.NextSequence();
    msg.inputText.utf16Count = static_cast<uint16_t>(chunk);
    std::memcpy(msg.inputText.utf16, text + offset, chunk * sizeof(uint16_t));
    msg.inputText.clientSendQpcUs = qpc_now_us();
    enqueue_control_input_message(msg);
    offset += chunk;
  }
}

bool local_hotkey_modifiers_active() {
  return (GetKeyState(VK_CONTROL) < 0) && (GetKeyState(VK_MENU) < 0);
}

bool is_committed_text_code_unit(uint16_t ch) {
  return (ch >= 0x20u) || ch == static_cast<uint16_t>('\r') ||
         ch == static_cast<uint16_t>('\t') || ch == static_cast<uint16_t>('\b');
}

void enqueue_committed_text_unit(uint16_t ch) {
  if (!is_committed_text_code_unit(ch)) return;
  enqueue_input_text_units(&ch, 1);
}

bool send_ime_result_text(HWND hwnd, LPARAM imeFlags) {
  if ((imeFlags & GCS_RESULTSTR) == 0) return false;
  HIMC imc = ImmGetContext(hwnd);
  if (!imc) return false;
  const LONG bytes = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
  if (bytes <= 0) {
    ImmReleaseContext(hwnd, imc);
    return false;
  }
  std::vector<uint16_t> text(static_cast<size_t>(bytes) / sizeof(uint16_t));
  const LONG copied = ImmGetCompositionStringW(imc, GCS_RESULTSTR, text.data(), bytes);
  ImmReleaseContext(hwnd, imc);
  if (copied <= 0 || text.empty()) return false;
  enqueue_input_text_units(text.data(), text.size());
  gSuppressedImeCharCount.fetch_add(static_cast<uint32_t>(text.size()), std::memory_order_relaxed);
  return true;
}

void release_mouse_capture_if_idle(HWND hwnd) {
  if ((gMouseButtons.load(std::memory_order_relaxed) & 0x7u) == 0 && GetCapture() == hwnd) {
    ReleaseCapture();
  }
}

void enqueue_release_for_pressed_mouse_buttons() {
  const uint16_t buttons = gMouseButtons.exchange(0, std::memory_order_acq_rel);
  if ((buttons & 0x7u) == 0) return;
  const int32_t vx = gLastInputVideoX.load(std::memory_order_relaxed);
  const int32_t vy = gLastInputVideoY.load(std::memory_order_relaxed);
  if ((buttons & 0x4u) != 0) enqueue_input_event(3, vx, vy, 0, VK_MBUTTON);
  if ((buttons & 0x2u) != 0) enqueue_input_event(3, vx, vy, 0, VK_RBUTTON);
  if ((buttons & 0x1u) != 0) enqueue_input_event(3, vx, vy, 0, VK_LBUTTON);
}

uint32_t coord_to_permille(int coord, int extent) {
  if (extent <= 1) return 5000;
  const int clamped = std::clamp(coord, 0, extent - 1);
  const uint64_t numerator = static_cast<uint64_t>(clamped) * 10000ULL +
                             static_cast<uint64_t>((extent - 1) / 2);
  return static_cast<uint32_t>(numerator / static_cast<uint64_t>(extent - 1));
}

void queue_window_list_request(const char* statusText = nullptr) {
  gWindowPanelState.RequestList(statusText);
}

void queue_window_select_request(uint64_t windowId, const char* statusText = nullptr) {
  gWindowPanelState.RequestSelect(windowId, statusText);
}

void set_window_panel_status(const std::string& status) {
  gWindowPanelState.SetStatus(status);
}

void apply_window_list_snapshot(const ControlWindowListMessage& msg) {
  const ClientLayout layout = compute_client_layout(gHwnd);
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const auto result = gWindowPanelState.ApplyWindowList(msg, grid.visibleCards);
  gHostSupportsThumbnails.store(
      (msg.flags & remote60::native_poc::kControlWindowListFlagThumbnails) != 0,
      std::memory_order_relaxed);
  queue_thumbnail_fetches_from_panel();
  log_client_line(result.logLine);
}

void set_picker_visible_and_sync_stream(bool visible) {
  gWindowPickerVisible.store(visible, std::memory_order_relaxed);
  gStreamStateControl.Request(!visible);
}

void apply_window_selected_result(const ControlWindowSelectedMessage& msg) {
  const auto result = gWindowPanelState.ApplyWindowSelected(msg);
  if (result.ok) {
    set_picker_visible_and_sync_stream(false);
  }
  log_client_line(result.logLine);
}

void scroll_window_list(HWND hwnd, int deltaSteps) {
  const ClientLayout layout = compute_client_layout(hwnd);
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int totalCards = 1 + static_cast<int>(gWindowPanelState.Snapshot().items.size());
  const int totalRows = (totalCards + grid.cols - 1) / grid.cols;
  const int maxScrollRow = std::max(0, totalRows - grid.visibleRows);
  const int cur = gGridScrollRow.load(std::memory_order_relaxed);
  gGridScrollRow.store(std::clamp(cur + deltaSteps, 0, maxScrollRow), std::memory_order_relaxed);
}

// Hit-test a grid card. Card index 0 is the pinned Desktop card; window items follow.
bool try_hit_window_list_item(HWND hwnd, int x, int y, uint64_t* outWindowId) {
  if (!outWindowId) return false;
  const ClientLayout layout = compute_client_layout(hwnd);
  if (!point_in_rect(layout.listRect, x, y)) return false;
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int relX = x - layout.listRect.left;
  const int relY = y - layout.listRect.top;
  const int col = relX / (grid.cardW + grid.gap);
  const int row = relY / (grid.cardH + grid.gap);
  if (col < 0 || col >= grid.cols || row < 0 || row >= grid.visibleRows) return false;
  // Reject clicks that land in the gaps between cards.
  if (relX - col * (grid.cardW + grid.gap) >= grid.cardW) return false;
  if (relY - row * (grid.cardH + grid.gap) >= grid.cardH) return false;
  const WindowPanelSnapshot snap = gWindowPanelState.Snapshot();
  const int cardIndex =
      gGridScrollRow.load(std::memory_order_relaxed) * grid.cols + row * grid.cols + col;
  if (cardIndex == 0) {
    *outWindowId = 0;
    return true;
  }
  const int itemIndex = cardIndex - 1;
  if (itemIndex < 0 || itemIndex >= static_cast<int>(snap.items.size())) return false;
  *outWindowId = snap.items[static_cast<size_t>(itemIndex)].id;
  return true;
}

void enqueue_capture_mode_request(uint16_t mode, uint32_t xPermille, uint32_t yPermille) {
  gCaptureModeRequests.Request(mode, xPermille, yPermille);
}

void request_capture_overview_mode() {
  enqueue_capture_mode_request(1, 5000, 5000);
}

void request_capture_focus_from_client_point(HWND hwnd, int x, int y) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int clientW = std::max<int>(1, static_cast<int>(rc.right - rc.left));
  const int clientH = std::max<int>(1, static_cast<int>(rc.bottom - rc.top));
  enqueue_capture_mode_request(2, coord_to_permille(x, clientW), coord_to_permille(y, clientH));
}

void draw_alpha_rect(HDC hdc, const RECT& rect, COLORREF color, BYTE alpha) {
  const int w = rect.right - rect.left;
  const int h = rect.bottom - rect.top;
  if (w <= 0 || h <= 0) return;
  HDC memDc = CreateCompatibleDC(hdc);
  if (!memDc) return;
  HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
  if (!bmp) {
    DeleteDC(memDc);
    return;
  }
  HGDIOBJ oldBmp = SelectObject(memDc, bmp);
  RECT fillRc{0, 0, w, h};
  FillRect(memDc, &fillRc, cached_brush(color));
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = alpha;
  blend.AlphaFormat = 0;
  AlphaBlend(hdc, rect.left, rect.top, w, h, memDc, 0, 0, w, h, blend);
  SelectObject(memDc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(memDc);
}

void draw_panel_button(HDC hdc, const RECT& rect, const char* label, bool active = false,
                       bool disabled = false) {
  COLORREF fill = RGB(60, 68, 80);
  if (disabled) {
    fill = RGB(42, 46, 54);
  } else if (active) {
    fill = RGB(48, 96, 62);
  }
  FillRect(hdc, &rect, cached_brush(fill));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, disabled ? RGB(160, 165, 170) : RGB(240, 240, 240));
  RECT textRect = rect;
  draw_text_utf8(hdc, label ? std::string(label) : std::string{}, &textRect,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void push_overlay_metric_sample(uint32_t recvFpsX100, uint32_t decodedFpsX100, uint32_t recvMbpsX1000,
                                uint64_t avgLatencyUs, uint64_t nowUs) {
  std::lock_guard<std::mutex> lk(gOverlayMetricsMu);
  gOverlayMetrics.push_back({nowUs, recvFpsX100, decodedFpsX100, recvMbpsX1000, avgLatencyUs});
  const uint64_t keepWindowUs = 12000000ULL;
  while (!gOverlayMetrics.empty() && nowUs > gOverlayMetrics.front().tsUs &&
         (nowUs - gOverlayMetrics.front().tsUs) > keepWindowUs) {
    gOverlayMetrics.pop_front();
  }
}

OverlayMetricAverages collect_overlay_averages(uint64_t nowUs, uint64_t windowUs) {
  OverlayMetricAverages out{};
  std::lock_guard<std::mutex> lk(gOverlayMetricsMu);
  uint64_t sumRecvFpsX100 = 0;
  uint64_t sumDecodedFpsX100 = 0;
  uint64_t sumRecvMbpsX1000 = 0;
  uint64_t sumLatencyUs = 0;
  for (const auto& s : gOverlayMetrics) {
    if (nowUs >= s.tsUs && (nowUs - s.tsUs) <= windowUs) {
      ++out.sampleCount;
      sumRecvFpsX100 += s.recvFpsX100;
      sumDecodedFpsX100 += s.decodedFpsX100;
      sumRecvMbpsX1000 += s.recvMbpsX1000;
      sumLatencyUs += s.avgLatencyUs;
    }
  }
  if (out.sampleCount > 0) {
    out.recvFpsX100 = static_cast<uint32_t>(sumRecvFpsX100 / out.sampleCount);
    out.decodedFpsX100 = static_cast<uint32_t>(sumDecodedFpsX100 / out.sampleCount);
    out.recvMbpsX1000 = static_cast<uint32_t>(sumRecvMbpsX1000 / out.sampleCount);
    out.avgLatencyUs = sumLatencyUs / out.sampleCount;
  }
  return out;
}

void apply_runtime_tune_delta(int bitrateStep, int keyintStep) {
  gRuntimeTuneState.ApplyDelta(
      bitrateStep, keyintStep, gClientMetrics.recvMbpsX1000.load(std::memory_order_relaxed));
}

void draw_thumbnail_into(HDC hdc, const RECT& dst, const WindowThumb& thumb) {
  if (thumb.bgra.empty() || thumb.width == 0 || thumb.height == 0) return;
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = static_cast<LONG>(thumb.width);
  bmi.bmiHeader.biHeight = -static_cast<LONG>(thumb.height);
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  const RECT fit = aspect_fit_rect(dst, thumb.width, thumb.height);
  // Thumbnails repaint rarely, so the quality mode is affordable here.
  SetStretchBltMode(hdc, HALFTONE);
  SetBrushOrgEx(hdc, 0, 0, nullptr);
  StretchDIBits(hdc, fit.left, fit.top, fit.right - fit.left, fit.bottom - fit.top, 0, 0,
                static_cast<int>(thumb.width), static_cast<int>(thumb.height), thumb.bgra.data(),
                &bmi, DIB_RGB_COLORS, SRCCOPY);
}

void draw_target_card(HDC hdc, const RECT& card, const CardGridMetrics& grid,
                      uint64_t windowId, const std::string& title, bool active, bool disabled) {
  const RECT thumbRect = make_rect(card.left, card.top, card.right - card.left, grid.thumbH);
  const RECT captionRect = make_rect(card.left, card.top + grid.thumbH, card.right - card.left,
                                     card.bottom - card.top - grid.thumbH);

  FillRect(hdc, &thumbRect, cached_brush(RGB(24, 28, 36)));
  FillRect(hdc, &captionRect, cached_brush(active ? RGB(38, 70, 52) : RGB(32, 37, 46)));

  // Snapshot under the lock, draw outside it: StretchDIBits under gThumbMu made the fetch
  // thread and the paint stall each other.
  std::shared_ptr<const WindowThumb> thumb;
  {
    std::lock_guard<std::mutex> lk(gThumbMu);
    const auto it = gThumbs.find(windowId);
    if (it != gThumbs.end()) thumb = it->second;
  }
  if (thumb) {
    draw_thumbnail_into(hdc, thumbRect, *thumb);
  } else {
    RECT ph = thumbRect;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(110, 118, 130));
    draw_text_utf8(hdc, windowId == 0 ? std::string("Desktop") : std::string("Loading preview..."),
                   &ph, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, disabled ? RGB(150, 155, 162) : RGB(236, 239, 243));
  RECT text = captionRect;
  text.left += dpi_scale(10);
  text.right -= dpi_scale(10);
  draw_text_utf8(hdc, title, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  RECT frame = card;
  FrameRect(hdc, &frame, cached_brush(active ? RGB(88, 178, 122) : RGB(52, 58, 70)));
  if (active) {
    RECT inner{card.left + 1, card.top + 1, card.right - 1, card.bottom - 1};
    FrameRect(hdc, &inner, cached_brush(RGB(88, 178, 122)));
  }
}

void draw_overlay(HDC hdc) {
  const ClientLayout layout = compute_client_layout(gHwnd);
  const bool pickerVisible = gWindowPickerVisible.load(std::memory_order_relaxed);
  if (!pickerVisible) {
    draw_panel_button(hdc, layout.toggleButtonRect, "Targets");
    draw_panel_button(hdc, layout.macroButtonRect, "Macro",
                      remote60::native_poc::macro_window_visible());
    return;
  }

  draw_alpha_rect(hdc, layout.clientRect, RGB(13, 15, 20), 255);

  const WindowPanelSnapshot windowPanel = gWindowPanelState.Snapshot();
  const std::vector<WindowTargetUiEntry>& windowItems = windowPanel.items;
  const uint64_t selectedId = windowPanel.selectedId;
  const std::string& panelStatus = windowPanel.status;
  const bool selectionLocked = windowPanel.selectionLocked;

  // Header: product title and status on the left, actions on the right.
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(240, 243, 247));
  RECT titleRect = layout.selectedInfoRect;
  {
    HGDIOBJ old = gUiTitleFont ? SelectObject(hdc, gUiTitleFont) : nullptr;
    RECT t = titleRect;
    DrawTextW(hdc, L"Remote60", -1, &t, DT_LEFT | DT_SINGLELINE);
    if (old) SelectObject(hdc, old);
  }
  RECT subRect = titleRect;
  subRect.top += dpi_scale(28);
  SetTextColor(hdc, RGB(150, 158, 170));
  std::string statusLine =
      selectionLocked ? std::string("Target locked by host config") : panelStatus;
  if (!gControlConnected.load(std::memory_order_relaxed)) statusLine = "Connecting to host...";
  draw_text_utf8(hdc, statusLine, &subRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

  draw_panel_button(hdc, layout.refreshButtonRect, "Refresh", false,
                    !gControlConnected.load(std::memory_order_relaxed));
  draw_panel_button(hdc, layout.desktopButtonRect, "Desktop", selectedId == 0,
                    !gControlConnected.load(std::memory_order_relaxed) || selectionLocked);

  // Card grid: desktop preview first, then one card per shareable window.
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int totalCards = 1 + static_cast<int>(windowItems.size());
  const int totalRows = (totalCards + grid.cols - 1) / grid.cols;
  const int maxScrollRow = std::max(0, totalRows - grid.visibleRows);
  int scrollRow = std::clamp(gGridScrollRow.load(std::memory_order_relaxed), 0, maxScrollRow);
  gGridScrollRow.store(scrollRow, std::memory_order_relaxed);
  const int firstCard = scrollRow * grid.cols;

  for (int slot = 0; slot < grid.visibleCards; ++slot) {
    const int cardIndex = firstCard + slot;
    if (cardIndex >= totalCards) break;
    const RECT card = card_rect_for_slot(layout.listRect, grid, slot);
    if (cardIndex == 0) {
      draw_target_card(hdc, card, grid, 0, "Desktop (full screen)", selectedId == 0,
                       selectionLocked);
    } else {
      const auto& entry = windowItems[static_cast<size_t>(cardIndex - 1)];
      draw_target_card(hdc, card, grid, entry.id, entry.title, entry.id == selectedId,
                       selectionLocked);
    }
  }

  if (windowItems.empty()) {
    RECT emptyRect = layout.listRect;
    emptyRect.top += grid.cardH + dpi_scale(18);
    SetTextColor(hdc, RGB(150, 158, 170));
    draw_text_utf8(hdc,
                   selectionLocked ? std::string("Window list hidden by host config")
                                   : std::string("No shareable windows yet. Click Refresh."),
                   &emptyRect, DT_CENTER | DT_SINGLELINE);
  }

  // Footer: connection and input state in one quiet line.
  std::ostringstream foot;
  foot << (gControlConnected.load(std::memory_order_relaxed) ? "Connected" : "Disconnected")
       << "   Input " << (gInputEnabled.load(std::memory_order_relaxed) ? "on" : "off");
  const uint32_t decFpsX100 = gClientMetrics.decodedFpsX100.load(std::memory_order_relaxed);
  if (decFpsX100 > 0) foot << "   " << (decFpsX100 / 100) << " fps";
  if (totalRows > grid.visibleRows) {
    foot << "   Rows " << (scrollRow + 1) << "-"
         << std::min(totalRows, scrollRow + grid.visibleRows) << " / " << totalRows
         << " (wheel to scroll)";
  }
  RECT footRect = layout.statsRect;
  SetTextColor(hdc, RGB(140, 148, 160));
  draw_text_utf8(hdc, foot.str(), &footRect,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void log_client_line(const std::string& line) {
  std::lock_guard<std::mutex> lk(gLogMu);
  const std::string withNewline = line + "\n";
  std::cout << withNewline;
}

void request_keyframe(uint16_t reason) {
  const uint64_t nowUs = qpc_now_us();
  const auto attempt = gKeyframeRequests.Request(reason, nowUs);
  if (!attempt.queued && (attempt.throttledCount % 120) == 1) {
    std::cout << "[native-video-client][control] keyframe-request-throttled total=" << attempt.throttledCount
              << " reason=" << (reason == 0 ? 1 : reason)
              << " cause=" << attempt.throttleCause << "\n";
  }
}

struct Nv12RenderTelemetry {
  uint64_t uploadYUs = 0;
  uint64_t uploadUVUs = 0;
  uint64_t drawUs = 0;
  uint64_t presentBlockUs = 0;
  const char* failStage = "none";
};

struct Nv12D3dRenderer {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
  Microsoft::WRL::ComPtr<ID3D11Buffer> uvConstants;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texY;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texUV;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvY;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvUV;
  uint32_t texW = 0;
  uint32_t texH = 0;
  UINT rtvW = 0;
  UINT rtvH = 0;
  uint64_t rtvCreateCount = 0;
  uint64_t rtvResizeCount = 0;
  bool ready = false;

  bool init(HWND hwnd) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL outLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                   &device, &outLevel, &context);
    if (FAILED(hr)) {
      hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                             levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                             &device, &outLevel, &context);
      if (FAILED(hr)) return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device.As(&dxgiDevice))) return false;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    // Flip-discard presents by reference through DWM instead of blitting the whole frame;
    // the legacy discard model costs a full-frame copy per present. Falls back for the
    // rare pre-Win10 driver that rejects the flip model.
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(factory->CreateSwapChain(device.Get(), &sd, &swapChain))) {
      sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
      if (FAILED(factory->CreateSwapChain(device.Get(), &sd, &swapChain))) return false;
    }
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    static const char* kVsSrc =
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "VSOut main(uint id : SV_VertexID) {"
        "  float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);"
        "  VSOut o;"
        "  o.pos = float4(p, 0, 1);"
        "  o.uv = float2((p.x + 1.0) * 0.5, 1.0 - ((p.y + 1.0) * 0.5));"
        "  return o;"
        "}";
    static const char* kPsSrc =
        "cbuffer FrameConstants : register(b0) { float4 uvRect; };"
        "Texture2D texY : register(t0);"
        "Texture2D texUV : register(t1);"
        "SamplerState smp : register(s0);"
        "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {"
        "  float2 sampleUv = uvRect.xy + uv * uvRect.zw;"
        "  float y = texY.Sample(smp, sampleUv).r;"
        "  float2 c = texUV.Sample(smp, sampleUv).rg;"
        "  float Y = max(0.0, y - 16.0 / 255.0);"
        "  float U = c.x - 128.0 / 255.0;"
        "  float V = c.y - 128.0 / 255.0;"
        // BT.709 limited range; must match bgra_to_nv12/nv12_to_bgra in mf_h264_codec.cpp.
        "  float r = 1.16438356 * Y + 1.79274107 * V;"
        "  float g = 1.16438356 * Y - 0.21324861 * U - 0.53290933 * V;"
        "  float b = 1.16438356 * Y + 2.11240178 * U;"
        "  return float4(saturate(r), saturate(g), saturate(b), 1.0);"
        "}";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSrc, std::strlen(kVsSrc), nullptr, nullptr, nullptr,
                          "main", "vs_4_0", 0, 0, &vsBlob, &errBlob))) {
      return false;
    }
    if (FAILED(D3DCompile(kPsSrc, std::strlen(kPsSrc), nullptr, nullptr, nullptr,
                          "main", "ps_4_0", 0, 0, &psBlob, &errBlob))) {
      return false;
    }
    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs))) {
      return false;
    }
    if (FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps))) {
      return false;
    }

    D3D11_SAMPLER_DESC sdSamp{};
    sdSamp.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sdSamp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdSamp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdSamp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdSamp.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sdSamp, &sampler))) return false;

    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = 16;
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&constantsDesc, nullptr, &uvConstants))) return false;

    ready = ensure_rtv(hwnd);
    return ready;
  }

  bool ensure_rtv(HWND hwnd) {
    if (!swapChain || !device) return false;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const UINT w = std::max<LONG>(1, rc.right - rc.left);
    const UINT h = std::max<LONG>(1, rc.bottom - rc.top);

    // The steady state is a cache hit: recreating the view every frame also re-queried the
    // swapchain descriptor every frame, all of it for a window that had not moved.
    if (rtv && rtvW == w && rtvH == h) return true;

    DXGI_SWAP_CHAIN_DESC sd{};
    if (FAILED(swapChain->GetDesc(&sd))) return false;
    if (sd.BufferDesc.Width != w || sd.BufferDesc.Height != h) {
      rtv.Reset();
      ++rtvResizeCount;
      if (FAILED(swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
    if (FAILED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv))) return false;
    ++rtvCreateCount;
    rtvW = w;
    rtvH = h;
    return true;
  }

  bool ensure_nv12_textures(uint32_t w, uint32_t h) {
    if (!device) return false;
    if (texY && texUV && texW == w && texH == h) return true;

    texY.Reset();
    texUV.Reset();
    srvY.Reset();
    srvUV.Reset();
    texW = 0;
    texH = 0;

    D3D11_TEXTURE2D_DESC yDesc{};
    yDesc.Width = w;
    yDesc.Height = h;
    yDesc.MipLevels = 1;
    yDesc.ArraySize = 1;
    yDesc.Format = DXGI_FORMAT_R8_UNORM;
    yDesc.SampleDesc.Count = 1;
    yDesc.Usage = D3D11_USAGE_DYNAMIC;
    yDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    yDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateTexture2D(&yDesc, nullptr, &texY))) return false;

    D3D11_TEXTURE2D_DESC uvDesc{};
    uvDesc.Width = w / 2;
    uvDesc.Height = h / 2;
    uvDesc.MipLevels = 1;
    uvDesc.ArraySize = 1;
    uvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    uvDesc.SampleDesc.Count = 1;
    uvDesc.Usage = D3D11_USAGE_DYNAMIC;
    uvDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    uvDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateTexture2D(&uvDesc, nullptr, &texUV))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC ySrvDesc{};
    ySrvDesc.Format = DXGI_FORMAT_R8_UNORM;
    ySrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    ySrvDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(texY.Get(), &ySrvDesc, &srvY))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC uvSrvDesc{};
    uvSrvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    uvSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    uvSrvDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(texUV.Get(), &uvSrvDesc, &srvUV))) return false;

    texW = w;
    texH = h;
    return true;
  }

  bool draw(HWND hwnd, const RECT& destRect, ID3D11ShaderResourceView* ySrv,
            ID3D11ShaderResourceView* uvSrv, const float uvRect[4],
            Nv12RenderTelemetry* telemetry) {
    if (!ensure_rtv(hwnd) || !ySrv || !uvSrv || !uvConstants) {
      if (telemetry) telemetry->failStage = "draw_args";
      return false;
    }
    context->UpdateSubresource(uvConstants.Get(), 0, nullptr, uvRect, 0, 0);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT drawRect = destRect;
    if (drawRect.right <= drawRect.left || drawRect.bottom <= drawRect.top) drawRect = rc;
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = static_cast<float>(drawRect.left);
    vp.TopLeftY = static_cast<float>(drawRect.top);
    vp.Width = static_cast<float>(std::max<LONG>(1, drawRect.right - drawRect.left));
    vp.Height = static_cast<float>(std::max<LONG>(1, drawRect.bottom - drawRect.top));
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    ID3D11RenderTargetView* rtvs[] = {rtv.Get()};
    context->OMSetRenderTargets(1, rtvs, nullptr);
    context->RSSetViewports(1, &vp);
    const float clearColor[4] = {0, 0, 0, 1};
    context->ClearRenderTargetView(rtv.Get(), clearColor);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[] = {ySrv, uvSrv};
    context->PSSetShaderResources(0, 2, srvs);
    ID3D11Buffer* constants[] = {uvConstants.Get()};
    context->PSSetConstantBuffers(0, 1, constants);
    ID3D11SamplerState* samplers[] = {sampler.Get()};
    context->PSSetSamplers(0, 1, samplers);
    const uint64_t drawStartUs = qpc_now_us();
    context->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrvs[] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, nullSrvs);
    const uint64_t drawEndUs = qpc_now_us();
    if (telemetry) telemetry->drawUs = drawEndUs - drawStartUs;

    const uint64_t presentStartUs = qpc_now_us();
    const HRESULT hr = swapChain->Present(0, 0);
    const uint64_t presentDoneUs = qpc_now_us();
    if (telemetry) telemetry->presentBlockUs = presentDoneUs - presentStartUs;
    if (!(SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED) && telemetry) telemetry->failStage = "present";
    return SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
  }

  bool render_surface(HWND hwnd, const RECT& destRect, ID3D11Texture2D* texture,
                      uint32_t subresource, uint32_t codedW, uint32_t codedH,
                      uint32_t visLeft, uint32_t visTop, uint32_t w, uint32_t h,
                      Nv12RenderTelemetry* telemetry) {
    if (telemetry) *telemetry = Nv12RenderTelemetry{};
    if (!ready || !texture || !codedW || !codedH || !w || !h) {
      if (telemetry) telemetry->failStage = "surface_args";
      return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    if (td.Format != DXGI_FORMAT_NV12 || subresource >= td.MipLevels * td.ArraySize ||
        visLeft + w > codedW || visTop + h > codedH) {
      if (telemetry) telemetry->failStage = "surface_desc";
      return false;
    }
    Microsoft::WRL::ComPtr<ID3D11Device> textureDevice;
    texture->GetDevice(&textureDevice);
    if (textureDevice.Get() != device.Get()) {
      if (telemetry) telemetry->failStage = "surface_device";
      return false;
    }
    const UINT mipSlice = subresource % td.MipLevels;
    const UINT arraySlice = subresource / td.MipLevels;
    auto make_view = [&](DXGI_FORMAT format,
                         Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>* out) -> bool {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
      desc.Format = format;
      if (td.ArraySize > 1) {
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MostDetailedMip = mipSlice;
        desc.Texture2DArray.MipLevels = 1;
        desc.Texture2DArray.FirstArraySlice = arraySlice;
        desc.Texture2DArray.ArraySize = 1;
      } else {
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = mipSlice;
        desc.Texture2D.MipLevels = 1;
      }
      return SUCCEEDED(device->CreateShaderResourceView(texture, &desc, out->ReleaseAndGetAddressOf()));
    };
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ySrv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uvSrv;
    if (!make_view(DXGI_FORMAT_R8_UNORM, &ySrv) ||
        !make_view(DXGI_FORMAT_R8G8_UNORM, &uvSrv)) {
      if (telemetry) telemetry->failStage = "surface_srv";
      return false;
    }
    const float uvRect[4] = {static_cast<float>(visLeft) / codedW,
                             static_cast<float>(visTop) / codedH,
                             static_cast<float>(w) / codedW,
                             static_cast<float>(h) / codedH};
    return draw(hwnd, destRect, ySrv.Get(), uvSrv.Get(), uvRect, telemetry);
  }

  /**
   * Draws the visible rect (w x h at visLeft/visTop) out of a coded NV12 plane. The textures
   * are sized to the visible picture, so the shader never samples the coded padding rows --
   * uploading the full 1088-row plane stretched 8 garbage rows into a 1080p picture and
   * distorted the aspect by 0.74%.
   */
  bool render(HWND hwnd, const RECT& destRect, const uint8_t* nv12, uint32_t codedW,
              uint32_t codedH, uint32_t visLeft, uint32_t visTop, uint32_t w, uint32_t h,
              Nv12RenderTelemetry* telemetry) {
    if (telemetry) {
      *telemetry = Nv12RenderTelemetry{};
    }
    if (!ready || !nv12 || codedW == 0 || codedH == 0 || w == 0 || h == 0 || (codedW & 1u) ||
        (codedH & 1u) || (w & 1u) || (h & 1u) || (visLeft & 1u) || (visTop & 1u) ||
        visLeft + w > codedW || visTop + h > codedH) {
      if (telemetry) telemetry->failStage = "invalid_args";
      return false;
    }
    if (!ensure_rtv(hwnd)) {
      if (telemetry) telemetry->failStage = "ensure_rtv";
      return false;
    }
    if (!ensure_nv12_textures(w, h)) {
      if (telemetry) telemetry->failStage = "ensure_nv12_textures";
      return false;
    }

    const uint8_t* yPlane = nv12 + static_cast<size_t>(visTop) * codedW + visLeft;
    const uint8_t* uvPlane = nv12 + static_cast<size_t>(codedW) * codedH +
                             static_cast<size_t>(visTop / 2) * codedW + visLeft;

    const uint64_t uploadYStartUs = qpc_now_us();
    D3D11_MAPPED_SUBRESOURCE yMap{};
    if (FAILED(context->Map(texY.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &yMap))) {
      if (telemetry) telemetry->failStage = "map_y";
      return false;
    }
    if (codedW == w && static_cast<UINT>(w) == yMap.RowPitch) {
      std::memcpy(reinterpret_cast<uint8_t*>(yMap.pData), yPlane, static_cast<size_t>(h) * w);
    } else {
      for (uint32_t row = 0; row < h; ++row) {
        std::memcpy(reinterpret_cast<uint8_t*>(yMap.pData) + static_cast<size_t>(row) * yMap.RowPitch,
                    yPlane + static_cast<size_t>(row) * codedW, w);
      }
    }
    context->Unmap(texY.Get(), 0);
    if (telemetry) telemetry->uploadYUs = qpc_now_us() - uploadYStartUs;

    const uint64_t uploadUVStartUs = qpc_now_us();
    D3D11_MAPPED_SUBRESOURCE uvMap{};
    if (FAILED(context->Map(texUV.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &uvMap))) {
      if (telemetry) telemetry->failStage = "map_uv";
      return false;
    }
    const uint32_t uvHeight = h / 2;
    if (codedW == w && static_cast<UINT>(w) == uvMap.RowPitch) {
      std::memcpy(reinterpret_cast<uint8_t*>(uvMap.pData), uvPlane,
                  static_cast<size_t>(uvHeight) * w);
    } else {
      for (uint32_t row = 0; row < uvHeight; ++row) {
        std::memcpy(reinterpret_cast<uint8_t*>(uvMap.pData) + static_cast<size_t>(row) * uvMap.RowPitch,
                    uvPlane + static_cast<size_t>(row) * codedW, w);
      }
    }
    context->Unmap(texUV.Get(), 0);
    if (telemetry) telemetry->uploadUVUs = qpc_now_us() - uploadUVStartUs;

    const float uvRect[4] = {0, 0, 1, 1};
    return draw(hwnd, destRect, srvY.Get(), srvUV.Get(), uvRect, telemetry);
  }
};

Nv12D3dRenderer gNv12Renderer;

remote60::native_poc::InputMacro gInputMacro;

void enqueue_input_event(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode) {
  if (kInputPolicyForceBlock) return;
  if (!gInputEnabled.load()) return;
  QueuedControlInputMessage msg{};
  msg.type = MessageType::ControlInputEvent;
  msg.inputEvent.header.magic = remote60::native_poc::kMagic;
  msg.inputEvent.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  msg.inputEvent.header.size = static_cast<uint16_t>(sizeof(msg.inputEvent));
  msg.inputEvent.seq = gInputQueueState.NextSequence();
  msg.inputEvent.kind = kind;
  msg.inputEvent.buttons = gMouseButtons.load();
  msg.inputEvent.x = x;
  msg.inputEvent.y = y;
  msg.inputEvent.wheelDelta = wheelDelta;
  msg.inputEvent.keyCode = keyCode;
  msg.inputEvent.clientSendQpcUs = qpc_now_us();
  // Recording taps the send path, so the macro sees exactly what the host will see -- the
  // engine keeps pointer actions and drops keys on its own.
  if (gInputMacro.IsRecording()) {
    gInputMacro.RecordEvent(msg.inputEvent, GetTickCount64());
  }
  enqueue_control_input_message(msg);
}

/** A replayed step carries its own recorded button state instead of today's live one. */
void enqueue_macro_step(const remote60::native_poc::MacroStep& step) {
  if (kInputPolicyForceBlock) return;
  if (!gInputEnabled.load()) return;
  QueuedControlInputMessage msg{};
  msg.type = MessageType::ControlInputEvent;
  msg.inputEvent.header.magic = remote60::native_poc::kMagic;
  msg.inputEvent.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  msg.inputEvent.header.size = static_cast<uint16_t>(sizeof(msg.inputEvent));
  msg.inputEvent.seq = gInputQueueState.NextSequence();
  msg.inputEvent.kind = step.kind;
  msg.inputEvent.buttons = step.buttons;
  msg.inputEvent.x = step.x;
  msg.inputEvent.y = step.y;
  msg.inputEvent.wheelDelta = step.wheelDelta;
  msg.inputEvent.keyCode = step.keyCode;
  msg.inputEvent.clientSendQpcUs = qpc_now_us();
  enqueue_control_input_message(msg);
}

void toggle_macro_window(HWND owner) {
  remote60::native_poc::MacroWindowHooks hooks;
  hooks.macro = &gInputMacro;
  hooks.sendStep = [](const remote60::native_poc::MacroStep& step) { enqueue_macro_step(step); };
  remote60::native_poc::macro_window_toggle(GetModuleHandleW(nullptr), owner, hooks);
  if (owner) InvalidateRect(owner, nullptr, FALSE);
}

std::atomic<bool> gMacroButtonDown{false};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CLOSE:
      gRunning = false;
      if (gSock != INVALID_SOCKET) shutdown(gSock, SD_BOTH);
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      destroy_cached_gdi_objects();
      PostQuitMessage(0);
      return 0;
    case WM_DPICHANGED: {
      ensure_ui_font(hwnd);
      const RECT* suggested = reinterpret_cast<const RECT*>(lp);
      if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      if ((gMouseButtons.load(std::memory_order_relaxed) & 0x7u) == 0) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        enqueue_input_event(1, vx, vy, 0, 0);
      }
      return 0;
    case WM_LBUTTONDOWN:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        gWindowPickerToggleDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        gMacroButtonDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      SetFocus(hwnd);
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        gMouseButtons.fetch_or(1);
        enqueue_input_event(2, vx, vy, 0, VK_LBUTTON);
      }
      return 0;
    case WM_LBUTTONUP: {
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      const int x = GET_X_LPARAM(lp);
      const int y = GET_Y_LPARAM(lp);
      const ClientLayout layout = compute_client_layout(hwnd);
      if (gWindowPickerToggleDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.toggleButtonRect, x, y)) {
          set_picker_visible_and_sync_stream(
              !gWindowPickerVisible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (gMacroButtonDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.macroButtonRect, x, y)) {
          toggle_macro_window(hwnd);
        }
        return 0;
      }
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (point_in_rect(layout.refreshButtonRect, x, y)) {
          queue_window_list_request("window_list_request pending");
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (point_in_rect(layout.desktopButtonRect, x, y)) {
          const bool alreadyDesktop = gWindowPanelState.IsDesktopSelected();
          if (alreadyDesktop) {
            set_picker_visible_and_sync_stream(false);
          } else {
            queue_window_select_request(0, "desktop_select_requested");
          }
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        uint64_t hitWindowId = 0;
        if (try_hit_window_list_item(hwnd, x, y, &hitWindowId)) {
          queue_window_select_request(hitWindowId, "window_select_requested");
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        return 0;
      }
      if (point_in_rect(layout.refreshButtonRect, x, y)) {
        queue_window_list_request("window_list_request pending");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (point_in_rect(layout.desktopButtonRect, x, y)) {
        queue_window_select_request(0, "desktop_select_requested");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      uint64_t hitWindowId = 0;
      if (try_hit_window_list_item(hwnd, x, y, &hitWindowId)) {
        queue_window_select_request(hitWindowId, "window_select_requested");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (point_in_panel_ui(hwnd, x, y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, x, y, &vx, &vy)) return 0;
        gMouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        enqueue_input_event(3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    }
    case WM_RBUTTONDOWN:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        gMouseButtons.fetch_or(2);
        enqueue_input_event(2, vx, vy, 0, VK_RBUTTON);
      }
      return 0;
    case WM_RBUTTONUP:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        gMouseButtons.fetch_and(static_cast<uint16_t>(~2u));
        enqueue_input_event(3, vx, vy, 0, VK_RBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    case WM_MBUTTONDOWN:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        gMouseButtons.fetch_or(4);
        enqueue_input_event(2, vx, vy, 0, VK_MBUTTON);
      }
      return 0;
    case WM_MBUTTONUP:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        gMouseButtons.fetch_and(static_cast<uint16_t>(~4u));
        enqueue_input_event(3, vx, vy, 0, VK_MBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    case WM_MOUSEWHEEL: {
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      const ClientLayout layout = compute_client_layout(hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) return 0;
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (point_in_rect(layout.listRect, p.x, p.y)) {
          const int wheel = GET_WHEEL_DELTA_WPARAM(wp);
          scroll_window_list(hwnd, (wheel < 0) ? 1 : -1);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_rect(layout.listRect, p.x, p.y)) {
        const int wheel = GET_WHEEL_DELTA_WPARAM(wp);
        scroll_window_list(hwnd, (wheel < 0) ? 1 : -1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (point_in_panel_ui(hwnd, p.x, p.y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(hwnd, p.x, p.y, &vx, &vy)) return 0;
      enqueue_input_event(4, vx, vy, GET_WHEEL_DELTA_WPARAM(wp), 0);
      return 0;
    }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP: {
      UINT32 pointerId = GET_POINTERID_WPARAM(wp);
      POINTER_INPUT_TYPE pointerType = PT_POINTER;
      if (!GetPointerType(pointerId, &pointerType) || pointerType != PT_TOUCH) {
        return DefWindowProcW(hwnd, msg, wp, lp);
      }
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      gSuppressMouseUntilUs.store(qpc_now_us() + 300000ULL, std::memory_order_relaxed);
      const ClientLayout layout = compute_client_layout(hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          gWindowPickerToggleDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP && gWindowPickerToggleDown.exchange(false, std::memory_order_relaxed)) {
          set_picker_visible_and_sync_stream(
              !gWindowPickerVisible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          gMacroButtonDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP &&
                   gMacroButtonDown.exchange(false, std::memory_order_relaxed)) {
          toggle_macro_window(hwnd);
        }
        return 0;
      }
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (msg == WM_POINTERUP) {
          if (point_in_rect(layout.refreshButtonRect, p.x, p.y)) {
            queue_window_list_request("window_list_request pending");
            InvalidateRect(hwnd, nullptr, FALSE);
          } else if (point_in_rect(layout.desktopButtonRect, p.x, p.y)) {
            const bool alreadyDesktop = gWindowPanelState.IsDesktopSelected();
            if (alreadyDesktop) {
              set_picker_visible_and_sync_stream(false);
            } else {
              queue_window_select_request(0, "desktop_select_requested");
            }
            InvalidateRect(hwnd, nullptr, FALSE);
          } else {
            uint64_t hitWindowId = 0;
            if (try_hit_window_list_item(hwnd, p.x, p.y, &hitWindowId)) {
              queue_window_select_request(hitWindowId, "window_select_requested");
              InvalidateRect(hwnd, nullptr, FALSE);
            }
          }
        }
        return 0;
      }
      if (point_in_panel_ui(hwnd, p.x, p.y)) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(hwnd, p.x, p.y, &vx, &vy)) return 0;
      if (msg == WM_POINTERDOWN) {
        if (gActiveTouchDown.load(std::memory_order_relaxed)) return 0;
        SetFocus(hwnd);
        SetCapture(hwnd);
        gActiveTouchPointerId.store(pointerId, std::memory_order_relaxed);
        gActiveTouchDown.store(true, std::memory_order_relaxed);
        gMouseButtons.fetch_or(1);
        enqueue_input_event(2, vx, vy, 0, VK_LBUTTON);
      } else if (msg == WM_POINTERUPDATE) {
        if (!gActiveTouchDown.load(std::memory_order_relaxed) ||
            gActiveTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        enqueue_input_event(1, vx, vy, 0, 0);
      } else {
        if (!gActiveTouchDown.load(std::memory_order_relaxed) ||
            gActiveTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        gMouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        gActiveTouchDown.store(false, std::memory_order_relaxed);
        gActiveTouchPointerId.store(0, std::memory_order_relaxed);
        enqueue_input_event(3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
      enqueue_release_for_pressed_mouse_buttons();
      gActiveTouchDown.store(false, std::memory_order_relaxed);
      gActiveTouchPointerId.store(0, std::memory_order_relaxed);
      return 0;
    case WM_IME_SETCONTEXT: {
      const LPARAM masked =
          lp & ~(static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 0) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 1) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 2) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 3) |
                 static_cast<LPARAM>(ISC_SHOWUIGUIDELINE));
      return DefWindowProcW(hwnd, msg, wp, masked);
    }
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_CHAR:
      return 0;
    case WM_IME_COMPOSITION:
      if (kInputPolicyForceBlock) return 0;
      (void)send_ime_result_text(hwnd, lp);
      return 0;
    case WM_KEYDOWN:
      if (local_hotkey_modifiers_active() && wp == VK_F5) {
        queue_window_list_request("window_list_request pending");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_F9) {
        request_capture_overview_mode();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_4) {  // [
        apply_runtime_tune_delta(-1, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_6) {  // ]
        apply_runtime_tune_delta(1, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_1) {  // ;
        apply_runtime_tune_delta(0, -1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_7) {  // '
        apply_runtime_tune_delta(0, 1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (kInputPolicyForceBlock) return 0;
      enqueue_input_event(5, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_KEYUP:
      if (kInputPolicyForceBlock) return 0;
      enqueue_input_event(6, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_SYSKEYDOWN:
      if (kInputPolicyForceBlock) return 0;
      enqueue_input_event(5, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_SYSKEYUP:
      if (kInputPolicyForceBlock) return 0;
      enqueue_input_event(6, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_CHAR:
      if (kInputPolicyForceBlock) return 0;
      {
        uint32_t suppress = gSuppressedImeCharCount.load(std::memory_order_relaxed);
        while (suppress > 0) {
          if (gSuppressedImeCharCount.compare_exchange_weak(
                  suppress, suppress - 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return 0;
          }
        }
      }
      enqueue_committed_text_unit(static_cast<uint16_t>(wp & 0xFFFFu));
      return 0;
    case WM_SYSCHAR:
      return 0;
    case WM_ERASEBKGND:
      // Avoid background erase flicker between frames.
      return 1;
    case WM_PAINT: {
      gPaintQueued = false;
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      const uint64_t paintStartUs = qpc_now_us();
      const ClientLayout layout = compute_client_layout(hwnd);
      const RECT& videoRect = layout.videoRect;
      const RECT contentRect = resolve_video_content_rect(hwnd, videoRect);
      const bool pickerVisible = gWindowPickerVisible.load(std::memory_order_relaxed);
      static bool hasPresentedAtLeastOneFrame = false;

      std::shared_ptr<std::vector<uint8_t>> local;
      Microsoft::WRL::ComPtr<IMFSample> localSurfaceSample;
      Microsoft::WRL::ComPtr<ID3D11Texture2D> localSurfaceTexture;
      uint32_t localSurfaceSubresource = 0;
      SharedFrame::PixelFormat localFormat = SharedFrame::PixelFormat::Unknown;
      uint32_t w = 0, h = 0;
      uint32_t codedW = 0, codedH = 0;
      uint32_t visL = 0, visT = 0;
      uint32_t seq = 0;
      uint64_t captureUs = 0;
      uint64_t encodeStartUs = 0;
      uint64_t encodeEndUs = 0;
      uint64_t sendUs = 0;
      uint64_t recvUs = 0;
      uint64_t decodeStartUs = 0;
      uint64_t decodeEndUs = 0;
      uint64_t queueSetUs = 0;
      uint64_t decodeToQueueUs = 0;
      uint64_t frameVersion = 0;
      {
        std::lock_guard<std::mutex> lk(gFrame.mu);
        if ((gFrame.bytes && !gFrame.bytes->empty()) || gFrame.surfaceTexture) {
          local = gFrame.bytes;
          localSurfaceSample = gFrame.surfaceSample;
          localSurfaceTexture = gFrame.surfaceTexture;
          localSurfaceSubresource = gFrame.surfaceSubresource;
          localFormat = gFrame.format;
          w = gFrame.width;
          h = gFrame.height;
          codedW = (gFrame.codedWidth > 0) ? gFrame.codedWidth : gFrame.width;
          codedH = (gFrame.codedHeight > 0) ? gFrame.codedHeight : gFrame.height;
          visL = gFrame.visibleLeft;
          visT = gFrame.visibleTop;
          seq = gFrame.seq;
          captureUs = gFrame.captureUs;
          encodeStartUs = gFrame.encodeStartUs;
          encodeEndUs = gFrame.encodeEndUs;
          sendUs = gFrame.sendUs;
          recvUs = gFrame.recvUs;
          decodeStartUs = gFrame.decodeStartUs;
          decodeEndUs = gFrame.decodeEndUs;
          queueSetUs = gFrame.queueSetUs;
          decodeToQueueUs = gFrame.decodeToQueueUs;
          frameVersion = gFrame.version;
        }
      }
      bool presented = false;
      Nv12RenderTelemetry renderTelemetry{};
      const char* renderPath = "none";
      const char* fallbackReason = "none";
      if (!pickerVisible && (local || localSurfaceTexture) && w > 0 && h > 0) {
        if (localFormat == SharedFrame::PixelFormat::Nv12) {
          if (!gNv12Renderer.ready) {
            if (!gNv12Renderer.init(hwnd)) {
              ++gD3dPresentFailCount;
              ++gFallbackInitFailCount;
              fallbackReason = "d3d_init_fail";
            }
          }
          if (gNv12Renderer.ready) {
            if (localSurfaceTexture) {
              presented = gNv12Renderer.render_surface(
                  hwnd, contentRect, localSurfaceTexture.Get(), localSurfaceSubresource,
                  codedW, codedH, visL, visT, w, h, &renderTelemetry);
            } else {
              presented = gNv12Renderer.render(hwnd, contentRect, local->data(), codedW, codedH,
                                               visL, visT, w, h, &renderTelemetry);
            }
            if (presented) {
              ++gD3dPresentSuccessCount;
              renderPath = localSurfaceTexture ? "d3d_nv12_surface" : "d3d_nv12";
            } else {
              ++gD3dPresentFailCount;
              ++gFallbackRenderFailCount;
              fallbackReason = renderTelemetry.failStage;
            }
          }
          if (!presented && local) {
            std::vector<uint8_t> bgra;
            if (nv12_to_bgra(local->data(), codedW, codedH, &bgra) && !bgra.empty()) {
              // The DIB carries the coded plane; the source rect and a row-offset base
              // pointer select only the visible picture out of it.
              BITMAPINFO bmi{};
              bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
              bmi.bmiHeader.biWidth = static_cast<LONG>(codedW);
              bmi.bmiHeader.biHeight = -static_cast<LONG>(h);
              bmi.bmiHeader.biPlanes = 1;
              bmi.bmiHeader.biBitCount = 32;
              bmi.bmiHeader.biCompression = BI_RGB;
              SetStretchBltMode(hdc, COLORONCOLOR);
              FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
              StretchDIBits(hdc, contentRect.left, contentRect.top,
                            contentRect.right - contentRect.left, contentRect.bottom - contentRect.top,
                            static_cast<int>(visL), 0, static_cast<int>(w), static_cast<int>(h),
                            bgra.data() + static_cast<size_t>(visT) * codedW * 4, &bmi,
                            DIB_RGB_COLORS, SRCCOPY);
              presented = true;
              ++gGdiFallbackPresentedCount;
              renderPath = "gdi_nv12_fallback";
            } else {
              ++gFallbackNv12ConvertFailCount;
              fallbackReason = "nv12_to_bgra_fail";
            }
          }
        } else if (localFormat == SharedFrame::PixelFormat::Bgra32) {
          BITMAPINFO bmi{};
          bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
          bmi.bmiHeader.biWidth = static_cast<LONG>(w);
          bmi.bmiHeader.biHeight = -static_cast<LONG>(h);  // top-down
          bmi.bmiHeader.biPlanes = 1;
          bmi.bmiHeader.biBitCount = 32;
          bmi.bmiHeader.biCompression = BI_RGB;
          SetStretchBltMode(hdc, COLORONCOLOR);
          FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
          StretchDIBits(hdc, contentRect.left, contentRect.top,
                        contentRect.right - contentRect.left, contentRect.bottom - contentRect.top,
                        0, 0, static_cast<int>(w), static_cast<int>(h),
                        local->data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
          presented = true;
          renderPath = "gdi_bgra";
        }
      }
      if (presented) {
        hasPresentedAtLeastOneFrame = true;
        static uint64_t lastPresentUs = 0;
        static uint64_t lastUserFeedbackUs = 0;
        static uint64_t lastUserFeedbackOverwrite = 0;
        gLastPresentedVersion.store(frameVersion, std::memory_order_relaxed);
        gLastPresentedCaptureUs.store(captureUs, std::memory_order_relaxed);
        const uint64_t presentUs = qpc_now_us();
        const uint64_t presentGapUs = (lastPresentUs > 0) ? (presentUs - lastPresentUs) : 0;
        const uint64_t queueToPaintUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
        const uint64_t queueToPresentUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
        const uint32_t traceEvery = gTraceEvery.load();
        const uint32_t traceMax = gTraceMax.load();
        if (traceEvery > 0 && (seq % traceEvery) == 0 &&
            (traceMax == 0 || gTracePresentPrinted.load() < traceMax)) {
          const auto nowPrinted = gTracePresentPrinted.fetch_add(1) + 1;
          if (traceMax == 0 || nowPrinted <= traceMax) {
            const uint64_t netUs = (recvUs >= sendUs) ? (recvUs - sendUs) : 0;
            const uint64_t c2eUs = (encodeStartUs >= captureUs) ? (encodeStartUs - captureUs) : 0;
            const uint64_t encUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
            const uint64_t e2sUs = (sendUs >= encodeEndUs) ? (sendUs - encodeEndUs) : 0;
            const uint64_t r2dUs = (decodeStartUs >= recvUs) ? (decodeStartUs - recvUs) : 0;
            const uint64_t decUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
            const uint64_t d2pUs = (presentUs >= decodeEndUs) ? (presentUs - decodeEndUs) : 0;
            const uint64_t renderUs = (presentUs >= recvUs) ? (presentUs - recvUs) : 0;
            const uint64_t queueWaitUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
            const uint64_t paintUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
            const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
            std::ostringstream oss;
            oss << "[native-video-client][trace_present] seq=" << seq
                << " captureUs=" << captureUs
                << " encodeStartUs=" << encodeStartUs
                << " encodeEndUs=" << encodeEndUs
                << " sendUs=" << sendUs
                << " recvUs=" << recvUs
                << " decodeStartUs=" << decodeStartUs
                << " decodeEndUs=" << decodeEndUs
                << " presentUs=" << presentUs
                << " c2eUs=" << c2eUs
                << " encUs=" << encUs
                << " e2sUs=" << e2sUs
                << " netUs=" << netUs
                << " r2dUs=" << r2dUs
                << " decUs=" << decUs
                << " d2pUs=" << d2pUs
                << " decodeToQueueUs=" << decodeToQueueUs
                << " queueWaitUs=" << queueWaitUs
                << " paintUs=" << paintUs
                << " uploadYUs=" << renderTelemetry.uploadYUs
                << " uploadUVUs=" << renderTelemetry.uploadUVUs
                << " drawUs=" << renderTelemetry.drawUs
                << " presentBlockUs=" << renderTelemetry.presentBlockUs
                << " renderUs=" << renderUs
                << " totalUs=" << totalUs
                << " renderPath=" << renderPath
                << " fallbackReason=" << fallbackReason;
            log_client_line(oss.str());
          }
        }
        const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
        if ((totalUs >= kUserFeedbackLagWarnUs || (presentGapUs >= kUserFeedbackGapWarnUs && lastPresentUs > 0)) &&
            (presentUs >= lastUserFeedbackUs + kUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
          const uint64_t overwriteCountNow = gOverwriteBeforePresentCount.load(std::memory_order_relaxed);
          const uint64_t overwriteDelta = (overwriteCountNow >= lastUserFeedbackOverwrite)
                                             ? (overwriteCountNow - lastUserFeedbackOverwrite)
                                             : 0;
          const uint64_t d3dSuccess = gD3dPresentSuccessCount.load(std::memory_order_relaxed);
          const uint64_t d3dFail = gD3dPresentFailCount.load(std::memory_order_relaxed);
          const uint64_t gdiFallback = gGdiFallbackPresentedCount.load(std::memory_order_relaxed);
          const uint64_t paintCoalesced = gPaintCoalescedCount.load(std::memory_order_relaxed);
          const uint64_t queueWaitUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
          const uint64_t paintUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
          const uint64_t netUs = (recvUs >= sendUs) ? (recvUs - sendUs) : 0;
          const uint64_t c2eUs = (encodeStartUs >= captureUs) ? (encodeStartUs - captureUs) : 0;
          const uint64_t encUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
          const uint64_t e2sUs = (sendUs >= encodeEndUs) ? (sendUs - encodeEndUs) : 0;
          const uint64_t r2dUs = (decodeStartUs >= recvUs) ? (decodeStartUs - recvUs) : 0;
          const uint64_t decUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
          const uint64_t d2pUs = (presentUs >= decodeEndUs) ? (presentUs - decodeEndUs) : 0;
          std::ostringstream oss;
          oss << "[native-video-client][user-feedback] seq=" << seq
              << " totalUs=" << totalUs
              << " capGapUs=" << presentGapUs
              << " queueToPaintUs=" << queueToPaintUs
              << " queueToPresentUs=" << queueToPresentUs
              << " d3dPresentSuccess=" << d3dSuccess
              << " d3dPresentFail=" << d3dFail
              << " gdiFallback=" << gdiFallback
              << " paintCoalesced=" << paintCoalesced
              << " overwriteDelta=" << overwriteDelta
              << " c2eUs=" << c2eUs
              << " encUs=" << encUs
              << " e2sUs=" << e2sUs
              << " netUs=" << netUs
              << " r2dUs=" << r2dUs
              << " decUs=" << decUs
              << " d2pUs=" << d2pUs
              << " decodeToQueueUs=" << decodeToQueueUs
              << " queueWaitUs=" << queueWaitUs
              << " paintUs=" << paintUs
              << " presentBlockUs=" << renderTelemetry.presentBlockUs
              << " renderPath=" << renderPath
              << " fallbackReason=" << fallbackReason;
          log_client_line(oss.str());
          lastUserFeedbackUs = presentUs;
          lastUserFeedbackOverwrite = overwriteCountNow;
        }
        lastPresentUs = presentUs;
      } else if (pickerVisible || !hasPresentedAtLeastOneFrame) {
        // Before first successful frame, keep a deterministic background.
        FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
      }
      draw_overlay(hdc);
      EndPaint(hwnd, &ps);
      uint64_t latestVersion = 0;
      {
        std::lock_guard<std::mutex> lk(gFrame.mu);
        latestVersion = gFrame.version;
      }
      if (!pickerVisible && latestVersion != frameVersion) {
        if (!gPaintQueued.exchange(true)) {
          InvalidateRect(hwnd, nullptr, FALSE);
        } else {
          ++gPaintCoalescedCount;
        }
      }
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

// UNICODE is not defined for this target, so the generic Win32 names resolve to the ANSI
// entry points. This window is registered and created wide, so every message API it touches
// must be the explicit *W form -- DefWindowProcA on a Unicode window read the wide title as
// ANSI and truncated it to "r", and delivered WM_CHAR as ANSI.
bool create_window() {
  HINSTANCE inst = GetModuleHandle(nullptr);
  const wchar_t* cls = L"Remote60NativeVideoClient";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // Keep background unmanaged so WM_ERASEBKGND can suppress flicker.
  wc.hbrBackground = nullptr;
  wc.lpszClassName = cls;
  if (!RegisterClassExW(&wc)) return false;

  gHwnd = CreateWindowExW(0, cls, L"remote60 native video client",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          static_cast<int>(gWindowW), static_cast<int>(gWindowH),
                          nullptr, nullptr, inst, nullptr);
  if (!gHwnd) return false;
  ensure_ui_font(gHwnd);
  // The process is per-monitor DPI aware, so the requested size is physical pixels; rescale
  // to keep the intended logical size on scaled displays.
  if (gUiDpi != 96) {
    SetWindowPos(gHwnd, nullptr, 0, 0, dpi_scale(static_cast<int>(gWindowW)),
                 dpi_scale(static_cast<int>(gWindowH)), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  ShowWindow(gHwnd, SW_SHOW);
  UpdateWindow(gHwnd);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);

  // Decoder/present deadlines should not lose their timeslice to ordinary background work.
  // Keep this reversible for diagnostics and battery-sensitive deployments.
  if (!env_truthy("REMOTE60_NATIVE_NORMAL_PRIORITY")) {
    const BOOL processPriorityOk =
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    const BOOL threadPriorityOk =
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    std::cout << "[native-video-client] latency-priority processAboveNormal="
              << (processPriorityOk ? 1 : 0)
              << " mainThreadAboveNormal=" << (threadPriorityOk ? 1 : 0) << "\n";
  }

  // Without this the OS bitmap-stretches the whole window on a scaled display, which blurs
  // both the panel text and the decoded video. Must run before any window is created.
  if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
  }

  const Args args = parse_args(argc, argv);
  gTraceEvery = args.traceEvery;
  gTraceMax = args.traceMax;
  const uint64_t keyframeReqMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_MIN_INTERVAL_US",
      static_cast<uint32_t>(kKeyframeRequestMinIntervalUsDefault), 10000, 1000000);
  const uint64_t keyframeReqTokenRefillUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_REFILL_US",
      static_cast<uint32_t>(kKeyframeRequestTokenRefillUsDefault), 10000, 2000000);
  const uint32_t keyframeReqTokenCapacity = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_CAPACITY",
      kKeyframeRequestTokenCapacityDefault, 1, 16);
  gKeyframeRequests.Configure(keyframeReqMinIntervalUs, keyframeReqTokenRefillUs, keyframeReqTokenCapacity);
  const uint64_t catchupReenterMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_CATCHUP_REENTER_MIN_INTERVAL_US",
      static_cast<uint32_t>(kCatchupReenterMinIntervalUsDefault), 100000, 3000000);
  const uint64_t staleCaptureDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_STALE_CAPTURE_DROP_US",
      static_cast<uint32_t>(kStaleCaptureDropUs), 1000, 2000000);
  const uint64_t congestionRecoverMinUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVER_MIN_US",
      static_cast<uint32_t>(kCongestionRecoverMinUsDefault), 50000, 5000000);
  const uint64_t congestionRecoveryTimeoutUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVERY_TIMEOUT_US",
      static_cast<uint32_t>(kCongestionRecoveryTimeoutUsDefault), 100000, 10000000);
  const uint32_t udpSimDropPm = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_PM", 0, 0, 1000);
  const uint32_t udpSimDropSeed = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_SEED", 0, 0, 0x7fffffffu);
  gKeyframeRequests.Reset();

  const bool useRaw = (args.codec == "raw");
  const bool useH264 = (args.codec == "h264");
  const bool encodedExperimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  if (!useRaw && !useH264) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec << " (supported: raw,h264)\n";
    return 10;
  }
  if (useH264 && !encodedExperimentEnabled) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 10;
  }
  std::string effectiveTransport = args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = useH264 ? "udp" : "tcp";
  }
  VideoTransport transport = VideoTransport::Tcp;
  if (!parse_video_transport(effectiveTransport, &transport)) {
    std::cerr << "[native-video-client] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 12;
  }
  if (transport == VideoTransport::Udp && useRaw) {
    std::cerr << "[native-video-client] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 13;
  }

  gOverlayConfig.host = args.host;
  gOverlayConfig.port = args.port;
  gOverlayConfig.controlPort = args.controlPort;
  gOverlayConfig.transport = video_transport_name(transport);
  gOverlayConfig.codec = args.codec;
  gOverlayConfig.fpsHint = args.fpsHint;
  gOverlayConfig.controlIntervalMs = args.controlIntervalMs;
  gOverlayConfig.tcpRecvBufKb = args.tcpRecvBufKb;
  gOverlayConfig.tcpSendBufKb = args.tcpSendBufKb;
  gOverlayConfig.udpMtu = args.udpMtu;
  gOverlayConfig.keyReqMinIntervalUs = gKeyframeRequests.min_interval_us();
  gOverlayConfig.keyReqTokenRefillUs = gKeyframeRequests.token_refill_us();
  gOverlayConfig.keyReqTokenCapacity = gKeyframeRequests.token_capacity();
  gOverlayConfig.udpSimDropPm = udpSimDropPm;
  gRuntimeTuneState.Reset(args.runtimeBitrate, args.runtimeKeyint);
  gControlConnected.store(false, std::memory_order_relaxed);
  const bool startInStreamView = env_truthy("REMOTE60_NATIVE_START_STREAM_VIEW");
  gCaptureOverviewMode.store(!startInStreamView, std::memory_order_relaxed);
  gWindowPickerVisible.store(!startInStreamView, std::memory_order_relaxed);
  gCaptureModeRequests.Reset();
  gWindowPanelState.Reset();
  gSuppressMouseUntilUs.store(0, std::memory_order_relaxed);
  gActiveTouchPointerId.store(0, std::memory_order_relaxed);
  gActiveTouchDown.store(false, std::memory_order_relaxed);
  gSuppressedImeCharCount.store(0, std::memory_order_relaxed);

  WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-client] WSAStartup failed\n";
    return 1;
  }

  if (!create_window()) {
    std::cerr << "[native-video-client] window create failed\n";
    return 2;
  }

  bool mfStarted = false;
  H264Decoder decoder;
  bool decoderReady = false;
  bool waitForKeyFrame = useH264;
  uint32_t decoderW = 0;
  uint32_t decoderH = 0;
  Microsoft::WRL::ComPtr<ID3D11Device> decD3dDevice;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> decD3dContext;
  if (useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-client] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      return 11;
    }
    mfStarted = true;
    // Supplying AMD's decoder with an external DXGI device manager can enter atidxx64's
    // direct-surface path even when the caller later reads a CPU buffer. Keep the proven
    // system-memory decoder path as the safe default; the zero-copy experiment is an
    // explicit opt-in because affected drivers can TDR or access-violate in that path.
    const bool enableDxgiDecodeSurface =
        env_truthy("REMOTE60_NATIVE_DXGI_DECODE_SURFACE") &&
        !env_truthy("REMOTE60_NATIVE_DISABLE_DXGI_DECODE_SURFACE");
    if (enableDxgiDecodeSurface) {
      // Decode and paint share one D3D11 device so an opt-in hardware-decoder NV12 surface
      // can be sampled directly without a GPU->CPU copy and CPU->GPU upload.
      if (!gNv12Renderer.ready) (void)gNv12Renderer.init(gHwnd);
      if (gNv12Renderer.ready) {
        decD3dDevice = gNv12Renderer.device;
        decD3dContext = gNv12Renderer.context;
        (void)decoder.set_d3d11_device(decD3dDevice.Get());
      } else {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        const HRESULT d3dHr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                D3D11_SDK_VERSION, &decD3dDevice, &fl,
                                                &decD3dContext);
        if (SUCCEEDED(d3dHr) && decD3dDevice) {
          (void)decoder.set_d3d11_device(decD3dDevice.Get());
        }
      }
    }
  }

  gSock = socket(AF_INET,
                 (transport == VideoTransport::Udp) ? SOCK_DGRAM : SOCK_STREAM,
                 (transport == VideoTransport::Udp) ? IPPROTO_UDP : IPPROTO_TCP);
  if (gSock == INVALID_SOCKET) {
    std::cerr << "[native-video-client] socket create failed\n";
    if (mfStarted) MFShutdown();
    return 3;
  }

  if (transport == VideoTransport::Tcp) {
    int noDelay = 1;
    setsockopt(gSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  }
  if (transport == VideoTransport::Udp) {
    if (args.tcpRecvBufKb == 0) {
      const int recvBuf = 1024 * 1024;
      (void)setsockopt(gSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    }
    if (args.tcpSendBufKb == 0) {
      const int sendBuf = 256 * 1024;
      (void)setsockopt(gSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
    }
  }
  if (args.tcpRecvBufKb > 0) {
    const int recvBuf = static_cast<int>(args.tcpRecvBufKb * 1024u);
    setsockopt(gSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
  }
  if (args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
    setsockopt(gSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(args.port);
  if (inet_pton(AF_INET, args.host.c_str(), &addr.sin_addr) != 1) {
    std::cerr << "[native-video-client] invalid host " << args.host << "\n";
    closesocket(gSock);
    gSock = INVALID_SOCKET;
    if (mfStarted) MFShutdown();
    return 4;
  }
  if (connect(gSock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "[native-video-client] connect failed " << args.host << ":" << args.port << "\n";
    closesocket(gSock);
    gSock = INVALID_SOCKET;
    if (mfStarted) MFShutdown();
    return 5;
  }
  if (transport == VideoTransport::Udp) {
    int timeoutMs = 200;
    (void)setsockopt(gSock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    bool handshakeOk = false;
    for (int attempt = 0; attempt < 40 && !handshakeOk; ++attempt) {
      UdpHelloPacket hello{};
      hello.kind = static_cast<uint16_t>(UdpPacketKind::Hello);
      const int sent = send(gSock, reinterpret_cast<const char*>(&hello), sizeof(hello), 0);
      if (sent <= 0) {
        Sleep(50);
        continue;
      }
      UdpHelloPacket ack{};
      const int n = recv(gSock, reinterpret_cast<char*>(&ack), sizeof(ack), 0);
      if (n >= static_cast<int>(sizeof(UdpHelloPacket)) &&
          ack.magic == remote60::native_poc::kMagic &&
          ack.kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) &&
          ack.version == remote60::native_poc::kUdpProtocolVersion &&
          (ack.features & remote60::native_poc::kUdpFeatureVideoFec) != 0) {
        handshakeOk = true;
        break;
      }
      Sleep(50);
    }
    timeoutMs = 0;
    (void)setsockopt(gSock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    if (!handshakeOk) {
      std::cerr << "[native-video-client] udp handshake failed " << args.host << ":" << args.port << "\n";
      closesocket(gSock);
      gSock = INVALID_SOCKET;
      if (mfStarted) MFShutdown();
      return 6;
    }
  }

  std::cout << "[native-video-client] connected host=" << args.host
            << " port=" << args.port
            << " transport=" << video_transport_name(transport)
            << " codec=" << args.codec
            << " seconds=" << args.seconds << "\n";
  std::cout << "[native-video-client] keyframe-request-limiter minIntervalUs="
            << gKeyframeRequests.min_interval_us()
            << " tokenRefillUs=" << gKeyframeRequests.token_refill_us()
            << " tokenCapacity=" << gKeyframeRequests.token_capacity()
            << " catchupReenterMinUs=" << catchupReenterMinIntervalUs
            << " staleCaptureDropUs=" << staleCaptureDropUs
            << " congestionRecoverMinUs=" << congestionRecoverMinUs
            << " congestionRecoveryTimeoutUs=" << congestionRecoveryTimeoutUs
            << "\n";
  if (kInputPolicyForceBlock) {
    std::cout << "[native-video-client] input channel blocked by compile-time policy\n";
  }
  int effectiveRecvBuf = 0;
  int effectiveRecvBufLen = sizeof(effectiveRecvBuf);
  (void)getsockopt(gSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&effectiveRecvBuf), &effectiveRecvBufLen);
  int effectiveSendBuf = 0;
  int effectiveSendBufLen = sizeof(effectiveSendBuf);
  (void)getsockopt(gSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
  std::cout << "[native-video-client] socket rcvbuf=" << effectiveRecvBuf
            << " sndbuf=" << effectiveSendBuf << " bytes\n";

  SOCKET controlSock = INVALID_SOCKET;
  std::thread controlThread;
  if (args.controlPort > 0) {
    controlSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (controlSock != INVALID_SOCKET) {
      int ctlNoDelay = 1;
      setsockopt(controlSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
      sockaddr_in ctlAddr{};
      ctlAddr.sin_family = AF_INET;
      ctlAddr.sin_port = htons(args.controlPort);
      if (inet_pton(AF_INET, args.host.c_str(), &ctlAddr.sin_addr) == 1 &&
          connect(controlSock, reinterpret_cast<const sockaddr*>(&ctlAddr), sizeof(ctlAddr)) == 0) {
        const bool inputChannelEnabled = args.enableInputChannel && !kInputPolicyForceBlock;
        gInputEnabled = inputChannelEnabled;
        gControlScheduler.Reset(args.controlIntervalMs, qpc_now_us());
        controlThread = std::thread([&]() {
        // Fetch one queued preview over the control socket. Runs between scheduler
        // actions on the same strict request/response pipeline, one card per call so a
        // large backlog cannot starve input events. Only invoked when the host advertised
        // the capability, because an older host would drain the request and never reply.
        // Returns: 1 fetched, 0 nothing to do, -1 socket failure (stream desynced).
        auto fetch_one_thumbnail = [&]() -> int {
          if (!gHostSupportsThumbnails.load(std::memory_order_relaxed)) return 0;
          uint64_t id = 0;
          {
            std::lock_guard<std::mutex> lk(gThumbMu);
            if (gThumbFetchQueue.empty()) return 0;
            id = gThumbFetchQueue.front();
            gThumbFetchQueue.pop_front();
          }
          remote60::native_poc::ControlWindowThumbnailRequestMessage req{};
          req.header.magic = remote60::native_poc::kMagic;
          req.header.type =
              static_cast<uint16_t>(MessageType::ControlWindowThumbnailRequest);
          req.header.size = static_cast<uint16_t>(sizeof(req));
          req.seq = 0;
          req.windowId = id;
          req.maxWidth = 256;
          req.maxHeight = 160;
          req.clientSendQpcUs = qpc_now_us();
          if (!remote60::native_poc::send_all(controlSock, &req, sizeof(req))) return -1;
          remote60::native_poc::ControlWindowThumbnailHeader rsp{};
          if (!remote60::native_poc::recv_all(controlSock, &rsp, sizeof(rsp))) return -1;
          if (rsp.header.magic != remote60::native_poc::kMagic ||
              rsp.header.type != static_cast<uint16_t>(MessageType::ControlWindowThumbnail) ||
              rsp.payloadSize > remote60::native_poc::kWindowThumbnailMaxPayloadBytes) {
            return -1;
          }
          std::vector<uint8_t> payload(rsp.payloadSize);
          if (rsp.payloadSize > 0 &&
              !remote60::native_poc::recv_all(controlSock, payload.data(), payload.size())) {
            return -1;
          }
          if ((rsp.flags & 0x1u) != 0 && rsp.width > 0 && rsp.height > 0 &&
              payload.size() == static_cast<size_t>(rsp.width) * rsp.height * 4u) {
            auto thumb = std::make_shared<WindowThumb>();
            thumb->width = rsp.width;
            thumb->height = rsp.height;
            thumb->bgra = std::move(payload);
            thumb->fetchedUs = qpc_now_us();
            {
              std::lock_guard<std::mutex> lk(gThumbMu);
              gThumbs[id] = std::move(thumb);
            }
            // Outside the lock: the paint handler takes gThumbMu, and invalidating while
            // holding it invited a stall on every received preview.
            InvalidateRect(gHwnd, nullptr, FALSE);
          }
          return 1;
        };
          while (gRunning.load()) {
            bool didWork = false;
            const uint64_t nowUs = qpc_now_us();
            ControlOutboundAction action{};
            if (gControlScheduler.NextAction(
                    nowUs, capture_client_control_metrics_snapshot(), &gWindowPanelState,
                    &gStreamStateControl, &gCaptureModeRequests, &gKeyframeRequests, &gRuntimeTuneState,
                    &gInputQueueState, &action)) {
              TcpControlResponse response{};
              // The desktop client only ever talks to a host it can reach directly, so it
              // stays on TCP; the link wrapper is stateless for that transport.
              remote60::native_poc::TcpControlLink controlLink(controlSock);
              if (!execute_control_action(controlLink, action, &response)) break;
              didWork = true;

              if (action.kind == ControlOutboundActionKind::CaptureMode) {
                gCaptureOverviewMode.store(action.captureMode.mode == 1, std::memory_order_relaxed);
                std::cout << "[native-video-client][control] capture-mode-request seq=" << action.captureMode.seq
                          << " mode=" << action.captureMode.mode
                          << " xPermille=" << action.captureMode.xPermille
                          << " yPermille=" << action.captureMode.yPermille
                          << "\n";
              } else if (action.kind == ControlOutboundActionKind::KeyframeRequest) {
                std::cout << "[native-video-client][control] keyframe-request seq=" << action.keyframe.seq
                          << " reason=" << action.keyframe.reason << "\n";
              } else if (action.kind == ControlOutboundActionKind::StreamState) {
                std::cout << "[native-video-client][control] stream-state seq="
                          << action.streamState.seq
                          << " active=" << ((action.streamState.flags & 0x1u) ? 1 : 0) << "\n";
              } else if (action.kind == ControlOutboundActionKind::RuntimeTune) {
                std::cout << "[native-video-client][control] runtime-config seq=" << action.runtimeTune.seq
                          << " bitrate=" << action.runtimeTune.bitrate
                          << " keyint=" << action.runtimeTune.keyint
                          << " flags=" << action.runtimeTune.flags
                          << "\n";
              }

              switch (response.kind) {
                case TcpControlResponseKind::Pong: {
                  const auto& pong = response.pong;
                  const uint64_t doneUs = qpc_now_us();
                  gControlScheduler.OnPingCompleted(doneUs);
                  gHostCaptureTargetPid.store(pong.captureTargetPid, std::memory_order_relaxed);
                  gHostCaptureTargetFlags.store(pong.captureTargetFlags, std::memory_order_relaxed);
                  gHostCaptureRebindCount.store(pong.captureRebindCount, std::memory_order_relaxed);
                  gHostCaptureTargetHwnd.store(pong.captureTargetHwnd, std::memory_order_relaxed);
                  gHostCaptureMetaUpdatedUs.store(doneUs, std::memory_order_relaxed);
                  gCaptureOverviewMode.store((pong.captureTargetFlags & 0x1u) == 0, std::memory_order_relaxed);
                  {
                    std::lock_guard<std::mutex> lk(gHostCaptureMetaMu);
                    gHostCaptureTargetProcess =
                        fixed_cstr_to_string(pong.captureTargetProcess, sizeof(pong.captureTargetProcess));
                    gHostCaptureTargetTitle =
                        fixed_cstr_to_string(pong.captureTargetTitle, sizeof(pong.captureTargetTitle));
                  }
                  const uint64_t rttUs =
                      (doneUs >= action.ping.clientSendQpcUs) ? (doneUs - action.ping.clientSendQpcUs) : 0;
                  std::cout << "[native-video-client][control] seq=" << pong.seq
                            << " rttUs=" << rttUs
                            << " hostQueueUs=" << ((pong.hostSendQpcUs >= pong.hostRecvQpcUs)
                                                        ? (pong.hostSendQpcUs - pong.hostRecvQpcUs)
                                                        : 0)
                            << " hostCapPid=" << pong.captureTargetPid
                            << " hostCapProc=" << fixed_cstr_to_string(
                                   pong.captureTargetProcess, sizeof(pong.captureTargetProcess))
                            << " hostCapRebind=" << pong.captureRebindCount
                            << "\n";
                  break;
                }
                case TcpControlResponseKind::WindowList:
                  apply_window_list_snapshot(response.windowList);
                  InvalidateRect(gHwnd, nullptr, FALSE);
                  break;
                case TcpControlResponseKind::WindowSelected:
                  apply_window_selected_result(response.windowSelected);
                  queue_window_list_request("window_list_request pending");
                  InvalidateRect(gHwnd, nullptr, FALSE);
                  break;
                case TcpControlResponseKind::InputAck: {
                  const uint64_t ackCount = gControlScheduler.RecordInputAck(args.inputLogEvery);
                  if (ackCount > 0) {
                    std::cout << "[native-video-client][input] ackSeq=" << response.inputAck.seq
                              << " sent=" << ackCount
                              << " dropped=" << gInputQueueState.dropped_count()
                              << "\n";
                  }
                  break;
                }
                case TcpControlResponseKind::None:
                default:
                  break;
              }
            }

            if (!didWork && gWindowPickerVisible.load(std::memory_order_relaxed)) {
              const int fetched = fetch_one_thumbnail();
              if (fetched < 0) break;
              didWork = (fetched > 0);
            }
            if (!didWork) Sleep(2);
          }
          gControlConnected.store(false, std::memory_order_relaxed);
          gRuntimeTuneState.SetEnabled(false);
          set_window_panel_status("control_disconnected");
          InvalidateRect(gHwnd, nullptr, FALSE);
        });
        gControlConnected.store(true, std::memory_order_relaxed);
        gRuntimeTuneState.SetEnabled(useH264);
        queue_window_list_request("window_list_request pending");
        if (useH264 && (args.runtimeBitrate > 0 || args.runtimeKeyint > 0)) {
          gRuntimeTuneState.MarkDirty();
        }
        std::cout << "[native-video-client] control connected port=" << args.controlPort
                  << " inputChannel=" << (inputChannelEnabled ? 1 : 0) << "\n";
      } else {
        closesocket(controlSock);
        controlSock = INVALID_SOCKET;
        gControlConnected.store(false, std::memory_order_relaxed);
        gRuntimeTuneState.SetEnabled(false);
        set_window_panel_status("control_connect_failed");
        std::cout << "[native-video-client] control connect failed port=" << args.controlPort << "\n";
      }
    }
  }

  const uint64_t startUs = qpc_now_us();
  std::thread recvThread([&]() {
    uint64_t statAtUs = qpc_now_us() + 1000000ULL;
    uint64_t recvFrames = 0;
    uint64_t decodedFrames = 0;
    uint64_t skippedQueued = 0;
    uint64_t recvBytes = 0;
    uint64_t decodedBytes = 0;
    uint64_t sumLatencyUs = 0;
    uint64_t maxLatencyUs = 0;
    uint64_t sumDecodeTailUs = 0;
    uint64_t maxDecodeTailUs = 0;
    uint64_t decodeFailCount = 0;
    uint64_t decodeTimestampOverflowCount = 0;
    uint64_t decodeEmptyCount = 0;
    uint64_t decodeEmptyStreak = 0;
    uint64_t decodeEmptyStreakStartUs = 0;
    uint64_t decodeEmptyRecoveryCount = 0;
    uint64_t waitingKeyDropCount = 0;
    uint64_t lagDropCount = 0;
    uint64_t udpChunkRecvCount = 0;
    uint64_t udpAssemblyCompletedCount = 0;
    uint64_t udpAssemblyDroppedCount = 0;
    uint64_t udpAssemblyMalformedCount = 0;
    uint64_t udpAssemblyReorderCount = 0;
    uint64_t udpAssemblyKeyReqCount = 0;
    uint64_t udpAssemblyFecRecoveredCount = 0;
    uint32_t udpAssemblyDropPmLast = 0;
    uint64_t lastPacketRecvUs = 0;
    uint32_t lagTriggerStreak = 0;
    uint64_t lastCatchupEnterUs = 0;
    uint64_t catchupEnterThrottledCount = 0;
    bool catchupMode = false;
    // lastPresentedCaptureUs is now gLastPresentedCaptureUs (atomic, updated after actual present)
    bool captureTimelineReady = false;
    uint64_t captureRemoteBaseUs = 0;
    uint64_t captureLocalBaseUs = 0;
    bool sendTimelineReady = false;
    uint64_t sendRemoteBaseUs = 0;
    uint64_t sendLocalBaseUs = 0;
    const uint64_t frameIntervalUs = std::max<uint64_t>(
        1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, args.fpsHint)));
    ClientCongestionState congestionState = ClientCongestionState::Normal;
    uint64_t congestionStateEnterUs = 0;
    uint64_t congestionTransitionCount = 0;
    uint64_t congestionRecoveryCount = 0;
    uint64_t congestionRecoveryTotalUs = 0;
    uint64_t congestionRecoveryMaxUs = 0;
    uint64_t congestionRecoveryRequestCount = 0;
    uint64_t staleDropCount = 0;
    uint64_t holdLatestDropCount = 0;
    uint64_t burstDropCount = 0;
    uint64_t latestCaptureSeenUs = 0;
    uint64_t queueDepthSampleCount = 0;
    uint64_t queueDepthHist[5] = {0, 0, 0, 0, 0};
    uint32_t queueDepthFramesMax = 0;
    uint64_t recoveringSinceUs = 0;
    uint32_t recoveringHealthyStreak = 0;
    uint64_t lastRecoveryRequestUs = 0;
    auto queue_depth_frames = [&](uint64_t lagUs) -> uint32_t {
      if (lagUs == 0) return 0;
      const uint64_t depth64 = (lagUs + frameIntervalUs - 1) / frameIntervalUs;
      return static_cast<uint32_t>(std::min<uint64_t>(depth64, 1000ULL));
    };
    auto sample_queue_depth = [&](uint64_t lagUs) {
      const uint32_t depthFrames = queue_depth_frames(lagUs);
      ++queueDepthSampleCount;
      if (depthFrames > queueDepthFramesMax) queueDepthFramesMax = depthFrames;
      if (depthFrames == 0) {
        ++queueDepthHist[0];
      } else if (depthFrames == 1) {
        ++queueDepthHist[1];
      } else if (depthFrames == 2) {
        ++queueDepthHist[2];
      } else if (depthFrames == 3) {
        ++queueDepthHist[3];
      } else {
        ++queueDepthHist[4];
      }
    };
    auto transition_congestion_state = [&](ClientCongestionState nextState, uint64_t nowUs, const char* reason,
                                           uint64_t streamLagUs, uint64_t decodeQueueLagEstimateUs, uint32_t seq) {
      if (nextState == congestionState) return;
      const ClientCongestionState prev = congestionState;
      if (prev != ClientCongestionState::Normal &&
          nextState == ClientCongestionState::Normal &&
          congestionStateEnterUs > 0 &&
          nowUs >= congestionStateEnterUs) {
        const uint64_t recoverUs = nowUs - congestionStateEnterUs;
        ++congestionRecoveryCount;
        congestionRecoveryTotalUs += recoverUs;
        if (recoverUs > congestionRecoveryMaxUs) congestionRecoveryMaxUs = recoverUs;
      }
      congestionState = nextState;
      congestionStateEnterUs = (nextState == ClientCongestionState::Normal) ? 0 : nowUs;
      if (nextState == ClientCongestionState::Recovering) {
        recoveringSinceUs = nowUs;
        recoveringHealthyStreak = 0;
      } else if (nextState != ClientCongestionState::Recovering) {
        recoveringSinceUs = 0;
        recoveringHealthyStreak = 0;
      }
      ++congestionTransitionCount;
      std::cout << "[native-video-client][congestion] state=" << congestion_state_name(nextState)
                << " prev=" << congestion_state_name(prev)
                << " reason=" << reason
                << " streamLagUs=" << streamLagUs
                << " decodeQueueLagUs=" << decodeQueueLagEstimateUs
                << " seq=" << seq
                << "\n";
    };
    struct PresentCounterSnapshot {
      uint64_t d3dPresentSuccess = 0;
      uint64_t d3dPresentFail = 0;
      uint64_t gdiFallbackPresented = 0;
      uint64_t fallbackInitFail = 0;
      uint64_t fallbackRenderFail = 0;
      uint64_t fallbackNv12ConvertFail = 0;
      uint64_t paintCoalesced = 0;
      uint64_t overwriteBeforePresent = 0;
    };
    auto load_present_counters = [&]() -> PresentCounterSnapshot {
      PresentCounterSnapshot s{};
      s.d3dPresentSuccess = gD3dPresentSuccessCount.load(std::memory_order_relaxed);
      s.d3dPresentFail = gD3dPresentFailCount.load(std::memory_order_relaxed);
      s.gdiFallbackPresented = gGdiFallbackPresentedCount.load(std::memory_order_relaxed);
      s.fallbackInitFail = gFallbackInitFailCount.load(std::memory_order_relaxed);
      s.fallbackRenderFail = gFallbackRenderFailCount.load(std::memory_order_relaxed);
      s.fallbackNv12ConvertFail = gFallbackNv12ConvertFailCount.load(std::memory_order_relaxed);
      s.paintCoalesced = gPaintCoalescedCount.load(std::memory_order_relaxed);
      s.overwriteBeforePresent = gOverwriteBeforePresentCount.load(std::memory_order_relaxed);
      return s;
    };
    PresentCounterSnapshot lastPresentCounters = load_present_counters();
    auto append_present_counter_fields = [&](std::ostream& os) {
      const PresentCounterSnapshot nowCounters = load_present_counters();
      const uint64_t d3dPresentSuccess = nowCounters.d3dPresentSuccess - lastPresentCounters.d3dPresentSuccess;
      const uint64_t d3dPresentFail = nowCounters.d3dPresentFail - lastPresentCounters.d3dPresentFail;
      const uint64_t gdiFallbackPresented =
          nowCounters.gdiFallbackPresented - lastPresentCounters.gdiFallbackPresented;
      const uint64_t fallbackInitFail = nowCounters.fallbackInitFail - lastPresentCounters.fallbackInitFail;
      const uint64_t fallbackRenderFail = nowCounters.fallbackRenderFail - lastPresentCounters.fallbackRenderFail;
      const uint64_t fallbackNv12ConvertFail =
          nowCounters.fallbackNv12ConvertFail - lastPresentCounters.fallbackNv12ConvertFail;
      const uint64_t paintCoalesced = nowCounters.paintCoalesced - lastPresentCounters.paintCoalesced;
      const uint64_t overwriteBeforePresent =
          nowCounters.overwriteBeforePresent - lastPresentCounters.overwriteBeforePresent;
      const uint64_t d3dAttempts = d3dPresentSuccess + d3dPresentFail;
      const uint64_t gdiFallbackRateX1000 = (d3dAttempts > 0)
          ? ((gdiFallbackPresented * 1000ULL) / d3dAttempts)
          : 0;
      os << " d3dPresentSuccess=" << d3dPresentSuccess
         << " d3dPresentFail=" << d3dPresentFail
         << " gdiFallbackPresented=" << gdiFallbackPresented
         << " gdiFallbackRateX1000=" << gdiFallbackRateX1000
         << " fallbackInitFail=" << fallbackInitFail
         << " fallbackRenderFail=" << fallbackRenderFail
         << " fallbackNv12ConvertFail=" << fallbackNv12ConvertFail
         << " paintCoalesced=" << paintCoalesced
         << " overwriteBeforePresent=" << overwriteBeforePresent;
      lastPresentCounters = nowCounters;
    };
    auto append_congestion_fields = [&](std::ostream& os) {
      const uint64_t recoveryAvgUs =
          (congestionRecoveryCount > 0) ? (congestionRecoveryTotalUs / congestionRecoveryCount) : 0;
      os << " congestionState=" << congestion_state_name(congestionState)
         << " congestionTransitions=" << congestionTransitionCount
         << " congestionRecoveryCount=" << congestionRecoveryCount
         << " congestionRecoveryAvgUs=" << recoveryAvgUs
         << " congestionRecoveryMaxUs=" << congestionRecoveryMaxUs
         << " congestionRecoveryReq=" << congestionRecoveryRequestCount
         << " staleDrops=" << staleDropCount
         << " holdLatestDrops=" << holdLatestDropCount
         << " burstDrops=" << burstDropCount
         << " queueDepthSamples=" << queueDepthSampleCount
         << " queueDepthMax=" << queueDepthFramesMax
         << " queueDepthH0=" << queueDepthHist[0]
         << " queueDepthH1=" << queueDepthHist[1]
         << " queueDepthH2=" << queueDepthHist[2]
         << " queueDepthH3=" << queueDepthHist[3]
         << " queueDepthH4p=" << queueDepthHist[4];
    };
    auto aligned_lag_us = [&](uint64_t remoteTsUs, uint64_t localNowUs,
                              bool& timelineReady, uint64_t& remoteBaseUs, uint64_t& localBaseUs) -> uint64_t {
      if (!timelineReady || remoteTsUs < remoteBaseUs) {
        timelineReady = true;
        remoteBaseUs = remoteTsUs;
        localBaseUs = localNowUs;
        return 0;
      }
      const uint64_t remoteDeltaUs = remoteTsUs - remoteBaseUs;
      uint64_t expectedLocalUs = localBaseUs;
      if (std::numeric_limits<uint64_t>::max() - expectedLocalUs < remoteDeltaUs) {
        expectedLocalUs = std::numeric_limits<uint64_t>::max();
      } else {
        expectedLocalUs += remoteDeltaUs;
      }
      return (localNowUs >= expectedLocalUs) ? (localNowUs - expectedLocalUs) : 0;
    };
    auto publish_metrics = [&](uint32_t metricW, uint32_t metricH, uint64_t nowUs,
                               uint64_t avgLatencyUs, uint64_t maxLatencyUsLocal,
                               uint64_t avgDecodeTailUs, uint64_t maxDecodeTailUsLocal,
                               double mbpsLocal) {
      const uint64_t cappedRecvFpsX100 = std::min<uint64_t>(recvFrames * 100ULL, 0xFFFFFFFFULL);
      const uint64_t cappedDecodedFpsX100 = std::min<uint64_t>(decodedFrames * 100ULL, 0xFFFFFFFFULL);
      const double mbpsX1000 = mbpsLocal * 1000.0;
      uint32_t recvMbpsX1000 = 0;
      if (mbpsX1000 > 0.0) {
        recvMbpsX1000 = static_cast<uint32_t>(
            std::min<double>(mbpsX1000, static_cast<double>(0xFFFFFFFFu)));
      }
      gClientMetrics.width = metricW;
      gClientMetrics.height = metricH;
      gClientMetrics.recvFpsX100 = static_cast<uint32_t>(cappedRecvFpsX100);
      gClientMetrics.decodedFpsX100 = static_cast<uint32_t>(cappedDecodedFpsX100);
      gClientMetrics.recvMbpsX1000 = recvMbpsX1000;
      gClientMetrics.skippedFrames = static_cast<uint32_t>(std::min<uint64_t>(skippedQueued, 0xFFFFFFFFULL));
      gClientMetrics.avgLatencyUs = avgLatencyUs;
      gClientMetrics.maxLatencyUs = maxLatencyUsLocal;
      gClientMetrics.avgDecodeTailUs = avgDecodeTailUs;
      gClientMetrics.maxDecodeTailUs = maxDecodeTailUsLocal;
      gClientMetrics.congestionState = static_cast<uint32_t>(congestionState);
      gClientMetrics.congestionTransitions =
          static_cast<uint32_t>(std::min<uint64_t>(congestionTransitionCount, 0xFFFFFFFFULL));
      gClientMetrics.congestionRecoveryCount =
          static_cast<uint32_t>(std::min<uint64_t>(congestionRecoveryCount, 0xFFFFFFFFULL));
      gClientMetrics.congestionRecoveryReq =
          static_cast<uint32_t>(std::min<uint64_t>(congestionRecoveryRequestCount, 0xFFFFFFFFULL));
      gClientMetrics.congestionRecoveryMaxUs =
          static_cast<uint32_t>(std::min<uint64_t>(congestionRecoveryMaxUs, 0xFFFFFFFFULL));
      gClientMetrics.queueDepthMax = queueDepthFramesMax;
      gClientMetrics.queueDepthH4p =
          static_cast<uint32_t>(std::min<uint64_t>(queueDepthHist[4], 0xFFFFFFFFULL));
      gClientMetrics.udpAssemblyDropPm = udpAssemblyDropPmLast;
      gClientMetrics.seq.fetch_add(1);
      gClientMetrics.updatedQpcUs = nowUs;
      push_overlay_metric_sample(gClientMetrics.recvFpsX100.load(std::memory_order_relaxed),
                                 gClientMetrics.decodedFpsX100.load(std::memory_order_relaxed),
                                 gClientMetrics.recvMbpsX1000.load(std::memory_order_relaxed),
                                 gClientMetrics.avgLatencyUs.load(std::memory_order_relaxed),
                                 nowUs);
      if (gHwnd && !gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (!gPaintQueued.exchange(true)) {
          InvalidateRect(gHwnd, nullptr, FALSE);
        } else {
          ++gPaintCoalescedCount;
        }
      }
    };
    auto process_h264_frame = [&](const EncodedFrameHeader& h, std::vector<uint8_t>* payloadPtr,
                                  uint64_t packetNowUs) -> bool {
      if (!payloadPtr) return true;
      ++recvFrames;
      recvBytes += h.payloadSize;
      const uint64_t recvGapUs =
          (lastPacketRecvUs > 0 && packetNowUs >= lastPacketRecvUs) ? (packetNowUs - lastPacketRecvUs) : 0;
      lastPacketRecvUs = packetNowUs;
      if (recvGapUs > 250000) {
        // Sparse arrival usually means source/capture stall, not decoder backlog.
        lagTriggerStreak = 0;
      }

      if (!useH264) {
        ++skippedQueued;
        return true;
      }

      if (!decoderReady || decoderW != h.width || decoderH != h.height) {
        if (!decoder.initialize(h.width, h.height, args.fpsHint)) {
          std::cerr << "[native-video-client] H264 decoder initialize failed size=" << h.width << "x" << h.height
                    << "\n";
          return false;
        }
    const std::string requestedDecoderBackend = env_string_or_empty("REMOTE60_NATIVE_DECODER_BACKEND");
    const std::string requestedDecoderBackendPrint =
        requestedDecoderBackend.empty() ? "default(mft_auto)" : requestedDecoderBackend;
        const std::string backendFallbackReason =
            backend_fallback_reason(requestedDecoderBackend, decoder.backend_name());
        std::cout << "[native-video-client] H264 decoder backend=" << decoder.backend_name()
                  << " backendRequested=" << requestedDecoderBackendPrint
                  << " backendResolved=" << decoder.backend_name()
                  << " backendFallbackReason=" << backendFallbackReason
                  << " hw=" << (decoder.using_hardware() ? 1 : 0)
                  << " size=" << h.width << "x" << h.height << "\n";
        decoderReady = true;
        decoderW = h.width;
        decoderH = h.height;
        waitForKeyFrame = true;
      }

      const bool keyFrame = ((h.flags & 1u) != 0);
      if (h.captureQpcUs > latestCaptureSeenUs) {
        latestCaptureSeenUs = h.captureQpcUs;
      }
      const uint64_t streamLagUs = aligned_lag_us(
          h.captureQpcUs, packetNowUs, captureTimelineReady, captureRemoteBaseUs, captureLocalBaseUs);
      const uint64_t presentedCapUs = gLastPresentedCaptureUs.load(std::memory_order_relaxed);
      const uint64_t decodeQueueLagEstimateUs =
          (presentedCapUs > 0 && h.captureQpcUs >= presentedCapUs)
              ? (h.captureQpcUs - presentedCapUs)
              : 0;
      sample_queue_depth(decodeQueueLagEstimateUs);
      const uint64_t staleBehindPresentedUs =
          (presentedCapUs > 0 && presentedCapUs > h.captureQpcUs)
              ? (presentedCapUs - h.captureQpcUs)
              : 0;
      const uint64_t staleBehindLatestUs =
          (latestCaptureSeenUs > h.captureQpcUs)
              ? (latestCaptureSeenUs - h.captureQpcUs)
              : 0;
      if (staleBehindPresentedUs > staleCaptureDropUs || staleBehindLatestUs > staleCaptureDropUs) {
        ++skippedQueued;
        ++lagDropCount;
        ++staleDropCount;
        if (staleBehindLatestUs > staleCaptureDropUs) {
          ++holdLatestDropCount;
        }
        if ((lagDropCount % 120) == 1) {
          std::cout << "[native-video-client] stale frame drop count=" << lagDropCount
                    << " staleBehindPresentedUs=" << staleBehindPresentedUs
                    << " staleBehindLatestUs=" << staleBehindLatestUs
                    << " seq=" << h.seq << "\n";
        }
        return true;
      }

      const bool lagTrigger =
          (decodeQueueLagEstimateUs > kDecodeQueueLagDropUs) ||
          (presentedCapUs > 0 && streamLagUs > kCatchupLagDropUs);
      const bool denseArrival = (recvGapUs == 0 || recvGapUs <= 150000);
      if (lagTrigger && denseArrival) {
        if (lagTriggerStreak < std::numeric_limits<uint32_t>::max()) {
          ++lagTriggerStreak;
        }
      } else {
        lagTriggerStreak = 0;
      }
      if (congestionState != ClientCongestionState::Congested && lagTriggerStreak >= 3) {
        lagTriggerStreak = 0;
        const bool catchupEnterAllowed =
            (lastCatchupEnterUs == 0) || (packetNowUs >= (lastCatchupEnterUs + catchupReenterMinIntervalUs));
        if (!catchupEnterAllowed) {
          ++catchupEnterThrottledCount;
          if ((catchupEnterThrottledCount % 120) == 1) {
            std::cout << "[native-video-client] catchup-enter-throttled count="
                      << catchupEnterThrottledCount
                      << " streamLagUs=" << streamLagUs
                      << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                      << " minIntervalUs=" << catchupReenterMinIntervalUs
                      << "\n";
          }
        } else {
          transition_congestion_state(ClientCongestionState::Congested, packetNowUs,
                                      (decodeQueueLagEstimateUs > kDecodeQueueLagDropUs)
                                          ? "decode_queue"
                                          : "stream_lag_emergency",
                                      streamLagUs, decodeQueueLagEstimateUs, h.seq);
          catchupMode = true;
          lastCatchupEnterUs = packetNowUs;
          waitForKeyFrame = true;
          decoder.reset();
          request_keyframe(1);
          ++congestionRecoveryRequestCount;
          std::cout << "[native-video-client] catchup enter streamLagUs=" << streamLagUs
                    << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                    << " recvGapUs=" << recvGapUs
                    << " reason="
                    << ((decodeQueueLagEstimateUs > kDecodeQueueLagDropUs) ? "decode_queue" : "stream_lag_emergency")
                    << " seq=" << h.seq << "\n";
        }
      }
      if (congestionState == ClientCongestionState::Congested && !keyFrame) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++lagDropCount;
        ++burstDropCount;
        if ((lagDropCount % 120) == 1) {
          std::cout << "[native-video-client] catchup drops=" << lagDropCount
                    << " streamLagUs=" << streamLagUs
                    << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                    << "\n";
        }
        return true;
      }
      if (congestionState == ClientCongestionState::Congested && keyFrame) {
        catchupMode = false;
        transition_congestion_state(ClientCongestionState::Recovering, packetNowUs, "keyframe",
                                    streamLagUs, decodeQueueLagEstimateUs, h.seq);
        std::cout << "[native-video-client] catchup exit streamLagUs=" << streamLagUs
                  << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                  << " seq=" << h.seq << "\n";
      }
      if (congestionState == ClientCongestionState::Recovering) {
        const bool lagHealthy =
            decodeQueueLagEstimateUs <= kDecodeQueueLagResumeUs &&
            streamLagUs <= kCatchupResumeKeyLagUs;
        if (lagHealthy) {
          if (recoveringHealthyStreak < std::numeric_limits<uint32_t>::max()) {
            ++recoveringHealthyStreak;
          }
        } else {
          recoveringHealthyStreak = 0;
        }
        const bool recoverMinElapsed =
            recoveringSinceUs > 0 && packetNowUs >= (recoveringSinceUs + congestionRecoverMinUs);
        if (lagHealthy && recoverMinElapsed && recoveringHealthyStreak >= 3) {
          transition_congestion_state(ClientCongestionState::Normal, packetNowUs, "recover_stable",
                                      streamLagUs, decodeQueueLagEstimateUs, h.seq);
        } else if (!lagHealthy &&
                   recoveringSinceUs > 0 &&
                   packetNowUs >= (recoveringSinceUs + congestionRecoveryTimeoutUs)) {
          const bool requestAllowed =
              (lastRecoveryRequestUs == 0) || (packetNowUs >= (lastRecoveryRequestUs + 300000));
          if (requestAllowed) {
            request_keyframe(1);
            ++congestionRecoveryRequestCount;
            lastRecoveryRequestUs = packetNowUs;
          }
          catchupMode = true;
          waitForKeyFrame = true;
          decoder.reset();
          lastCatchupEnterUs = packetNowUs;
          transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "recover_timeout",
                                      streamLagUs, decodeQueueLagEstimateUs, h.seq);
        }
      }

      if (waitForKeyFrame && !keyFrame) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++waitingKeyDropCount;
        ++burstDropCount;
        if ((waitingKeyDropCount % 30) == 1) {
          request_keyframe(3);
        }
        if ((waitingKeyDropCount % 120) == 1) {
          std::cout << "[native-video-client] waiting keyframe drops=" << waitingKeyDropCount << "\n";
        }
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, packetNowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }

      const uint64_t decodeStartUs = qpc_now_us();
      std::vector<DecodedFrameNv12> outFrames;
      const int64_t inputSampleTimeHns = static_cast<int64_t>(h.captureQpcUs) * 10;
      bool pendingTimestampOverflow = false;
      if (!decoder.decode_access_unit(*payloadPtr, keyFrame, inputSampleTimeHns, &outFrames,
                                      &pendingTimestampOverflow)) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++decodeFailCount;
        request_keyframe(4);
        ++congestionRecoveryRequestCount;
        if ((decodeFailCount % 60) == 1) {
          std::cout << "[native-video-client] decode failed count=" << decodeFailCount << "\n";
        }
        catchupMode = true;
        lastCatchupEnterUs = packetNowUs;
        waitForKeyFrame = true;
        decoder.reset();
        transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "decode_fail",
                                    streamLagUs, decodeQueueLagEstimateUs, h.seq);
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, packetNowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }
      if (pendingTimestampOverflow) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++decodeTimestampOverflowCount;
        request_keyframe(4);
        ++congestionRecoveryRequestCount;
        if ((decodeTimestampOverflowCount % 10ULL) == 1ULL) {
          std::cout << "[native-video-client] decoder timestamp queue overflow count="
                    << decodeTimestampOverflowCount << "\n";
        }
        catchupMode = true;
        lastCatchupEnterUs = packetNowUs;
        waitForKeyFrame = true;
        decoder.reset();
        transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "decode_ts_overflow",
                                    streamLagUs, decodeQueueLagEstimateUs, h.seq);
        return true;
      }
      waitForKeyFrame = false;
      if (outFrames.empty()) {
        ++decodeEmptyCount;
        ++decodeEmptyStreak;
        if (decodeEmptyStreak == 1) {
          decodeEmptyStreakStartUs = packetNowUs;
        }
        const uint64_t emptyStreakUs =
            (decodeEmptyStreakStartUs > 0 && packetNowUs >= decodeEmptyStreakStartUs)
                ? (packetNowUs - decodeEmptyStreakStartUs)
                : 0;
        if (decodeEmptyStreak >= 12 || emptyStreakUs >= 300000) {
          const bool catchupEnterAllowed =
              (lastCatchupEnterUs == 0) || (packetNowUs >= (lastCatchupEnterUs + catchupReenterMinIntervalUs));
          if (catchupEnterAllowed) {
            ++decodeEmptyRecoveryCount;
            waitForKeyFrame = true;
            catchupMode = true;
            lastCatchupEnterUs = packetNowUs;
            request_keyframe(5);
            ++congestionRecoveryRequestCount;
            decoder.reset();
            transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "decode_empty",
                                        streamLagUs, decodeQueueLagEstimateUs, h.seq);
            if ((decodeEmptyRecoveryCount % 10) == 1) {
              std::cout << "[native-video-client] decode empty recovery count=" << decodeEmptyRecoveryCount
                        << " streak=" << decodeEmptyStreak
                        << " emptyUs=" << emptyStreakUs
                        << "\n";
            }
          } else {
            ++catchupEnterThrottledCount;
            if ((catchupEnterThrottledCount % 120) == 1) {
              std::cout << "[native-video-client] decode-empty-recovery-throttled count="
                        << catchupEnterThrottledCount
                        << " streak=" << decodeEmptyStreak
                        << " emptyUs=" << emptyStreakUs
                        << " minIntervalUs=" << catchupReenterMinIntervalUs
                        << "\n";
            }
          }
          decodeEmptyStreak = 0;
          decodeEmptyStreakStartUs = 0;
        }
        if ((decodeEmptyCount % 120) == 1) {
          std::cout << "[native-video-client] decode output empty count=" << decodeEmptyCount
                    << " streak=" << decodeEmptyStreak
                    << " emptyUs=" << emptyStreakUs
                    << "\n";
        }
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, packetNowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }
      decodeEmptyStreak = 0;
      decodeEmptyStreakStartUs = 0;

      auto& decoded = outFrames.back();
      const bool tsFromMft = decoded.sampleTimeFromOutput && (decoded.sampleTimeHns > 0);
      const bool tsFromInputFallback = (!decoded.sampleTimeFromOutput) && (decoded.sampleTimeHns > 0);
      const bool tsFromHeaderFallback = (decoded.sampleTimeHns <= 0);
      const uint64_t decodedCaptureUs =
          tsFromHeaderFallback ? h.captureQpcUs : static_cast<uint64_t>(decoded.sampleTimeHns / 10);
      const char* tsSource = tsFromMft ? "mft" : (tsFromInputFallback ? "input_fallback" : "header_fallback");
      if (decoded.bytes.empty() && !decoded.surfaceTexture) {
        ++skippedQueued;
        waitForKeyFrame = true;
        return true;
      }
      const uint64_t decodedPayloadBytes = decoded.bytes.empty()
          ? (static_cast<uint64_t>(decoded.width) * decoded.height * 3 / 2)
          : static_cast<uint64_t>(decoded.bytes.size());
      const uint64_t decodeEndUs = qpc_now_us();
      std::shared_ptr<std::vector<uint8_t>> frameNv12;
      if (!decoded.bytes.empty()) {
        frameNv12 = std::make_shared<std::vector<uint8_t>>(std::move(decoded.bytes));
        if (!frameNv12 || frameNv12->empty()) {
          ++skippedQueued;
          waitForKeyFrame = true;
          return true;
        }
      }

      const uint64_t nowUs = qpc_now_us();
      const uint64_t queueSetUs = nowUs;
      const uint64_t decodeToQueueUs = (queueSetUs >= decodeEndUs) ? (queueSetUs - decodeEndUs) : 0;
      {
        std::lock_guard<std::mutex> lk(gFrame.mu);
        const uint64_t prevVersion = gFrame.version;
        const uint64_t lastPresentedVersion = gLastPresentedVersion.load(std::memory_order_relaxed);
        if (prevVersion > lastPresentedVersion) {
          ++gOverwriteBeforePresentCount;
        }
        gFrame.format = SharedFrame::PixelFormat::Nv12;
        gFrame.width = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;
        gFrame.height = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;
        gFrame.codedWidth = decoded.width;
        gFrame.codedHeight = decoded.height;
        gFrame.visibleLeft = decoded.visibleLeft;
        gFrame.visibleTop = decoded.visibleTop;
        gFrame.stride = decoded.width;
        gFrame.seq = h.seq;
        gFrame.captureUs = decodedCaptureUs;
        gFrame.encodeStartUs = h.encodeStartQpcUs;
        gFrame.encodeEndUs = h.encodeEndQpcUs;
        gFrame.sendUs = h.sendQpcUs;
        gFrame.recvUs = packetNowUs;
        gFrame.decodeStartUs = decodeStartUs;
        gFrame.decodeEndUs = decodeEndUs;
        gFrame.queueSetUs = queueSetUs;
        gFrame.decodeToQueueUs = decodeToQueueUs;
        gFrame.streamGeneration = h.streamGeneration;
        gFrame.version = prevVersion + 1;
        gFrame.bytes = std::move(frameNv12);
        gFrame.surfaceSample = std::move(decoded.surfaceSample);
        gFrame.surfaceTexture = std::move(decoded.surfaceTexture);
        gFrame.surfaceSubresource = decoded.surfaceSubresource;
      }
      if (gHwnd) {
        if (!gPaintQueued.exchange(true)) {
          InvalidateRect(gHwnd, nullptr, FALSE);
        } else {
          ++gPaintCoalescedCount;
        }
      }

      if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || gTraceRecvPrinted.load() < args.traceMax)) {
        const auto nowPrinted = gTraceRecvPrinted.fetch_add(1) + 1;
        if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
          std::ostringstream oss;
          oss << "[native-video-client][trace_recv] seq=" << h.seq
              << " captureUs=" << decodedCaptureUs
              << " hdrCaptureUs=" << h.captureQpcUs
              << " encodeStartUs=" << h.encodeStartQpcUs
              << " encodeEndUs=" << h.encodeEndQpcUs
              << " sendUs=" << h.sendQpcUs
              << " recvUs=" << packetNowUs
              << " decodeStartUs=" << decodeStartUs
              << " decodeEndUs=" << decodeEndUs
              << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
              << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
              << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
              << " netUs=" << ((packetNowUs >= h.sendQpcUs) ? (packetNowUs - h.sendQpcUs) : 0)
              << " r2dUs=" << ((decodeStartUs >= packetNowUs) ? (decodeStartUs - packetNowUs) : 0)
              << " decUs=" << ((decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0)
              << " decodeQueueLagUs=" << ((h.captureQpcUs >= decodedCaptureUs) ? (h.captureQpcUs - decodedCaptureUs) : 0)
              << " tsSource=" << tsSource
              << " bytes=" << h.payloadSize
              << " key=" << (keyFrame ? 1 : 0);
          log_client_line(oss.str());
        }
      }

      ++decodedFrames;
      decodedBytes += decodedPayloadBytes;
      // lastPresentedCaptureUs is now updated by render thread via gLastPresentedCaptureUs
      const uint64_t latencyUs = aligned_lag_us(
          decodedCaptureUs, nowUs, captureTimelineReady, captureRemoteBaseUs, captureLocalBaseUs);
      const uint64_t decodeTailUs = aligned_lag_us(
          h.sendQpcUs, nowUs, sendTimelineReady, sendRemoteBaseUs, sendLocalBaseUs);
      sumLatencyUs += latencyUs;
      sumDecodeTailUs += decodeTailUs;
      maxLatencyUs = std::max(maxLatencyUs, latencyUs);
      maxDecodeTailUs = std::max(maxDecodeTailUs, decodeTailUs);

      if (nowUs >= statAtUs) {
        const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
        const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
        const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
        const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
        const uint64_t decodeRatioX100 =
            (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
        const uint32_t visibleW = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;
        const uint32_t visibleH = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;
        publish_metrics(visibleW, visibleH, nowUs,
                        avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
        std::ostringstream oss;
        oss << "[native-video-client] recvFrames=" << recvFrames
            << " decodedFrames=" << decodedFrames
            << " skippedQueued=" << skippedQueued
            << " avgLatencyUs=" << avgLatencyUs
            << " maxLatencyUs=" << maxLatencyUs
            << " avgDecodeTailUs=" << avgDecodeTailUs
            << " maxDecodeTailUs=" << maxDecodeTailUs
            << " mbps=" << mbps
            << " decodedRawMbps=" << decodedRawMbps
            << " decodeRatioX100=" << decodeRatioX100
            << " size=" << visibleW << "x" << visibleH
            << " codedSize=" << decoded.width << "x" << decoded.height;
        append_congestion_fields(oss);
        append_present_counter_fields(oss);
        log_client_line(oss.str());
        recvFrames = 0;
        decodedFrames = 0;
        skippedQueued = 0;
        recvBytes = 0;
        decodedBytes = 0;
        sumLatencyUs = 0;
        maxLatencyUs = 0;
        sumDecodeTailUs = 0;
        maxDecodeTailUs = 0;
        statAtUs += 1000000ULL;
      }
      return true;
    };

    if (transport == VideoTransport::Udp) {
      std::array<uint8_t, 1600> datagram{};
      const uint32_t effectiveUdpSimDropSeed = (udpSimDropSeed > 0)
                                                   ? udpSimDropSeed
                                                   : static_cast<uint32_t>(qpc_now_us() & 0x7fffffffu);
      std::minstd_rand udpSimRng(effectiveUdpSimDropSeed);
      std::uniform_int_distribution<uint32_t> udpSimDropDist(0, 999);
      UdpH264FrameAssembler assembler;
      uint64_t assemblyDropped = 0;
      uint64_t oversizePayloadDropCount = 0;
      uint64_t udpSimDroppedCount = 0;
      uint64_t udpSimAcceptedCount = 0;
      uint64_t udpAssemblyStatAtUs = qpc_now_us() + 1000000ULL;
      uint64_t lastUdpChunkRecvCount = 0;
      uint64_t lastUdpAssemblyCompletedCount = 0;
      uint64_t lastUdpAssemblyDroppedCount = 0;
      uint64_t lastUdpAssemblyMalformedCount = 0;
      uint64_t lastUdpAssemblyReorderCount = 0;
      uint64_t lastUdpAssemblyKeyReqCount = 0;
      uint64_t lastUdpAssemblyFecRecoveredCount = 0;
      uint64_t lastUdpSimDroppedCount = 0;
      uint64_t lastUdpSimAcceptedCount = 0;

      while (gRunning.load()) {
        const int n = recv(gSock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
        if (n <= 0) break;
        if (n < static_cast<int>(sizeof(UdpVideoChunkHeader))) continue;

        UdpVideoChunkHeader u{};
        std::memcpy(&u, datagram.data(), sizeof(u));
        if (u.magic != remote60::native_poc::kMagic ||
            u.kind != static_cast<uint16_t>(UdpPacketKind::VideoChunk) ||
            u.size != sizeof(UdpVideoChunkHeader)) {
          continue;
        }
        if (u.codec != static_cast<uint16_t>(UdpCodec::H264)) {
          ++skippedQueued;
          continue;
        }
        if (udpSimDropPm > 0) {
          const uint32_t samplePm = udpSimDropDist(udpSimRng);
          if (samplePm < udpSimDropPm) {
            ++udpSimDroppedCount;
            ++skippedQueued;
            continue;
          }
        }
        ++udpSimAcceptedCount;
        ++udpChunkRecvCount;

        const auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n));
        if (assembleResult.fecRecovered) {
          udpAssemblyFecRecoveredCount += assembleResult.fecRecoveredChunks;
        }
        bool discontinuityHandled = false;
        auto handle_udp_discontinuity = [&]() {
          if (discontinuityHandled) return;
          discontinuityHandled = true;
          waitForKeyFrame = true;
          decoder.reset();
          request_keyframe(2);
          ++udpAssemblyKeyReqCount;
        };
        if (assembleResult.droppedPreviousIncomplete) {
          ++assemblyDropped;
          ++udpAssemblyDroppedCount;
          handle_udp_discontinuity();
        }

        if (assembleResult.disposition == UdpH264AssemblyDisposition::Malformed) {
          ++skippedQueued;
          ++udpAssemblyMalformedCount;
          handle_udp_discontinuity();
          if (assembleResult.oversizePayload && ((++oversizePayloadDropCount % 30ULL) == 1ULL)) {
            std::cout << "[native-video-client] dropped oversized udp payload bytes="
                      << assembleResult.rejectedPayloadSize
                      << " count=" << oversizePayloadDropCount << "\n";
          }
          continue;
        }

        if (assembleResult.disposition == UdpH264AssemblyDisposition::Dropped) {
          ++skippedQueued;
          ++assemblyDropped;
          ++udpAssemblyDroppedCount;
          if (assembleResult.reorderDetected) ++udpAssemblyReorderCount;
          handle_udp_discontinuity();
          if ((assemblyDropped % 120) == 1) {
            std::cout << "[native-video-client] udp assembly drop count=" << assemblyDropped
                      << " seq=" << u.seq
                      << " expectedSeq=" << assembleResult.expectedSeq
                      << " chunkOffset=" << u.chunkOffset
                      << " nextOffset=" << assembleResult.expectedNextOffset
                      << "\n";
          }
          continue;
        }

        if (assembleResult.disposition == UdpH264AssemblyDisposition::Completed) {
          ++udpAssemblyCompletedCount;
          const uint64_t packetNowUs = qpc_now_us();
          auto payload = std::move(assembleResult.frame.payload);
          if (!process_h264_frame(assembleResult.frame.header, &payload, packetNowUs)) break;
        }

        const uint64_t nowUs = qpc_now_us();
        if (nowUs >= udpAssemblyStatAtUs) {
          const uint64_t chunksDelta = udpChunkRecvCount - lastUdpChunkRecvCount;
          const uint64_t completedDelta = udpAssemblyCompletedCount - lastUdpAssemblyCompletedCount;
          const uint64_t droppedDelta = udpAssemblyDroppedCount - lastUdpAssemblyDroppedCount;
          const uint64_t malformedDelta = udpAssemblyMalformedCount - lastUdpAssemblyMalformedCount;
          const uint64_t reorderDelta = udpAssemblyReorderCount - lastUdpAssemblyReorderCount;
          const uint64_t keyReqDelta = udpAssemblyKeyReqCount - lastUdpAssemblyKeyReqCount;
          const uint64_t fecRecoveredDelta =
              udpAssemblyFecRecoveredCount - lastUdpAssemblyFecRecoveredCount;
          const uint64_t simDroppedDelta = udpSimDroppedCount - lastUdpSimDroppedCount;
          const uint64_t simAcceptedDelta = udpSimAcceptedCount - lastUdpSimAcceptedCount;
          const uint64_t simTotalDelta = simDroppedDelta + simAcceptedDelta;
          const uint64_t simDropPermille = (simTotalDelta > 0)
              ? ((simDroppedDelta * 1000ULL) / simTotalDelta)
              : 0;
          const uint64_t totalFramesDelta = completedDelta + droppedDelta;
          const uint64_t dropPermille = (totalFramesDelta > 0)
              ? ((droppedDelta * 1000ULL) / totalFramesDelta)
              : 0;
          udpAssemblyDropPmLast = static_cast<uint32_t>(std::min<uint64_t>(dropPermille, 1000ULL));
          std::cout << "[native-video-client] udp-assembly chunks=" << chunksDelta
                    << " completed=" << completedDelta
                    << " dropped=" << droppedDelta
                    << " dropPm=" << dropPermille
                    << " malformed=" << malformedDelta
                    << " reorder=" << reorderDelta
                    << " keyReq=" << keyReqDelta
                    << " fecRecovered=" << fecRecoveredDelta
                    << " simDropPm=" << simDropPermille
                    << " simDropTotal=" << simDroppedDelta
                    << " waitForKey=" << (waitForKeyFrame ? 1 : 0)
                    << " catchup=" << (catchupMode ? 1 : 0)
                    << "\n";
          lastUdpChunkRecvCount = udpChunkRecvCount;
          lastUdpAssemblyCompletedCount = udpAssemblyCompletedCount;
          lastUdpAssemblyDroppedCount = udpAssemblyDroppedCount;
          lastUdpAssemblyMalformedCount = udpAssemblyMalformedCount;
          lastUdpAssemblyReorderCount = udpAssemblyReorderCount;
          lastUdpAssemblyKeyReqCount = udpAssemblyKeyReqCount;
          lastUdpAssemblyFecRecoveredCount = udpAssemblyFecRecoveredCount;
          lastUdpSimDroppedCount = udpSimDroppedCount;
          lastUdpSimAcceptedCount = udpSimAcceptedCount;
          udpAssemblyStatAtUs += 1000000ULL;
        }
        if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
          break;
        }
      }

      gRunning = false;
      if (gHwnd) PostMessageW(gHwnd, WM_CLOSE, 0, 0);
      return;
    }

    while (gRunning.load()) {
      MessageHeader header{};
      if (!::recv_all(gSock, &header, sizeof(header))) break;
      if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
      const auto msgType = static_cast<MessageType>(header.type);

      if (msgType == MessageType::RawFrameBgra && header.size == sizeof(RawFrameHeader)) {
        RawFrameHeader h{};
        h.header = header;
        if (!::recv_all(gSock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
        std::vector<uint8_t> payload(h.payloadSize);
        if (!::recv_all(gSock, payload.data(), payload.size())) break;

        if (!useRaw) {
          ++skippedQueued;
          continue;
        }

        const uint64_t nowUs = qpc_now_us();
        const uint64_t queueSetUs = nowUs;
        auto frameBgra = std::make_shared<std::vector<uint8_t>>(std::move(payload));
        if (!frameBgra || frameBgra->empty()) {
          ++skippedQueued;
          continue;
        }
        {
          std::lock_guard<std::mutex> lk(gFrame.mu);
          const uint64_t prevVersion = gFrame.version;
          const uint64_t lastPresentedVersion = gLastPresentedVersion.load(std::memory_order_relaxed);
          if (prevVersion > lastPresentedVersion) {
            ++gOverwriteBeforePresentCount;
          }
          gFrame.format = SharedFrame::PixelFormat::Bgra32;
          gFrame.width = h.width;
          gFrame.height = h.height;
          gFrame.codedWidth = h.width;
          gFrame.codedHeight = h.height;
          gFrame.visibleLeft = 0;
          gFrame.visibleTop = 0;
          gFrame.stride = h.stride;
          gFrame.seq = h.seq;
          gFrame.captureUs = h.captureQpcUs;
          gFrame.encodeStartUs = h.encodeStartQpcUs;
          gFrame.encodeEndUs = h.encodeEndQpcUs;
          gFrame.sendUs = h.sendQpcUs;
          gFrame.recvUs = nowUs;
          gFrame.decodeStartUs = nowUs;
          gFrame.decodeEndUs = nowUs;
          gFrame.queueSetUs = queueSetUs;
          gFrame.decodeToQueueUs = 0;
          gFrame.streamGeneration = h.streamGeneration;
          gFrame.version = prevVersion + 1;
          gFrame.bytes = std::move(frameBgra);
          gFrame.surfaceSample.Reset();
          gFrame.surfaceTexture.Reset();
          gFrame.surfaceSubresource = 0;
        }
        if (gHwnd) {
          if (!gPaintQueued.exchange(true)) {
            InvalidateRect(gHwnd, nullptr, FALSE);
          } else {
            ++gPaintCoalescedCount;
          }
        }

        if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
            (args.traceMax == 0 || gTraceRecvPrinted.load() < args.traceMax)) {
          const auto nowPrinted = gTraceRecvPrinted.fetch_add(1) + 1;
          if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
            std::ostringstream oss;
            oss << "[native-video-client][trace_recv] seq=" << h.seq
                << " captureUs=" << h.captureQpcUs
                << " encodeStartUs=" << h.encodeStartQpcUs
                << " encodeEndUs=" << h.encodeEndQpcUs
                << " sendUs=" << h.sendQpcUs
                << " recvUs=" << nowUs
                << " decodeStartUs=" << nowUs
                << " decodeEndUs=" << nowUs
                << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
                << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
                << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
                << " netUs=" << ((nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0)
                << " r2dUs=0"
                << " decUs=0"
                << " bytes=" << h.payloadSize;
            log_client_line(oss.str());
          }
        }

        ++recvFrames;
        ++decodedFrames;
        recvBytes += h.payloadSize;
        decodedBytes += static_cast<uint64_t>(h.payloadSize);
        const uint64_t latencyUs = (nowUs >= h.captureQpcUs) ? (nowUs - h.captureQpcUs) : 0;
        const uint64_t decodeTailUs = (nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0;
        sumLatencyUs += latencyUs;
        sumDecodeTailUs += decodeTailUs;
        maxLatencyUs = std::max(maxLatencyUs, latencyUs);
        maxDecodeTailUs = std::max(maxDecodeTailUs, decodeTailUs);

        if (nowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (recvFrames > 0) ? (sumLatencyUs / recvFrames) : 0;
          const uint64_t avgDecodeTailUs = (recvFrames > 0) ? (sumDecodeTailUs / recvFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, nowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
      } else if (msgType == MessageType::EncodedFrameH264 && header.size == sizeof(EncodedFrameHeader)) {
        EncodedFrameHeader h{};
        h.header = header;
        if (!::recv_all(gSock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
        std::vector<uint8_t> payload(h.payloadSize);
        if (!::recv_all(gSock, payload.data(), payload.size())) break;
        const uint64_t packetNowUs = qpc_now_us();
        if (!process_h264_frame(h, &payload, packetNowUs)) break;
      } else {
        const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
        if (bodySize > 0 && !recv_discard(gSock, bodySize)) break;
        ++skippedQueued;
      }

      const uint64_t nowUs = qpc_now_us();
      if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
        break;
      }
    }
    gRunning = false;
    if (gHwnd) PostMessageW(gHwnd, WM_CLOSE, 0, 0);
  });

  MSG msg{};
  while (gRunning.load()) {
    bool hadMessage = false;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      hadMessage = true;
      if (msg.message == WM_QUIT) {
        gRunning = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!gRunning.load()) break;

    if (args.seconds > 0) {
      const uint64_t nowUs = qpc_now_us();
      if (nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
        gRunning = false;
        break;
      }
    }

    if (!hadMessage) {
      Sleep(5);
    }
  }

  gRunning = false;
  gInputEnabled = false;
  gInputMacro.StopPlayback();
  gInputMacro.StopRecording();
  remote60::native_poc::macro_window_destroy();
  if (gSock != INVALID_SOCKET) {
    shutdown(gSock, SD_BOTH);
    closesocket(gSock);
    gSock = INVALID_SOCKET;
  }
  if (controlSock != INVALID_SOCKET) {
    shutdown(controlSock, SD_BOTH);
    closesocket(controlSock);
    controlSock = INVALID_SOCKET;
  }
  if (controlThread.joinable()) controlThread.join();
  if (recvThread.joinable()) recvThread.join();

  if (useH264) {
    {
      std::lock_guard<std::mutex> lk(gFrame.mu);
      gFrame.surfaceSample.Reset();
      gFrame.surfaceTexture.Reset();
      gFrame.bytes.reset();
    }
    decoder.shutdown();
    if (mfStarted) MFShutdown();
  }

  std::cout << "[native-video-client] done\n";
  return 0;
}
