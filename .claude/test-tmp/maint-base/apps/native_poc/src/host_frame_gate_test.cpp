// Pins the static-frame gate of FrameGatingState (host_frame_gate.hpp): the static / motion streaks and
// their caps, the enter / exit transitions, the reference-miss reset, and the skip decision in static
// mode, on the paced motion path and on the unpaced path. Change estimates are fed directly, so nothing
// here touches pixels.
//
// Host split refactor Phase 2-T2 (2026-08-26).

#include "host_frame_gate.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using remote60::native_poc::FrameGatingState;

namespace {

int gFailures = 0;

void expect(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++gFailures;
  }
}

constexpr uint64_t kFrameUs = 33333;       // 30 fps target
constexpr uint64_t kStaticUs = 200000;     // 5 fps in static mode

FrameGatingState Make() {
  FrameGatingState g;
  g.enabled = true;
  g.staticFps = 5;
  g.enterFrames = 3;
  g.exitFrames = 2;
  g.sampleTarget = 4096;
  g.staticIntervalUs = kStaticUs;
  return g;
}

// One frame with the given change estimate: record, then run the transition; returns whether the mode changed.
bool Frame(FrameGatingState& g, uint64_t changePermille) {
  g.RecordChange(changePermille);
  return g.UpdateMode();
}

void TestEnterStaticAfterEnterFrames() {
  FrameGatingState g = Make();
  expect(!Frame(g, 0) && !g.staticMode && g.staticStreak == 1, "gate: first identical frame does not enter static");
  expect(!Frame(g, 0) && !g.staticMode && g.staticStreak == 2, "gate: second identical frame does not enter static");
  expect(Frame(g, 0) && g.staticMode && g.staticStreak == 3, "gate: enterFrames identical frames enter static (mode changed)");
  expect(!Frame(g, 0) && g.staticMode, "gate: staying static is not a change");
  expect(g.changePermilleCount == 4 && g.changePermilleSum == 0, "gate: change telemetry accumulates");
}

void TestFirstChangedFrameLeavesStatic() {
  FrameGatingState g = Make();
  Frame(g, 0); Frame(g, 0); Frame(g, 0);
  expect(g.staticMode, "gate: precondition static");
  expect(Frame(g, 7) && !g.staticMode && g.motionStreak == 1 && g.staticStreak == 0,
         "gate: any change at all leaves static on that very frame");
  // Back to still: the streak restarts from zero, so it takes enterFrames again.
  expect(!Frame(g, 0) && !Frame(g, 0) && Frame(g, 0) && g.staticMode, "gate: re-enters static after enterFrames again");
}

void TestMotionStreakResetsStaticStreak() {
  FrameGatingState g = Make();
  Frame(g, 0); Frame(g, 0);
  Frame(g, 12);
  expect(g.staticStreak == 0 && g.motionStreak == 1 && !g.staticMode, "gate: a changed frame resets the static streak");
  Frame(g, 0);
  expect(g.staticStreak == 1 && g.motionStreak == 0, "gate: an identical frame resets the motion streak");
}

void TestReferenceMissIsMotion() {
  FrameGatingState g = Make();
  Frame(g, 0); Frame(g, 0); Frame(g, 0);
  expect(g.staticMode, "gate: precondition static");
  g.RecordReferenceMiss();
  expect(g.staticStreak == 0 && g.motionStreak == 0 && g.changePermilleLast == 1000, "gate: reference miss resets streaks and counts as full change");
  expect(g.UpdateMode() && !g.staticMode, "gate: reference miss leaves static");
  expect(g.changePermilleCount == 3, "gate: a miss is not a change sample");
}

void TestStreaksAreCapped() {
  FrameGatingState g = Make();
  for (int i = 0; i < 60005; ++i) g.RecordChange(0);
  expect(g.staticStreak == 60000, "gate: static streak caps at 60000");
  for (int i = 0; i < 60005; ++i) g.RecordChange(3);
  expect(g.motionStreak == 60000, "gate: motion streak caps at 60000");
}

void TestSkipInStaticMode() {
  FrameGatingState g = Make();
  Frame(g, 0); Frame(g, 0); Frame(g, 0);
  g.lastSentUs = 1000000;
  const uint64_t soon = g.lastSentUs + 100000;   // inside the 200ms static interval
  const uint64_t later = g.lastSentUs + kStaticUs; // exactly one interval: not skipped
  expect(g.ShouldSkip(soon, false, kFrameUs, true), "gate: static mode throttles an identical frame inside the interval");
  expect(!g.ShouldSkip(later, false, kFrameUs, true), "gate: static mode lets the frame through once the interval elapsed");
  expect(!g.ShouldSkip(soon, true, kFrameUs, true), "gate: a pending key request is never throttled");
  g.RecordChange(5);
  expect(!g.ShouldSkip(soon, false, kFrameUs, true), "gate: a changed frame is never throttled");
  g.RecordChange(0);
  g.lastSentUs = 0;
  expect(!g.ShouldSkip(soon, false, kFrameUs, true), "gate: nothing sent yet -> no throttle");
}

void TestMotionModePacedNeverSkips() {
  FrameGatingState g = Make();
  Frame(g, 0);  // motion mode, identical frame
  g.lastSentUs = 1000000;
  expect(!g.ShouldSkip(g.lastSentUs + 1000, false, kFrameUs, true), "gate: paced motion mode leaves rate limiting to the tick");
}

void TestMotionModeUnpacedUsesFrameInterval() {
  FrameGatingState g = Make();
  Frame(g, 0);  // motion mode, identical frame
  g.lastSentUs = 1000000;
  expect(g.ShouldSkip(g.lastSentUs + 10000, false, kFrameUs, false), "gate: unpaced path throttles inside the frame interval");
  expect(!g.ShouldSkip(g.lastSentUs + kFrameUs, false, kFrameUs, false), "gate: unpaced path passes at the frame interval");
  Frame(g, 9);
  expect(!g.ShouldSkip(g.lastSentUs + 10000, false, kFrameUs, false), "gate: unpaced path never throttles a changed frame");
}

}  // namespace

int main() {
  TestEnterStaticAfterEnterFrames();
  TestFirstChangedFrameLeavesStatic();
  TestMotionStreakResetsStaticStreak();
  TestReferenceMissIsMotion();
  TestStreaksAreCapped();
  TestSkipInStaticMode();
  TestMotionModePacedNeverSkips();
  TestMotionModeUnpacedUsesFrameInterval();
  if (gFailures == 0) {
    std::printf("host_frame_gate_test: PASS\n");
    return 0;
  }
  std::printf("host_frame_gate_test: FAIL (%d)\n", gFailures);
  return 1;
}
