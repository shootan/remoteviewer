#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace remote60::native_poc {

enum class VideoTransport : uint8_t {
  Tcp = 0,
  Udp = 1,
};

inline bool parse_video_transport(const std::string& s, VideoTransport* out) {
  if (!out) return false;
  if (s == "tcp" || s == "TCP") {
    *out = VideoTransport::Tcp;
    return true;
  }
  if (s == "udp" || s == "UDP") {
    *out = VideoTransport::Udp;
    return true;
  }
  return false;
}

inline const char* video_transport_name(VideoTransport t) {
  return (t == VideoTransport::Udp) ? "udp" : "tcp";
}

inline uint32_t clamp_udp_mtu(uint32_t mtu) {
  return std::clamp<uint32_t>(mtu, 400, 1400);
}

}  // namespace remote60::native_poc
