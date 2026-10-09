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

  // --- B1-1: every datagram passes the 2s window admission; the 300000 + 75000 + 20000 sequence is
  //     blocked at the replay. ---------------------------------------------------------------------
  {
    BurstLedger L; L.SetRate(cap, 0);
    // 300000 of recent history (as committed sends).
    L.CommitSent(1, 300'000);
    check("window at 300000 after history", L.window_bytes(1) == 300'000);
    // a 75000 grant datagram reserves+commits -> window 375000 (== 2r, admitted).
    check("reserve 75000 fits (300000+75000 == 2r)", L.Reserve(1, 75'000));
    L.CommitSent(1, 75'000);
    check("window now exactly 2r", L.window_bytes(1) == twoR);
    // a further 20000 (replay or normal) is REFUSED -- 395000 > 2r. This is the counter-example.
    check("B1-1: a further 20000 is REFUSED by the window (395000 > 2r)", !L.Reserve(1, 20'000));
    check("peak window stayed <= 2r", L.max_window_bytes() <= twoR);
  }
  // --- CancelUnsent returns a reservation; a disabled cap always permits -------------------------
  {
    BurstLedger L; L.SetRate(cap, 0);
    check("reserve near full", L.Reserve(1, twoR));
    check("second reserve refused (pending full)", !L.Reserve(1, 1));
    L.CancelUnsent(twoR);
    check("after cancel, reserve fits again", L.Reserve(1, twoR));
    BurstLedger off; off.SetRate(0, 0);
    check("disabled cap permits any reserve", off.Reserve(1, 9'999'999));
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
  // --- B1-3 downshift re-limits the active grant --------------------------------------------------
  {
    BurstLedger L; L.SetRate(6'000'000, 0);  // r=750000, grant up to 750000
    const uint64_t t = kBurstWarmupUs + 1;
    const uint64_t g = L.GrantForIdr(t, au(1));
    check("6Mbps grant == min(256KiB, r) = 256KiB", g == kBurstGrantMaxBytes);
    L.SetRate(1'500'000, t);  // downshift: new 2r = 375000, window ~0 -> grant re-limited to <= 375000
    check("downshift re-limits the active grant coverage", L.GrantCoverage(au(1), 9'999'999) <= twoR);
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
