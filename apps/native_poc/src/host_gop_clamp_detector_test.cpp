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

  // --- G: classify_self_idr directly -------------------------------------------------------------
  check("G: a non-forced real IDR with a known interval is a valid self-IDR",
        classify_self_idr(true, false, 400, 1, 100, 1).valid);
  check("G: a forced-key IDR is a boundary, not a sample",
        classify_self_idr(true, true, 400, 1, 100, 1).boundary &&
            !classify_self_idr(true, true, 400, 1, 100, 1).valid);
  check("G: a non-NAL5 (CleanPoint-only) key is ignored entirely",
        !classify_self_idr(false, false, 400, 1, 100, 1).valid &&
            !classify_self_idr(false, false, 400, 1, 100, 1).boundary);
  check("G: an epoch change STARTS a baseline, not a cross-epoch interval",
        classify_self_idr(true, false, 400, 2, 100, 1).baseline &&
            !classify_self_idr(true, false, 400, 2, 100, 1).valid);
  check("G: lost provenance (ordinal 0) is a boundary", classify_self_idr(true, false, 0, 1, 100, 1).boundary);

  // mirror the _au.cpp feed: valid -> interval + baseline; baseline -> set baseline (no interval);
  // boundary -> NoteBoundary + invalidate the baseline (next self starts fresh).
  const auto feed = [](GopClampDetector& d, uint64_t& lastOrd, uint64_t& lastEp, uint32_t keyint,
                       bool rawIdr, bool forced, uint64_t ordinal, uint64_t epoch) {
    const SelfIdrSample s = classify_self_idr(rawIdr, forced, ordinal, epoch, lastOrd, lastEp);
    if (s.valid) { (void)d.OnEncoderSelfKey(s.intervalInputs, keyint); lastOrd = ordinal; lastEp = epoch; }
    else if (s.baseline) { lastOrd = ordinal; lastEp = epoch; }
    else if (s.boundary) { d.NoteBoundary(); lastOrd = 0; lastEp = 0; }
  };

  // G1 counter-example: a NORMAL GOP=300 encoder whose self-IDRs ride synthetic/idle refresh inputs.
  // The ordinal (incl synthetic) makes the interval ~300, so it is NOT misjudged as a clamp -- the bug
  // the old realInputsSinceKey wiring had (small host count for synthetic-refresh self-IDRs).
  {
    GopClampDetector d; uint64_t lo = 0, le = 0;
    for (uint64_t ord = 300; ord <= 1800; ord += 300) feed(d, lo, le, 300, true, false, ord, 1);
    check("G1: normal GOP + idle/synthetic refresh is NOT detected as clamp", !d.clamped);
  }
  // G2 counter-example: forced keys interleaved. Each forced key is a boundary (re-baseline + streak
  // reset), so a short streak cannot accrue across them.
  {
    GopClampDetector d; uint64_t lo = 0, le = 0;
    // self-IDR at 300, then a host forced key at 320, then self-IDR at 620 (300 after the forced key).
    feed(d, lo, le, 300, true, false, 300, 1);
    feed(d, lo, le, 300, true, /*forced=*/true, 320, 1);
    feed(d, lo, le, 300, true, false, 620, 1);
    feed(d, lo, le, 300, true, /*forced=*/true, 650, 1);
    feed(d, lo, le, 300, true, false, 950, 1);
    check("G2: forced keys mixed in are NOT detected as clamp (boundaries break the streak)", !d.clamped);
  }
  // positive: a REAL clamp -- non-forced self-IDRs every ~30 inputs, same epoch -> latches.
  {
    GopClampDetector d; uint64_t lo = 0, le = 0;
    for (uint64_t ord = 30; ord <= 300; ord += 30) feed(d, lo, le, 300, true, false, ord, 1);
    check("G: a real clamp pattern (non-forced self-IDRs every ~30) IS detected", d.clamped);
  }
  // epoch change breaks a forming streak before it latches.
  {
    GopClampDetector d; uint64_t lo = 0, le = 0;
    feed(d, lo, le, 300, true, false, 30, 1);
    feed(d, lo, le, 300, true, false, 60, 1);   // streak building (2 short)
    feed(d, lo, le, 300, true, false, 30, 2);   // epoch change -> boundary -> streak reset
    feed(d, lo, le, 300, true, false, 60, 2);   // 1 short in the new epoch
    check("G: an epoch change breaks the streak before it latches", !d.clamped);
  }

  if (gFailures == 0) { std::printf("host_gop_clamp_detector_test: PASS\n"); return 0; }
  std::printf("host_gop_clamp_detector_test: FAIL (%d)\n", gFailures);
  return 1;
}
