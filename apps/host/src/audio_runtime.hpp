#pragma once

#include <cstdint>
#include <string>

namespace remote60::host {

struct AudioProbeStats {
  bool deviceOk = false;
  bool clientOk = false;
  bool loopbackStarted = false;
  uint32_t sampleRate = 0;
  uint32_t channels = 0;
  uint64_t packets = 0;
  uint64_t pcmFrames = 0;
  uint64_t opusFrames20ms = 0;
  uint64_t opusBytes = 0;
  uint64_t rtpPackets = 0;
  uint16_t firstSeq = 0;
  uint16_t lastSeq = 0;
  uint32_t firstTimestamp = 0;
  uint32_t lastTimestamp = 0;
  double elapsedSec = 0.0;
  std::string detail;
};

AudioProbeStats run_audio_loopback_probe_seconds(int seconds);

}  // namespace remote60::host
