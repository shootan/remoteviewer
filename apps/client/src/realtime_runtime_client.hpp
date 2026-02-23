#pragma once

#include <cstdint>
#include <string>

namespace remote60::client {

struct RuntimeViewStats {
  bool webrtcReady = false;
  bool connected = false;
  bool gotVideoTrack = false;
  bool gotAudioTrack = false;
  bool inputChannelOpen = false;
  uint64_t videoRtpPackets = 0;
  uint64_t videoFrames = 0;
  uint64_t audioRtpPackets = 0;
  uint64_t inputEventsSent = 0;
  uint64_t inputAcks = 0;
  std::string detail;
};

RuntimeViewStats run_runtime_view_client(int seconds);

}  // namespace remote60::client
