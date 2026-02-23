#pragma once

#include <cstdint>
#include <vector>

namespace remote60::common {

struct OpusRtpPacket {
  uint16_t sequence = 0;
  uint32_t timestamp = 0;
  bool marker = false;
  std::vector<uint8_t> payload;
};

class OpusRtpPacketizer {
 public:
  OpusRtpPacket packetize_20ms_frame(const std::vector<uint8_t>& opusPayload, uint16_t& sequence,
                                     uint32_t& timestamp48k) const;
};

}  // namespace remote60::common
