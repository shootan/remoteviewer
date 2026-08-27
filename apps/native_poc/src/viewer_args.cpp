// See viewer_args.hpp. Extracted verbatim from native_video_client_main.cpp (Phase 0-4).

#include "viewer_args.hpp"

#include <iostream>

namespace remote60::native_poc::viewer {

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
    } else if (k == "--directory-url" && i + 1 < argc) {
      a.directoryUrl = argv[++i];
    } else if (k == "--directory-id" && i + 1 < argc) {
      a.directoryAccount = argv[++i];
    } else if (k == "--directory-pw" && i + 1 < argc) {
      a.directoryPassword = argv[++i];
    } else if (k == "--directory-session" && i + 1 < argc) {
      a.directorySession = argv[++i];
    } else if (k == "--directory-host-id" && i + 1 < argc) {
      a.directoryHostId = argv[++i];
    } else if (k == "--directory-host-name" && i + 1 < argc) {
      a.directoryHostName = argv[++i];
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
    } else if (k == "--runtime-fps" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.runtimeFps = std::clamp<uint32_t>(v, 1, 240);
    } else if (k == "--monitor" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.monitorId = v;
    } else if (k == "--initial-view" && i + 1 < argc) {
      a.initialView = ascii_lower(trim_ascii(argv[++i]));
    } else if (k == "--start-in-picker") {
      a.initialView = "targets";
    }
  }
  return a;
}

}  // namespace remote60::native_poc::viewer
