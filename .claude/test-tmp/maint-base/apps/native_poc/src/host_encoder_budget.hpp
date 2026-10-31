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
// Callers: EncoderState::ApplyTarget / startup (through EncoderState::EncoderBudgetFor /
//          PayloadBudgetFor), host_encoder_budget_test.
//
// The wire cost of a payload byte comes from the real packetization (host_net_io.hpp
// udp_chunk_geometry): an AU of P bytes is cut into ceil(P / (mtu - header)) data datagrams, one XOR
// parity datagram of one full chunk stride per kUdpVideoFecGroupSize data datagrams (a frame that
// fits one datagram gets a parity of its own size), and every datagram carries the UdpVideoChunkHeader
// (88 bytes, packed) plus 28 bytes of UDP/IP. Two models of that cost live here:
//   - the large-AU asymptote, wire ~= P * (1 + 1/groupSize) * (1 + (header + 28) / (mtu - header)),
//     used when no frame rate is known (fps 0) -- the factors the diagnostic line prints;
//   - the per-AU model at a frame rate: an encoder holding a payload mean P produces P/8/fps bytes per
//     frame; that AU's exact chunk / parity / header cost is paid `fps` times a second, and P is the
//     largest mean whose wire cost fits (bisection; the cost is monotone in P). E is P minus the
//     overshoot slack. This is what the product uses: at 1.5 Mbps / 60 fps an AU of ~2 KB is two data
//     chunks plus a 1112-byte parity plus three headers -- 115% of R under the asymptote (Codex, r2),
//     inside R here. It prices SAME-SIZE AUs at a steady cadence with the default tight single-chunk
//     parity: a real stream's IDR/P distribution, ceil boundaries and cadence differ, and the padded
//     parity layout (REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE=0) costs more for small AUs than this
//     model says -- the margins below and the limiter/gate remain necessary, not optional.
// Two further allowances, each named and set once, and both NOMINAL:
//   nackReservePercent       -- a slice of R the model does not give to video, so that NACK replay has
//                               room. It is a margin in this arithmetic, not a reservation anything
//                               schedules: nothing stops a loss burst from using more.
//   encoderOvershootPercent  -- an encoder's mean is a target, not a bound. Measured here: the AMD MFT
//                               produces several times its mean under motion for seconds; NVIDIA
//                               pre-types vbr_peak sat at x1.05-x1.13 (Home PC, 2026-10-11). This is
//                               slack for that in E, NOT a guarantee that any encoder stays within it,
//                               and NOT a reason the cap's input gate could be removed: when an encoder
//                               exceeds E the gate still skips, by design.
// At the shipped geometry (mtu 1200, 88-byte header, groups of 8, 5% + 12%) the asymptote gives
// E ~= 0.68 R; the per-AU model gives less at low rates / high frame rates. Both are computed
// consequences of those inputs, not constants: change the MTU, the group or the frame rate and the
// budget follows. Nothing here reads R from the encoder's own figures, and nothing else in the host
// subtracts headroom a second time (C1: one conversion point).

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

struct WireGeometry {
  uint32_t mtuBytes = 1200;          // clamp_udp_mtu(args.udpMtu): what one datagram may carry incl. the chunk header
  uint32_t chunkHeaderBytes = 88;    // sizeof(UdpVideoChunkHeader) (pack(1)); the product fills it from sizeof
  uint32_t fecGroupSize = 8;         // kUdpVideoFecGroupSize: one parity datagram per this many data datagrams
  uint32_t ipUdpHeaderBytes = 28;    // what the wire cap charges on top of every datagram
};

struct EncoderBudgetPolicy {
  uint32_t nackReservePercent = 5;       // nominal share of R kept out of the video model (see above)
  uint32_t encoderOvershootPercent = 12; // nominal share of E the encoder may exceed its mean by (see above)
};

// The parts of the conversion, for the diagnostic line: each factor x1000.
struct EncoderBudgetBreakdown {
  uint32_t fecFactorX1000 = 1000;      // asymptote: (1 + 1/groupSize)
  uint32_t headerFactorX1000 = 1000;   // asymptote: (1 + (header + 28) / (mtu - header))
  uint32_t wireAfterReserveBps = 0;    // R minus the nominal NACK margin: what payload+parity+headers may use
  uint32_t payloadWireBps = 0;         // the payload the wire can carry at that budget (asymptote, or per-AU at fps)
  uint32_t encoderBps = 0;             // ... minus the overshoot slack: the mean to hand the encoder (E)
  uint32_t auBytesAtFps = 0;           // per-AU model only: the AU the model priced = payloadWireBps/8/fps (the payload budget
                                       // per frame, BEFORE the overshoot slack is taken off to make E)
  uint32_t wireAtFpsBps = 0;           // per-AU model only: the wire cost of payloadWireBps at fps (<= wireAfterReserveBps)
};

/** The exact wire bytes of one AU of `payloadBytes`, as the product chunks it (tight single-chunk stride). */
inline uint64_t wire_bytes_per_au(uint64_t payloadBytes, const WireGeometry& g) {
  if (payloadBytes == 0) return 0;
  const uint32_t maxChunk = g.mtuBytes > g.chunkHeaderBytes ? g.mtuBytes - g.chunkHeaderBytes : 0;
  const uint64_t perDatagram = static_cast<uint64_t>(g.chunkHeaderBytes) + g.ipUdpHeaderBytes;
  if (maxChunk == 0) return payloadBytes + perDatagram;  // degenerate geometry: one datagram, no parity model
  const uint64_t stride = payloadBytes <= maxChunk ? payloadBytes : maxChunk;
  const uint64_t chunks = (payloadBytes + stride - 1) / stride;
  const uint64_t groups = g.fecGroupSize > 0 ? (chunks + g.fecGroupSize - 1) / g.fecGroupSize : 0;
  return payloadBytes + groups * stride + (chunks + groups) * perDatagram;
}

/** The wire bps of an encoder holding mean `payloadBps` at `fps` (per-AU model). */
inline uint64_t wire_bps_at_fps(uint64_t payloadBps, uint32_t fps, const WireGeometry& g) {
  if (fps == 0) return 0;
  const uint64_t auBytes = payloadBps / 8ULL / fps;
  return wire_bytes_per_au(auBytes, g) * 8ULL * fps;
}

/**
 * E for a wire budget R. Monotone in R, strictly below R for any non-degenerate geometry, never
 * below 100 kbps (the encoder's own floor) when R is at least that. fps 0 selects the large-AU
 * asymptote; fps > 0 the per-AU model at that frame rate. A degenerate geometry (mtu not above the
 * header, a zero group) is treated as "no FEC / no header cost" in the asymptote rather than dividing
 * by zero.
 */
inline EncoderBudgetBreakdown encoder_budget_for(uint32_t wireBps, const WireGeometry& g,
                                                 const EncoderBudgetPolicy& p, uint32_t fps = 0) {
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
  uint64_t payload = afterReserve * 1000ULL * 1000ULL / (fecX1000 * headerX1000);
  if (fps > 0 && maxChunk > 0) {
    // The largest payload mean whose per-AU wire cost at this frame rate fits afterReserve. The cost
    // is monotone in the mean (more bytes never cost fewer datagrams), so bisection converges; the
    // asymptote above is an upper bound and the search starts from it.
    uint64_t lo = 0, hi = payload;
    for (int i = 0; i < 48 && lo < hi; ++i) {
      const uint64_t mid = (lo + hi + 1) / 2;
      if (wire_bps_at_fps(mid, fps, g) <= afterReserve) lo = mid; else hi = mid - 1;
    }
    payload = lo;
    b.auBytesAtFps = static_cast<uint32_t>(std::min<uint64_t>(payload / 8ULL / fps, 0xFFFFFFFFULL));
    b.wireAtFpsBps = static_cast<uint32_t>(std::min<uint64_t>(wire_bps_at_fps(payload, fps, g), 0xFFFFFFFFULL));
  }
  const uint64_t encoder = payload * 100ULL / (100ULL + overshoot);
  b.wireAfterReserveBps = static_cast<uint32_t>(std::min<uint64_t>(afterReserve, 0xFFFFFFFFULL));
  b.payloadWireBps = static_cast<uint32_t>(std::min<uint64_t>(payload, 0xFFFFFFFFULL));
  b.encoderBps = static_cast<uint32_t>(std::min<uint64_t>(encoder, 0xFFFFFFFFULL));
  if (wireBps >= 100000u) b.encoderBps = std::max<uint32_t>(100000u, b.encoderBps);
  return b;
}

inline uint32_t encoder_payload_budget_bps(uint32_t wireBps, const WireGeometry& g = WireGeometry{},
                                           const EncoderBudgetPolicy& p = EncoderBudgetPolicy{}, uint32_t fps = 0) {
  return encoder_budget_for(wireBps, g, p, fps).encoderBps;
}

}  // namespace remote60::native_poc
