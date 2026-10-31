#pragma once

// Command line of GNLinkViewer.
//
// Role:    struct Args (every --flag the viewer accepts, with defaults) and parse_args, including
//          the --config JSON profile overrides.
// Thread:  main only, before anything else starts.
// Input:   argc/argv, optional JSON profile, environment.
// Output:  a filled Args.
// Callers: main(); the shell (client_shell_main.cpp) is the other side of this contract.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-4).

#include "viewer_common.hpp"
#include "viewer_env_util.hpp"

namespace remote60::native_poc::viewer {

struct Args {
  std::string host = "127.0.0.1";
  uint16_t port = 43000;
  uint16_t controlPort = 0;
  // Reaching a host through the directory instead of by address. When these are set the client
  // signs in, races the candidates the server offers and uses whichever answers -- which is the
  // only way to reach a PC behind NAT, and the only way to use the relay at all.
  std::string directoryUrl;
  std::string directoryAccount;
  std::string directoryPassword;
  // Preferred over the password when the caller already signed in. A command line is readable by
  // any process that can enumerate them, and a session token expires where a password does not.
  std::string directorySession;
  std::string directoryHostId;
  // Empty means "the only host on the account", which is the common case and saves the caller
  // from having to look an id up first.
  std::string directoryHostName;
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
  uint32_t runtimeFps = 0;
  // Which screen to open on a host with more than one. Zero is the primary, which is what the
  // client always asked for implicitly.
  uint32_t monitorId = 0;
  // How the session opens. "targets" starts on the capture-target picker and streams only after
  // the user selects one (the product flow, mirroring the Android client); "stream" goes straight
  // to the host's default desktop. Empty falls back to the REMOTE60_NATIVE_START_STREAM_VIEW env
  // var so existing manual/headless probes keep working unchanged.
  std::string initialView;
};

Args parse_args(int argc, char** argv);

}  // namespace remote60::native_poc::viewer
