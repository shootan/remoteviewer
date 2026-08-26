#pragma once

// Static-frame gating state (FrameGatingState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

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
};

}  // namespace remote60::native_poc
