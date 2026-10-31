#pragma once

// Static-frame gating state (FrameGatingState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace remote60::native_poc {

// Static-frame gating (Phase 1-7 state struct). Skips near-identical frames on a still screen
// and drops the encode cadence to staticFps, so a static desktop does not spend bitrate on
// identical P-frames. Config fields are set once from env in main(); the rest is loop state.
// thread: main encode loop only.
struct FrameGatingState {
  // Config (REMOTE60_NATIVE_FRAME_GATING_* / STATIC_SCENE_FPS), fixed after startup.
  bool enabled = false;
  uint32_t staticFps = 0;
  uint32_t staticThresholdPermille = 0;
  uint32_t enterFrames = 0;
  uint32_t exitFrames = 0;
  uint32_t sampleTarget = 0;
  uint64_t staticIntervalUs = 0;  // derived from staticFps and the active frame interval
  // Reference frame for the change estimate + static/motion streaks.
  std::shared_ptr<std::vector<uint8_t>> refPayload;
  uint32_t refW = 0;
  uint32_t refH = 0;
  uint32_t refStride = 0;
  uint32_t staticStreak = 0;
  uint32_t motionStreak = 0;
  bool staticMode = false;
  uint64_t lastSentUs = 0;
  // Telemetry for the stats line.
  uint64_t skipCount = 0;
  uint64_t staticSkipCount = 0;
  uint64_t changePermilleLast = 1000;
  uint64_t changePermilleSum = 0;
  uint64_t changePermilleCount = 0;

  // --- decisions (Phase 2-T2: the former stage_gate_static lines, pure on this state) ---
  // The reference frame matched: record this frame's change estimate and advance the streaks.
  void RecordChange(uint64_t changePermille) {
    FrameGatingState& frameGating = *this;
    frameGating.changePermilleLast = changePermille;
    frameGating.changePermilleSum += frameGating.changePermilleLast;
    ++frameGating.changePermilleCount;

    if (frameGating.changePermilleLast == 0) {
      frameGating.staticStreak = std::min<uint32_t>(frameGating.staticStreak + 1, 60000);
      frameGating.motionStreak = 0;
    } else {
      frameGating.motionStreak = std::min<uint32_t>(frameGating.motionStreak + 1, 60000);
      frameGating.staticStreak = 0;
    }
  }
  // No usable reference (first frame, size change): the streaks restart and the frame is motion.
  void RecordReferenceMiss() {
    FrameGatingState& frameGating = *this;
    frameGating.staticStreak = 0;
    frameGating.motionStreak = 0;
    frameGating.changePermilleLast = 1000;
  }
  // Static <-> motion transition on the streaks; true when the mode changed (the caller logs it).
  bool UpdateMode() {
    FrameGatingState& frameGating = *this;
    const bool prevStaticMode = frameGating.staticMode;
    // Any difference at all counts as motion. estimate_bgra_change_permille returns 0 only
    // for a byte-identical frame, so this both leaves static mode on the first changed
    // frame and never throttles an edit that is too small to move a percentage threshold.
    const bool motionNow = frameGating.changePermilleLast > 0;
    if (!frameGating.staticMode && frameGating.staticStreak >= frameGating.enterFrames) {
      frameGating.staticMode = true;
    } else if (frameGating.staticMode &&
               (motionNow || frameGating.motionStreak >= frameGating.exitFrames)) {
      frameGating.staticMode = false;
    }
    return prevStaticMode != frameGating.staticMode;
  }
  // Whether this frame is throttled by the gate: only in static mode or on the unpaced path, never
  // a frame that changed or one a key request is waiting on.
  bool ShouldSkip(uint64_t queuePopUs, bool keyReqPending, uint64_t activeFrameIntervalUs,
                  bool paceByTick) const {
    const FrameGatingState& frameGating = *this;
    const bool motionNow = frameGating.changePermilleLast > 0;
    const uint64_t targetIntervalUs = frameGating.staticMode ? frameGating.staticIntervalUs : activeFrameIntervalUs;
    // The static interval throttles idle scenes; it must never hold back a frame that
    // actually changed, or the first interaction after idle arrives late.
    // In paced motion mode the main tick already enforces encoder.activeFrameIntervalUs. Applying
    // the same interval here a second time makes a slightly-early capture timestamp skip
    // the entire tick (measured 1-6 lost frames/s at 60fps). Keep this limiter only for
    // static throttling or the explicitly unpaced throughput path.
    const bool needsGatingRateLimit = frameGating.staticMode || !paceByTick;
    return needsGatingRateLimit && !keyReqPending && !motionNow &&
        frameGating.lastSentUs > 0 &&
        queuePopUs < (frameGating.lastSentUs + targetIntervalUs);
  }
};

}  // namespace remote60::native_poc
