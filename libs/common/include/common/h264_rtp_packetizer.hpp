#pragma once

#include <cstdint>
#include <vector>

namespace remote60::common {

struct RtpPacket {
  uint16_t sequence = 0;
  uint32_t timestamp = 0;
  bool marker = false;
  std::vector<uint8_t> payload;
};

class H264RtpPacketizer {
 public:
  explicit H264RtpPacketizer(uint32_t mtu = 1200);
  std::vector<RtpPacket> packetize_annexb(const std::vector<uint8_t>& annexb, uint32_t timestamp90k,
                                          uint16_t& sequence);
  std::vector<uint8_t> build_initial_nalus_annexb(const std::vector<uint8_t>& annexb);

 private:
  uint32_t mtu_;
};

}  // namespace remote60::common
