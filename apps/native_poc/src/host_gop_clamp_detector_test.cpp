// Unit tests for the runtime GOP-clamp detector (stutter-keyframe r3 B). Pure; no MFT.

#include "host_gop_clamp_detector.hpp"

#include <cstdio>
#include <string>

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!ok) { ++gFailures; if (!detail.empty()) std::printf("        %s\n", detail.c_str()); }
}
}  // namespace

int main() {
  std::printf("host_gop_clamp_detector_test\n");
  const uint32_t policy = 300;  // 10s @ 30fps; threshold = 150

  // --- clamp: the company case. Self-IDRs every ~30 inputs (fps clamp). Latches after kClampWindow. --
  {
    GopClampDetector d;
    bool latched = false;
    for (uint32_t i = 0; i < kClampWindow; ++i) latched = d.OnEncoderSelfKey(30, policy) || latched;
    check("clamp latches after kClampWindow short self-IDRs", d.clamped && latched);
    // first-latch edge only fires once
    check("first-latch edge fires exactly once", !d.OnEncoderSelfKey(30, policy) && d.clamped);
  }

  // --- not clamp: self-IDRs at the policy length (non-clamp encoder honouring GOP=300) ------------
  {
    GopClampDetector d;
    for (int i = 0; i < 5; ++i) (void)d.OnEncoderSelfKey(300, policy);
    check("policy-length self-IDRs never latch a clamp", !d.clamped && d.shortStreak == 0);
  }

  // --- just above threshold does NOT latch; exactly at threshold does NOT (strictly <) ------------
  {
    GopClampDetector d;
    for (int i = 0; i < kClampWindow + 2; ++i) (void)d.OnEncoderSelfKey(150, policy);  // == threshold
    check("interval == policy/2 is not 'far shorter' (no clamp)", !d.clamped);
  }

  // --- a one-off short interval does not latch; the streak must be consecutive -------------------
  {
    GopClampDetector d;
    (void)d.OnEncoderSelfKey(30, policy);   // short
    (void)d.OnEncoderSelfKey(300, policy);  // policy -> breaks streak
    (void)d.OnEncoderSelfKey(30, policy);   // short again
    check("a broken streak does not latch", !d.clamped, "streak=" + std::to_string(d.shortStreak));
  }

  // --- Reset clears the latch (fresh encoder init) -----------------------------------------------
  {
    GopClampDetector d;
    for (uint32_t i = 0; i < kClampWindow; ++i) (void)d.OnEncoderSelfKey(20, policy);
    check("latched before reset", d.clamped);
    d.Reset();
    check("Reset clears the latch and streak", !d.clamped && d.shortStreak == 0);
  }

  // --- activeKeyint 0 guarded ---------------------------------------------------------------------
  {
    GopClampDetector d;
    check("activeKeyint 0 never latches", !d.OnEncoderSelfKey(1, 0) && !d.clamped);
  }

  // --- the one-time SW transition decision -------------------------------------------------------
  check("transition fires when clamped & not yet done", decide_clamp_sw_transition(true, false));
  check("transition does NOT fire a second time (bounded, no round-trip)",
        !decide_clamp_sw_transition(true, true));
  check("transition does NOT fire without a clamp", !decide_clamp_sw_transition(false, false));

  // --- clamp-fake path: detect -> transition -> recover; and the mutation (transition removed) ----
  // Models the company clamp as short self-IDRs. The real SW reinit is exercised by the bench / C;
  // here the seam is: a transition Resets the detector and the (SW) encoder then honours the policy.
  {
    GopClampDetector d;
    for (uint32_t i = 0; i < kClampWindow; ++i) (void)d.OnEncoderSelfKey(30, policy);  // clamp
    const bool transition = decide_clamp_sw_transition(d.clamped, /*done=*/false);
    check("clamp-fake: a clamp is detected and a transition is decided", d.clamped && transition);
    // Apply the transition: SW honours the GOP -> Reset + policy-length self-IDRs -> no re-latch.
    d.Reset();
    for (int i = 0; i < 5; ++i) (void)d.OnEncoderSelfKey(300, policy);
    check("clamp-fake: after the SW transition the clamp does NOT recur (recovered)", !d.clamped);
  }
  {
    // Mutation: the transition is REMOVED (always "already done"). The clamp persists -- the same
    // "recovered" assertion now FAILS, which is what makes the transition load-bearing.
    GopClampDetector d;
    for (uint32_t i = 0; i < kClampWindow; ++i) (void)d.OnEncoderSelfKey(30, policy);
    const bool transition = decide_clamp_sw_transition(d.clamped, /*done=*/true);  // removed
    // No transition applied -> the encoder keeps clamping -> still latched.
    for (uint32_t i = 0; i < kClampWindow; ++i) (void)d.OnEncoderSelfKey(30, policy);
    check("clamp-fake MUTATION: with the transition removed the clamp persists (load-bearing)",
          !transition && d.clamped);
  }

  if (gFailures == 0) { std::printf("host_gop_clamp_detector_test: PASS\n"); return 0; }
  std::printf("host_gop_clamp_detector_test: FAIL (%d)\n", gFailures);
  return 1;
}
