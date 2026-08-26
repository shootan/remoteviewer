#pragma once

// Host command-line argument record and the small environment/number parsing helpers.
//
// Role:    struct Args (every --flag the host accepts, with defaults) plus parse_u32 and the
//          env_* readers used by parse_args and by the REMOTE60_NATIVE_* switch prelude in main().
// Thread:  none -- plain data and pure functions. Args is filled once in main() before any thread
//          starts and is read-only afterwards.
// Input:   argv strings / environment variable names.
// Output:  populated Args / parsed values with fallbacks and clamps.
// Callers: native_video_host_main.cpp (parse_args, main prologue), host_bgra_scale
//          (choose_h264_encode_size reads encodeWidth/encodeHeight/bitrate).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-7a). Header-only
// so no new translation unit is added and behavior is byte-identical. parse_args itself and the env
// switch prelude move later (Phase 0-7b) once every module they feed has been split out.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace remote60::native_poc {

inline std::string env_string_or_empty(const char* key) {
  if (!key) return std::string{};
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string{};
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

inline bool parse_u32(const char* s, uint32_t* out) {
  if (!s || !out) return false;
  char* end = nullptr;
  const unsigned long v = std::strtoul(s, &end, 10);
  if (!end || *end != '\0') return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

inline bool env_truthy(const char* key) {
  if (!key) return false;
  const char* v = std::getenv(key);
  if (!v) return false;
  const std::string s = v;
  return s == "1" || s == "true" || s == "TRUE" || s == "on" || s == "ON";
}

inline uint32_t env_u32_clamped(const char* key, uint32_t fallback, uint32_t minValue, uint32_t maxValue) {
  if (!key) return fallback;
  const char* raw = std::getenv(key);
  if (!raw) return fallback;
  uint32_t parsed = 0;
  if (!parse_u32(raw, &parsed)) return fallback;
  return std::clamp<uint32_t>(parsed, minValue, maxValue);
}

}  // namespace remote60::native_poc
