#pragma once

// The shared client session's video receive policy: assembly, in-order hold, NACK repair, the
// sequence-gap rule and the stuck-head give-up -- per datagram and per tick, with no socket.
//
// Role:    what ClientSessionController::VideoReceiveMain (the Android app) does with each video
//          datagram, pulled out so a test can drive it with synthetic loss.
// Why:     quality r4 (2026-09-27). The phone asked for a keyframe 46 times in 5 minutes, every
//          one 61-172 ms after a host keyframe, and the user saw the video stop each time. A
//          1080p keyframe is ~120 KB -- 125 chunks sent in ~11 ms -- and on the phone's Wi-Fi some
//          of them do not arrive. The session delivered AUs the moment they completed, so the next
//          frame, 33 ms later, completed first and threw the incomplete keyframe away: no NACK had
//          time to repair it (the host served 0 NACK bytes), a new IDR was requested, and the
//          picture waited for it. The Windows viewer has had an in-order hold for exactly this
//          since the NACK work -- the assembler header even said "the Android path is unchanged".
//          This is that policy for the shared session: with a host that negotiated NACK, a
//          completed AU waits up to holdUs behind an incomplete older one while a retransmit may
//          still repair it.
// Contract: with nackEnabled false (an old host) or holdUs 0 it behaves exactly as the session
//          did before -- immediate delivery, gaps asked for (except behind a complete keyframe,
//          respond_to_sequence_gap).

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>

#include "native_video_client_shared_core.hpp"
#include "poc_protocol.hpp"
#include "udp_video_nack.hpp"

namespace remote60::native_poc {

struct SessionVideoPipelineConfig {
  bool nackEnabled = false;       // the host acknowledged kUdpFeatureVideoNack
  uint64_t holdUs = 0;            // in-order hold (0 = legacy immediate delivery)
  size_t maxConcurrent = 8;       // assemblies held (~120 ms at 60 fps)
  // Stuck-head give-up (the Windows viewer's A04 rule, without an RTT source here): a reply
  // allowance after the last NACK, and a failure budget for an AU that keeps making progress.
  uint64_t replyAllowanceUs = 50000;
  uint64_t giveUpHardCapUs = 5000000;
  // NACK scheduler tuning (bitrate-hard-cap r2): lets the progress-based vs legacy age-based tail be
  // selected (tailAgeBasedLegacy) and the graces set. Default = the product's default scheduler.
  VideoNackConfig nackConfig{};
};

struct SessionVideoPipelineStats {
  uint64_t delivered = 0;
  uint64_t keyframeRequests = 0;
  uint64_t discontinuities = 0;
  uint64_t gapsBehindCompleteKey = 0;  // gaps the IDR in hand resynced (no request)
  uint64_t nacksSent = 0;
  uint64_t stuckHeadGiveUps = 0;
  uint64_t fecRecoveredChunks = 0;
  uint64_t malformed = 0;
  uint64_t dropped = 0;
};

class SessionVideoPipeline {
 public:
  struct Callbacks {
    std::function<void(UdpH264AssembledFrame&&)> deliver;  // a complete AU, in sequence order
    std::function<void()> requestKeyframe;
    std::function<void()> discontinuity;                   // the reference chain broke
    std::function<void(const UdpVideoNackPacket&)> sendNack;
  };

  SessionVideoPipeline(const SessionVideoPipelineConfig& cfg, Callbacks cb)
      : cfg_(cfg), cb_(std::move(cb)) {
    if (HoldEnabled()) assembler_.ConfigureInOrderHold(cfg_.holdUs, cfg_.maxConcurrent);
    nack_.Configure(cfg_.nackConfig);
  }

  bool HoldEnabled() const { return cfg_.nackEnabled && cfg_.holdUs > 0; }
  bool WaitingForKeyframe() const { return waitForKeyframe_; }
  const SessionVideoPipelineStats& stats() const { return stats_; }
  const UdpH264FrameAssembler& assembler() const { return assembler_; }

  /** One received video datagram (control datagrams are the caller's). */
  void OnDatagram(const uint8_t* data, size_t len, uint64_t nowUs) {
    UdpH264AssemblyStepResult r = assembler_.PushDatagram(data, len, nowUs);
    if (r.fecRecovered) stats_.fecRecoveredChunks += r.fecRecoveredChunks;
    // Legacy delivery reports a gap on the datagram that revealed it; with the hold the gap
    // belongs to the delivery that carries it (Drain), so a hold still running is not loss.
    if (r.droppedPreviousIncomplete && !HoldEnabled()) Gap(r);
    if (r.disposition == UdpH264AssemblyDisposition::Malformed) {
      ++stats_.malformed;
      Break();
      return;
    }
    if (r.disposition == UdpH264AssemblyDisposition::Dropped) {
      ++stats_.dropped;
      Break();
      return;
    }
    if (r.disposition == UdpH264AssemblyDisposition::Completed) DeliverCompleted(r);
    Drain(nowUs);
    MaybeNack(nowUs);
  }

  /** Clock-driven work: the hold expires on the clock, NACK rounds are spaced on it. */
  void OnTick(uint64_t nowUs) {
    Drain(nowUs);
    MaybeNack(nowUs);
    GiveUpStuckHead(nowUs);
  }

 private:
  void Break() {
    ++stats_.discontinuities;
    if (!waitForKeyframe_ && cb_.discontinuity) cb_.discontinuity();
    waitForKeyframe_ = true;
    Request();
  }
  void Request() {
    ++stats_.keyframeRequests;
    if (cb_.requestKeyframe) cb_.requestKeyframe();
  }
  void Gap(const UdpH264AssemblyStepResult& r) {
    const SequenceGapResponse gap = respond_to_sequence_gap(r);
    if (!gap.requestKeyframe && !gap.discontinuity) {
      ++stats_.gapsBehindCompleteKey;
      return;
    }
    if (gap.discontinuity) {
      ++stats_.discontinuities;
      if (!waitForKeyframe_ && cb_.discontinuity) cb_.discontinuity();
      waitForKeyframe_ = true;
    }
    if (gap.requestKeyframe) Request();
  }
  void DeliverCompleted(UdpH264AssemblyStepResult& r) {
    const bool key = (r.frame.header.flags & 1u) != 0;
    if (waitForKeyframe_ && !key) {
      Request();
      return;
    }
    if (key) waitForKeyframe_ = false;
    ++stats_.delivered;
    if (cb_.deliver) cb_.deliver(std::move(r.frame));
  }
  void Drain(uint64_t nowUs) {
    if (!HoldEnabled()) return;
    UdpH264AssemblyStepResult ready{};
    while (assembler_.PopDelivery(nowUs, !waitForKeyframe_, &ready)) {
      if (ready.droppedPreviousIncomplete) Gap(ready);
      if (ready.disposition == UdpH264AssemblyDisposition::Completed) DeliverCompleted(ready);
      ready = UdpH264AssemblyStepResult{};
    }
  }
  void MaybeNack(uint64_t nowUs) {
    if (!cfg_.nackEnabled) return;
    UdpVideoNackPacket pkt{};
    if (nack_.Poll(assembler_, !waitForKeyframe_, nowUs, &pkt)) {
      ++stats_.nacksSent;
      if (cb_.sendNack) cb_.sendNack(pkt);
    }
  }
  // The Windows viewer's A04 rule for an incomplete head with nothing complete behind it (a
  // still screen: PopDelivery has nothing to release, so without this the picture would sit on
  // the last frame with an exhausted NACK and no keyframe asked for).
  void GiveUpStuckHead(uint64_t nowUs) {
    if (!HoldEnabled() || assembler_.AnyComplete()) return;
    uint16_t missing[kUdpVideoNackMaxMissing];
    UdpH264FrameAssembler::IncompleteAuInfo info{};
    if (!assembler_.OldestIncomplete(missing, kUdpVideoNackMaxMissing, &info) || info.firstPacketUs == 0) return;
    const auto& ncfg = nack_.config();
    const uint64_t allowanceUs = cfg_.replyAllowanceUs;
    const uint64_t terminalMinUs =
        ncfg.tailGraceUs + static_cast<uint64_t>(ncfg.maxRounds) * ncfg.roundUs + allowanceUs;
    const uint64_t hardCapUs = std::max<uint64_t>(cfg_.giveUpHardCapUs, terminalMinUs);
    const uint64_t ageUs = nowUs >= info.firstPacketUs ? nowUs - info.firstPacketUs : 0;
    const uint64_t sinceProgressUs =
        (info.lastProgressUs != 0 && nowUs >= info.lastProgressUs) ? nowUs - info.lastProgressUs : ageUs;
    const uint16_t have = std::min<uint16_t>(info.missingTotal, kUdpVideoNackMaxMissing);
    const bool hasTail = have > 0 && missing[have - 1] >= info.highWater;
    const bool chased = cfg_.nackEnabled && nack_.current_seq() == info.seq &&
                        nack_.current_generation() == info.generation;
    const bool noProgress = sinceProgressUs >= allowanceUs;
    const bool oldEnough = ageUs >= terminalMinUs;
    bool giveUp = false;
    if (ageUs >= hardCapUs) {
      giveUp = true;
    } else if (waitForKeyframe_ && !info.keyFrame) {
      giveUp = noProgress;
    } else if (chased) {
      const bool spent = nack_.spent_for(hasTail);
      const bool replyOver = nack_.last_sent_us() != 0 && nowUs >= nack_.last_sent_us() + allowanceUs;
      giveUp = spent && replyOver && noProgress && oldEnough;
    } else {
      giveUp = noProgress && oldEnough;
    }
    if (giveUp && assembler_.GiveUpIncomplete(info.generation, info.seq, nowUs)) {
      if (chased) nack_.Reset();
      ++stats_.stuckHeadGiveUps;
      Break();
    }
  }

  SessionVideoPipelineConfig cfg_;
  Callbacks cb_;
  UdpH264FrameAssembler assembler_;
  VideoNackScheduler nack_;
  SessionVideoPipelineStats stats_;
  bool waitForKeyframe_ = true;
};

}  // namespace remote60::native_poc
