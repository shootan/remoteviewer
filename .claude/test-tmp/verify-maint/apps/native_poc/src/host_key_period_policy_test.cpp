// Unit tests for the periodic-key period policy (stutter-keyframe r2, step 4b).
// Pure function, no MFT / no network: every branch of compute_effective_keyint is a case here.

#include "host_key_period_policy.hpp"

#include <cstdio>
#include <string>

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!ok) {
    ++gFailures;
    if (!detail.empty()) std::printf("        %s\n", detail.c_str());
  }
}
uint32_t eff(uint32_t requested, uint32_t fps, uint32_t override_ = 0) {
  KeyPeriodInputs in;
  in.requestedKeyint = requested;
  in.fps = fps;
  in.overrideKeyint = override_;
  return compute_effective_keyint(in);
}
}  // namespace

int main() {
  std::printf("host_key_period_policy_test\n");

  // The production case: runtime config asks keyint=120 (4s) at 30fps -> raised to the 10s floor.
  check("config 120 @ 30fps -> 10s floor = 300", eff(120, 30) == 300,
        "got " + std::to_string(eff(120, 30)));
  // 60fps: 10s floor = 600, which is also the frame ceiling.
  check("config 120 @ 60fps -> 10s floor = 600", eff(120, 60) == 600,
        "got " + std::to_string(eff(120, 60)));
  // A caller already above the floor keeps its longer period (never lowered).
  check("config 500 @ 30fps stays 500 (above floor)", eff(500, 30) == 500,
        "got " + std::to_string(eff(500, 30)));
  // The floor clamps to the frame ceiling: 90fps*10 = 900 -> 600.
  check("config 120 @ 90fps clamps to ceiling 600", eff(120, 90) == 600,
        "got " + std::to_string(eff(120, 90)));

  // Explicit override wins verbatim (A/B), bypassing the floor...
  check("override 120 wins over the floor (=120)", eff(120, 30, 120) == 120,
        "got " + std::to_string(eff(120, 30, 120)));
  check("override 60 wins (=60, below floor)", eff(120, 30, 60) == 60,
        "got " + std::to_string(eff(120, 30, 60)));
  // ...but is still clamped to the ceiling.
  check("override 9999 clamps to ceiling 600", eff(120, 30, 9999) == 600,
        "got " + std::to_string(eff(120, 30, 9999)));

  // Degenerate inputs are guarded, never 0 or an overflow.
  check("requested 0 @ 30fps -> floor 300", eff(0, 30) == 300, "got " + std::to_string(eff(0, 30)));
  check("fps 0 guarded (requested 120 kept, floor=10)", eff(120, 0) == 120,
        "got " + std::to_string(eff(120, 0)));

  if (gFailures == 0) {
    std::printf("host_key_period_policy_test: PASS\n");
    return 0;
  }
  std::printf("host_key_period_policy_test: FAIL (%d)\n", gFailures);
  return 1;
}
