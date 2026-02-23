#include "runtime_server.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include "common/version.hpp"
#include "realtime_runtime.hpp"

namespace remote60::host {

namespace {

void print_runtime_stats(const char* prefix, const RealtimeRuntimeStats& r) {
  std::cout << prefix
            << " detail=" << r.detail
            << " webrtcReady=" << r.webrtcReady
            << " connected=" << r.connected
            << " captureReady=" << r.captureReady
            << " encoderReady=" << r.encoderReady
            << " videoTrackOpen=" << r.videoTrackOpen
            << " audioTrackOpen=" << r.audioTrackOpen
            << " capturedFrames=" << r.capturedFrames
            << " encodedFrames=" << r.encodedFrames
            << " videoRtpSent=" << r.videoRtpSent
            << " videoRtpDropped=" << r.videoRtpDropped
            << " audioRtpSent=" << r.audioRtpSent
            << " audioRtpDropped=" << r.audioRtpDropped
            << " inputEventsReceived=" << r.inputEventsReceived
            << " inputEventsApplied=" << r.inputEventsApplied << "\n";
}

}  // namespace

int run_runtime_stream_once(int seconds) {
#if REMOTE60_HAS_REALTIME_MEDIA
  const auto r = run_realtime_runtime_host(seconds);
  print_runtime_stats("[host] runtime_stream", r);
  std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
  return (r.detail == "ok") ? 0 : 13;
#else
  (void)seconds;
  std::cout << "[host] runtime_stream detail=disabled_missing_deps\n";
  std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
  return 11;
#endif
}

int run_runtime_server_forever() {
#if REMOTE60_HAS_REALTIME_MEDIA
  std::cout << "[host] runtime_server mode=persistent waiting_for_clients=1\n";
  while (true) {
    const auto r = run_realtime_runtime_host(0);
    print_runtime_stats("[host] runtime_server cycle", r);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "[host] runtime_server restart_cycle=1\n";
  }
#else
  std::cout << "[host] runtime_server detail=disabled_missing_deps\n";
  std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
  return 11;
#endif
}

}  // namespace remote60::host
