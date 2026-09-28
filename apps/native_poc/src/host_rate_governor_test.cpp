// quality r5: RateGovernor -- the closed loop that moves the encoder's quantiser ceiling.
#include <cstdio>
#include <string>

#include "host_rate_governor.hpp"

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
uint64_t bytes_at(double timesTarget) { return static_cast<uint64_t>(kTarget / 8.0 * timesTarget); }
}  // namespace

int main() {
  {
    RateGovernor g;
    check("starts at the text floor (32)", g.maxQp() == 32);
    for (int i = 0; i < 30; ++i) g.OnSecond(bytes_at(0.5), kTarget);
    check("a stream under target never leaves the floor", g.maxQp() == 32);
    for (int i = 0; i < 30; ++i) g.OnSecond(bytes_at(1.1), kTarget);
    check("slightly over (110%) is inside the band: no change", g.maxQp() == 32);
  }
  {
    RateGovernor g;
    g.OnSecond(bytes_at(3.0), kTarget);
    check("one over-second alone does not step (a burst)", g.maxQp() == 32);
    g.OnSecond(bytes_at(3.0), kTarget);
    check("two consecutive over-seconds step up by 3", g.maxQp() == 35);
    g.OnSecond(bytes_at(3.0), kTarget);
    check("the count restarts after a step", g.maxQp() == 35);
    g.OnSecond(bytes_at(3.0), kTarget);
    check("...and steps again after two more", g.maxQp() == 38);
    g.OnSecond(bytes_at(3.0), kTarget);
    g.OnSecond(bytes_at(3.0), kTarget);
    check("the last step stops at the cap (40), not 41", g.maxQp() == 40);
    for (int i = 0; i < 20; ++i) g.OnSecond(bytes_at(3.0), kTarget);
    check("never above the cap (40)", g.maxQp() == 40);
  }
  {
    RateGovernor g;
    for (int i = 0; i < 20; ++i) g.OnSecond(bytes_at(3.0), kTarget);
    g.OnSecond(bytes_at(0.5), kTarget);
    g.OnSecond(bytes_at(0.5), kTarget);
    check("two under-seconds are not yet a step down", g.maxQp() == 40);
    g.OnSecond(bytes_at(0.5), kTarget);
    check("three under-seconds step down by 2", g.maxQp() == 38);
    for (int i = 0; i < 60; ++i) g.OnSecond(bytes_at(0.5), kTarget);
    check("back at the floor, and not below it", g.maxQp() == 32);
  }
  {
    RateGovernor g;
    for (int i = 0; i < 20; ++i) g.OnSecond(bytes_at(3.0), kTarget);
    g.OnSecond(bytes_at(0.2), kTarget);
    check("one calm second (under 40%: the motion stopped) returns straight to the floor", g.maxQp() == 32);
  }
  {
    RateGovernor g;
    g.OnSecond(bytes_at(3.0), kTarget);
    g.OnSecond(bytes_at(1.0), kTarget);
    g.OnSecond(bytes_at(3.0), kTarget);
    check("over-seconds must be consecutive (an in-band second resets)", g.maxQp() == 32);
  }
  {
    RateGovernor g;
    for (int i = 0; i < 4; ++i) g.OnSecond(bytes_at(3.0), kTarget);
    g.Reset();
    check("Reset returns to the floor", g.maxQp() == 32);
    check("a zero target is ignored", g.OnSecond(bytes_at(3.0), 0) == 32);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
