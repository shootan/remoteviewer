// quality r5/r6: RateGovernor -- the closed loop on the quantiser ceiling, then the frame rate.
#include <cstdio>
#include <string>

#include "host_rate_governor.hpp"

using remote60::native_poc::RateDecision;
using remote60::native_poc::RateGovernor;

namespace {
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
}
constexpr uint32_t kTarget = 6000000;
constexpr uint32_t kUserFps = 60;
uint64_t bytes_at(double timesTarget) { return static_cast<uint64_t>(kTarget / 8.0 * timesTarget); }
RateDecision sec(RateGovernor& g, double timesTarget, uint32_t userFps = kUserFps) {
  return g.OnSecond(bytes_at(timesTarget), kTarget, userFps);
}
}  // namespace

int main() {
  // ------------------------------------------------------------------ r5: the ceiling
  {
    RateGovernor g;
    check("starts at the text floor (32) and the user's rate", g.maxQp() == 32 && sec(g, 0.5).fps == 60);
    for (int i = 0; i < 30; ++i) sec(g, 0.5);
    check("a stream under target never leaves the floor", g.maxQp() == 32);
    for (int i = 0; i < 30; ++i) sec(g, 1.1);
    check("slightly over (110%) is inside the band: no change", g.maxQp() == 32 && g.fpsStep() == 0);
  }
  {
    RateGovernor g;
    sec(g, 3.0);
    check("one over-second alone does not step (a burst)", g.maxQp() == 32);
    sec(g, 3.0);
    check("two consecutive over-seconds step up by 3", g.maxQp() == 35);
    sec(g, 3.0);
    check("the count restarts after a step", g.maxQp() == 35);
    sec(g, 3.0);
    check("...and steps again after two more", g.maxQp() == 38);
    sec(g, 3.0);
    sec(g, 3.0);
    check("the last step stops at the cap (40), not 41", g.maxQp() == 40);
    check("the frame rate is untouched while the ceiling still has room", g.fpsStep() == 0);
  }
  {
    RateGovernor g;
    sec(g, 3.0);
    sec(g, 1.0);
    sec(g, 3.0);
    check("over-seconds must be consecutive (an in-band second resets)", g.maxQp() == 32);
  }
  {
    RateGovernor g;
    for (int i = 0; i < 6; ++i) sec(g, 2.0);
    // Back down at the full rate: under 85% for 3 s steps by 2.
    sec(g, 0.5);
    sec(g, 0.5);
    check("two under-seconds are not yet a step down", g.maxQp() == 40);
    sec(g, 0.5);
    check("three under-seconds step down by 2", g.maxQp() == 38);
    for (int i = 0; i < 60; ++i) sec(g, 0.5);
    check("back at the floor, and not below it", g.maxQp() == 32);
  }
  // ------------------------------------------------------------------ r6: the frame rate
  {
    RateGovernor g;
    for (int i = 0; i < 6; ++i) sec(g, 2.0);  // 32 -> 35 -> 38 -> 40
    check("at the cap", g.maxQp() == 40 && g.fpsStep() == 0);
    sec(g, 2.0);
    sec(g, 2.0);
    check("two over-seconds at the cap are not yet a frame-rate step", g.fpsStep() == 0);
    const RateDecision d1 = sec(g, 2.0);
    check("three over-seconds at the cap: 60 -> 45", g.fpsStep() == 1 && d1.fps == 45 && d1.maxQp == 40);
    sec(g, 2.0);
    sec(g, 2.0);
    const RateDecision d2 = sec(g, 2.0);
    check("still over at 45: 45 -> 30", g.fpsStep() == 2 && d2.fps == 30);
    for (int i = 0; i < 10; ++i) sec(g, 2.0);
    check("never below the second step (1/2)", g.fpsStep() == 2 && sec(g, 2.0).fps == 30);
  }
  {
    RateGovernor g;
    for (int i = 0; i < 12; ++i) sec(g, 2.0);  // at cap, 60 -> 45 -> 30
    check("setup: at 30 fps, ceiling 40", g.fpsStep() == 2 && g.maxQp() == 40);
    // At 30 fps the payload is 0.5x: at 45 it would be 0.75x (< 90%) -- the rate comes back first.
    for (int i = 0; i < 4; ++i) sec(g, 0.5);
    check("four fitting seconds are not yet a step back up", g.fpsStep() == 2 && g.maxQp() == 40);
    sec(g, 0.5);
    check("five: 30 -> 45, the ceiling still 40", g.fpsStep() == 1 && g.maxQp() == 40);
    // At 45 the payload is 0.7x: at 60 it would be 0.93x -- does not fit, stay.
    for (int i = 0; i < 10; ++i) sec(g, 0.7);
    check("45 -> 60 waits while 60 is predicted over 90%", g.fpsStep() == 1 && g.maxQp() == 40);
    for (int i = 0; i < 5; ++i) sec(g, 0.6);  // predicted 0.8x at 60
    check("then 45 -> 60 once it fits", g.fpsStep() == 0 && g.maxQp() == 40);
    for (int i = 0; i < 3; ++i) sec(g, 0.6);
    check("only at the full rate does the ceiling step down (40 -> 38)", g.maxQp() == 38);
  }
  {
    RateGovernor g;
    for (int i = 0; i < 12; ++i) sec(g, 2.0);
    const RateDecision calm = sec(g, 0.2);
    check("a calm second (the motion stopped) restores the full rate and the floor at once",
          calm.fps == 60 && calm.maxQp == 32 && g.fpsStep() == 0);
  }
  {
    RateGovernor g;
    check("a 30 fps user: steps are 22 and 15", g.FpsForStep(30, 1) == 22 && g.FpsForStep(30, 2) == 15);
    check("a 20 fps user: 15 then no lower", g.FpsForStep(20, 1) == 15 && g.FpsForStep(20, 2) == 15);
    for (int i = 0; i < 30; ++i) g.OnSecond(bytes_at(2.0), kTarget, 20);
    check("...so at 20 fps only one step is taken (no step that changes nothing)", g.fpsStep() == 1);
  }
  {
    RateGovernor g;
    for (int i = 0; i < 12; ++i) sec(g, 2.0);
    g.Reset();
    check("Reset returns to the floor and the full rate", g.maxQp() == 32 && g.fpsStep() == 0);
    check("a zero target is ignored", g.OnSecond(bytes_at(3.0), 0, kUserFps).maxQp == 32);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
