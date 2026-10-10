// Unit tests for the window-admission burst ledger (stutter-keyframe r4 B1, corrected per Codex
// 4fba8a1). Pure; injected clock. Proves per-packet admission (B1-1), AU-owned grant (B1-2), warm-up /
// no-cold-credit / downshift (B1-3), and the admission-removed negative control.

#include "host_burst_ledger.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!ok) { ++gFailures; if (!detail.empty()) std::printf("        %s\n", detail.c_str()); }
}
BurstAuId au(uint32_t seq) { return BurstAuId{1, 1, 1, seq, true}; }
}  // namespace

int main() {
  std::printf("host_burst_ledger_test\n");
  const uint64_t cap = 1'500'000;   // r=187500, 2r=375000
  const uint64_t r = cap / 8;
  const uint64_t twoR = 2 * r;

  // The window budget ramps from 0 over the first 2s (R3 mixed-rate / warm-up), so these admission tests
  // run at W == 2s, where a constant-rate ledger's budget is the full flat 2r.
  const uint64_t W = kBurstWindowUs;

  // --- B1-1: every datagram passes the 2s window admission; the 300000 + 75000 + 20000 sequence is
  //     blocked at the replay. ---------------------------------------------------------------------
  {
    BurstLedger L; L.SetRate(cap, 0);  // started at t=0; warm by t=W
    check("budget is the flat 2r once warm", L.window_bytes(W) == 0);  // (no sends yet)
    // 300000 of recent history (as committed sends), all inside (W-2s, W].
    L.CommitSent(W, 300'000);
    check("window at 300000 after history", L.window_bytes(W) == 300'000);
    // a 75000 grant datagram reserves+commits -> window 375000 (== 2r, admitted).
    check("reserve 75000 fits (300000+75000 == 2r)", L.Reserve(W, 75'000));
    L.CommitSent(W, 75'000);
    check("window now exactly 2r", L.window_bytes(W) == twoR);
    // a further 20000 (replay or normal) is REFUSED -- 395000 > 2r. This is the counter-example.
    check("B1-1: a further 20000 is REFUSED by the window (395000 > 2r)", !L.Reserve(W, 20'000));
    check("peak window stayed <= 2r", L.max_window_bytes() <= twoR);
  }
  // --- CancelUnsent returns a reservation; a disabled cap always permits -------------------------
  {
    BurstLedger L; L.SetRate(cap, 0);
    check("reserve near full", L.Reserve(W, twoR));
    check("second reserve refused (pending full)", !L.Reserve(W, 1));
    L.CancelUnsent(twoR);
    check("after cancel, reserve fits again", L.Reserve(W, twoR));
    BurstLedger off; off.SetRate(0, 0);
    check("disabled cap permits any reserve", off.Reserve(1, 9'999'999));
  }
  // --- R3 cold ramp: a cold ledger has NO full 2r budget -- a burst that would fit when warm is
  //     refused right after start (no retroactive/cold credit). ------------------------------------
  {
    BurstLedger L; L.SetRate(cap, 0);  // started at t=0
    check("[R3] at t=0.5s the budget is only ~r*0.5s, so a 2r reserve is refused",
          !L.Reserve(500'000, twoR));
    check("[R3] the same 2r reserve fits once warm at 2s", L.Reserve(W, twoR));
  }
  // --- R3 upshift gives NO retroactive credit: raising the rate only earns budget for time AFTER the
  //     change, so a warm ledger at the OLD rate cannot immediately burst at the new 2r. -----------
  {
    BurstLedger L; L.SetRate(cap, 0);         // r=187500 from t=0; warm at t=W
    L.SetRate(2 * cap, W);                     // upshift to 2r_old at t=2s
    // Right after the upshift the window still integrates the old rate for the preceding 2s; only an
    // instant has passed at the new rate, so the budget is ~2r_old (375000), NOT 2*(2r_old)=750000.
    check("[R3] no retroactive credit: just-upshifted budget still ~old 2r, 500000 refused",
          !L.Reserve(W, 500'000));
    check("[R3] old 2r still fits at the upshift instant", L.Reserve(W, twoR));
  }

  // --- B1-3 warm-up: no grant on a cold ledger; only after kBurstWarmupUs ------------------------
  {
    BurstLedger L; L.SetRate(cap, 1'000'000);  // started at t=1s
    check("no grant during warm-up", L.GrantForIdr(1'500'000, au(1)) == 0);  // 0.5s in
    check("grant after warm-up", L.GrantForIdr(1'000'000 + kBurstWarmupUs + 1, au(1)) > 0);
  }
  // --- grant caps + one-at-a-time -----------------------------------------------------------------
  {
    BurstLedger L; L.SetRate(cap, 0);
    const uint64_t t = kBurstWarmupUs + 1;
    const uint64_t g = L.GrantForIdr(t, au(1));
    check("grant == min(256KiB, r) = r at 1.5Mbps", g == r, "g=" + std::to_string(g));
    check("second grant while active refused", L.GrantForIdr(t, au(2)) == 0);
  }
  // --- B1-2 owner: a foreign AU cannot spend or end A's grant ------------------------------------
  {
    BurstLedger L; L.SetRate(cap, 0);
    const uint64_t t = kBurstWarmupUs + 1;
    (void)L.GrantForIdr(t, au(1));
    check("owner A has coverage", L.GrantCoverage(au(1), 1000) == 1000);
    check("foreign AU B has NO coverage", L.GrantCoverage(au(2), 1000) == 0);
    L.EndGrant(au(2));  // foreign end is ignored
    check("foreign EndGrant did not release A's grant", L.GrantCoverage(au(1), 1000) == 1000);
    L.EndGrant(au(1));  // the owner ends it
    check("owner EndGrant released the grant", L.GrantCoverage(au(1), 1000) == 0);
  }
  // --- B1-2 no re-grant to the same AU (cooldown), >= 1s apart otherwise --------------------------
  {
    BurstLedger L; L.SetRate(cap, 0);
    uint64_t t = kBurstWarmupUs + 1;
    (void)L.GrantForIdr(t, au(1));
    L.EndGrant(au(1));
    check("same AU is NOT re-granted after its grant ends", L.GrantForIdr(t + kBurstMinIntervalUs + 1, au(1)) == 0);
    check("a different AU >=1s later IS granted", L.GrantForIdr(t + kBurstMinIntervalUs + 1, au(2)) > 0);
  }
  // --- B1-3 / R3 downshift re-limits the active grant to the NEW per-key cap (not just the window) ---
  {
    BurstLedger L; L.SetRate(6'000'000, 0);  // r=750000, grant up to 256KiB
    const uint64_t t = kBurstWarmupUs + 1;
    const uint64_t g = L.GrantForIdr(t, au(1));
    check("6Mbps grant == min(256KiB, r) = 256KiB", g == kBurstGrantMaxBytes);
    L.SetRate(1'500'000, t);  // downshift 6M->1.5M: new per-key = min(256KiB, 187500) = 187500
    const uint64_t cov = L.GrantCoverage(au(1), 9'999'999);
    check("[R3] downshift 6M->1.5M re-limits the grant to the new per-key 187500",
          cov <= r, "cov=" + std::to_string(cov) + " r=" + std::to_string(r));
  }
  // --- R3 downshift accounts for ALREADY-SPENT grant: a grant that has spent part of its budget is
  //     re-limited so spent + remaining <= the new per-key cap. ------------------------------------
  {
    BurstLedger L; L.SetRate(6'000'000, 0);  // r=750000
    const uint64_t t = kBurstWarmupUs + 1;
    (void)L.GrantForIdr(t, au(1));                 // granted 256KiB
    L.DebitGrant(au(1), 150'000);                  // spent 150000; remaining ~112144
    L.SetRate(1'500'000, t);                       // downshift: new per-key 187500, spent 150000
    const uint64_t cov = L.GrantCoverage(au(1), 9'999'999);
    check("[R3] spent 150000 + remaining <= new per-key 187500 (remaining <= 37500)",
          cov <= (r - 150'000), "cov=" + std::to_string(cov));
  }

  // --- S2 (Codex 72a22d2): a rate DOWNSHIFT leaves transition debt. The conservative admission budget
  //     min(mixed integral, new flat 2r) never lets a NEW admission push the window past the new 2r, so
  //     the "571000 committed vs 501375 shrunk integral" violation Codex arithmetic-derived cannot form.
  //     Numbers mirror the review (datagram cost 1000): 6M burst 262000, downshift to 1.5M (2r=375000).
  {
    BurstLedger L; L.SetRate(6'000'000, 0);  // 6M from t=0; warm by t=W
    uint64_t t = W;
    check("S2: a 262000 burst is admitted under 6M (262000 < 2*r6=1.5M)", L.Reserve(t, 262'000));
    L.CommitSent(t, 262'000);
    L.SetRate(1'500'000, t);  // downshift: new 2r=375000; integral still ~1.5M but admission is capped at 2r_new
    check("S2: new sends are admitted only up to the new 2r total (375000-262000=113000)", L.Reserve(t, 113'000));
    L.CommitSent(t, 113'000);
    check("S2: the window is exactly the new 2r, NOT the integral's ~571000", L.window_bytes(t) == 375'000);
    check("S2: a further new reserve is REFUSED -- debt counts against the new cap (no integral credit)",
          !L.Reserve(t, 1'000));
    // STALL: new sends stay blocked while the debt occupies the new budget -- a deliberate safe backoff on
    // a downshift. Here all debt was committed at t, so it blocks until it expires one window later.
    check("S2: new sends still blocked 1.5s into the transition (debt not yet expired)",
          !L.Reserve(t + 1'500'000, 1'000));
    // As the integral shrinks past this point it NEVER drops below the committed window, because we never
    // admitted past 375000 (the whole point): no retroactive violation forms.
    check("S2: the window (<=375000) is never exceeded by the shrunk integral",
          L.window_bytes(t + 1'800'000) <= 375'000);
    // Once the debt fully ages out (one window later), a full new-rate 2s window is available at the new 2r.
    const uint64_t t3 = t + kBurstWindowUs + 1;
    check("S2: after the debt drains, the new 2r is fully available again",
          L.window_bytes(t3) == 0 && L.Reserve(t3, 375'000));
  }

  // --- NEGATIVE CONTROL: without the admission (CommitSent directly, no Reserve gate) the 2s window
  //     is exceeded -- the window admission is load-bearing. ----------------------------------------
  {
    BurstLedger L; L.SetRate(cap, 0);
    const uint64_t frameUs = 33'333;
    const uint64_t deltaBytes = (r * frameUs / 1'000'000) * 3 / 10;
    uint64_t maxW = 0;
    for (int tick = 0; tick < 300; ++tick) {
      const uint64_t t = static_cast<uint64_t>(tick) * frameUs;
      L.CommitSent(t, deltaBytes);                                   // deltas, no admission
      if (tick > 0 && tick % 30 == 0) L.CommitSent(t, 200'000);      // a full IDR, no admission
      maxW = std::max(maxW, L.window_bytes(t));
    }
    check("[neg] admission removed (CommitSent w/o Reserve) VIOLATES 2r", maxW > twoR,
          "maxW=" + std::to_string(maxW) + " 2r=" + std::to_string(twoR));
  }

  if (gFailures == 0) { std::printf("host_burst_ledger_test: PASS\n"); return 0; }
  std::printf("host_burst_ledger_test: FAIL (%d)\n", gFailures);
  return 1;
}
