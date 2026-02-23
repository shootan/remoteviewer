#pragma once

#include <string>

namespace remote60::host {

struct RealtimeRuntimeStats {
  bool webrtcReady = false;
  bool connected = false;
  bool captureReady = false;
  bool encoderReady = false;
  bool videoTrackOpen = false;
  bool audioTrackOpen = false;
  uint64_t capturedFrames = 0;
  uint64_t encodedFrames = 0;
  uint64_t videoRtpSent = 0;
  uint64_t videoRtpDropped = 0;
  uint64_t audioRtpSent = 0;
  uint64_t audioRtpDropped = 0;
  uint64_t inputEventsReceived = 0;
  uint64_t inputEventsApplied = 0;
  std::string detail;
};

RealtimeRuntimeStats run_realtime_runtime_host(int seconds);

}  // namespace remote60::host
