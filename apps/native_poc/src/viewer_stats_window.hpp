#pragma once

// The recv thread's once-a-second stats window (motion-settle r1, M4).
//
// Role:    decides when the "[native-video-client] recvFrames=..." line is due, how long the window
//          it closes really was, and turns that window's counts into per-second rates for the
//          metrics the viewer reports to the host (ControlClientMetrics -> host ABR).
// Thread:  recv only.
// Input:   the QPC time of each frame the recv thread processes (the window only moves when a frame
//          is processed -- there is no timer).
// Output:  StatsWindowClose per frame; rates per window.
// Callers: VideoReceiver::Run (Start), VideoReceiver::flush_stats_if_due (Poll + rates).
// Pure, no Windows headers: viewer_stats_window_test.cpp drives it with a hand-made clock.

#include <cstdint>

namespace remote60::native_poc::viewer {

struct StatsWindowClose {
  bool due = false;         // a stats line is due at this frame
  bool publish = false;     // ...and its rates go to the host as this second's metrics
  uint64_t elapsedUs = 0;   // the length of the window being closed (the rates' denominator)
};

class StatsWindow {
 public:
  static constexpr uint64_t kWindowUs = 1000000ULL;

  void Start(uint64_t nowUs) { dueUs_ = nowUs + kWindowUs; }

  StatsWindowClose Poll(uint64_t nowUs) {
    StatsWindowClose c;
    if (nowUs < dueUs_) return c;
    c.due = true;
    c.publish = true;
    c.elapsedUs = kWindowUs;
    dueUs_ += kWindowUs;
    return c;
  }

  // count events over elapsedUs, as events per second x100 (capped to 32 bits).
  static uint32_t PerSecondX100(uint64_t count, uint64_t elapsedUs) {
    if (elapsedUs == 0) return 0;
    const uint64_t v = (count * 100ULL * kWindowUs) / elapsedUs;
    return v > 0xFFFFFFFFULL ? 0xFFFFFFFFu : static_cast<uint32_t>(v);
  }
  // bytes over elapsedUs, as megabits per second.
  static double Mbps(uint64_t bytes, uint64_t elapsedUs) {
    if (elapsedUs == 0) return 0.0;
    return (static_cast<double>(bytes) * 8.0) / static_cast<double>(elapsedUs);
  }

 private:
  uint64_t dueUs_ = 0;
};

}  // namespace remote60::native_poc::viewer
