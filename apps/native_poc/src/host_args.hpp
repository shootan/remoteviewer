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
// (parse_args itself lives in host_args.cpp, Phase 0-7b). Behavior is byte-identical. The env
// switch prelude at the top of main() stays there until Phase 1 folds it into the state structs.
// The env_* helpers moved to env_util.hpp, shared with the viewer (viewer split refactor Phase 0-15).

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "env_util.hpp"

namespace remote60::native_poc {

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

// Command line (+ optional --config JSON profile) -> Args. Defined in host_args.cpp.
Args parse_args(int argc, char** argv);

}  // namespace remote60::native_poc
