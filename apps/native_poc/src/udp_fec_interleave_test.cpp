// Wi-Fi does not lose packets one at a time; it loses runs of them. This checks that a run of
// losses is still repairable, which is the whole reason the parity groups are interleaved.

#include "native_video_client_shared_core.hpp"
#include "poc_protocol.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using remote60::native_poc::kMagic;
using remote60::native_poc::kUdpVideoFecGroupSize;
using remote60::native_poc::UdpH264AssemblyDisposition;
using remote60::native_poc::UdpH264FrameAssembler;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;

namespace {

int gFailures = 0;

void expect(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++gFailures;
  }
}

std::vector<uint8_t> MakePayload(size_t bytes) {
  std::vector<uint8_t> payload(bytes);
  for (size_t i = 0; i < bytes; ++i) {
    payload[i] = static_cast<uint8_t>((i * 31u + (i >> 8) * 7u) & 0xffu);
  }
  return payload;
}

std::vector<uint8_t> MakeDatagram(const UdpVideoChunkHeader& header, const uint8_t* body,
                                  uint32_t bodySize) {
  std::vector<uint8_t> datagram(sizeof(UdpVideoChunkHeader) + bodySize);
  std::memcpy(datagram.data(), &header, sizeof(header));
  if (bodySize > 0) std::memcpy(datagram.data() + sizeof(header), body, bodySize);
  return datagram;
}

// Mirrors the host's layout in send_udp_chunks_impl. Returns data packets followed by parity.
std::vector<std::vector<uint8_t>> BuildFramePackets(const std::vector<uint8_t>& payload,
                                                    uint32_t stride, bool interleaved) {
  const uint32_t payloadSize = static_cast<uint32_t>(payload.size());
  const uint32_t chunkCount = (payloadSize + stride - 1u) / stride;
  const uint32_t groupCount = (chunkCount + kUdpVideoFecGroupSize - 1u) / kUdpVideoFecGroupSize;

  UdpVideoChunkHeader base{};
  base.magic = kMagic;
  base.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  base.size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  base.seq = 7;
  base.width = 1920;
  base.height = 1080;
  base.payloadSize = payloadSize;
  base.chunkCount = static_cast<uint16_t>(chunkCount);
  base.chunkStride = stride;
  base.streamGeneration = 2;
  base.captureQpcUs = 1000000;

  std::vector<std::vector<uint8_t>> packets;
  for (uint32_t index = 0; index < chunkCount; ++index) {
    const uint32_t offset = index * stride;
    const uint32_t size = std::min<uint32_t>(stride, payloadSize - offset);
    UdpVideoChunkHeader h = base;
    h.chunkIndex = static_cast<uint16_t>(index);
    h.chunkOffset = offset;
    h.chunkSize = size;
    if (index == 0) h.flags |= 0x2u;
    if (offset + size >= payloadSize) h.flags |= 0x4u;
    packets.push_back(MakeDatagram(h, payload.data() + offset, size));
  }

  std::vector<uint8_t> parity(stride, 0);
  for (uint32_t group = 0; group < groupCount; ++group) {
    std::fill(parity.begin(), parity.end(), 0);
    const uint32_t first = interleaved ? group : (group * kUdpVideoFecGroupSize);
    const uint32_t step = interleaved ? groupCount : 1u;
    const uint32_t limit =
        interleaved ? chunkCount
                    : std::min<uint32_t>(chunkCount, first + kUdpVideoFecGroupSize);
    for (uint32_t index = first; index < limit; index += step) {
      const uint32_t offset = index * stride;
      const uint32_t size = std::min<uint32_t>(stride, payloadSize - offset);
      for (uint32_t i = 0; i < size; ++i) parity[i] ^= payload[offset + i];
    }
    UdpVideoChunkHeader h = base;
    h.flags |= 0x10u;
    if (interleaved) h.flags |= 0x20u;
    h.chunkIndex = static_cast<uint16_t>(first);
    h.chunkOffset = first * stride;
    h.chunkSize = stride;
    packets.push_back(MakeDatagram(h, parity.data(), stride));
  }
  return packets;
}

struct DeliveryResult {
  bool completed = false;
  bool payloadMatches = false;
};

// Delivers every packet except a run of `burst` data chunks starting at `burstStart`.
DeliveryResult DeliverWithBurstLoss(const std::vector<uint8_t>& payload, uint32_t stride,
                                    bool interleaved, uint32_t burstStart, uint32_t burst) {
  const auto packets = BuildFramePackets(payload, stride, interleaved);
  const uint32_t payloadSize = static_cast<uint32_t>(payload.size());
  const uint32_t chunkCount = (payloadSize + stride - 1u) / stride;

  UdpH264FrameAssembler assembler;
  DeliveryResult out;
  for (size_t i = 0; i < packets.size(); ++i) {
    if (i < chunkCount && i >= burstStart && i < burstStart + burst) continue;  // dropped
    const auto step = assembler.PushDatagram(packets[i].data(), packets[i].size());
    if (step.disposition == UdpH264AssemblyDisposition::Completed) {
      out.completed = true;
      out.payloadMatches = step.frame.payload.size() == payload.size() &&
                           std::memcmp(step.frame.payload.data(), payload.data(),
                                       payload.size()) == 0;
    }
  }
  return out;
}

void TestSingleLossIsRepairedEitherWay() {
  std::printf("a single lost packet is repaired under both layouts\n");
  const auto payload = MakePayload(20000);
  for (bool interleaved : {false, true}) {
    const auto r = DeliverWithBurstLoss(payload, 1160, interleaved, 5, 1);
    expect(r.completed, std::string(interleaved ? "interleaved" : "consecutive") +
                            ": frame completed");
    expect(r.payloadMatches, std::string(interleaved ? "interleaved" : "consecutive") +
                                 ": payload byte-identical");
  }
}

void TestBurstLossNeedsInterleaving() {
  // The point of the change. Two adjacent losses land in one consecutive group, where a
  // single XOR repairs nothing; interleaved they land in different groups and both come back.
  std::printf("a burst of adjacent losses is repaired only when interleaved\n");
  const auto payload = MakePayload(20000);

  const auto consecutive = DeliverWithBurstLoss(payload, 1160, false, 5, 2);
  expect(!consecutive.completed, "consecutive grouping cannot repair a 2-packet burst");

  const auto interleaved = DeliverWithBurstLoss(payload, 1160, true, 5, 2);
  expect(interleaved.completed, "interleaved grouping repairs a 2-packet burst");
  expect(interleaved.payloadMatches, "interleaved repair is byte-identical");
}

void TestBurstUpToGroupCountIsRepaired() {
  std::printf("interleaving repairs a burst as long as there are parity groups\n");
  // 18 chunks -> 3 groups -> bursts of up to 3 are one loss per group.
  const auto payload = MakePayload(20000);
  const uint32_t stride = 1160;
  const uint32_t chunkCount = (static_cast<uint32_t>(payload.size()) + stride - 1u) / stride;
  const uint32_t groupCount = (chunkCount + kUdpVideoFecGroupSize - 1u) / kUdpVideoFecGroupSize;
  std::printf("  chunks=%u groups=%u\n", chunkCount, groupCount);

  for (uint32_t burst = 1; burst <= groupCount; ++burst) {
    const auto r = DeliverWithBurstLoss(payload, stride, true, 4, burst);
    expect(r.completed && r.payloadMatches,
           "burst of " + std::to_string(burst) + " repaired");
  }
  // One more than the group count puts two losses in one group; nothing can repair that with
  // a single parity per group, and it must fail cleanly rather than deliver corruption.
  const auto tooBig = DeliverWithBurstLoss(payload, stride, true, 4, groupCount + 1);
  expect(!tooBig.completed,
         "burst of " + std::to_string(groupCount + 1) + " is not silently mis-repaired");
}

void TestLosslessDeliveryStillWorks() {
  std::printf("an undamaged frame still assembles under both layouts\n");
  const auto payload = MakePayload(20000);
  for (bool interleaved : {false, true}) {
    const auto r = DeliverWithBurstLoss(payload, 1160, interleaved, 0, 0);
    expect(r.completed && r.payloadMatches,
           std::string(interleaved ? "interleaved" : "consecutive") + ": clean delivery");
  }
}

}  // namespace

int main() {
  TestLosslessDeliveryStillWorks();
  TestSingleLossIsRepairedEitherWay();
  TestBurstLossNeedsInterleaving();
  TestBurstUpToGroupCountIsRepaired();

  if (gFailures != 0) {
    std::printf("udp_fec_interleave_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("udp_fec_interleave_test: PASS\n");
  return 0;
}
