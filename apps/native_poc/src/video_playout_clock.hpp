#pragma once

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

// Decides when each arriving frame should reach the screen.
//
// Frames do not arrive on a cadence. The host's async encoder releases two or three access
// units at once, the capture clock alternates 16/50 ms steps even while producing its 30
// frames a second, and the network adds its own jitter on top. Handing frames to the display
// as they arrive puts all of that on screen, which is what "30 fps that does not look like 30
// fps" is made of.
//
// So: hold a little headroom, and release on a clock. Two properties make that clock work.
//
//  - It advances at the rate frames actually arrive, measured, not the rate that was
//    requested. A host asked for 60 fps commonly delivers 33; a clock ticking 60 times a
//    second against 33 arrivals falls behind by the shortfall every second until a frame comes
//    due before it arrives, and recovering from that means jumping the clock. The jump is
//    visible, and it is worse the larger the shortfall -- which is why a 60 fps request could
//    look worse than a 30 fps one.
//  - It is anchored to the sender's capture timeline, corrected toward it by a bounded slew
//    each frame. Free-running would drift; following exactly would reproduce capture jitter.
//    The bound is what buys smoothness without letting error accumulate.
class VideoPlayoutClock {
 public:
  struct Decision {
    uint64_t presentAtUs = 0;
    bool reanchored = false;
    // Static string, valid for the lifetime of the program. Empty unless `reanchored`.
    const char* reanchorReason = "";
  };

  // Headroom held against arrival jitter, derived from the measured cadence: enough frames to
  // absorb an encoder burst, capped so latency stays bounded when the cadence is slow.
  static constexpr uint64_t kLeadMinUs = 60000;
  static constexpr uint64_t kLeadMaxUs = 120000;
  // Bounds on a believable inter-frame step, so one absurd capture timestamp cannot poison
  // the cadence.
  static constexpr uint64_t kStepMinUs = 4000;
  static constexpr uint64_t kStepMaxUs = 200000;
  // A capture gap this large is a stream interruption, not jitter; the timeline restarts.
  static constexpr uint64_t kDiscontinuityUs = 500000;

  void SetTargetFrameIntervalUs(uint64_t intervalUs) {
    targetFrameIntervalUs_ = (intervalUs > 0) ? intervalUs : 1;
    Reset();
    // A rate change makes the measured cadence describe the old rate. Seed from the new
    // request rather than spending a second converging away from a stale value.
    stepUs_ = targetFrameIntervalUs_;
  }

  uint64_t TargetFrameIntervalUs() const { return targetFrameIntervalUs_; }

  // Forgets the timeline. Keeps the measured cadence, which describes the sender and does not
  // change because the receiver reset its decoder.
  void Reset() {
    streamGeneration_ = 0;
    remoteBaseUs_ = 0;
    localBaseUs_ = 0;
    lastPresentAtUs_ = 0;
    lastRemoteCaptureUs_ = 0;
  }

  Decision Schedule(uint64_t nowUs, uint64_t remoteCaptureUs, uint64_t streamGeneration) {
    bool reanchor = (localBaseUs_ == 0);
    const char* reason = reanchor ? "init" : "";

    if (!reanchor && streamGeneration_ != 0 && streamGeneration != streamGeneration_) {
      reanchor = true;
      reason = "stream_generation";
    }
    if (!reanchor && remoteCaptureUs == 0) {
      reanchor = true;
      reason = "zero_capture";
      ++fallbackCount_;
    }
    if (!reanchor && remoteBaseUs_ == 0) {
      reanchor = true;
      reason = "remote_base_missing";
      ++fallbackCount_;
    }
    if (!reanchor && lastRemoteCaptureUs_ != 0 && remoteCaptureUs < lastRemoteCaptureUs_) {
      reanchor = true;
      reason = "capture_backwards";
      ++fallbackCount_;
    }

    uint64_t presentAtUs = nowUs;
    if (!reanchor && remoteCaptureUs < remoteBaseUs_) {
      reanchor = true;
      reason = "capture_before_base";
      ++fallbackCount_;
    } else if (!reanchor) {
      const uint64_t remoteStepUs =
          (lastRemoteCaptureUs_ != 0) ? (remoteCaptureUs - lastRemoteCaptureUs_) : 0;
      if (lastRemoteCaptureUs_ != 0 && remoteStepUs > kDiscontinuityUs) {
        reanchor = true;
        reason = "capture_gap";
        ++fallbackCount_;
      } else {
        if (stepUs_ == 0) stepUs_ = targetFrameIntervalUs_;
        if (remoteStepUs >= kStepMinUs && remoteStepUs <= kStepMaxUs) {
          stepUs_ = (stepUs_ * 7 + remoteStepUs) / 8;
        }
        // Where the sender's timeline says this frame belongs...
        const int64_t anchoredUs = static_cast<int64_t>(localBaseUs_) +
                                   static_cast<int64_t>(remoteCaptureUs - remoteBaseUs_);
        // ...and where a steady cadence would put it. Follow the cadence, corrected toward
        // the anchor by at most a quarter frame, so neither jitter nor drift gets through.
        const int64_t cadenceUs =
            static_cast<int64_t>(lastPresentAtUs_) + static_cast<int64_t>(stepUs_);
        const int64_t maxSlewUs = static_cast<int64_t>(stepUs_ / 4);
        int64_t correctionUs = anchoredUs - cadenceUs;
        correctionUs = std::clamp(correctionUs, -maxSlewUs, maxSlewUs);
        const int64_t pacedUs = cadenceUs + correctionUs;
        presentAtUs = (pacedUs > 0) ? static_cast<uint64_t>(pacedUs) : nowUs;

        const uint64_t leadUs = LeadForStepUs(stepUs_);
        if (nowUs >= presentAtUs) {
          // Due before it arrived. The buffer is dry and no amount of pacing saves this
          // frame; rebuilding the headroom is the only recovery.
          reanchor = true;
          reason = "playout_underrun";
          ++fallbackCount_;
        } else {
          // Hold the buffer near its target depth by walking the anchor an eighth of a frame
          // at a time. Nudging a single frame would achieve nothing -- the next correction is
          // measured against the anchor and would pull it straight back, leaving the buffer
          // to drain until it ran dry and the clock jumped.
          const uint64_t adjustUs = std::max<uint64_t>(1, stepUs_ / 8);
          const uint64_t headroomUs = presentAtUs - nowUs;
          if (headroomUs < leadUs / 2) {
            // Arrivals drifted later than the anchor predicted; buy headroom back.
            localBaseUs_ += adjustUs;
            presentAtUs += adjustUs;
          } else if (headroomUs > leadUs * 2 && localBaseUs_ > adjustUs &&
                     presentAtUs > adjustUs) {
            // Deeper than it needs to be, and depth beyond what jitter requires is latency.
            localBaseUs_ -= adjustUs;
            presentAtUs -= adjustUs;
          }
        }
      }
    }

    if (reanchor) {
      remoteBaseUs_ = remoteCaptureUs;
      if (stepUs_ == 0) stepUs_ = targetFrameIntervalUs_;
      localBaseUs_ = nowUs + LeadForStepUs(stepUs_);
      presentAtUs = localBaseUs_;
      ++reanchorCount_;
    }

    if (lastPresentAtUs_ > 0 && presentAtUs <= lastPresentAtUs_) {
      presentAtUs = lastPresentAtUs_ + 1;
      ++monotonicClampCount_;
    }

    streamGeneration_ = streamGeneration;
    lastPresentAtUs_ = presentAtUs;
    lastRemoteCaptureUs_ = remoteCaptureUs;

    Decision decision;
    decision.presentAtUs = presentAtUs;
    decision.reanchored = reanchor;
    decision.reanchorReason = reanchor ? reason : "";
    return decision;
  }

  static uint64_t LeadForStepUs(uint64_t stepUs) {
    return std::clamp(stepUs * 5 / 2, kLeadMinUs, kLeadMaxUs);
  }

  uint64_t StepUs() const { return stepUs_; }
  uint64_t LeadUs() const { return LeadForStepUs(stepUs_); }
  uint64_t ReanchorCount() const { return reanchorCount_; }
  uint64_t MonotonicClampCount() const { return monotonicClampCount_; }
  uint64_t FallbackCount() const { return fallbackCount_; }
  uint64_t StreamGeneration() const { return streamGeneration_; }
  uint64_t RemoteBaseUs() const { return remoteBaseUs_; }
  uint64_t LocalBaseUs() const { return localBaseUs_; }
  uint64_t LastPresentAtUs() const { return lastPresentAtUs_; }
  uint64_t LastRemoteCaptureUs() const { return lastRemoteCaptureUs_; }

 private:
  uint64_t targetFrameIntervalUs_ = 33333;
  uint64_t stepUs_ = 33333;
  uint64_t streamGeneration_ = 0;
  uint64_t remoteBaseUs_ = 0;
  uint64_t localBaseUs_ = 0;
  uint64_t lastPresentAtUs_ = 0;
  uint64_t lastRemoteCaptureUs_ = 0;
  uint64_t reanchorCount_ = 0;
  uint64_t monotonicClampCount_ = 0;
  uint64_t fallbackCount_ = 0;
};

}  // namespace remote60::native_poc
