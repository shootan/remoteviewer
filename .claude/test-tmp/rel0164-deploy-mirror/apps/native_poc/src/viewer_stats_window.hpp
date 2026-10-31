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

// The window used to be a deadline that advanced one second per stats line and nothing else. After a
// gap with no frame (a still screen, a picker, a key wait) the deadline sat in the past and caught up
// one second per FRAME: every frame closed a "window" of its own, recvFrames=1, published as 1 fps,
// dozens of reports a second -- and the host, sending a full cadence again, read 1 fps as a relay
// collapse (field 2026-10-12 18:04: 190 s gap, 89 reports in 9 s, high_to_mid_severe). Now:
//   - a window's rates are per second of the time that really elapsed since the previous close;
//   - the next deadline is one window after the previous one, or after now when that is already past
//     -- a late close never leaves a backlog of deadlines;
//   - a window that spans a gap (longer than kGapUs) is logged but NOT published: its few frames
//     over a long span say nothing about the stream that just resumed. The next window is short
//     (kAfterGapUs), so the host has a measured rate before its stale-feedback rule (P7, 3 s fresh +
//     2 stale seconds) could count the gap as feedback lost.
// No frames at all still means no report -- a real stall stays visible to the host as stale feedback.
class StatsWindow {
 public:
  static constexpr uint64_t kWindowUs = 1000000ULL;
  static constexpr uint64_t kGapUs = 2 * kWindowUs;
  static constexpr uint64_t kAfterGapUs = kWindowUs / 2;

  void Start(uint64_t nowUs) {
    startUs_ = nowUs;
    dueUs_ = nowUs + kWindowUs;
  }

  StatsWindowClose Poll(uint64_t nowUs) {
    StatsWindowClose c;
    if (nowUs < dueUs_) return c;
    c.due = true;
    c.elapsedUs = nowUs > startUs_ ? nowUs - startUs_ : 0;
    c.publish = c.elapsedUs > 0 && c.elapsedUs <= kGapUs;
    startUs_ = nowUs;
    if (!c.publish) {
      dueUs_ = nowUs + kAfterGapUs;
    } else {
      dueUs_ += kWindowUs;
      if (dueUs_ <= nowUs) dueUs_ = nowUs + kWindowUs;
    }
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
  uint64_t startUs_ = 0;  // the previous close (or Start): the window's real beginning
  uint64_t dueUs_ = 0;
};

}  // namespace remote60::native_poc::viewer
