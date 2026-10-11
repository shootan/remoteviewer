#pragma once

// encoder-rc-budget C1: the encoder's payload budget E, derived from the wire budget R.
//
// Role:    Two numbers that used to be one. R is what the user (and ABR) allow on the wire for this
//          stream -- video payload, FEC parity, NACK replay and the UDP/IP header of every datagram,
//          judged by the hard wire cap in every 250 ms / 1 s / 2 s window. E is what the encoder is
//          told to produce (AVEncCommonMeanBitRate). Handing the encoder R as its mean was the defect:
//          even an encoder that holds its mean exactly produces R of payload, the wire then carries
//          R x (1 + parity + headers), and the cap serialises the excess -- a large IDR or a motion
//          burst waits behind the limiter and the picture stalls (2026-10-11).
// Thread:  pure; called from the main loop where the encoder target is decided.
// Callers: EncoderState::ApplyTarget / startup (through EncoderState::EncoderBudgetFor),
//          host_encoder_budget_test.
//
// What the wire adds to a payload byte, from the real packetization (host_net_io.hpp
// udp_chunk_geometry): an AU of P bytes is cut into ceil(P / (mtu - header)) data datagrams, one XOR
// parity datagram of one full chunk stride per kUdpVideoFecGroupSize data datagrams, and every
// datagram carries the UdpVideoChunkHeader plus 28 bytes of UDP/IP. For an AU of many chunks that is
//   wire ~= P * (1 + 1/groupSize) * (1 + (header + 28) / (mtu - header))
// -- the asymptote this budget uses. Small AUs (one or two chunks) pay proportionally more (the
// single-chunk parity is a full replica), but they are also a small share of the bytes; the margin
// below absorbs that, it is not modelled per frame. Two further allowances, each named and set once:
//   nackReservePercent       -- replay of lost chunks is wire the cap charges too; a slice of R is
//                               kept for it rather than letting a loss burst stall video;
//   encoderOvershootPercent  -- an encoder's mean is a target, not a bound. Measured here: the AMD
//                               MFT holds roughly its mean over seconds but a text scene change is
//                               several times it for one frame; NVIDIA pre-types vbr_peak sat at
//                               x1.05-x1.13 of its mean (Home PC, 2026-10-11). This is the slack
//                               for that, separate from the FEC/header cost which is exact.
// The result for the shipped geometry (mtu 1200, 92-byte packed chunk header, groups of 8, 5% + 12%)
// is E ~= 0.68 R -- the contract's ~0.70 seed. It is a computed consequence of those inputs, not a constant:
// change the MTU or the FEC group and the budget follows. Nothing here reads R from the encoder's
// own figures, and nothing else in the host subtracts headroom a second time (C1: one conversion
// point).

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

struct WireGeometry {
  uint32_t mtuBytes = 1200;          // clamp_udp_mtu(args.udpMtu): what one datagram may carry incl. the chunk header
  uint32_t chunkHeaderBytes = 92;    // sizeof(UdpVideoChunkHeader) (pack(1)); the product fills it from sizeof
  uint32_t fecGroupSize = 8;         // kUdpVideoFecGroupSize: one parity datagram per this many data datagrams
  uint32_t ipUdpHeaderBytes = 28;    // what the wire cap charges on top of every datagram
};

struct EncoderBudgetPolicy {
  uint32_t nackReservePercent = 5;       // share of R kept for NACK replay
  uint32_t encoderOvershootPercent = 12; // share of E the encoder may exceed its mean by, on average
};

// The parts of the conversion, for the diagnostic line: each factor x1000.
struct EncoderBudgetBreakdown {
  uint32_t fecFactorX1000 = 1000;      // (1 + 1/groupSize)
  uint32_t headerFactorX1000 = 1000;   // (1 + (header + 28) / (mtu - header))
  uint32_t wireAfterReserveBps = 0;    // R minus the NACK reserve: what payload+parity+headers may use
  uint32_t payloadWireBps = 0;         // ... divided by the fec and header factors: payload bytes the wire can carry
  uint32_t encoderBps = 0;             // ... minus the overshoot slack: the mean to hand the encoder (E)
};

/**
 * E for a wire budget R. Monotone in R, strictly below R for any non-degenerate geometry, never
 * below 100 kbps (the encoder's own floor) when R is at least that. A degenerate geometry (mtu not
 * above the header, a zero group) is treated as "no FEC / no header cost" rather than dividing by
 * zero -- the caller has bigger problems then, and E == R minus the two allowances is still a usable
 * answer.
 */
inline EncoderBudgetBreakdown encoder_budget_for(uint32_t wireBps, const WireGeometry& g,
                                                 const EncoderBudgetPolicy& p) {
  EncoderBudgetBreakdown b;
  const uint64_t fecX1000 = g.fecGroupSize > 0 ? 1000ULL + 1000ULL / g.fecGroupSize : 1000ULL;
  const uint32_t maxChunk = g.mtuBytes > g.chunkHeaderBytes ? g.mtuBytes - g.chunkHeaderBytes : 0;
  const uint64_t headerX1000 =
      maxChunk > 0 ? 1000ULL + (1000ULL * (g.chunkHeaderBytes + g.ipUdpHeaderBytes)) / maxChunk : 1000ULL;
  b.fecFactorX1000 = static_cast<uint32_t>(fecX1000);
  b.headerFactorX1000 = static_cast<uint32_t>(headerX1000);
  const uint32_t reserve = std::min<uint32_t>(p.nackReservePercent, 90);
  const uint32_t overshoot = std::min<uint32_t>(p.encoderOvershootPercent, 90);
  const uint64_t afterReserve = static_cast<uint64_t>(wireBps) * (100ULL - reserve) / 100ULL;
  const uint64_t payload = afterReserve * 1000ULL * 1000ULL / (fecX1000 * headerX1000);
  const uint64_t encoder = payload * 100ULL / (100ULL + overshoot);
  b.wireAfterReserveBps = static_cast<uint32_t>(std::min<uint64_t>(afterReserve, 0xFFFFFFFFULL));
  b.payloadWireBps = static_cast<uint32_t>(std::min<uint64_t>(payload, 0xFFFFFFFFULL));
  b.encoderBps = static_cast<uint32_t>(std::min<uint64_t>(encoder, 0xFFFFFFFFULL));
  if (wireBps >= 100000u) b.encoderBps = std::max<uint32_t>(100000u, b.encoderBps);
  return b;
}

inline uint32_t encoder_payload_budget_bps(uint32_t wireBps, const WireGeometry& g = WireGeometry{},
                                           const EncoderBudgetPolicy& p = EncoderBudgetPolicy{}) {
  return encoder_budget_for(wireBps, g, p).encoderBps;
}

}  // namespace remote60::native_poc
