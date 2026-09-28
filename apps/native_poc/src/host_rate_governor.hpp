#pragma once

// quality r5/r6: a closed loop on top of the encoder's own rate control.
//
// Role:    RateGovernor -- once a second, compares the payload the host actually sent with the
//          user's target and moves two levers a step at a time:
//            1. the encoder's quantiser ceiling (AVEncVideoMaxQP), 32 -> 35 -> 38 -> 40;
//            2. (r6) with the ceiling at its cap and the payload still over, the frame rate,
//               user fps -> 3/4 -> 1/2 (60 -> 45 -> 30).
//          Unwinding is the reverse: the frame rate comes back first, then the ceiling.
// Thread:  the main loop's 1 s stats tick only (not thread-safe, pure).
// Callers: host_stage_stats_h264 (the tick), host_rate_governor_test.
//
// Why a loop at all: on this PC's AMD hardware MFT the rate-control settings are accepted and read
// back as set (PeakConstrainedVBR, mean, peak, VBV) and still do not hold the mean under motion --
// measured 3-5x the target at 1080p60, with the mean changed from 1.5 to 12 Mbps barely moving the
// output (r5/r6 tables, quality_report.md). The quantiser ceiling moves it a little; the frame rate
// moves it in proportion (60 fps costs twice 30 fps, frame size being set by quantiser and content).
// The ceiling of 32 exists for text (10db65c); it stays the floor this loop returns to, and a
// static desktop -- cheap to code, never over target -- keeps exactly the r2 sharpness and the
// user's frame rate.

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
  // of the target): straight back to the floor and the full rate, so text is sharp again at once.
  uint32_t calmPercent = 40;
  // r6, the frame-rate stage. Only once the ceiling is at its cap.
  uint32_t fpsDownAfterSec = 3;   // consecutive over-seconds at the cap before a frame-rate step down
  uint32_t fpsUpPercent = 90;     // the payload predicted at the next rate up must be under this...
  uint32_t fpsUpAfterSec = 5;     // ...for this many consecutive seconds
  uint32_t minFps = 15;
};

struct RateDecision {
  uint32_t maxQp = 0;
  uint32_t fps = 0;     // the frame rate to encode at (<= the user's)
};

class RateGovernor {
 public:
  explicit RateGovernor(RateGovernorConfig config = {}) : config_(config), qp_(config.baseQp) {}

  /** The frame rate of a step: the user's, then 3/4 of it, then 1/2 -- never below minFps. */
  uint32_t FpsForStep(uint32_t userFps, uint32_t step) const {
    if (step == 0 || userFps == 0) return userFps;
    const uint32_t f = step == 1 ? userFps * 3 / 4 : userFps / 2;
    return std::min(userFps, std::max(config_.minFps, f));
  }

  /**
   * One second of the stream: `payloadBytes` sent in it, against `targetBps`, at the user's frame
   * rate `userFps` (the ceiling this loop never exceeds). Returns what to use now; the caller
   * applies whatever differs from the previous decision.
   */
  RateDecision OnSecond(uint64_t payloadBytes, uint32_t targetBps, uint32_t userFps) {
    if (targetBps == 0) return Current(userFps);
    const uint64_t percent = payloadBytes * 8ULL * 100ULL / targetBps;
    if (percent < config_.calmPercent) {
      qp_ = config_.baseQp;
      fpsStep_ = 0;
      over_ = under_ = overAtCap_ = fpsOk_ = 0;
      return Current(userFps);
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

    // Down the ladder: the ceiling first, then the frame rate.
    if (qp_ >= config_.capQp && percent > config_.overPercent) {
      ++overAtCap_;
    } else {
      overAtCap_ = 0;
    }
    if (over_ >= config_.upAfterSec && qp_ < config_.capQp) {
      qp_ = std::min(config_.capQp, qp_ + config_.stepUpQp);
      over_ = 0;
      return Current(userFps);
    }
    if (overAtCap_ >= config_.fpsDownAfterSec && fpsStep_ < 2 &&
        FpsForStep(userFps, fpsStep_ + 1) < FpsForStep(userFps, fpsStep_)) {
      ++fpsStep_;
      overAtCap_ = 0;
      fpsOk_ = 0;
      return Current(userFps);
    }

    // Up the ladder in reverse: the frame rate back first (when the rate one step up is predicted
    // to fit), then the ceiling.
    if (fpsStep_ > 0) {
      const uint32_t cur = FpsForStep(userFps, fpsStep_);
      const uint32_t next = FpsForStep(userFps, fpsStep_ - 1);
      const uint64_t predicted = cur > 0 ? percent * next / cur : percent;
      fpsOk_ = predicted < config_.fpsUpPercent ? fpsOk_ + 1 : 0;
      if (fpsOk_ >= config_.fpsUpAfterSec) {
        --fpsStep_;
        fpsOk_ = 0;
        under_ = 0;
      }
      return Current(userFps);
    }
    if (under_ >= config_.downAfterSec && qp_ > config_.baseQp) {
      qp_ = std::max(config_.baseQp, qp_ - std::min(config_.stepDownQp, qp_ - config_.baseQp));
      under_ = 0;
    }
    return Current(userFps);
  }

  uint32_t maxQp() const { return qp_; }
  uint32_t fpsStep() const { return fpsStep_; }

  /** A new encoder target or a new user frame rate: start again from the floor at full rate. */
  void Reset() {
    qp_ = config_.baseQp;
    fpsStep_ = 0;
    over_ = under_ = overAtCap_ = fpsOk_ = 0;
  }

 private:
  RateDecision Current(uint32_t userFps) const { return RateDecision{qp_, FpsForStep(userFps, fpsStep_)}; }

  RateGovernorConfig config_;
  uint32_t qp_;
  uint32_t fpsStep_ = 0;
  uint32_t over_ = 0;
  uint32_t under_ = 0;
  uint32_t overAtCap_ = 0;
  uint32_t fpsOk_ = 0;
};

}  // namespace remote60::native_poc
