#include "realtime_runtime.hpp"

#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <wmcodecdsp.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <rtc/rtc.hpp>
#include <opus/opus.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iostream>
#include <sstream>
#include <string_view>

#include "common/input_protocol.hpp"
#include "common/h264_rtp_packetizer.hpp"
#include "common/opus_rtp_packetizer.hpp"
#include "common/signaling_protocol.hpp"
#include "common/ws_rtc_signaling_client.hpp"

#ifndef REMOTE60_ENABLE_WINDOW_MODE
#define REMOTE60_ENABLE_WINDOW_MODE 1
#endif

namespace remote60::host {
namespace {

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

constexpr bool kWindowModeCompiled = (REMOTE60_ENABLE_WINDOW_MODE != 0);

struct WindowListEntry {
  uint64_t id = 0;
  HWND hwnd = nullptr;
  uint32_t pid = 0;
  int width = 0;
  int height = 0;
  bool minimized = false;
  std::string title;
};

uint64_t hwnd_to_id(HWND hwnd) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd));
}

HWND window_id_to_hwnd(uint64_t id) {
  return reinterpret_cast<HWND>(static_cast<uintptr_t>(id));
}

std::atomic<uint64_t> gSelectedWindowId{0};

std::string utf16_to_utf8(const std::wstring& ws) {
  if (ws.empty()) return {};
  const int needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out(static_cast<size_t>(needed), '\0');
  const int written = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()),
                                          out.data(), needed, nullptr, nullptr);
  if (written <= 0) return {};
  return out;
}

std::string window_class_name(HWND hwnd) {
  if (!hwnd) return {};
  char name[128]{};
  const int n = GetClassNameA(hwnd, name, static_cast<int>(sizeof(name)));
  if (n <= 0) return {};
  return std::string(name, static_cast<size_t>(n));
}

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          std::ostringstream oss;
          oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
          out += oss.str();
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
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
  const std::string title = utf16_to_utf8(wtitle);
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
  e.title = utf16_to_utf8(wtitle);
  return e;
}

HWND selected_window_hwnd() {
  if (!kWindowModeCompiled) return nullptr;
  const uint64_t id = gSelectedWindowId.load();
  if (id == 0) return nullptr;
  const HWND hwnd = window_id_to_hwnd(id);
  if (!should_include_window(hwnd)) {
    gSelectedWindowId = 0;
    return nullptr;
  }
  return hwnd;
}

bool set_selected_window_by_id(uint64_t id, std::string* reasonOut, std::string* titleOut) {
  if (!kWindowModeCompiled) {
    if (reasonOut) *reasonOut = "window_mode_compiled_out";
    return false;
  }
  if (id == 0) {
    gSelectedWindowId = 0;
    if (reasonOut) *reasonOut = "desktop_mode_selected";
    if (titleOut) titleOut->clear();
    return true;
  }
  const auto found = find_window_by_id(id);
  if (!found.has_value()) {
    if (reasonOut) *reasonOut = "window_not_found_or_not_shareable";
    return false;
  }
  gSelectedWindowId = id;
  if (reasonOut) *reasonOut = "ok";
  if (titleOut) *titleOut = found->title;
  return true;
}

std::string build_window_list_json(const std::vector<WindowListEntry>& windows, uint64_t selectedId) {
  std::ostringstream oss;
  oss << "{\"type\":\"window_list\",\"selectedWindowId\":\"" << selectedId << "\",\"windows\":[";
  for (size_t i = 0; i < windows.size(); ++i) {
    const auto& w = windows[i];
    if (i > 0) oss << ",";
    oss << "{"
        << "\"id\":\"" << w.id << "\","
        << "\"title\":\"" << json_escape(w.title) << "\","
        << "\"pid\":" << w.pid << ","
        << "\"width\":" << w.width << ","
        << "\"height\":" << w.height << ","
        << "\"minimized\":" << (w.minimized ? "true" : "false")
        << "}";
  }
  oss << "]}";
  return oss.str();
}

bool json_type_is(const std::string& json, const char* typeName) {
  const std::string token = std::string("\"type\":\"") + typeName + "\"";
  return json.find(token) != std::string::npos;
}

std::optional<std::string> read_json_string_field(const std::string& json, const std::string& key) {
  const std::string token = "\"" + key + "\":\"";
  const auto p = json.find(token);
  if (p == std::string::npos) return std::nullopt;
  size_t i = p + token.size();
  std::string out;
  while (i < json.size()) {
    const char c = json[i++];
    if (c == '"') return out;
    if (c == '\\' && i < json.size()) {
      const char n = json[i++];
      switch (n) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        default: out.push_back(n); break;
      }
    } else {
      out.push_back(c);
    }
  }
  return std::nullopt;
}

std::optional<uint64_t> parse_u64_decimal(const std::string& s) {
  if (s.empty()) return std::nullopt;
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    const uint64_t d = static_cast<uint64_t>(c - '0');
    if (v > (std::numeric_limits<uint64_t>::max() - d) / 10u) return std::nullopt;
    v = v * 10u + d;
  }
  return v;
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForPrimaryMonitor() {
  HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  if (!monitor) return nullptr;
  auto interop = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  interop->CreateForMonitor(monitor, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                            winrt::put_abi(item));
  return item;
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForWindow(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return nullptr;
  auto interop = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  interop->CreateForWindow(hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                           winrt::put_abi(item));
  return item;
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> SurfaceToTexture(IDirect3DSurface const& surface) {
  winrt::com_ptr<::IInspectable> inspectable = surface.as<::IInspectable>();
  Microsoft::WRL::ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
  winrt::check_hresult(inspectable->QueryInterface(IID_PPV_ARGS(&access)));
  Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
  winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(tex.GetAddressOf())));
  return tex;
}

std::vector<uint8_t> make_rtp_packet(uint8_t payloadType, uint32_t ssrc, uint16_t seq, uint32_t ts, bool marker,
                                     const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> out(12 + payload.size());
  out[0] = 0x80;
  out[1] = static_cast<uint8_t>((marker ? 0x80 : 0x00) | (payloadType & 0x7F));
  out[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(seq & 0xFF);
  out[4] = static_cast<uint8_t>((ts >> 24) & 0xFF);
  out[5] = static_cast<uint8_t>((ts >> 16) & 0xFF);
  out[6] = static_cast<uint8_t>((ts >> 8) & 0xFF);
  out[7] = static_cast<uint8_t>(ts & 0xFF);
  out[8] = static_cast<uint8_t>((ssrc >> 24) & 0xFF);
  out[9] = static_cast<uint8_t>((ssrc >> 16) & 0xFF);
  out[10] = static_cast<uint8_t>((ssrc >> 8) & 0xFF);
  out[11] = static_cast<uint8_t>(ssrc & 0xFF);
  memcpy(out.data() + 12, payload.data(), payload.size());
  return out;
}

std::string trim_copy(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
  return s.substr(b, e - b);
}

std::string env_or(const char* key, const std::string& fallback) {
  const char* v = std::getenv(key);
  if (!v || !*v) return fallback;
  return std::string(v);
}

uint32_t env_u32(const char* key, uint32_t fallback, uint32_t minVal, uint32_t maxVal) {
  const char* v = std::getenv(key);
  if (!v || !*v) return fallback;
  try {
    uint32_t x = static_cast<uint32_t>(std::stoul(v));
    if (x < minVal) return minVal;
    if (x > maxVal) return maxVal;
    return x;
  } catch (...) {
    return fallback;
  }
}

bool env_bool(const char* key, bool fallback) {
  const char* v = std::getenv(key);
  if (!v || !*v) return fallback;
  std::string s(v);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
  if (s == "0" || s == "false" || s == "no" || s == "off") return false;
  return fallback;
}

POINT clamp_to_screen_point(int x, int y) {
  const int sx = GetSystemMetrics(SM_CXSCREEN);
  const int sy = GetSystemMetrics(SM_CYSCREEN);
  const int maxX = (sx > 0) ? (sx - 1) : 0;
  const int maxY = (sy > 0) ? (sy - 1) : 0;
  POINT p{};
  p.x = x;
  p.y = y;
  if (p.x < 0) p.x = 0;
  if (p.x > maxX) p.x = maxX;
  if (p.y < 0) p.y = 0;
  if (p.y > maxY) p.y = maxY;
  return p;
}

struct InputPointerState {
  POINT lastTarget{};
  bool hasLastTarget = false;
  HWND lastHwnd = nullptr;
};

std::mutex gInputPointerMu;
std::mutex gInputDispatchMu;
InputPointerState gInputPointerState;
std::mutex gInputClickMu;
uint64_t gLastLeftDownAtMs = 0;
POINT gLastLeftDownPt{};
HWND gLastLeftDownHwnd = nullptr;
std::mutex gCursorVisibilityMu;
bool gRemoteCursorHidden = false;
std::atomic<bool> gLeftButtonDown{false};
std::atomic<bool> gRightButtonDown{false};
std::atomic<bool> gMiddleButtonDown{false};
std::atomic<uint64_t> gLeftButtonDownAtMs{0};
std::atomic<uint64_t> gRightButtonDownAtMs{0};
std::atomic<uint64_t> gMiddleButtonDownAtMs{0};

// Runtime-stop path may still crash inside rtc object destruction on some builds.
// Keep refs in a never-freed holder to avoid destructor-time crashes during disconnect.
std::mutex gRtcLeakHoldMu;
auto* gRtcLeakHoldPeers = new std::vector<std::shared_ptr<rtc::PeerConnection>>();
auto* gRtcLeakHoldTracks = new std::vector<std::shared_ptr<rtc::Track>>();

uint64_t tick_ms() {
  return static_cast<uint64_t>(::GetTickCount64());
}

void set_remote_cursor_hidden(bool hide) {
  std::lock_guard<std::mutex> lk(gCursorVisibilityMu);
  if (hide == gRemoteCursorHidden) return;
  if (hide) {
    int guard = 0;
    int r = ShowCursor(FALSE);
    while (r >= 0 && guard++ < 16) r = ShowCursor(FALSE);
    gRemoteCursorHidden = true;
    return;
  }
  int guard = 0;
  int r = ShowCursor(TRUE);
  while (r < 0 && guard++ < 16) r = ShowCursor(TRUE);
  gRemoteCursorHidden = false;
}

void force_release_pressed_mouse_buttons() {
  INPUT ups[3]{};
  int n = 0;
  if (gLeftButtonDown.exchange(false)) {
    gLeftButtonDownAtMs = 0;
    ups[n].type = INPUT_MOUSE;
    ups[n].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    ++n;
  }
  if (gRightButtonDown.exchange(false)) {
    gRightButtonDownAtMs = 0;
    ups[n].type = INPUT_MOUSE;
    ups[n].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    ++n;
  }
  if (gMiddleButtonDown.exchange(false)) {
    gMiddleButtonDownAtMs = 0;
    ups[n].type = INPUT_MOUSE;
    ups[n].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
    ++n;
  }
  if (n > 0) {
    SendInput(static_cast<UINT>(n), ups, sizeof(INPUT));
  }
}

void release_stale_pressed_mouse_buttons(uint64_t staleMs) {
  const uint64_t now = tick_ms();
  INPUT ups[3]{};
  int n = 0;

  if (gLeftButtonDown.load() && gLeftButtonDownAtMs.load() > 0 &&
      (now - gLeftButtonDownAtMs.load()) >= staleMs && gLeftButtonDown.exchange(false)) {
    gLeftButtonDownAtMs = 0;
    ups[n].type = INPUT_MOUSE;
    ups[n].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    ++n;
  }
  if (gRightButtonDown.load() && gRightButtonDownAtMs.load() > 0 &&
      (now - gRightButtonDownAtMs.load()) >= staleMs && gRightButtonDown.exchange(false)) {
    gRightButtonDownAtMs = 0;
    ups[n].type = INPUT_MOUSE;
    ups[n].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    ++n;
  }
  if (gMiddleButtonDown.load() && gMiddleButtonDownAtMs.load() > 0 &&
      (now - gMiddleButtonDownAtMs.load()) >= staleMs && gMiddleButtonDown.exchange(false)) {
    gMiddleButtonDownAtMs = 0;
    ups[n].type = INPUT_MOUSE;
    ups[n].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
    ++n;
  }

  if (n > 0) {
    SendInput(static_cast<UINT>(n), ups, sizeof(INPUT));
  }
}

std::optional<uint8_t> parse_h264_payload_type_from_offer_sdp(const std::string& sdp) {
  struct H264PayloadInfo {
    bool isH264 = false;
    bool packetizationMode1 = false;
    std::string profileLevelId;
  };

  auto to_lower_copy = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
  };

  std::istringstream iss(sdp);
  std::string line;
  bool inVideo = false;
  std::vector<int> videoPtOrder;
  std::unordered_set<int> videoPts;
  std::unordered_map<int, H264PayloadInfo> h264Info;

  auto trim_eol = [](std::string& l) {
    while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
  };

  while (std::getline(iss, line)) {
    trim_eol(line);
    if (line.rfind("m=", 0) == 0) {
      inVideo = (line.rfind("m=video ", 0) == 0);
      if (inVideo) {
        std::istringstream ms(line);
        std::string tok;
        int idx = 0;
        while (ms >> tok) {
          ++idx;
          if (idx <= 3) continue;  // m=video <port> <proto>
          try {
            const int pt = std::stoi(tok);
            if (pt >= 0 && pt <= 127) {
              if (videoPts.insert(pt).second) videoPtOrder.push_back(pt);
            }
          } catch (...) {
          }
        }
      }
      continue;
    }
    if (!inVideo) continue;

    if (line.rfind("a=rtpmap:", 0) == 0) {
      const auto spacePos = line.find(' ');
      if (spacePos == std::string::npos) continue;
      try {
        const int pt = std::stoi(line.substr(9, spacePos - 9));
        if (videoPts.count(pt) == 0) continue;
        const std::string codec = to_lower_copy(line.substr(spacePos + 1));
        if (codec.rfind("h264/", 0) == 0) h264Info[pt].isH264 = true;
      } catch (...) {
      }
      continue;
    }

    if (line.rfind("a=fmtp:", 0) == 0) {
      const auto spacePos = line.find(' ');
      if (spacePos == std::string::npos) continue;
      try {
        const int pt = std::stoi(line.substr(7, spacePos - 7));
        if (videoPts.count(pt) == 0) continue;
        const std::string params = to_lower_copy(line.substr(spacePos + 1));
        auto& info = h264Info[pt];
        if (params.find("packetization-mode=1") != std::string::npos) info.packetizationMode1 = true;
        const std::string key = "profile-level-id=";
        const auto profilePos = params.find(key);
        if (profilePos != std::string::npos) {
          size_t begin = profilePos + key.size();
          size_t end = begin;
          while (end < params.size() && std::isxdigit(static_cast<unsigned char>(params[end])) != 0) ++end;
          if (end > begin) info.profileLevelId = params.substr(begin, end - begin);
        }
      } catch (...) {
      }
      continue;
    }
  }

  auto pick_first_match = [&](auto pred) -> std::optional<uint8_t> {
    for (const int pt : videoPtOrder) {
      const auto it = h264Info.find(pt);
      if (it == h264Info.end() || !it->second.isH264) continue;
      if (pred(it->second)) return static_cast<uint8_t>(pt);
    }
    return std::nullopt;
  };

  if (const auto p = pick_first_match([](const H264PayloadInfo& info) {
        return info.packetizationMode1 && info.profileLevelId == "42e01f";
      }); p.has_value()) return p;
  if (const auto p = pick_first_match([](const H264PayloadInfo& info) {
        return info.packetizationMode1 && info.profileLevelId == "42001f";
      }); p.has_value()) return p;
  if (const auto p = pick_first_match([](const H264PayloadInfo& info) {
        return info.packetizationMode1 && info.profileLevelId.rfind("42", 0) == 0;
      }); p.has_value()) return p;
  if (const auto p = pick_first_match([](const H264PayloadInfo& info) {
        return info.packetizationMode1;
      }); p.has_value()) return p;
  if (const auto p = pick_first_match([](const H264PayloadInfo& info) {
        return info.profileLevelId.rfind("42", 0) == 0;
      }); p.has_value()) return p;
  return pick_first_match([](const H264PayloadInfo&) { return true; });
}

std::optional<std::string> parse_mid_for_media_from_offer_sdp(const std::string& sdp,
                                                               const std::string& media) {
  std::istringstream iss(sdp);
  std::string line;
  bool inTargetMedia = false;

  auto trim_eol = [](std::string& l) {
    while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
  };

  const std::string mediaPrefix = "m=" + media + " ";
  while (std::getline(iss, line)) {
    trim_eol(line);
    if (line.rfind("m=", 0) == 0) {
      inTargetMedia = (line.rfind(mediaPrefix, 0) == 0);
      continue;
    }
    if (!inTargetMedia) continue;
    if (line.rfind("a=mid:", 0) == 0) {
      const std::string mid = trim_copy(line.substr(6));
      if (!mid.empty()) return mid;
    }
  }

  return std::nullopt;
}

rtc::Configuration make_rtc_config_from_env() {
  rtc::Configuration cfg;
  const std::string raw = env_or("REMOTE60_ICE_SERVERS", "");
  std::string token;
  token.reserve(128);
  auto flush_token = [&]() {
    const std::string t = trim_copy(token);
    token.clear();
    if (t.empty()) return;
    try {
      cfg.iceServers.emplace_back(t);
    } catch (...) {
    }
  };
  for (char c : raw) {
    if (c == ',' || c == ';') {
      flush_token();
    } else {
      token.push_back(c);
    }
  }
  flush_token();
  return cfg;
}

bool apply_input_event_runtime(const remote60::common::input::Event& e) {
  static const bool preserveLocalCursor = env_bool("REMOTE60_PRESERVE_LOCAL_CURSOR", true);
  static const bool safeClickMode = env_bool("REMOTE60_SAFE_CLICK_MODE", true);
  static const bool messageClickMode = env_bool("REMOTE60_MESSAGE_CLICK_MODE", false);
  static const bool hideLocalCursorForInput = env_bool("REMOTE60_HIDE_LOCAL_CURSOR_FOR_INPUT", true);
  static const bool windowModeEnabled = kWindowModeCompiled && env_bool("REMOTE60_WINDOW_MODE", true);
  auto clamp_abs = [](double v) -> LONG {
    if (v < 0.0) v = 0.0;
    if (v > 65535.0) v = 65535.0;
    return static_cast<LONG>(std::lround(v));
  };
  auto make_abs_move = [&](const POINT& p) {
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    if (sx < 1) sx = 1;
    if (sy < 1) sy = 1;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    in.mi.dx = clamp_abs((static_cast<double>(p.x) * 65535.0) / static_cast<double>(sx - 1));
    in.mi.dy = clamp_abs((static_cast<double>(p.y) * 65535.0) / static_cast<double>(sy - 1));
    return in;
  };
  auto send_with_cursor_restore = [&](const POINT& target,
                                      bool hadOriginal,
                                      const POINT& original,
                                      const std::vector<INPUT>& body) {
    std::vector<INPUT> seq;
    seq.reserve(body.size() + 2);
    seq.push_back(make_abs_move(target));
    for (const auto& i : body) seq.push_back(i);
    if (hadOriginal) seq.push_back(make_abs_move(original));
    const UINT n = static_cast<UINT>(seq.size());
    return SendInput(n, seq.data(), sizeof(INPUT)) == n;
  };
  auto current_mouse_key_flags = [&]() -> WPARAM {
    WPARAM w = 0;
    if (gLeftButtonDown.load()) w |= MK_LBUTTON;
    if (gRightButtonDown.load()) w |= MK_RBUTTON;
    if (gMiddleButtonDown.load()) w |= MK_MBUTTON;
    return w;
  };
  auto map_remote_point_to_screen = [&](int rx, int ry) -> POINT {
    if (windowModeEnabled) {
      if (HWND hwnd = selected_window_hwnd(); hwnd && IsWindow(hwnd)) {
        RECT wr{};
        if (GetWindowRect(hwnd, &wr)) {
          const int wwRaw = static_cast<int>(wr.right - wr.left);
          const int whRaw = static_cast<int>(wr.bottom - wr.top);
          const int ww = (wwRaw > 0) ? wwRaw : 1;
          const int wh = (whRaw > 0) ? whRaw : 1;
          POINT p{};
          p.x = wr.left + std::clamp(rx, 0, ww - 1);
          p.y = wr.top + std::clamp(ry, 0, wh - 1);
          return p;
        }
      }
    }
    return clamp_to_screen_point(rx, ry);
  };
  auto resolve_message_target = [&](HWND seedHwnd,
                                    const POINT& screenPt,
                                    HWND* outHwnd,
                                    POINT* outClientPt) -> bool {
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
  };
  auto post_mouse_msg = [&](UINT msg, WPARAM wp, const POINT& screenPt, bool clientCoords) -> bool {
    HWND seed = nullptr;
    if (windowModeEnabled) {
      seed = selected_window_hwnd();
    }
    {
      std::lock_guard<std::mutex> lk(gInputPointerMu);
      if (!seed) seed = gInputPointerState.lastHwnd;
      if (!seed || !IsWindow(seed)) {
        seed = WindowFromPoint(screenPt);
      }
    }
    HWND hwnd = nullptr;
    POINT clientPt{};
    if (!resolve_message_target(seed, screenPt, &hwnd, &clientPt)) return false;
    {
      std::lock_guard<std::mutex> lk(gInputPointerMu);
      gInputPointerState.lastHwnd = hwnd;
    }
    const POINT& lpPt = clientCoords ? clientPt : screenPt;
    const LPARAM lp = MAKELPARAM(static_cast<short>(lpPt.x), static_cast<short>(lpPt.y));
    return PostMessage(hwnd, msg, wp, lp) != 0;
  };
  auto resolve_target_hwnd_client = [&](const POINT& screenPt, HWND* outHwnd, POINT* outClientPt) -> bool {
    if (!outHwnd || !outClientPt) return false;
    HWND seed = nullptr;
    if (windowModeEnabled) {
      seed = selected_window_hwnd();
    }
    {
      std::lock_guard<std::mutex> lk(gInputPointerMu);
      if (!seed) seed = gInputPointerState.lastHwnd;
      if (!seed || !IsWindow(seed)) seed = WindowFromPoint(screenPt);
    }
    HWND hwnd = nullptr;
    POINT clientPt{};
    if (!resolve_message_target(seed, screenPt, &hwnd, &clientPt)) return false;
    {
      std::lock_guard<std::mutex> lk(gInputPointerMu);
      gInputPointerState.lastHwnd = hwnd;
    }
    *outHwnd = hwnd;
    *outClientPt = clientPt;
    return true;
  };

  if (e.type == remote60::common::input::EventType::MouseMove) {
    const POINT target = map_remote_point_to_screen(e.x, e.y);
    {
      std::lock_guard<std::mutex> lk(gInputPointerMu);
      gInputPointerState.lastTarget = target;
      gInputPointerState.hasLastTarget = true;
      if (messageClickMode) {
        HWND hwnd = windowModeEnabled ? selected_window_hwnd() : nullptr;
        if (!hwnd) hwnd = WindowFromPoint(target);
        if (hwnd) gInputPointerState.lastHwnd = hwnd;
      }
    }
    if (preserveLocalCursor && !messageClickMode) {
      if (hideLocalCursorForInput) set_remote_cursor_hidden(true);
      // Preserve-local-cursor mode: keep host cursor anchored and only cache target.
      return true;
    }
    if (preserveLocalCursor && messageClickMode) {
      const WPARAM wp = current_mouse_key_flags();
      post_mouse_msg(WM_MOUSEMOVE, wp, target, true);
      return true;
    }
    if (preserveLocalCursor) return true;
    return SetCursorPos(target.x, target.y) != 0;
  }
  if (e.type == remote60::common::input::EventType::MouseButtonDown ||
      e.type == remote60::common::input::EventType::MouseButtonUp) {
    POINT original{};
    const bool hadOriginal = (GetCursorPos(&original) != 0);
    POINT target{};
    bool hasTarget = false;
    {
      std::lock_guard<std::mutex> lk(gInputPointerMu);
      target = gInputPointerState.lastTarget;
      hasTarget = gInputPointerState.hasLastTarget;
    }

    const bool down = (e.type == remote60::common::input::EventType::MouseButtonDown);
    DWORD downFlag = MOUSEEVENTF_LEFTDOWN;
    DWORD upFlag = MOUSEEVENTF_LEFTUP;
    switch (e.button) {
      case 2:
        downFlag = MOUSEEVENTF_RIGHTDOWN;
        upFlag = MOUSEEVENTF_RIGHTUP;
        break;
      case 1:
        downFlag = MOUSEEVENTF_MIDDLEDOWN;
        upFlag = MOUSEEVENTF_MIDDLEUP;
        break;
      default:
        downFlag = MOUSEEVENTF_LEFTDOWN;
        upFlag = MOUSEEVENTF_LEFTUP;
        break;
    }
    UINT downMsg = WM_LBUTTONDOWN;
    UINT upMsg = WM_LBUTTONUP;
    WPARAM buttonMask = MK_LBUTTON;
    switch (e.button) {
      case 2:
        downMsg = WM_RBUTTONDOWN;
        upMsg = WM_RBUTTONUP;
        buttonMask = MK_RBUTTON;
        break;
      case 1:
        downMsg = WM_MBUTTONDOWN;
        upMsg = WM_MBUTTONUP;
        buttonMask = MK_MBUTTON;
        break;
      default:
        break;
    }

    if (preserveLocalCursor && !messageClickMode) {
      if (hideLocalCursorForInput) set_remote_cursor_hidden(true);
      INPUT in{};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = down ? downFlag : upFlag;
      bool ok = false;
      if (hasTarget) {
        const std::vector<INPUT> body{in};
        ok = send_with_cursor_restore(target, hadOriginal, original, body);
      } else {
        ok = (SendInput(1, &in, sizeof(INPUT)) == 1);
      }
      if (ok) {
        const uint64_t now = tick_ms();
        switch (e.button) {
          case 2:
            gRightButtonDown = down;
            gRightButtonDownAtMs = down ? now : 0;
            break;
          case 1:
            gMiddleButtonDown = down;
            gMiddleButtonDownAtMs = down ? now : 0;
            break;
          default:
            gLeftButtonDown = down;
            gLeftButtonDownAtMs = down ? now : 0;
            break;
        }
        release_stale_pressed_mouse_buttons(1500);
      }
      return ok;
    }

    bool ok = false;
    if (safeClickMode) {
      if (down) {
        const uint64_t now = tick_ms();
        switch (e.button) {
          case 2:
            gRightButtonDown = true;
            gRightButtonDownAtMs = now;
            break;
          case 1:
            gMiddleButtonDown = true;
            gMiddleButtonDownAtMs = now;
            break;
          default:
            gLeftButtonDown = true;
            gLeftButtonDownAtMs = now;
            break;
        }
        if (preserveLocalCursor && hasTarget && messageClickMode) {
          if (e.button == 0) {
            HWND hwnd = nullptr;
            POINT clientPt{};
            if (!resolve_target_hwnd_client(target, &hwnd, &clientPt)) {
              ok = false;
            } else {
              const uint64_t nowMs = tick_ms();
              bool isDoubleClick = false;
              {
                std::lock_guard<std::mutex> lk(gInputClickMu);
                const UINT dblMs = std::max<UINT>(800, GetDoubleClickTime());
                int dx = GetSystemMetrics(SM_CXDOUBLECLK);
                int dy = GetSystemMetrics(SM_CYDOUBLECLK);
                if (dx <= 0) dx = 4;
                if (dy <= 0) dy = 4;
                dx = std::max(dx, 24);
                dy = std::max(dy, 24);
                isDoubleClick =
                    (gLastLeftDownHwnd == hwnd) &&
                    (gLastLeftDownAtMs > 0) &&
                    (nowMs >= gLastLeftDownAtMs) &&
                    ((nowMs - gLastLeftDownAtMs) <= static_cast<uint64_t>(dblMs)) &&
                    (std::abs(clientPt.x - gLastLeftDownPt.x) <= dx) &&
                    (std::abs(clientPt.y - gLastLeftDownPt.y) <= dy);
                gLastLeftDownAtMs = nowMs;
                gLastLeftDownPt = clientPt;
                gLastLeftDownHwnd = hwnd;
              }
              const WPARAM wp = current_mouse_key_flags() | buttonMask;
              const LPARAM lp = MAKELPARAM(static_cast<short>(clientPt.x), static_cast<short>(clientPt.y));
              const bool moved = PostMessage(hwnd, WM_MOUSEMOVE, wp, lp) != 0;
              const UINT pressMsg = isDoubleClick ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN;
              const bool pressed = PostMessage(hwnd, pressMsg, wp, lp) != 0;
              ok = moved && pressed;
            }
          } else {
            const WPARAM wp = current_mouse_key_flags() | buttonMask;
            const bool moved = post_mouse_msg(WM_MOUSEMOVE, wp, target, true);
            const bool pressed = post_mouse_msg(downMsg, wp, target, true);
            ok = moved && pressed;
          }
        } else {
          INPUT downIn{};
          downIn.type = INPUT_MOUSE;
          downIn.mi.dwFlags = downFlag;
          const std::vector<INPUT> body{downIn};
          if (preserveLocalCursor && hasTarget) {
            ok = send_with_cursor_restore(target, hadOriginal, original, body);
          } else {
            ok = (SendInput(1, &downIn, sizeof(INPUT)) == 1);
          }
        }
      } else {
        if (preserveLocalCursor && hasTarget && messageClickMode) {
          const WPARAM wUp = current_mouse_key_flags() & ~buttonMask;
          ok = post_mouse_msg(upMsg, wUp, target, true);
        } else {
          INPUT upIn{};
          upIn.type = INPUT_MOUSE;
          upIn.mi.dwFlags = upFlag;
          const std::vector<INPUT> body{upIn};
          if (preserveLocalCursor && hasTarget) {
            ok = send_with_cursor_restore(target, hadOriginal, original, body);
          } else {
            ok = (SendInput(static_cast<UINT>(body.size()),
                            const_cast<INPUT*>(body.data()), sizeof(INPUT)) ==
                  static_cast<UINT>(body.size()));
          }
        }
        switch (e.button) {
          case 2:
            gRightButtonDown = false;
            gRightButtonDownAtMs = 0;
            break;
          case 1:
            gMiddleButtonDown = false;
            gMiddleButtonDownAtMs = 0;
            break;
          default:
            gLeftButtonDown = false;
            gLeftButtonDownAtMs = 0;
            break;
        }
      }
    } else {
      if (preserveLocalCursor && hasTarget && messageClickMode) {
        const UINT msg = down ? downMsg : upMsg;
        const WPARAM wp = down ? (current_mouse_key_flags() | buttonMask)
                               : (current_mouse_key_flags() & ~buttonMask);
        ok = post_mouse_msg(msg, wp, target, true);
      } else {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = down ? downFlag : upFlag;
        const std::vector<INPUT> body{in};
        if (preserveLocalCursor && hasTarget) {
          ok = send_with_cursor_restore(target, hadOriginal, original, body);
        } else {
          ok = (SendInput(1, &in, sizeof(INPUT)) == 1);
        }
      }
      if (ok) {
        const uint64_t now = tick_ms();
        switch (e.button) {
          case 2:
            gRightButtonDown = down;
            gRightButtonDownAtMs = down ? now : 0;
            break;
          case 1:
            gMiddleButtonDown = down;
            gMiddleButtonDownAtMs = down ? now : 0;
            break;
          default:
            gLeftButtonDown = down;
            gLeftButtonDownAtMs = down ? now : 0;
            break;
        }
      }
    }

    if (ok) {
      release_stale_pressed_mouse_buttons(1500);
    }
    return ok;
  }
  if (e.type == remote60::common::input::EventType::MouseWheel) {
    POINT original{};
    const bool hadOriginal = (GetCursorPos(&original) != 0);
    POINT target{};
    bool hasTarget = false;
    {
      std::lock_guard<std::mutex> lk(gInputPointerMu);
      target = gInputPointerState.lastTarget;
      hasTarget = gInputPointerState.hasLastTarget;
    }

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = static_cast<DWORD>(static_cast<SHORT>(e.wheelDelta));
    bool ok = false;
    if (preserveLocalCursor && !messageClickMode) {
      if (hideLocalCursorForInput) set_remote_cursor_hidden(true);
      if (hasTarget) {
        const std::vector<INPUT> body{in};
        ok = send_with_cursor_restore(target, hadOriginal, original, body);
      } else {
        ok = (SendInput(1, &in, sizeof(INPUT)) == 1);
      }
    } else if (preserveLocalCursor && hasTarget && messageClickMode) {
      const WPARAM wp = MAKEWPARAM(current_mouse_key_flags(), static_cast<WORD>(static_cast<SHORT>(e.wheelDelta)));
      ok = post_mouse_msg(WM_MOUSEWHEEL, wp, target, false);
    } else if (preserveLocalCursor && hasTarget) {
      const std::vector<INPUT> body{in};
      ok = send_with_cursor_restore(target, hadOriginal, original, body);
    } else {
      ok = (SendInput(1, &in, sizeof(INPUT)) == 1);
    }
    return ok;
  }
  if (e.type == remote60::common::input::EventType::KeyDown ||
      e.type == remote60::common::input::EventType::KeyUp) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = static_cast<WORD>(e.keyCode & 0xFFFF);
    in.ki.dwFlags = (e.type == remote60::common::input::EventType::KeyUp) ? KEYEVENTF_KEYUP : 0;
    return SendInput(1, &in, sizeof(INPUT)) == 1;
  }
  return false;
}

std::string now_local_string() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto tt = clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &tt);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

void runtime_diag_log(const std::string& sessionId,
                      const std::chrono::steady_clock::time_point& startedAt,
                      const std::string& phase,
                      const std::string& message) {
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startedAt).count();
  const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
  std::ostringstream line;
  line << "[" << now_local_string() << "][host][runtime]"
       << " session=" << sessionId
       << " thread=" << tid
       << " elapsedMs=" << elapsedMs
       << " phase=" << phase
       << " " << message;
  std::cout << line.str() << std::endl;
  try {
    const std::filesystem::path dir = "logs";
    std::filesystem::create_directories(dir);
    std::ofstream ofs(dir / "host_runtime_diag.log", std::ios::app);
    ofs << line.str() << "\n";
    ofs.flush();
  } catch (...) {
  }
}

void release_mouse_capture_guard() {
  // Defensive guard: runtime capture must not lock user cursor to a window/region.
  ClipCursor(nullptr);
  ReleaseCapture();
}

bool configure_encoder(IMFTransform* enc, uint32_t w, uint32_t h, uint32_t fps, uint32_t bitrate) {
  IMFMediaType* outType = nullptr;
  for (DWORD i = 0; ; ++i) {
    if (FAILED(enc->GetOutputAvailableType(0, i, &outType)) || !outType) return false;
    GUID st{};
    if (SUCCEEDED(outType->GetGUID(MF_MT_SUBTYPE, &st)) && st == MFVideoFormat_H264) {
      MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, w, h);
      MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, fps, 1);
      outType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
      outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
      if (SUCCEEDED(enc->SetOutputType(0, outType, 0))) {
        outType->Release();
        break;
      }
    }
    outType->Release();
    outType = nullptr;
  }

  IMFMediaType* inType = nullptr;
  for (DWORD i = 0; ; ++i) {
    if (FAILED(enc->GetInputAvailableType(0, i, &inType)) || !inType) return false;
    GUID st{};
    if (SUCCEEDED(inType->GetGUID(MF_MT_SUBTYPE, &st)) && st == MFVideoFormat_NV12) {
      MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, w, h);
      MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, fps, 1);
      MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
      inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
      if (SUCCEEDED(enc->SetInputType(0, inType, 0))) {
        inType->Release();
        return true;
      }
    }
    inType->Release();
    inType = nullptr;
  }
}

void configure_encoder_low_latency(IMFTransform* enc, uint32_t fps, uint32_t bitrate) {
  if (!enc) return;
  Microsoft::WRL::ComPtr<ICodecAPI> codecApi;
  if (FAILED(enc->QueryInterface(IID_PPV_ARGS(&codecApi))) || !codecApi) return;

  auto set_bool = [&](const GUID& key, bool v) {
    VARIANT var{};
    var.vt = VT_BOOL;
    var.boolVal = v ? VARIANT_TRUE : VARIANT_FALSE;
    (void)codecApi->SetValue(&key, &var);
  };
  auto set_u32 = [&](const GUID& key, uint32_t v) {
    VARIANT var{};
    var.vt = VT_UI4;
    var.ulVal = v;
    (void)codecApi->SetValue(&key, &var);
  };

  // Favor low-latency over compression efficiency.
  set_bool(CODECAPI_AVLowLatencyMode, true);
  set_bool(CODECAPI_AVEncCommonRealTime, true);
  set_u32(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
  set_u32(CODECAPI_AVEncCommonMeanBitRate, bitrate);
  set_u32(CODECAPI_AVEncMPVDefaultBPictureCount, 0);
  set_u32(CODECAPI_AVEncMPVGOPSize, std::max<uint32_t>(1, fps));
  set_u32(CODECAPI_AVEncCommonQualityVsSpeed, 100);
}

}  // namespace

RealtimeRuntimeStats run_realtime_runtime_host(int seconds) {
  RealtimeRuntimeStats out;
#if !REMOTE60_HAS_REALTIME_MEDIA
  out.detail = "realtime_disabled";
  return out;
#else
  using remote60::common::WsRtcSignalingClient;
  using remote60::common::signaling::Message;
  using remote60::common::signaling::MessageType;

  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  if (!GraphicsCaptureSession::IsSupported()) {
    out.detail = "wgc_not_supported";
    return out;
  }

  HRESULT hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    out.detail = "mf_startup_failed";
    return out;
  }

  Microsoft::WRL::ComPtr<ID3D11Device> d3d;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                         nullptr, 0, D3D11_SDK_VERSION, &d3d, &fl, &ctx);
  if (FAILED(hr)) {
    out.detail = "d3d11_create_failed";
    MFShutdown();
    return out;
  }

  const bool windowModeEnabledRuntime = kWindowModeCompiled && env_bool("REMOTE60_WINDOW_MODE", true);
  HWND captureWindow = nullptr;
  uint64_t captureWindowId = 0;
  std::string captureTarget = "desktop_primary_monitor";
  if (windowModeEnabledRuntime) {
    captureWindow = selected_window_hwnd();
    if (captureWindow) {
      captureWindowId = hwnd_to_id(captureWindow);
      if (const auto sel = find_window_by_id(captureWindowId); sel.has_value()) {
        captureTarget = std::string("window:") + std::to_string(captureWindowId) + ":" + sel->title;
      } else {
        captureTarget = std::string("window:") + std::to_string(captureWindowId);
      }
      std::lock_guard<std::mutex> lk(gInputPointerMu);
      gInputPointerState.lastHwnd = captureWindow;
    } else {
      captureTarget = "desktop_fallback_no_selected_window";
    }
  }

  auto item = (captureWindow != nullptr) ? CreateItemForWindow(captureWindow) : CreateItemForPrimaryMonitor();
  if (!item && captureWindow != nullptr) {
    // Selected window may be minimized/invalid; keep host alive by falling back to desktop.
    captureWindow = nullptr;
    captureWindowId = 0;
    captureTarget = "desktop_fallback_window_item_failed";
    item = CreateItemForPrimaryMonitor();
  }
  if (!item) {
    out.detail = "capture_item_failed";
    MFShutdown();
    return out;
  }
  auto size = item.Size();
  uint32_t captureW = static_cast<uint32_t>(size.Width);
  uint32_t captureH = static_cast<uint32_t>(size.Height);
  // NV12 texture/encoder dimensions must be even numbers.
  uint32_t w = (captureW >= 2) ? (captureW & ~1u) : 0;
  uint32_t h = (captureH >= 2) ? (captureH & ~1u) : 0;
  if ((w < 2 || h < 2) && captureWindow != nullptr) {
    captureWindow = nullptr;
    captureWindowId = 0;
    captureTarget = "desktop_fallback_window_size_invalid";
    item = CreateItemForPrimaryMonitor();
    if (!item) {
      out.detail = "capture_item_failed";
      MFShutdown();
      return out;
    }
    size = item.Size();
    captureW = static_cast<uint32_t>(size.Width);
    captureH = static_cast<uint32_t>(size.Height);
    w = (captureW >= 2) ? (captureW & ~1u) : 0;
    h = (captureH >= 2) ? (captureH & ~1u) : 0;
  }
  if (w < 2 || h < 2) {
    out.detail = "capture_size_invalid";
    MFShutdown();
    return out;
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
  d3d.As(&dxgi);
  winrt::com_ptr<::IInspectable> inspectable;
  winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
  auto d3dDevice = inspectable.as<IDirect3DDevice>();

  auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(d3dDevice,
      winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
  auto session = pool.CreateCaptureSession(item);

  Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext;
  d3d.As(&videoDevice);
  ctx.As(&videoContext);

  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
  desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputWidth = captureW;
  desc.InputHeight = captureH;
  desc.OutputWidth = w;
  desc.OutputHeight = h;
  desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> vpEnum;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> vp;
  if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&desc, &vpEnum)) ||
      FAILED(videoDevice->CreateVideoProcessor(vpEnum.Get(), 0, &vp))) {
    out.detail = "vp_create_failed";
    MFShutdown();
    return out;
  }

  D3D11_TEXTURE2D_DESC nv12Desc{};
  nv12Desc.Width = w; nv12Desc.Height = h; nv12Desc.MipLevels = 1; nv12Desc.ArraySize = 1;
  nv12Desc.Format = DXGI_FORMAT_NV12; nv12Desc.SampleDesc.Count = 1; nv12Desc.Usage = D3D11_USAGE_DEFAULT;
  nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> nv12Tex;
  if (FAILED(d3d->CreateTexture2D(&nv12Desc, nullptr, &nv12Tex))) {
    out.detail = "nv12_create_failed";
    MFShutdown();
    return out;
  }

  D3D11_TEXTURE2D_DESC st = nv12Desc;
  st.Usage = D3D11_USAGE_STAGING;
  st.BindFlags = 0;
  st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> stTex;
  if (FAILED(d3d->CreateTexture2D(&st, nullptr, &stTex))) {
    out.detail = "staging_create_failed";
    MFShutdown();
    return out;
  }

  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov{};
  ov.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outView;
  if (FAILED(videoDevice->CreateVideoProcessorOutputView(nv12Tex.Get(), vpEnum.Get(), &ov, &outView))) {
    out.detail = "vp_output_view_failed";
    MFShutdown();
    return out;
  }

  const uint32_t videoFps = env_u32("REMOTE60_VIDEO_FPS", 60, 10, 120);
  const uint32_t videoBitrate = env_u32("REMOTE60_VIDEO_BITRATE_BPS", 8u * 1000u * 1000u, 500000, 40000000);

  IMFTransform* enc = nullptr;
  hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enc));
  if (FAILED(hr) || !enc || !configure_encoder(enc, w, h, videoFps, videoBitrate)) {
    if (enc) enc->Release();
    out.detail = "encoder_setup_failed";
    MFShutdown();
    return out;
  }
  configure_encoder_low_latency(enc, videoFps, videoBitrate);
  enc->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  enc->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  out.encoderReady = true;

  enum class RuntimeState { Waiting, Negotiating, Streaming };

  const auto runtimeStartedAt = std::chrono::steady_clock::now();
  const std::string sessionId = std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(::GetTickCount64());
  try {
    // Best-effort request to suppress the OS capture border. On systems that do not allow it,
    // this may be ignored by Windows.
    session.IsBorderRequired(false);
    runtime_diag_log(sessionId, runtimeStartedAt, "capture", "border_request=hidden");
  } catch (...) {
    runtime_diag_log(sessionId, runtimeStartedAt, "capture", "border_request=failed");
  }
  try {
    // Hide host-side cursor in captured stream to avoid double-cursor effect on web client.
    session.IsCursorCaptureEnabled(false);
    runtime_diag_log(sessionId, runtimeStartedAt, "capture", "cursor_capture=disabled");
  } catch (...) {
    runtime_diag_log(sessionId, runtimeStartedAt, "capture", "cursor_capture=disable_failed");
  }
  release_mouse_capture_guard();
  runtime_diag_log(sessionId, runtimeStartedAt, "lifecycle", "begin run_realtime_runtime_host seconds=" + std::to_string(seconds));
  runtime_diag_log(sessionId, runtimeStartedAt, "input_guard", "mouse_release_guard applied=1");
  runtime_diag_log(sessionId, runtimeStartedAt, "runtime_config",
                   "videoFps=" + std::to_string(videoFps) + " videoBitrate=" + std::to_string(videoBitrate) +
                        " preserveLocalCursor=" + (env_bool("REMOTE60_PRESERVE_LOCAL_CURSOR", true) ? "1" : "0") +
                        " safeClickMode=" + (env_bool("REMOTE60_SAFE_CLICK_MODE", true) ? "1" : "0") +
                        " messageClickMode=" + (env_bool("REMOTE60_MESSAGE_CLICK_MODE", false) ? "1" : "0") +
                        " hideLocalCursorForInput=" + (env_bool("REMOTE60_HIDE_LOCAL_CURSOR_FOR_INPUT", true) ? "1" : "0") +
                        " windowModeEnabled=" + (windowModeEnabledRuntime ? "1" : "0") +
                        " captureWindowId=" + std::to_string(captureWindowId) +
                        " captureSize=" + std::to_string(captureW) + "x" + std::to_string(captureH) +
                        " encodeSize=" + std::to_string(w) + "x" + std::to_string(h) +
                        " captureTarget=" + captureTarget);

  std::shared_ptr<rtc::PeerConnection> pc;
  std::shared_ptr<rtc::Track> vtrack;
  std::shared_ptr<rtc::Track> atrack;
  std::mutex peerMu;
  std::atomic<bool> connected{false}, trackOpen{false}, audioTrackOpen{false};
  std::atomic<bool> gotRemoteOffer{false};
  std::atomic<bool> hadActivePeerInCycle{false};
  std::atomic<bool> sessionRestartRequested{false};
  std::atomic<bool> pendingSessionEndRecreate{false};
  std::atomic<uint64_t> controlEventSeq{0};
  std::atomic<uint64_t> sessionEndSeq{0};
  std::atomic<uint64_t> pendingSessionEndStartedAtMs{0};
  std::atomic<uint64_t> activePeerGeneration{0};
  std::atomic<uint8_t> videoPayloadType{96};
  std::atomic<uint64_t> inputEventsReceived{0};
  std::atomic<uint64_t> inputEventsApplied{0};
  std::atomic<uint64_t> dataChannelMessagesReceived{0};
  std::atomic<RuntimeState> state{RuntimeState::Waiting};
  const bool serverMode = (seconds <= 0);
  const uint32_t sessionEndGraceMs = env_u32("REMOTE60_SESSION_END_GRACE_MS", 2500, 200, 60000);

  auto state_str = [](RuntimeState s) {
    switch (s) {
      case RuntimeState::Waiting: return "WAITING";
      case RuntimeState::Negotiating: return "NEGOTIATING";
      case RuntimeState::Streaming: return "STREAMING";
    }
    return "UNKNOWN";
  };

  auto set_state = [&](RuntimeState next, const char* reason) {
    const auto prev = state.exchange(next);
    if (prev != next) {
      runtime_diag_log(sessionId, runtimeStartedAt, "state",
                       std::string("transition=") + state_str(prev) + "->" + state_str(next) +
                           " reason=" + reason);
    }
  };

  enum class ControlEventKind {
    SignalingOffer,
    SignalingIce,
    ClientSessionEnd,
    PeerStateChanged,
    LocalDescription,
    LocalCandidate
  };

  auto control_kind_str = [](ControlEventKind k) {
    switch (k) {
      case ControlEventKind::SignalingOffer: return "SignalingOffer";
      case ControlEventKind::SignalingIce: return "SignalingIce";
      case ControlEventKind::ClientSessionEnd: return "ClientSessionEnd";
      case ControlEventKind::PeerStateChanged: return "PeerStateChanged";
      case ControlEventKind::LocalDescription: return "LocalDescription";
      case ControlEventKind::LocalCandidate: return "LocalCandidate";
    }
    return "Unknown";
  };

  struct ControlEvent {
    ControlEventKind kind;
    std::string text;
    std::string candidateMid;
    std::optional<int> candidateMLineIndex;
    uint64_t seq = 0;
    uint64_t sessionEndSeqAtEnqueue = 0;
    uint64_t peerGeneration = 0;
    rtc::PeerConnection::State pcState = rtc::PeerConnection::State::New;
    rtc::Description::Type localDescType = rtc::Description::Type::Unspec;
  };

  std::mutex controlMu;
  std::deque<ControlEvent> controlQueue;

  WsRtcSignalingClient sig;

  rtc::InitLogger(rtc::LogLevel::Warning, nullptr);
  rtc::Configuration cfg = make_rtc_config_from_env();
  const std::string signalingUrl = env_or("REMOTE60_SIGNALING_URL", "ws://127.0.0.1:3000");
  std::atomic<bool> stop{false};
  std::atomic<bool> shuttingDown{false};
  std::atomic<bool> acceptPeerCallbacks{true};
  std::atomic<bool> acceptCaptureCallbacks{true};
  std::condition_variable controlCv;
  std::mutex frameMu;
  std::condition_variable frameCv;
  std::vector<uint8_t> latestNv12;
  std::atomic<uint64_t> frameCount{0};
  uint64_t frameVersion = 0;

  auto push_control_and_wake = [&](ControlEvent ev) {
    if (shuttingDown.load() || !acceptPeerCallbacks.load()) return;
    ev.seq = controlEventSeq.fetch_add(1) + 1;
    ev.sessionEndSeqAtEnqueue = sessionEndSeq.load();
    const uint64_t evSeq = ev.seq;
    const auto evKind = ev.kind;
    const uint64_t evSessionEndSeq = ev.sessionEndSeqAtEnqueue;
    const uint64_t evPeerGen = ev.peerGeneration;
    size_t qsize = 0;
    {
      std::lock_guard<std::mutex> lk(controlMu);
      controlQueue.push_back(std::move(ev));
      qsize = controlQueue.size();
    }
    runtime_diag_log(sessionId, runtimeStartedAt, "control_queue",
                     std::string("enqueue seq=") + std::to_string(evSeq) +
                         " kind=" + control_kind_str(evKind) +
                         " qsize=" + std::to_string(qsize) +
                         " sessionEndSeq=" + std::to_string(evSessionEndSeq) +
                         " peerGen=" + std::to_string(evPeerGen));
    controlCv.notify_one();
  };

  std::string currentVideoMid = "video";
  std::string currentAudioMid = "audio";
  uint8_t currentVideoPayloadType = 96;

  auto close_peer_refs = [&](std::shared_ptr<rtc::PeerConnection> oldPc,
                             std::shared_ptr<rtc::Track> oldVtrack,
                             std::shared_ptr<rtc::Track> oldAtrack,
                             const char* reason) {
    runtime_diag_log(sessionId, runtimeStartedAt, "peer_close",
                     std::string("begin reason=") + reason +
                         " hasPc=" + (oldPc ? "1" : "0") +
                         " hasVtrack=" + (oldVtrack ? "1" : "0") +
                         " hasAtrack=" + (oldAtrack ? "1" : "0"));
    const std::string_view closeReason(reason ? reason : "");
    const bool fastDropWithLeakHold =
        (closeReason == "runtime_stop" || closeReason == "replace_after_create");
    if (fastDropWithLeakHold) {
      // Avoid calling into rtc close/reset/destructor paths on known-crash paths.
      // Hold refs in leak containers so objects are not destroyed during connect/disconnect cycles.
      {
        std::lock_guard<std::mutex> lk(gRtcLeakHoldMu);
        if (oldVtrack) gRtcLeakHoldTracks->push_back(std::move(oldVtrack));
        if (oldAtrack) gRtcLeakHoldTracks->push_back(std::move(oldAtrack));
        if (oldPc) gRtcLeakHoldPeers->push_back(std::move(oldPc));
      }
      runtime_diag_log(sessionId, runtimeStartedAt, "peer_close",
                       std::string("fast_drop=1 leak_hold=1 reason=") + std::string(closeReason));
      runtime_diag_log(sessionId, runtimeStartedAt, "peer_close", std::string("end reason=") + reason);
      return;
    }
    try {
      if (oldVtrack) {
        oldVtrack->resetCallbacks();
      }
    } catch (...) {
      runtime_diag_log(sessionId, runtimeStartedAt, "peer_close", "vtrack_close_failed=1");
    }
    try {
      if (oldAtrack) {
        oldAtrack->resetCallbacks();
      }
    } catch (...) {
      runtime_diag_log(sessionId, runtimeStartedAt, "peer_close", "atrack_close_failed=1");
    }
    oldVtrack.reset();
    oldAtrack.reset();
    try {
      if (oldPc) {
        oldPc->resetCallbacks();
      }
    } catch (...) {
      runtime_diag_log(sessionId, runtimeStartedAt, "peer_close", "pc_close_failed=1");
    }
    oldPc.reset();
    runtime_diag_log(sessionId, runtimeStartedAt, "peer_close", std::string("end reason=") + reason);
  };

  auto detach_current_peer = [&](const char* reason) {
    std::shared_ptr<rtc::PeerConnection> oldPc;
    std::shared_ptr<rtc::Track> oldVtrack;
    std::shared_ptr<rtc::Track> oldAtrack;
    {
      std::lock_guard<std::mutex> lk(peerMu);
      oldPc = std::move(pc);
      oldVtrack = std::move(vtrack);
      oldAtrack = std::move(atrack);
    }
    close_peer_refs(std::move(oldPc), std::move(oldVtrack), std::move(oldAtrack), reason);
  };

  auto create_peer = [&](const char* reason, const std::string& videoMid, const std::string& audioMid,
                         uint8_t videoPt) {
    runtime_diag_log(sessionId, runtimeStartedAt, "peer_recreate",
                     std::string("begin reason=") + reason + " videoMid=" + videoMid + " audioMid=" + audioMid +
                         " videoPt=" + std::to_string(static_cast<int>(videoPt)));
    const uint64_t generation = activePeerGeneration.fetch_add(1) + 1;
    auto newPc = std::make_shared<rtc::PeerConnection>(cfg);
    rtc::Description::Video v(videoMid, rtc::Description::Direction::SendOnly);
    v.addH264Codec(videoPt);
    v.addSSRC(0x22334455, "video-cname", "remote60", "v0");
    rtc::Description::Audio a(audioMid, rtc::Description::Direction::SendOnly);
    a.addOpusCodec(111);
    a.addSSRC(0x11223344, "audio-cname", "remote60", "a0");
    auto newVtrack = newPc ? newPc->addTrack(v) : nullptr;
    auto newAtrack = newPc ? newPc->addTrack(a) : nullptr;
    if (!newPc || !newVtrack || !newAtrack) {
      runtime_diag_log(sessionId, runtimeStartedAt, "peer_recreate",
                       std::string("end success=0 reason=") + reason);
      return false;
    }

    newVtrack->onOpen([&]() {
      if (shuttingDown.load() || !acceptPeerCallbacks.load()) return;
      trackOpen = true;
      runtime_diag_log(sessionId, runtimeStartedAt, "track", "video_track_open begin/end=open");
    });
    newAtrack->onOpen([&]() {
      if (shuttingDown.load() || !acceptPeerCallbacks.load()) return;
      audioTrackOpen = true;
      runtime_diag_log(sessionId, runtimeStartedAt, "track", "audio_track_open begin/end=open");
    });

    newPc->onDataChannel([&, generation](std::shared_ptr<rtc::DataChannel> dc) {
      if (shuttingDown.load() || !acceptPeerCallbacks.load()) return;
      if (!dc) return;
      runtime_diag_log(sessionId, runtimeStartedAt, "datachannel",
                       "opened label=" + dc->label() + " generation=" + std::to_string(generation));
      static const bool ackInputEvents = env_bool("REMOTE60_INPUT_ACK_EVENTS", false);
      struct DataChannelMessageContext {
        std::string sessionId;
        std::chrono::steady_clock::time_point runtimeStartedAt{};
        std::atomic<bool>* shuttingDown = nullptr;
        std::atomic<bool>* acceptPeerCallbacks = nullptr;
        std::atomic<uint64_t>* sessionEndSeq = nullptr;
        std::atomic<bool>* connected = nullptr;
        std::atomic<bool>* trackOpen = nullptr;
        std::atomic<bool>* audioTrackOpen = nullptr;
        std::atomic<uint64_t>* inputEventsReceived = nullptr;
        std::atomic<uint64_t>* inputEventsApplied = nullptr;
        std::atomic<bool>* sessionRestartRequested = nullptr;
        std::atomic<bool>* stop = nullptr;
        std::condition_variable* controlCv = nullptr;
        std::condition_variable* frameCv = nullptr;
        std::shared_ptr<rtc::DataChannel> dc;
        bool ackInputEvents = false;
        uint64_t generation = 0;
      };
      auto dcCtx = std::make_shared<DataChannelMessageContext>();
      dcCtx->sessionId = sessionId;
      dcCtx->runtimeStartedAt = runtimeStartedAt;
      dcCtx->shuttingDown = &shuttingDown;
      dcCtx->acceptPeerCallbacks = &acceptPeerCallbacks;
      dcCtx->sessionEndSeq = &sessionEndSeq;
      dcCtx->connected = &connected;
      dcCtx->trackOpen = &trackOpen;
      dcCtx->audioTrackOpen = &audioTrackOpen;
      dcCtx->inputEventsReceived = &inputEventsReceived;
      dcCtx->inputEventsApplied = &inputEventsApplied;
      dcCtx->sessionRestartRequested = &sessionRestartRequested;
      dcCtx->stop = &stop;
      dcCtx->controlCv = &controlCv;
      dcCtx->frameCv = &frameCv;
      dcCtx->dc = dc;
      dcCtx->ackInputEvents = ackInputEvents;
      dcCtx->generation = generation;
      dc->onOpen([dcCtx]() {
        runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "datachannel",
                         "on_open generation=" + std::to_string(dcCtx->generation));
      });

      dc->onMessage([dcCtx, &push_control_and_wake, &dataChannelMessagesReceived](rtc::message_variant data) {
        if (dcCtx->shuttingDown->load() || !dcCtx->acceptPeerCallbacks->load()) return;
        const uint64_t msgCount = dataChannelMessagesReceived.fetch_add(1) + 1;
        auto safe_send = [&](const std::string& text) {
          try {
            if (dcCtx->shuttingDown->load()) return;
            if (dcCtx->dc && dcCtx->dc->isOpen()) dcCtx->dc->send(text);
          } catch (...) {
            runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "datachannel",
                             "send_failed generation=" + std::to_string(dcCtx->generation));
          }
        };
        try {
          std::string text;
          if (const auto* s = std::get_if<std::string>(&data)) {
            text = *s;
          } else if (const auto* b = std::get_if<rtc::binary>(&data)) {
            text.assign(reinterpret_cast<const char*>(b->data()), b->size());
          } else {
            if (msgCount <= 20 || (msgCount % 100) == 0) {
              runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "datachannel",
                               "message_drop type=unsupported count=" + std::to_string(msgCount));
            }
            return;
          }
          if (msgCount <= 20 || (msgCount % 100) == 0) {
            runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "datachannel",
                             "message_received count=" + std::to_string(msgCount) +
                                 " bytes=" + std::to_string(text.size()));
          }

          if (text == "input_ping") {
            if (msgCount <= 20 || (msgCount % 100) == 0) {
              runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "datachannel",
                               "message_type=input_ping count=" + std::to_string(msgCount));
            }
            safe_send("{\"type\":\"input_ack\"}");
            return;
          }
          if (text == "session_end") {
            const uint64_t sseq = dcCtx->sessionEndSeq->fetch_add(1) + 1;
            runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "lifecycle",
                             "session_end_received_on_datachannel=1 sessionEndSeq=" + std::to_string(sseq) +
                                 " generation=" + std::to_string(dcCtx->generation));
            dcCtx->connected->store(false);
            dcCtx->trackOpen->store(false);
            dcCtx->audioTrackOpen->store(false);
            ControlEvent endEv;
            endEv.kind = ControlEventKind::ClientSessionEnd;
            push_control_and_wake(std::move(endEv));
            return;
          }
          if (json_type_is(text, "window_list_request")) {
            const auto windows = enumerate_shareable_windows();
            const std::string payload = build_window_list_json(windows, gSelectedWindowId.load());
            runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "window_mode",
                             "window_list_request windows=" + std::to_string(windows.size()) +
                                 " selected=" + std::to_string(gSelectedWindowId.load()));
            safe_send(payload);
            return;
          }
          if (json_type_is(text, "window_select")) {
            const auto rawIdOpt = read_json_string_field(text, "windowId");
            const std::string rawId = rawIdOpt.value_or("0");
            const auto parsed = parse_u64_decimal(rawId);
            bool ok = false;
            std::string reason = "invalid_window_id";
            std::string title;
            uint64_t selectedId = 0;
            if (parsed.has_value()) {
              selectedId = *parsed;
              ok = set_selected_window_by_id(selectedId, &reason, &title);
            }
            std::ostringstream rsp;
            rsp << "{\"type\":\"window_selected\",\"windowId\":\"" << selectedId
                << "\",\"ok\":" << (ok ? "true" : "false")
                << ",\"reason\":\"" << json_escape(reason) << "\""
                << ",\"title\":\"" << json_escape(title) << "\""
                << ",\"restartRequested\":" << (ok ? "true" : "false")
                << "}";
            safe_send(rsp.str());
            runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "window_mode",
                             "window_select id=" + std::to_string(selectedId) +
                                 " ok=" + (ok ? "1" : "0") +
                                 " reason=" + reason);
            if (ok) {
              // Capture target switch is applied on next runtime cycle.
              // Requesting a controlled restart keeps existing rollback path intact.
              if (dcCtx->sessionRestartRequested) dcCtx->sessionRestartRequested->store(true);
              if (dcCtx->stop) dcCtx->stop->store(true);
              if (dcCtx->controlCv) dcCtx->controlCv->notify_all();
              if (dcCtx->frameCv) dcCtx->frameCv->notify_all();
            }
            return;
          }

          const auto ev = remote60::common::input::from_json(text);
          if (!ev.has_value()) {
            runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "input",
                             "event_parse_failed count=" + std::to_string(msgCount));
            safe_send("{\"type\":\"input_nack\"}");
            return;
          }

          const uint64_t recvCount = ++(*dcCtx->inputEventsReceived);
          if (recvCount <= 20 || (recvCount % 100) == 0) {
            runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "input",
                             "event_received count=" + std::to_string(recvCount) +
                                  " type=" + remote60::common::input::to_string(ev->type) +
                                  " x=" + std::to_string(ev->x) + " y=" + std::to_string(ev->y) +
                                  " button=" + std::to_string(ev->button) +
                                  " wheelDelta=" + std::to_string(ev->wheelDelta));
          }
          bool applied = false;
          {
            std::lock_guard<std::mutex> lk(gInputDispatchMu);
            applied = apply_input_event_runtime(*ev);
          }
          if (applied) {
            const uint64_t appliedCount = ++(*dcCtx->inputEventsApplied);
            if (appliedCount <= 20 || (appliedCount % 100) == 0) {
              runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "input",
                               "event_applied count=" + std::to_string(appliedCount));
            }
          }
          if (dcCtx->ackInputEvents) {
            safe_send(applied ? "{\"type\":\"input_ack\"}" : "{\"type\":\"input_nack\"}");
          }
        } catch (...) {
          runtime_diag_log(dcCtx->sessionId, dcCtx->runtimeStartedAt, "datachannel",
                           "onMessage_exception generation=" + std::to_string(dcCtx->generation));
          safe_send("{\"type\":\"input_nack\"}");
        }
      });
    });

    newPc->onStateChange([&, generation](rtc::PeerConnection::State s) {
      if (shuttingDown.load() || !acceptPeerCallbacks.load()) return;
      runtime_diag_log(sessionId, runtimeStartedAt, "peer_state_cb",
                       "enqueue generation=" + std::to_string(generation) +
                           " state=" + std::to_string(static_cast<int>(s)));
      ControlEvent ev;
      ev.kind = ControlEventKind::PeerStateChanged;
      ev.peerGeneration = generation;
      ev.pcState = s;
      push_control_and_wake(std::move(ev));
    });
    newPc->onLocalDescription([&, generation](rtc::Description d) {
      if (shuttingDown.load() || !acceptPeerCallbacks.load()) return;
      runtime_diag_log(sessionId, runtimeStartedAt, "local_desc_cb",
                       "enqueue generation=" + std::to_string(generation) +
                           " type=" + std::to_string(static_cast<int>(d.type())) +
                           " bytes=" + std::to_string(std::string(d).size()));
      ControlEvent ev;
      ev.kind = ControlEventKind::LocalDescription;
      ev.peerGeneration = generation;
      ev.localDescType = d.type();
      ev.text = std::string(d);
      push_control_and_wake(std::move(ev));
    });
    newPc->onLocalCandidate([&, generation](rtc::Candidate c) {
      if (shuttingDown.load() || !acceptPeerCallbacks.load()) return;
      std::string mid = c.mid();
      if (mid.empty()) mid = "0";
      std::optional<int> mline;
      try {
        size_t pos = 0;
        const int parsed = std::stoi(mid, &pos);
        if (pos == mid.size()) mline = parsed;
      } catch (...) {
      }
      ControlEvent ev;
      ev.kind = ControlEventKind::LocalCandidate;
      ev.peerGeneration = generation;
      ev.text = std::string(c);
      ev.candidateMid = mid;
      ev.candidateMLineIndex = mline;
      runtime_diag_log(sessionId, runtimeStartedAt, "local_ice_cb",
                       "enqueue generation=" + std::to_string(generation) +
                           " mid=" + mid +
                           " bytes=" + std::to_string(ev.text.size()));
      push_control_and_wake(std::move(ev));
    });

    connected = false;
    trackOpen = false;
    audioTrackOpen = false;
    sessionRestartRequested = false;
    std::shared_ptr<rtc::PeerConnection> oldPc;
    std::shared_ptr<rtc::Track> oldVtrack;
    std::shared_ptr<rtc::Track> oldAtrack;
    {
      std::lock_guard<std::mutex> lk(peerMu);
      oldPc = std::move(pc);
      oldVtrack = std::move(vtrack);
      oldAtrack = std::move(atrack);
      pc = std::move(newPc);
      vtrack = std::move(newVtrack);
      atrack = std::move(newAtrack);
    }
    close_peer_refs(std::move(oldPc), std::move(oldVtrack), std::move(oldAtrack), "replace_after_create");
    out.webrtcReady = true;
    runtime_diag_log(sessionId, runtimeStartedAt, "peer_recreate",
                     std::string("end success=1 reason=") + reason + " generation=" + std::to_string(generation));
    return true;
  };

  if (!create_peer("initial_create", currentVideoMid, currentAudioMid, currentVideoPayloadType)) {
    out.webrtcReady = false;
    enc->Release();
    MFShutdown();
    out.detail = "webrtc_create_failed";
    return out;
  }

  sig.set_on_message([&](const Message& m) {
    try {
      if (shuttingDown.load() || !acceptPeerCallbacks.load()) return;
      if (m.type == MessageType::Offer && m.sdp.rfind("v=0", 0) == 0) {
        try {
          std::filesystem::create_directories("logs");
          std::ofstream ofs("logs/runtime_offer_from_client_latest.sdp", std::ios::trunc);
          ofs << m.sdp;
        } catch (...) {
        }
        runtime_diag_log(sessionId, runtimeStartedAt, "signaling", "offer_received enqueue bytes=" + std::to_string(m.sdp.size()));
        push_control_and_wake(ControlEvent{ControlEventKind::SignalingOffer, m.sdp});
      } else if (m.type == MessageType::Ice && !m.candidate.empty()) {
        ControlEvent ev;
        ev.kind = ControlEventKind::SignalingIce;
        ev.text = m.candidate;
        ev.candidateMid = m.candidateMid;
        ev.candidateMLineIndex = m.candidateMLineIndex;
        push_control_and_wake(std::move(ev));
      } else if (m.type == MessageType::PeerState && m.peerOnline.has_value()) {
        const bool peerOnline = *m.peerOnline;
        runtime_diag_log(sessionId, runtimeStartedAt, "signaling",
                         std::string("peer_state peerOnline=") + (peerOnline ? "1" : "0"));
        if (peerOnline) {
          hadActivePeerInCycle = true;
        }
        if (!peerOnline) {
          if (!hadActivePeerInCycle.load()) {
            runtime_diag_log(sessionId, runtimeStartedAt, "signaling",
                             "peer_state peerOnline=0 ignored reason=no_active_peer_yet");
            return;
          }
          connected = false;
          trackOpen = false;
          audioTrackOpen = false;
          set_state(RuntimeState::Waiting, "peer_offline_signal");
          runtime_diag_log(sessionId, runtimeStartedAt, "signaling",
                           "peer_state peerOnline=0 noted wait_for_pc_terminal_state=1");
        }
      }
    } catch (...) {
      runtime_diag_log(sessionId, runtimeStartedAt, "signaling", "on_message_exception");
    }
  });

  sig.set_on_state([&](const std::string& stateMsg) {
    if (shuttingDown.load() || !acceptPeerCallbacks.load()) return;
    runtime_diag_log(sessionId, runtimeStartedAt, "signaling_state", "event=" + stateMsg);
    if (stateMsg.rfind("disconnected:", 0) == 0) {
      connected = false;
      if (!stop.load()) {
        sessionRestartRequested = true;
        set_state(RuntimeState::Waiting, "signaling_disconnected");
        runtime_diag_log(sessionId, runtimeStartedAt, "lifecycle",
                         "server_cycle_restart_requested reason=signaling_disconnected");
      }
      controlCv.notify_all();
      frameCv.notify_all();
    }
  });

  auto token = pool.FrameArrived([&](Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
    if (shuttingDown.load() || !acceptCaptureCallbacks.load()) {
      return;
    }
    while (auto frame = sender.TryGetNextFrame()) {
      if (shuttingDown.load() || !acceptCaptureCallbacks.load()) {
        break;
      }
      try {
        auto src = SurfaceToTexture(frame.Surface());
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};
        iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inView;
        if (FAILED(videoDevice->CreateVideoProcessorInputView(src.Get(), vpEnum.Get(), &iv, &inView))) continue;
        RECT srcRect{0, 0, static_cast<LONG>(captureW), static_cast<LONG>(captureH)};
        RECT dstRect{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        videoContext->VideoProcessorSetStreamSourceRect(vp.Get(), 0, TRUE, &srcRect);
        videoContext->VideoProcessorSetStreamDestRect(vp.Get(), 0, TRUE, &dstRect);
        videoContext->VideoProcessorSetOutputTargetRect(vp.Get(), TRUE, &dstRect);
        D3D11_VIDEO_PROCESSOR_STREAM stm{}; stm.Enable = TRUE; stm.pInputSurface = inView.Get();
        if (FAILED(videoContext->VideoProcessorBlt(vp.Get(), outView.Get(), 0, 1, &stm))) continue;
        ctx->CopyResource(stTex.Get(), nv12Tex.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(ctx->Map(stTex.Get(), 0, D3D11_MAP_READ, 0, &map))) continue;
        std::vector<uint8_t> nv12(w * h * 3 / 2);
        uint8_t* dstY = nv12.data();
        for (uint32_t y = 0; y < h; ++y) memcpy(dstY + y * w, (uint8_t*)map.pData + y * map.RowPitch, w);
        uint8_t* dstUV = nv12.data() + w * h;
        uint8_t* srcUV = (uint8_t*)map.pData + map.RowPitch * h;
        for (uint32_t y = 0; y < h / 2; ++y) memcpy(dstUV + y * w, srcUV + y * map.RowPitch, w);
        ctx->Unmap(stTex.Get(), 0);
        {
          std::lock_guard<std::mutex> lk(frameMu);
          latestNv12.swap(nv12);
          ++frameVersion;
        }
        ++frameCount;
        frameCv.notify_one();
      } catch (...) {
      }
    }
  });

  runtime_diag_log(sessionId, runtimeStartedAt, "signaling",
                   "connect_begin url=" + signalingUrl + " role=host iceServers=" +
                       std::to_string(cfg.iceServers.size()));
  if (!sig.connect(signalingUrl) || !sig.register_role("host")) {
    runtime_diag_log(sessionId, runtimeStartedAt, "signaling", "connect_end success=0");
    enc->Release(); MFShutdown(); out.detail = "signaling_failed"; return out;
  }
  runtime_diag_log(sessionId, runtimeStartedAt, "signaling", "connect_end success=1");
  set_state(RuntimeState::Waiting, "host_registered");

  runtime_diag_log(sessionId, runtimeStartedAt, "capture", "start_begin");
  session.StartCapture();
  out.captureReady = true;
  runtime_diag_log(sessionId, runtimeStartedAt, "capture", "start_end success=1");

  std::atomic<uint64_t> encodedFrames{0};
  std::atomic<uint64_t> sentPkts{0};
  std::atomic<uint64_t> dropPkts{0};
  std::atomic<uint64_t> audioSentPkts{0};
  std::atomic<uint64_t> audioDropPkts{0};

  std::thread controlThread([&]() {
    try {
      while (!stop.load()) {
        ControlEvent ev;
        bool hasEvent = false;
        size_t qsizeAfterPop = 0;
        {
          std::unique_lock<std::mutex> lk(controlMu);
          controlCv.wait_for(lk, std::chrono::milliseconds(50), [&]() { return stop.load() || !controlQueue.empty(); });
          if (stop.load()) break;
          if (!controlQueue.empty()) {
            ev = std::move(controlQueue.front());
            controlQueue.pop_front();
            hasEvent = true;
            qsizeAfterPop = controlQueue.size();
          }
        }
        if (!hasEvent) continue;
        if (shuttingDown.load()) continue;
        runtime_diag_log(sessionId, runtimeStartedAt, "control_queue",
                         "dispatch_begin seq=" + std::to_string(ev.seq) +
                             " kind=" + control_kind_str(ev.kind) +
                             " qsize_after_pop=" + std::to_string(qsizeAfterPop) +
                             " evPeerGen=" + std::to_string(ev.peerGeneration) +
                             " activePeerGen=" + std::to_string(activePeerGeneration.load()) +
                             " evSessionEndSeq=" + std::to_string(ev.sessionEndSeqAtEnqueue) +
                             " curSessionEndSeq=" + std::to_string(sessionEndSeq.load()));

        if ((ev.kind == ControlEventKind::PeerStateChanged ||
             ev.kind == ControlEventKind::LocalDescription ||
             ev.kind == ControlEventKind::LocalCandidate) &&
            ev.peerGeneration != activePeerGeneration.load()) {
          runtime_diag_log(sessionId, runtimeStartedAt, "stale_event_ignore",
                           "kind=" + std::to_string(static_cast<int>(ev.kind)) +
                               " evGen=" + std::to_string(ev.peerGeneration) +
                               " activeGen=" + std::to_string(activePeerGeneration.load()));
          runtime_diag_log(sessionId, runtimeStartedAt, "control_queue",
                           "dispatch_end seq=" + std::to_string(ev.seq) + " reason=stale_event");
          continue;
        }

        if (ev.kind == ControlEventKind::SignalingOffer) {
          if (pendingSessionEndRecreate.exchange(false)) {
            pendingSessionEndStartedAtMs = 0;
            runtime_diag_log(sessionId, runtimeStartedAt, "peer_recreate",
                             "offer_path_recreate_after_session_end=1");
            if (!create_peer("offer_after_session_end", currentVideoMid, currentAudioMid, currentVideoPayloadType)) {
              sessionRestartRequested = true;
              set_state(RuntimeState::Waiting, "offer_after_session_end_recreate_failed_restart");
              stop = true;
              controlCv.notify_all();
              frameCv.notify_all();
              runtime_diag_log(sessionId, runtimeStartedAt, "control_queue",
                               "dispatch_end seq=" + std::to_string(ev.seq) + " reason=offer_after_session_end_recreate_failed");
              continue;
            }
          }

          const std::string offerVideoMid =
              parse_mid_for_media_from_offer_sdp(ev.text, "video").value_or(currentVideoMid);
          const std::string offerAudioMid =
              parse_mid_for_media_from_offer_sdp(ev.text, "audio").value_or(currentAudioMid);
          const uint8_t offerVideoPayloadType =
              parse_h264_payload_type_from_offer_sdp(ev.text).value_or(currentVideoPayloadType);
          if (offerVideoMid != currentVideoMid || offerAudioMid != currentAudioMid ||
              offerVideoPayloadType != currentVideoPayloadType) {
            runtime_diag_log(sessionId, runtimeStartedAt, "peer_recreate",
                             "offer_mid_update from videoMid=" + currentVideoMid +
                                 " audioMid=" + currentAudioMid +
                                 " videoPt=" + std::to_string(static_cast<int>(currentVideoPayloadType)) +
                                 " to videoMid=" + offerVideoMid + " audioMid=" + offerAudioMid +
                                 " videoPt=" + std::to_string(static_cast<int>(offerVideoPayloadType)));
            if (!create_peer("offer_mid_update", offerVideoMid, offerAudioMid, offerVideoPayloadType)) {
              sessionRestartRequested = true;
              set_state(RuntimeState::Waiting, "offer_mid_update_failed_restart");
              stop = true;
              controlCv.notify_all();
              frameCv.notify_all();
              runtime_diag_log(sessionId, runtimeStartedAt, "control_queue",
                               "dispatch_end seq=" + std::to_string(ev.seq) + " reason=offer_mid_update_recreate_failed");
              continue;
            }
            currentVideoMid = offerVideoMid;
            currentAudioMid = offerAudioMid;
            currentVideoPayloadType = offerVideoPayloadType;
          }

          if (gotRemoteOffer.load()) {
            runtime_diag_log(sessionId, runtimeStartedAt, "offer_apply",
                             "skip reason=already_has_active_offer action=restart_cycle");
            sessionRestartRequested = true;
            set_state(RuntimeState::Waiting, "offer_while_active_restart");
            stop = true;
            controlCv.notify_all();
            frameCv.notify_all();
            runtime_diag_log(sessionId, runtimeStartedAt, "control_queue",
                             "dispatch_end seq=" + std::to_string(ev.seq) + " reason=offer_already_active_restart");
            continue;
          }
          gotRemoteOffer = true;
          hadActivePeerInCycle = true;
          videoPayloadType = currentVideoPayloadType;
          runtime_diag_log(sessionId, runtimeStartedAt, "codec",
                           "offer_h264_payload_type_selected=" +
                               std::to_string(static_cast<int>(currentVideoPayloadType)));
          sessionRestartRequested = false;
          set_state(RuntimeState::Negotiating, "offer_received");
          runtime_diag_log(sessionId, runtimeStartedAt, "offer_apply", "begin sdpBytes=" + std::to_string(ev.text.size()));
          rtc::Description off(ev.text, "offer");
          try {
            std::shared_ptr<rtc::PeerConnection> pcSnapshot;
            {
              std::lock_guard<std::mutex> lk(peerMu);
              pcSnapshot = pc;
            }
            if (!pcSnapshot) throw std::runtime_error("peer_connection_missing");
            pcSnapshot->setRemoteDescription(off);
            sessionRestartRequested = false;
            runtime_diag_log(sessionId, runtimeStartedAt, "offer_apply", "end success=1");
          } catch (...) {
            runtime_diag_log(sessionId, runtimeStartedAt, "offer_apply", "end success=0 exception=1");
            sessionRestartRequested = true;
            stop = true;
            set_state(RuntimeState::Waiting, "offer_apply_failed_restart");
            controlCv.notify_all();
            frameCv.notify_all();
            runtime_diag_log(sessionId, runtimeStartedAt, "control_queue",
                             "dispatch_end seq=" + std::to_string(ev.seq) + " reason=offer_apply_failed_restart");
          }
        } else if (ev.kind == ControlEventKind::SignalingIce) {
          try {
            const std::string cand = ev.text;
            std::string mid = ev.candidateMid;
            if (mid.empty() && ev.candidateMLineIndex.has_value()) mid = std::to_string(*ev.candidateMLineIndex);
            if (mid.empty()) mid = "0";
            runtime_diag_log(sessionId, runtimeStartedAt, "ice_apply",
                             "remote_candidate_add mid=" + mid + " candidateBytes=" + std::to_string(cand.size()));
            std::shared_ptr<rtc::PeerConnection> pcSnapshot;
            {
              std::lock_guard<std::mutex> lk(peerMu);
              pcSnapshot = pc;
            }
            if (!pcSnapshot) throw std::runtime_error("peer_connection_missing_for_ice");
            pcSnapshot->addRemoteCandidate(rtc::Candidate(cand, mid));
          } catch (...) {
            runtime_diag_log(sessionId, runtimeStartedAt, "ice_apply", "remote_candidate_add_failed exception=1");
          }
        } else if (ev.kind == ControlEventKind::ClientSessionEnd) {
          bool hasPcNow = false;
          bool hasVtrackNow = false;
          bool hasAtrackNow = false;
          {
            std::lock_guard<std::mutex> lk(peerMu);
            hasPcNow = static_cast<bool>(pc);
            hasVtrackNow = static_cast<bool>(vtrack);
            hasAtrackNow = static_cast<bool>(atrack);
          }
          runtime_diag_log(sessionId, runtimeStartedAt, "lifecycle",
                           "session_end_requested_by_client=1 seq=" + std::to_string(sessionEndSeq.load()) +
                               " hasPc=" + (hasPcNow ? "1" : "0") +
                               " hasVtrack=" + (hasVtrackNow ? "1" : "0") +
                               " hasAtrack=" + (hasAtrackNow ? "1" : "0"));
          connected = false;
          trackOpen = false;
          audioTrackOpen = false;
          set_remote_cursor_hidden(false);
          gotRemoteOffer = false;
          force_release_pressed_mouse_buttons();
          set_state(RuntimeState::Waiting, "session_end_by_client");
          pendingSessionEndRecreate = true;
          pendingSessionEndStartedAtMs = ::GetTickCount64();
          runtime_diag_log(sessionId, runtimeStartedAt, "lifecycle",
                           "session_end_wait_peer_terminal_or_next_offer=1 graceMs=" +
                               std::to_string(sessionEndGraceMs));
        } else if (ev.kind == ControlEventKind::PeerStateChanged) {
          const bool isConnected = (ev.pcState == rtc::PeerConnection::State::Connected);
          connected = isConnected;
          if (isConnected) {
            hadActivePeerInCycle = true;
            set_state(RuntimeState::Streaming, "peer_connected");
          } else {
            runtime_diag_log(sessionId, runtimeStartedAt, "peer_state",
                             "non_connected state=" + std::to_string(static_cast<int>(ev.pcState)));
            // Do not clear negotiated-offer state on transient non-connected states (e.g. New/Connecting).
            // Only perform full cleanup on terminal states.
            if (ev.pcState == rtc::PeerConnection::State::Disconnected ||
                ev.pcState == rtc::PeerConnection::State::Failed ||
                ev.pcState == rtc::PeerConnection::State::Closed) {
              runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "begin reason=peer_state_terminal");
              set_remote_cursor_hidden(false);
              force_release_pressed_mouse_buttons();
              trackOpen = false;
              audioTrackOpen = false;
              gotRemoteOffer = false;
              set_state(RuntimeState::Waiting, "peer_disconnected_terminal");
              if (pendingSessionEndRecreate.exchange(false)) {
                pendingSessionEndStartedAtMs = 0;
                runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup",
                                 "peer_state_terminal_recreate_after_session_end=1");
              }
              if (!create_peer("peer_state_terminal", currentVideoMid, currentAudioMid, currentVideoPayloadType)) {
                sessionRestartRequested = true;
                runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup",
                                 "peer_state_terminal_restart_requested=1 reason=recreate_failed");
                stop = true;
                controlCv.notify_all();
                frameCv.notify_all();
              } else {
                runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup",
                                 "peer_state_terminal_restart_skipped=1 reason=recreate_ok");
              }
              runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "end reason=peer_state_terminal");
            }
          }
        } else if (ev.kind == ControlEventKind::LocalDescription) {
          if (gotRemoteOffer.load() && ev.localDescType == rtc::Description::Type::Answer) {
            try {
              std::filesystem::create_directories("logs");
              std::ofstream ofs("logs/runtime_answer_from_host_latest.sdp", std::ios::trunc);
              ofs << ev.text;
            } catch (...) {
            }
            runtime_diag_log(sessionId, runtimeStartedAt, "codec",
                             "answer_h264_payload_type_skip_parse keep_video_payload_type=" +
                                 std::to_string(static_cast<int>(videoPayloadType.load())));
            runtime_diag_log(sessionId, runtimeStartedAt, "answer_sdp",
                             std::string("has_sendonly=") + (ev.text.find("a=sendonly") != std::string::npos ? "1" : "0") +
                             " has_sendrecv=" + (ev.text.find("a=sendrecv") != std::string::npos ? "1" : "0") +
                             " has_inactive=" + (ev.text.find("a=inactive") != std::string::npos ? "1" : "0"));
            Message ans;
            ans.type = MessageType::Answer;
            ans.sdp = ev.text;
            sig.send_message(ans);
          }
        } else if (ev.kind == ControlEventKind::LocalCandidate) {
          Message ice;
          ice.type = MessageType::Ice;
          ice.candidate = ev.text;
          ice.candidateMid = ev.candidateMid.empty() ? "0" : ev.candidateMid;
          ice.candidateMLineIndex = ev.candidateMLineIndex;
          runtime_diag_log(sessionId, runtimeStartedAt, "ice_send",
                           "local_candidate_send mid=" + ice.candidateMid +
                               " mline=" + (ice.candidateMLineIndex.has_value() ? std::to_string(*ice.candidateMLineIndex) : "") +
                               " candidateBytes=" + std::to_string(ev.text.size()));
          sig.send_message(ice);
        }
        runtime_diag_log(sessionId, runtimeStartedAt, "control_queue",
                         "dispatch_end seq=" + std::to_string(ev.seq) +
                             " kind=" + control_kind_str(ev.kind));
      }
    } catch (...) {
      runtime_diag_log(sessionId, runtimeStartedAt, "fatal", "control_thread_exception");
      stop = true;
      sessionRestartRequested = true;
      controlCv.notify_all();
      frameCv.notify_all();
    }
  });

  std::thread mediaThread([&]() {
    try {
      remote60::common::H264RtpPacketizer pkt(1188);
      remote60::common::OpusRtpPacketizer opusPkt;
      uint16_t seq = 5000;
      uint32_t ts = 90000;
      uint16_t aseq = 10000;
      uint32_t ats = 48000;
      const uint32_t fpsSafe = (videoFps == 0 ? 1u : videoFps);
      uint32_t videoTsStep = 90000u / fpsSafe;
      if (videoTsStep == 0) videoTsStep = 1;
      const LONGLONG sampleDurationHns = std::max<LONGLONG>(1, 10000000LL / static_cast<LONGLONG>(fpsSafe));
      const uint32_t packetPacingUs = env_u32("REMOTE60_VIDEO_PACKET_PACING_US", 0, 0, 5000);
      const bool verboseMediaLogs = env_bool("REMOTE60_VERBOSE_MEDIA_LOGS", false);
      const int kAudioFrame = 960;
      std::vector<float> pcm(kAudioFrame * 2, 0.0f);
      std::vector<uint8_t> encodedAudio(4000);
      double phase = 0.0;
      auto nextAudioTick = std::chrono::steady_clock::now();
      int opusErr = OPUS_OK;
      OpusEncoder* opusEnc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &opusErr);
      if (opusEnc && opusErr == OPUS_OK) {
        opus_encoder_ctl(opusEnc, OPUS_SET_BITRATE(64000));
        opus_encoder_ctl(opusEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
      } else {
        opusEnc = nullptr;
        runtime_diag_log(sessionId, runtimeStartedAt, "audio", "opus_encoder_create_failed runtime_audio_disabled=1");
      }
      uint64_t localFrameVersion = 0;

    uint64_t waitOfferCount = 0;
    uint64_t waitConnectedCount = 0;
    uint64_t waitTrackCount = 0;
    uint64_t waitSessionEndPendingCount = 0;
    bool lastTrackOpenObserved = false;
    while (!stop.load()) {
      std::shared_ptr<rtc::Track> videoTrackSnapshot;
      std::shared_ptr<rtc::Track> audioTrackSnapshot;
      {
        std::lock_guard<std::mutex> lk(peerMu);
        videoTrackSnapshot = vtrack;
        audioTrackSnapshot = atrack;
      }

      if (pendingSessionEndRecreate.load()) {
        const uint64_t startedAtMs = pendingSessionEndStartedAtMs.load();
        const uint64_t nowMs = ::GetTickCount64();
        const uint64_t elapsedMs = (startedAtMs > 0 && nowMs >= startedAtMs) ? (nowMs - startedAtMs) : 0;
        ++waitSessionEndPendingCount;
        if ((waitSessionEndPendingCount % 100) == 1) {
          runtime_diag_log(sessionId, runtimeStartedAt, "media_wait",
                           "reason=session_end_pending count=" + std::to_string(waitSessionEndPendingCount) +
                               " elapsedMs=" + std::to_string(elapsedMs) +
                               " graceMs=" + std::to_string(sessionEndGraceMs));
        }
        if (startedAtMs > 0 && elapsedMs >= sessionEndGraceMs) {
          if (pendingSessionEndRecreate.exchange(false)) {
            pendingSessionEndStartedAtMs = 0;
            runtime_diag_log(sessionId, runtimeStartedAt, "lifecycle",
                             "session_end_pending_timeout elapsedMs=" + std::to_string(elapsedMs) +
                                 " action=restart_cycle");
            sessionRestartRequested = true;
            stop = true;
            controlCv.notify_all();
            frameCv.notify_all();
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      if (opusEnc) {
        const auto now = std::chrono::steady_clock::now();
        const bool audioReady = gotRemoteOffer.load() && connected.load() && audioTrackOpen.load() &&
                                static_cast<bool>(audioTrackSnapshot);
        if (audioReady && now >= nextAudioTick) {
          for (int i = 0; i < kAudioFrame; ++i) {
            const float s = static_cast<float>(0.10 * std::sin(phase));
            phase += (2.0 * 3.14159265358979323846 * 440.0) / 48000.0;
            pcm[i * 2 + 0] = s;
            pcm[i * 2 + 1] = s;
          }
          const int n = opus_encode_float(opusEnc, pcm.data(), kAudioFrame, encodedAudio.data(),
                                          static_cast<opus_int32>(encodedAudio.size()));
          if (n > 0) {
            std::vector<uint8_t> payload(encodedAudio.begin(), encodedAudio.begin() + n);
            const auto p = opusPkt.packetize_20ms_frame(payload, aseq, ats);
            const auto raw = make_rtp_packet(111, 0x11223344, p.sequence, p.timestamp, false, p.payload);
            bool sent = false;
            try {
              if (audioTrackSnapshot && audioTrackOpen.load() && connected.load()) {
                sent = audioTrackSnapshot->send(reinterpret_cast<const rtc::byte*>(raw.data()), raw.size());
              }
            } catch (const std::exception& ex) {
              runtime_diag_log(sessionId, runtimeStartedAt, "audio_send",
                               std::string("exception what=") + ex.what());
              sent = false;
            } catch (...) {
              runtime_diag_log(sessionId, runtimeStartedAt, "audio_send", "exception what=unknown");
              sent = false;
            }
            if (sent) {
              if (!audioTrackOpen.load()) {
                audioTrackOpen = true;
                runtime_diag_log(sessionId, runtimeStartedAt, "track", "audio_track_open inferred_by_send_success=1");
              }
              ++audioSentPkts;
            } else {
              ++audioDropPkts;
            }
          }
          nextAudioTick += std::chrono::milliseconds(20);
          if (nextAudioTick + std::chrono::milliseconds(200) < now) {
            nextAudioTick = now + std::chrono::milliseconds(20);
          }
        }
      }

      const bool trackOpenNow = (trackOpen.load() && static_cast<bool>(videoTrackSnapshot));
      if (trackOpenNow != lastTrackOpenObserved) {
        lastTrackOpenObserved = trackOpenNow;
        runtime_diag_log(sessionId, runtimeStartedAt, "track",
                         std::string("video_track_isOpen_transition open=") + (trackOpenNow ? "1" : "0"));
      }
      if (!gotRemoteOffer.load()) {
        set_state(RuntimeState::Waiting, "no_offer_yet");
        ++waitOfferCount;
        if ((waitOfferCount % 200) == 1) {
          runtime_diag_log(sessionId, runtimeStartedAt, "media_wait",
                           "reason=no_offer count=" + std::to_string(waitOfferCount));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      if (!connected.load()) {
        ++waitConnectedCount;
        if ((waitConnectedCount % 200) == 1) {
          runtime_diag_log(sessionId, runtimeStartedAt, "media_wait",
                           "reason=not_connected count=" + std::to_string(waitConnectedCount));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      if (!(trackOpen.load() && static_cast<bool>(videoTrackSnapshot))) {
        ++waitTrackCount;
        if ((waitTrackCount % 200) == 1) {
          runtime_diag_log(sessionId, runtimeStartedAt, "media_wait",
                           "reason=track_not_open_nonblocking count=" + std::to_string(waitTrackCount));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      std::vector<uint8_t> nv12;
      {
        std::unique_lock<std::mutex> lk(frameMu);
        frameCv.wait_for(lk, std::chrono::milliseconds(50), [&]() {
          return stop.load() || frameVersion != localFrameVersion;
        });
        if (stop.load()) break;
        if (frameVersion == localFrameVersion || latestNv12.empty()) continue;
        localFrameVersion = frameVersion;
        nv12 = latestNv12;
      }

      IMFSample* sample = nullptr;
      IMFMediaBuffer* mb = nullptr;
      if (SUCCEEDED(MFCreateMemoryBuffer((DWORD)nv12.size(), &mb)) && SUCCEEDED(MFCreateSample(&sample))) {
        BYTE* p = nullptr; DWORD maxLen = 0, curLen = 0;
        if (SUCCEEDED(mb->Lock(&p, &maxLen, &curLen))) {
          memcpy(p, nv12.data(), nv12.size());
          mb->Unlock();
          mb->SetCurrentLength((DWORD)nv12.size());
          sample->AddBuffer(mb);
          sample->SetSampleDuration(sampleDurationHns);
          sample->SetSampleTime(static_cast<LONGLONG>(encodedFrames.load()) * sampleDurationHns);
          const HRESULT inHr = enc->ProcessInput(0, sample, 0);
          if (SUCCEEDED(inHr)) {
            MFT_OUTPUT_STREAM_INFO osi{};
            enc->GetOutputStreamInfo(0, &osi);
            IMFMediaBuffer* ob = nullptr; IMFSample* os = nullptr;
            if (SUCCEEDED(MFCreateMemoryBuffer(osi.cbSize > 0 ? osi.cbSize : (1 << 20), &ob)) && SUCCEEDED(MFCreateSample(&os))) {
              os->AddBuffer(ob);
              MFT_OUTPUT_DATA_BUFFER odb{}; odb.dwStreamID = 0; odb.pSample = os;
              DWORD stt = 0;
              HRESULT po = enc->ProcessOutput(0, 1, &odb, &stt);
              if (po == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                static std::atomic<uint64_t> needMoreInputCount{0};
                const auto c = ++needMoreInputCount;
                if ((c % 120) == 1) {
                  runtime_diag_log(sessionId, runtimeStartedAt, "encoder_output", "need_more_input count=" + std::to_string(c));
                }
              } else if (SUCCEEDED(po)) {
                IMFMediaBuffer* outBuf = nullptr;
                if (SUCCEEDED(os->ConvertToContiguousBuffer(&outBuf)) && outBuf) {
                  BYTE* op = nullptr; DWORD ml = 0, cl = 0;
                  const bool locked = SUCCEEDED(outBuf->Lock(&op, &ml, &cl));
                  if (locked) {
                    if (cl > 0) {
                      const uint64_t frameIndex = encodedFrames.load() + 1;
                      const bool logFrame = verboseMediaLogs || frameIndex <= 5 || (frameIndex % 120) == 0;
                      if (logFrame) {
                        runtime_diag_log(sessionId, runtimeStartedAt, "encoder_output",
                                         "begin frame=" + std::to_string(frameIndex) +
                                             " bytes=" + std::to_string(cl));
                      }
                      std::vector<uint8_t> annexb(op, op + cl);
                      auto rtpPkts = pkt.packetize_annexb(annexb, ts, seq);
                      ts += videoTsStep;
                      uint64_t sentLocal = 0;
                      uint64_t droppedLocal = 0;
                      if (logFrame) {
                        runtime_diag_log(sessionId, runtimeStartedAt, "rtp_send",
                                         "begin frame=" + std::to_string(frameIndex) +
                                             " packets=" + std::to_string(rtpPkts.size()));
                      }
                    for (auto& pld : rtpPkts) {
                      if (pendingSessionEndRecreate.load()) {
                        runtime_diag_log(sessionId, runtimeStartedAt, "rtp_send", "break reason=session_end_pending");
                        break;
                      }
                      if (!connected.load()) break;
                      if (!videoTrackSnapshot) break;
                      if (!trackOpen.load()) {
                        runtime_diag_log(sessionId, runtimeStartedAt, "rtp_send", "break reason=track_not_open");
                        break;
                      }
                      auto raw = make_rtp_packet(videoPayloadType.load(), 0x22334455, pld.sequence, pld.timestamp,
                                                 pld.marker, pld.payload);
                      bool sent = false;
                      try {
                        sent = videoTrackSnapshot->send(reinterpret_cast<const rtc::byte*>(raw.data()), raw.size());
                      } catch (const std::exception& ex) {
                        runtime_diag_log(sessionId, runtimeStartedAt, "rtp_send",
                                         std::string("send_exception what=") + ex.what());
                        sent = false;
                      } catch (...) {
                        runtime_diag_log(sessionId, runtimeStartedAt, "rtp_send", "send_exception what=unknown");
                        sent = false;
                      }
                      if (sent) {
                        if (!trackOpen.load()) {
                          trackOpen = true;
                          runtime_diag_log(sessionId, runtimeStartedAt, "track", "video_track_open inferred_by_send_success=1");
                        }
                        ++sentPkts;
                          ++sentLocal;
                      } else {
                          ++dropPkts;
                          ++droppedLocal;
                        }
                        if (packetPacingUs > 0) {
                          std::this_thread::sleep_for(std::chrono::microseconds(packetPacingUs));
                        }
                      }
                      ++encodedFrames;
                      if (logFrame) {
                        runtime_diag_log(sessionId, runtimeStartedAt, "rtp_send",
                                         "end frame=" + std::to_string(frameIndex) +
                                             " sent=" + std::to_string(sentLocal) +
                                             " dropped=" + std::to_string(droppedLocal));
                        runtime_diag_log(sessionId, runtimeStartedAt, "encoder_output",
                                         "end encodedFrames=" + std::to_string(encodedFrames.load()));
                      }
                    }
                    outBuf->Unlock();
                  }
                  outBuf->Release();
                }
              }
              if (odb.pEvents) odb.pEvents->Release();
              ob->Release(); os->Release();
            }
          } else {
            static std::atomic<uint64_t> processInputFailCount{0};
            const auto c = ++processInputFailCount;
            if ((c % 60) == 1) {
              runtime_diag_log(sessionId, runtimeStartedAt, "encoder_input",
                               "process_input_failed hr=0x" + [&]() { std::ostringstream oss; oss << std::hex << std::uppercase << static_cast<unsigned long>(inHr); return oss.str(); }() +
                                   " count=" + std::to_string(c));
            }
          }
        }
      }
      if (mb) mb->Release();
      if (sample) sample->Release();
    }
      if (opusEnc) opus_encoder_destroy(opusEnc);
    } catch (const std::exception& ex) {
      runtime_diag_log(sessionId, runtimeStartedAt, "fatal",
                       std::string("media_thread_exception what=") + ex.what());
      stop = true;
      sessionRestartRequested = true;
      controlCv.notify_all();
      frameCv.notify_all();
    } catch (...) {
      runtime_diag_log(sessionId, runtimeStartedAt, "fatal", "media_thread_exception what=unknown");
      stop = true;
      sessionRestartRequested = true;
      controlCv.notify_all();
      frameCv.notify_all();
    }
  });

  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds > 0 ? seconds : 1);
  auto nextCursorGuardTick = std::chrono::steady_clock::now();
  bool cycleRestartRequested = false;
  while (serverMode || std::chrono::steady_clock::now() < until) {
    if (stop.load()) {
      cycleRestartRequested = serverMode && sessionRestartRequested.load();
      runtime_diag_log(sessionId, runtimeStartedAt, "lifecycle",
                       std::string("stop_observed break=1 cycleRestartRequested=") +
                           (cycleRestartRequested ? "1" : "0"));
      break;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= nextCursorGuardTick) {
      release_mouse_capture_guard();
      release_stale_pressed_mouse_buttons(1500);
      nextCursorGuardTick = now + std::chrono::milliseconds(250);
    }
    if (serverMode && sessionRestartRequested.exchange(false)) {
      cycleRestartRequested = true;
      runtime_diag_log(sessionId, runtimeStartedAt, "lifecycle",
                       "server_cycle_restart_requested=1 keep_running=0");
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  shuttingDown = true;
  acceptPeerCallbacks = false;
  acceptCaptureCallbacks = false;
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "begin reason=runtime_stop");
  stop = true;
  force_release_pressed_mouse_buttons();
  controlCv.notify_all();
  frameCv.notify_all();
  if (controlThread.joinable()) controlThread.join();
  if (mediaThread.joinable()) mediaThread.join();

  out.capturedFrames = frameCount.load();
  out.encodedFrames = encodedFrames.load();
  out.videoRtpSent = sentPkts.load();
  out.videoRtpDropped = dropPkts.load();
  out.audioRtpSent = audioSentPkts.load();
  out.audioRtpDropped = audioDropPkts.load();
  out.inputEventsReceived = inputEventsReceived.load();
  out.inputEventsApplied = inputEventsApplied.load();

  detach_current_peer("runtime_stop");
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=frame_handler_unsubscribe begin");
  try {
    pool.FrameArrived(token);
  } catch (...) {
    runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup",
                     "step=frame_handler_unsubscribe exception=1");
  }
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=frame_handler_unsubscribe end");
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=session_close begin");
  try {
    session.Close();
  } catch (...) {
    runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=session_close exception=1");
  }
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=session_close end");
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=pool_close begin");
  try {
    pool.Close();
  } catch (...) {
    runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=pool_close exception=1");
  }
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=pool_close end");
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=signaling_disconnect begin");
  sig.set_on_message({});
  sig.set_on_state({});
  sig.disconnect();
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=signaling_disconnect end");
  out.connected = connected.load();
  out.videoTrackOpen = trackOpen.load();
  out.audioTrackOpen = audioTrackOpen.load();
  if (serverMode && cycleRestartRequested) {
    out.detail = "cycle_restart";
  } else {
    out.detail = (out.connected && out.videoRtpSent > 0) ? "ok" : "runtime_no_video";
  }

  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=encoder_release begin");
  enc->Release();
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=encoder_release end");
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=mf_shutdown begin");
  MFShutdown();
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup", "step=mf_shutdown end");
  force_release_pressed_mouse_buttons();
  release_mouse_capture_guard();
  set_remote_cursor_hidden(false);
  runtime_diag_log(sessionId, runtimeStartedAt, "input_guard", "mouse_release_guard applied_on_shutdown=1");
  runtime_diag_log(sessionId, runtimeStartedAt, "disconnect_cleanup",
                   "end reason=runtime_stop detail=" + out.detail + " capturedFrames=" +
                       std::to_string(out.capturedFrames) + " encodedFrames=" + std::to_string(out.encodedFrames) +
                       " videoRtpSent=" + std::to_string(out.videoRtpSent) + " videoRtpDropped=" +
                       std::to_string(out.videoRtpDropped) + " audioRtpSent=" + std::to_string(out.audioRtpSent) +
                       " audioRtpDropped=" + std::to_string(out.audioRtpDropped) + " inputEventsReceived=" +
                       std::to_string(out.inputEventsReceived) + " inputEventsApplied=" +
                       std::to_string(out.inputEventsApplied));
  return out;
#endif
}

}  // namespace remote60::host
