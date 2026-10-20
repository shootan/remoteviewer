// See host_args.hpp for the module summary. parse_args below is moved verbatim from
// native_video_host_main.cpp (host split refactor Phase 0-7b); no logic change.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "bind_port_candidates.hpp"
#include "host_args.hpp"
#include "json_profile.hpp"
#include "native_video_transport.hpp"

namespace remote60::native_poc {

Args parse_args(int argc, char** argv) {
  Args a;
  std::string configPath;
  bool prelogin = false;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    if (k == "--config" && i + 1 < argc) {
      configPath = argv[++i];
    } else if (k == "--prelogin") {
      prelogin = true;
    }
  }
  // r6 V5: a --prelogin worker takes nothing from a profile -- not its settings, not the environment
  // overrides a profile can carry. The service starts it with --prelogin alone; one given anyway is
  // refused at startup (preloginRefusedConfig), never half-applied.
  if (prelogin && !configPath.empty()) {
    a.preloginRefusedConfig = true;
    configPath.clear();
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
    } else if (k == "--prelogin") {
      a.preloginMode = true;
    } else if (k == "--directory-url" && i + 1 < argc) {
      a.directoryUrl = argv[++i];
    } else if (k == "--directory-cache" && i + 1 < argc) {
      a.directoryCachePath = argv[++i];
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
  // r6 G1: pin the prelogin role's required transport/codec when they are at their defaults so that
  // `--prelogin` alone enters the UDP+H264 directory-registration path. A conflicting EXPLICIT value is
  // left unchanged here on purpose: resolve_transport refuses it with a diagnostic (no silent fallback).
#ifdef REMOTE60_STREAM_TEST_SEAM
  // TEST BUILD ONLY (GNLinkStreamMigrationTest, r6 V2): a worker started with the service's own argument
  // list (--prelogin alone) would take the product port on every interface -- which the real GNLink on
  // the test machine may hold. The test names a loopback address, a free port and a lifetime here.
  if (const char* v = std::getenv("GNLINK_STREAM_TEST_BIND_ADDRESS")) a.bindAddress = v;
  if (const char* v = std::getenv("GNLINK_STREAM_TEST_BIND_PORT")) {
    a.bindPortCandidates = remote60::native_poc::parse_bind_port_candidates(v);
    if (!a.bindPortCandidates.empty()) a.bindPort = a.bindPortCandidates.front();
  }
  if (const char* v = std::getenv("GNLINK_STREAM_TEST_SECONDS")) a.seconds = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
  // r9 W3: GNLinkHost names its child's TCP control port (43001), which the real GNLink may hold.
  if (const char* v = std::getenv("GNLINK_STREAM_TEST_CONTROL_PORT")) {
    a.controlPort = static_cast<uint16_t>(std::strtoul(v, nullptr, 10));
  }
#endif
  if (a.preloginMode) {
    (void)ApplyPreloginRoleContract(a.transport, a.codec);
    // r6 V6: the same listeners GNLinkHost gives its child (43000 primary, 3478 second; host_app_main),
    // so a viewer reaches the PC on the same ports before and after login. No control port: before login
    // there is no separate TCP control link (r6 G2).
    if (a.bindPortCandidates.empty()) {
      a.bindPortCandidates = remote60::native_poc::parse_bind_port_candidates("43000,3478");
      if (!a.bindPortCandidates.empty()) a.bindPort = a.bindPortCandidates.front();
    }
  }
  return a;
}

}  // namespace remote60::native_poc
