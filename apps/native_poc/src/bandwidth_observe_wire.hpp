#pragma once

// C0 stage 1 -- the bandwidth observation on the wire: building it (viewer), judging it (host),
// and the negotiation rule both sides share. OBSERVATION ONLY; see viewer_bwe.hpp.

#include <algorithm>
#include <cstdint>

#include "poc_protocol.hpp"
#include "viewer_bwe.hpp"

namespace remote60::native_poc {

/**
 * What the estimator takes from a received video chunk. One function, used by the viewer's
 * receive loop and by the observation e2e, so the harness feeds exactly what the product feeds.
 */
inline BweChunkSample bwe_chunk_from_header(const UdpVideoChunkHeader& h, uint32_t datagramBytes,
                                            uint64_t recvUs) {
  BweChunkSample s;
  s.streamGeneration = h.streamGeneration;
  s.seq = h.seq;
  s.chunkIndex = h.chunkIndex;
  s.flags = h.flags;
  s.sendQpcUs = h.sendQpcUs;
  s.recvUs = recvUs;
  s.wireBytes = datagramBytes;
  s.payloadBytes = h.chunkSize;
  return s;
}

/** The viewer sends only when it asked AND the host's HelloAck carries the bit. */
inline bool bandwidth_observe_negotiated(bool requested, uint32_t ackFeatures) {
  return requested && (ackFeatures & kUdpFeatureBandwidthObserve) != 0;
}

/** The host logs only for a client whose accepted Hello asked for it. */
inline bool host_bandwidth_observe_requested(uint32_t helloFeatures) {
  return (helloFeatures & kUdpFeatureBandwidthObserve) != 0;
}

inline ControlClientBandwidthMessage make_client_bandwidth_message(const BweReport& r, uint32_t seq,
                                                                   uint64_t clientNowUs) {
  ControlClientBandwidthMessage m{};
  m.header.magic = kMagic;
  m.header.type = static_cast<uint16_t>(MessageType::ControlClientBandwidth);
  m.header.size = static_cast<uint16_t>(sizeof(m));
  m.seq = seq;
  m.flags = (r.appLimited ? kBandwidthFlagAppLimited : 0u) | (r.staticHold ? kBandwidthFlagStaticHold : 0u);
  m.usage = static_cast<uint8_t>(r.usage);
  m.rateState = static_cast<uint8_t>(r.rateState);
  m.lossPm = static_cast<uint16_t>(std::min<uint32_t>(r.lossPm, 1000));
  m.bweBps = r.bweBps;
  m.goodputUniqueBps = r.goodputUniqueBps;
  m.wireLoadBps = r.wireLoadBps;
  m.delayGradientUs = r.delayGradientUs;
  m.thresholdUs = r.thresholdUs;
  m.delaySamples = static_cast<uint16_t>(std::min<uint32_t>(r.delaySamples, 0xFFFFu));
  m.streamGeneration = r.streamGeneration;
  m.clientSendQpcUs = clientNowUs;
  return m;
}

/** Everything the host checks before it believes a word of it. Size is checked by the caller. */
inline bool client_bandwidth_message_valid(const ControlClientBandwidthMessage& m) {
  if (m.header.magic != kMagic) return false;
  if (m.header.type != static_cast<uint16_t>(MessageType::ControlClientBandwidth)) return false;
  if (m.header.size != sizeof(ControlClientBandwidthMessage)) return false;
  if (m.usage > 2 || m.rateState > 2 || m.lossPm > 1000) return false;
  if ((m.flags & ~(kBandwidthFlagAppLimited | kBandwidthFlagStaticHold)) != 0) return false;
  if (m.reserved != 0) return false;
  return true;
}

}  // namespace remote60::native_poc
