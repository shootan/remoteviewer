#pragma once

// quality r5: a closed loop on top of the encoder's own rate control.
//
// Role:    RateGovernor -- once a second, compares the payload the host actually sent with the
//          user's target and moves the encoder's quantiser ceiling (AVEncVideoMaxQP) a step at a
//          time: up while the payload stays well over the target, back down once it is under.
// Thread:  the main loop's 1 s stats tick only (not thread-safe, pure).
// Callers: host_stage_stats_h264 (the tick), host_rate_governor_test.
//
// Why a loop at all: on this PC's AMD hardware MFT the rate-control settings are accepted and read
// back as set (PeakConstrainedVBR, mean, peak, VBV) and still do not hold the mean under motion --
// measured 3-5x the target at 1080p60, with the mean changed from 1.5 to 12 Mbps barely moving the
// output. The quantiser ceiling is the lever that does move it (r5 table, quality_report.md). The
// ceiling of 32 exists for text (10db65c); it stays the floor this loop returns to, so a static
// desktop -- cheap to code, never over target -- keeps exactly the r2 sharpness.

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

struct RateGovernorConfig {
  uint32_t baseQp = 32;       // the text floor; where the loop rests
  // Never above. On this PC's AMD MFT the output is lowest at a ceiling of 40 and RISES past it
  // (1080p60 pan, Mbps: 32=20, 36=12.9, 38=11.5, 40=10.3, 42=11.0, 44=14.6, 48=20.9, 51=24.5), so
  // 40 is both the best step there and a safe one on encoders where the ceiling is monotonic.
  uint32_t capQp = 40;
  uint32_t stepUpQp = 3;
  uint32_t stepDownQp = 2;
  uint32_t overPercent = 125;   // payload above this share of the target counts as over
  uint32_t underPercent = 85;   // ...below this share, as under
  uint32_t upAfterSec = 2;      // consecutive over-seconds before a step up
  uint32_t downAfterSec = 3;    // consecutive under-seconds before a step down
  // A second under this share means the motion has stopped (a static desktop costs a few percent
  // of the target): straight back to the floor, so text is sharp again at once, not in 18 s.
  uint32_t calmPercent = 40;
};

class RateGovernor {
 public:
  explicit RateGovernor(RateGovernorConfig config = {}) : config_(config), qp_(config.baseQp) {}

  /**
   * One second of the stream: `payloadBytes` sent in it, against `targetBps`. Returns the ceiling
   * to use now; the caller applies it only when it differs from maxQp() before the call.
   */
  uint32_t OnSecond(uint64_t payloadBytes, uint32_t targetBps) {
    if (targetBps == 0) return qp_;
    const uint64_t percent = payloadBytes * 8ULL * 100ULL / targetBps;
    if (percent < config_.calmPercent) {
      qp_ = config_.baseQp;
      over_ = 0;
      under_ = 0;
      return qp_;
    }
    if (percent > config_.overPercent) {
      ++over_;
      under_ = 0;
    } else if (percent < config_.underPercent) {
      ++under_;
      over_ = 0;
    } else {
      over_ = 0;
      under_ = 0;
    }
    if (over_ >= config_.upAfterSec && qp_ < config_.capQp) {
      qp_ = std::min(config_.capQp, qp_ + config_.stepUpQp);
      over_ = 0;
    } else if (under_ >= config_.downAfterSec && qp_ > config_.baseQp) {
      qp_ = std::max(config_.baseQp, qp_ - std::min(config_.stepDownQp, qp_ - config_.baseQp));
      under_ = 0;
    }
    return qp_;
  }

  uint32_t maxQp() const { return qp_; }

  /** A new encoder or a new target: start again from the text floor. */
  void Reset() {
    qp_ = config_.baseQp;
    over_ = 0;
    under_ = 0;
  }

 private:
  RateGovernorConfig config_;
  uint32_t qp_;
  uint32_t over_ = 0;
  uint32_t under_ = 0;
};

}  // namespace remote60::native_poc
