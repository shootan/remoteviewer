#pragma once

// Video NACK scheduler: when to ask the host to replay the missing chunks of the oldest stuck AU.
//
// Role:    the retransmit-request policy of a UDP video receive loop, shared by the Windows viewer
//          (VideoReceiver::run_udp) and the session controller (ClientSessionController, Android /
//          shell), so both paths negotiate and drive NACK the same way. Pure decision object: the
//          caller owns the socket and sends the packet it fills in.
// Policy:  (0.2.96, Codex-reviewed) a missing index below the assembler's highWater is a confirmed
//          hole and is asked for after a short reorder grace; an index at/above highWater is the
//          still-in-flight tail of a large frame and waits a much longer grace (NACKing it early is
//          what ignited retransmit floods). Rounds are spaced and bounded; past maxRounds the
//          keyframe path takes over. While the caller waits for an IDR only an incomplete keyframe
//          is worth repairing -- and it MUST be repairable, or a lossy 200KB IDR never completes.
// Thread:  the receive thread that owns the assembler.
// Input:   the assembler's OldestIncomplete(), the caller's keyframe-wait state, the clock.
// Output:  at most one UdpVideoNackPacket per Poll(), plus counters for the stats line.
// Callers: VideoReceiver::run_udp, ClientSessionController::VideoReceiveMain,
//          native_video_client_shared_core_test.
//
// Header-only so the Android build (its own CMake lists the shared sources) picks it up unchanged.

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "native_video_client_shared_core.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

struct VideoNackConfig {
  uint64_t gapGraceUs = 25000;    // reorder settle before a confirmed hole is asked for
  uint64_t tailGraceUs = 120000;  // a large frame should be fully sent by now
  uint64_t roundUs = 25000;       // spacing between rounds (>= a few RTTs)
  uint32_t maxRounds = 3;         // then give up -> the IDR path recovers
};

struct VideoNackStats {
  uint64_t packetsSent = 0;      // NACK datagrams handed to the caller
  uint64_t chunksRequested = 0;  // missing indices carried in them
  uint64_t roundsExhausted = 0;  // AUs that used every round without completing
};

class VideoNackScheduler {
 public:
  VideoNackScheduler() = default;
  explicit VideoNackScheduler(const VideoNackConfig& cfg) : cfg_(cfg) {}

  void Configure(const VideoNackConfig& cfg) { cfg_ = cfg; }
  const VideoNackConfig& config() const { return cfg_; }
  const VideoNackStats& stats() const { return stats_; }
  uint32_t current_seq() const { return seq_; }
  uint32_t current_round() const { return rounds_; }

  // Forget the AU being chased (a new stream generation, a decoder resync).
  void Reset() {
    seq_ = 0;
    generation_ = 0;
    firstUs_ = 0;
    lastUs_ = 0;
    rounds_ = 0;
    exhaustedCounted_ = false;
  }

  // Decide whether a NACK is due now for the assembler's oldest incomplete AU. `repairNonKey`
  // false means the caller is waiting for a keyframe: a non-key incomplete AU will be resynced by
  // the coming IDR, so only an incomplete keyframe is chased. True with `out` filled when the
  // caller should send it; the round schedule advances on that answer.
  bool Poll(const UdpH264FrameAssembler& assembler, bool repairNonKey, uint64_t nowUs,
            UdpVideoNackPacket* out) {
    constexpr uint16_t kMax = kUdpVideoNackMaxMissing;
    uint16_t missing[kMax];
    UdpH264FrameAssembler::IncompleteAuInfo info{};
    if (!assembler.OldestIncomplete(missing, kMax, &info)) {
      Reset();
      return false;
    }
    if (!repairNonKey && !info.keyFrame) {
      Reset();
      return false;
    }
    if (info.seq != seq_ || info.generation != generation_) {
      // A different AU is now the blocker: restart the round schedule for it. The graces run from
      // the AU's own first datagram when the assembler stamped it, so a poll that comes late
      // (a busy loop, a 25 ms receive timeout) does not push the tail grace out by that much.
      seq_ = info.seq;
      generation_ = info.generation;
      firstUs_ = (info.firstPacketUs != 0 && info.firstPacketUs <= nowUs) ? info.firstPacketUs : nowUs;
      lastUs_ = 0;
      rounds_ = 0;
      exhaustedCounted_ = false;
    }
    if (rounds_ >= cfg_.maxRounds) {
      if (!exhaustedCounted_) {
        ++stats_.roundsExhausted;
        exhaustedCounted_ = true;
      }
      return false;
    }
    if (lastUs_ != 0 && nowUs - lastUs_ < cfg_.roundUs) return false;
    const uint64_t age = (nowUs >= firstUs_) ? (nowUs - firstUs_) : 0;
    const bool gapEligible = age >= cfg_.gapGraceUs;
    const bool tailEligible = age >= cfg_.tailGraceUs;
    if (!gapEligible && !tailEligible) return false;
    const uint16_t have = std::min<uint16_t>(info.missingTotal, kMax);
    UdpVideoNackPacket nack{};
    uint16_t outCount = 0;
    for (uint16_t i = 0; i < have; ++i) {
      const bool isTail = missing[i] >= info.highWater;
      if (isTail ? !tailEligible : !gapEligible) continue;
      nack.missing[outCount++] = missing[i];
    }
    if (outCount == 0) return false;  // only the tail remains and its long grace has not elapsed
    nack.streamGeneration = info.generation;
    nack.seq = info.seq;
    nack.chunkCount = info.chunkCount;
    nack.missingCount = outCount;
    nack.round = static_cast<uint16_t>(rounds_);
    ++rounds_;
    lastUs_ = nowUs;
    ++stats_.packetsSent;
    stats_.chunksRequested += outCount;
    if (out) *out = nack;
    return true;
  }

 private:
  VideoNackConfig cfg_{};
  VideoNackStats stats_{};
  uint32_t seq_ = 0;
  uint64_t generation_ = 0;
  uint64_t firstUs_ = 0;
  uint64_t lastUs_ = 0;
  uint32_t rounds_ = 0;
  bool exhaustedCounted_ = false;
};

}  // namespace remote60::native_poc
