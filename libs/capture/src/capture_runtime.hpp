#pragma once

#include <cstdint>
#include <string>

namespace remote60::host {

struct CaptureLoopStats {
  uint64_t frames = 0;
  uint64_t callbacks = 0;
  uint64_t ticks = 0;
  uint64_t freshTicks = 0;
  uint64_t reusedTicks = 0;
  uint64_t idleTicks = 0;
  uint64_t convertedFrames = 0;
  uint64_t convertFailures = 0;
  double elapsedSec = 0.0;
  double fps = 0.0;
  double timelineFps = 0.0;
  double targetFps = 60.0;
  double avgConvertMs = 0.0;
  std::string detail;
};

CaptureLoopStats run_capture_loop_seconds(int seconds);

}  // namespace remote60::host
