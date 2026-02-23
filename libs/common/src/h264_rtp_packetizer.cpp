#include "common/h264_rtp_packetizer.hpp"

#include <algorithm>
#include <string>

namespace remote60::common {
namespace {

std::vector<std::vector<uint8_t>> split_annexb_nals(const std::vector<uint8_t>& data) {
  std::vector<std::vector<uint8_t>> nals;
  size_t i = 0;
  while (i + 3 < data.size()) {
    size_t sc = std::string::npos;
    for (size_t j = i; j + 3 < data.size(); ++j) {
      if (data[j] == 0 && data[j + 1] == 0 &&
          ((data[j + 2] == 1) || (j + 3 < data.size() && data[j + 2] == 0 && data[j + 3] == 1))) {
        sc = j;
        break;
      }
    }
    if (sc == std::string::npos) break;
    size_t start = sc + ((data[sc + 2] == 1) ? 3 : 4);
    size_t next = data.size();
    for (size_t j = start; j + 3 < data.size(); ++j) {
      if (data[j] == 0 && data[j + 1] == 0 &&
          ((data[j + 2] == 1) || (j + 3 < data.size() && data[j + 2] == 0 && data[j + 3] == 1))) {
        next = j;
        break;
      }
    }
    if (next > start)
      nals.emplace_back(data.begin() + static_cast<long long>(start), data.begin() + static_cast<long long>(next));
    i = next;
  }
  return nals;
}

}  // namespace

H264RtpPacketizer::H264RtpPacketizer(uint32_t mtu) : mtu_(mtu) {}

std::vector<RtpPacket> H264RtpPacketizer::packetize_annexb(const std::vector<uint8_t>& annexb,
                                                           uint32_t timestamp90k, uint16_t& sequence) {
  std::vector<RtpPacket> out;
  auto nals = split_annexb_nals(annexb);
  if (nals.empty()) return out;

  for (size_t n = 0; n < nals.size(); ++n) {
    const auto& nal = nals[n];
    const bool isLastNal = (n + 1 == nals.size());
    if (nal.size() + 1 <= mtu_) {
      RtpPacket p;
      p.sequence = sequence++;
      p.timestamp = timestamp90k;
      p.marker = isLastNal;
      p.payload = nal;
      out.push_back(std::move(p));
      continue;
    }

    const uint8_t nalHeader = nal[0];
    const uint8_t fuIndicator = static_cast<uint8_t>((nalHeader & 0xE0) | 28);  // FU-A
    const uint8_t nalType = static_cast<uint8_t>(nalHeader & 0x1F);
    const size_t maxChunk = (mtu_ > 2) ? mtu_ - 2 : 0;
    size_t pos = 1;
    while (pos < nal.size() && maxChunk > 0) {
      const size_t remain = nal.size() - pos;
      const size_t chunk = std::min(remain, maxChunk);
      const bool start = (pos == 1);
      const bool end = (pos + chunk == nal.size());

      RtpPacket p;
      p.sequence = sequence++;
      p.timestamp = timestamp90k;
      p.marker = (isLastNal && end);
      p.payload.reserve(chunk + 2);
      p.payload.push_back(fuIndicator);
      uint8_t fuHeader = nalType;
      if (start) fuHeader |= 0x80;
      if (end) fuHeader |= 0x40;
      p.payload.push_back(fuHeader);
      p.payload.insert(p.payload.end(), nal.begin() + static_cast<long long>(pos),
                       nal.begin() + static_cast<long long>(pos + chunk));
      out.push_back(std::move(p));
      pos += chunk;
    }
  }

  return out;
}

std::vector<uint8_t> H264RtpPacketizer::build_initial_nalus_annexb(const std::vector<uint8_t>& annexb) {
  auto nals = split_annexb_nals(annexb);
  std::vector<uint8_t> out;
  auto append_sc = [&]() {
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x01);
  };

  for (const auto& nal : nals) {
    if (nal.empty()) continue;
    const uint8_t nalType = static_cast<uint8_t>(nal[0] & 0x1F);
    if (nalType == 7 || nalType == 8) {  // SPS/PPS
      append_sc();
      out.insert(out.end(), nal.begin(), nal.end());
    }
  }

  return out;
}

}  // namespace remote60::common
