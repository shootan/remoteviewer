#pragma once

#include <string>

namespace remote60::host {

struct RealtimeMediaProbeStats {
  bool libdatachannelOk = false;
  bool opusOk = false;
  bool peerConnectionCreated = false;
  bool dataChannelCreated = false;
  bool videoTrackCreated = false;
  bool audioTrackCreated = false;
  bool localDescriptionSet = false;
  bool loopbackConnected = false;
  uint64_t candidatesExchanged = 0;
  uint64_t videoRtpTried = 0;
  uint64_t videoRtpSent = 0;
  uint64_t audioRtpTried = 0;
  uint64_t audioRtpSent = 0;
  int opusLookahead = 0;
  std::string detail;
};

RealtimeMediaProbeStats run_realtime_media_probe();

}  // namespace remote60::host
