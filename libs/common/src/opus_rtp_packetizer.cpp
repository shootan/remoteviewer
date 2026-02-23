#include "common/opus_rtp_packetizer.hpp"

namespace remote60::common {

OpusRtpPacket OpusRtpPacketizer::packetize_20ms_frame(const std::vector<uint8_t>& opusPayload,
                                                      uint16_t& sequence, uint32_t& timestamp48k) const {
  OpusRtpPacket p;
  p.sequence = sequence++;
  p.timestamp = timestamp48k;
  p.marker = false;
  p.payload = opusPayload;
  timestamp48k += 960;  // 20ms @ 48kHz
  return p;
}

}  // namespace remote60::common
