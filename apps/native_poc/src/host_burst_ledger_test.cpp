// Unit tests for the conditional IDR burst + 2s rolling-window ledger (stutter-keyframe r4 B1).
// Pure; injected clock via explicit nowUs. Proves the grant caps, the window invariant A(t-2s,t]<=2r,
// and the ledger-removed negative control (the same invariant FAILS without the window cap).

#include "host_burst_ledger.hpp"

#include <algorithm>
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
}  // namespace

int main() {
  std::printf("host_burst_ledger_test\n");
  const uint64_t cap = 1'500'000;       // 1.5 Mbps
  const uint64_t r = cap / 8;           // 187500 bytes/s
  const uint64_t twoR = 2 * r;          // 375000 bytes / 2s

  // --- grant caps -------------------------------------------------------------------------------
  {
    BurstLedger L; L.SetRate(cap);
    // empty window: grant = min(auBytes, min(256KiB, r*1s), window_remaining=2r).
    const uint64_t g = L.GrantForIdr(0, 1'000'000);  // a 1MB AU
    check("grant capped to min(256KiB, r*1s) = r at 1.5Mbps", g == r, "g=" + std::to_string(g));
    check("a grant is now active (one at a time)", L.grant_active());
    check("second grant while active is refused", L.GrantForIdr(0, 1'000'000) == 0);
    L.EndGrant();
    check("grant <1s after the last is refused", L.GrantForIdr(500'000, 1'000'000) == 0);
    check("grant >=1s after the last is allowed", L.GrantForIdr(1'000'001, 1'000'000) > 0);
  }
  // --- window full -> no grant (strict) ----------------------------------------------------------
  {
    BurstLedger L; L.SetRate(cap);
    L.Record(0, twoR);  // fill the 2s window exactly
    check("a full 2s window leaves no burst (grant 0)", L.GrantForIdr(1'000'001, 200'000) == 0);
    check("window_remaining is 0 when full", L.window_remaining(1'000'001) == 0);
  }
  // --- peak cap: max(R, min(4R, 12Mbps)) ---------------------------------------------------------
  {
    BurstLedger L; L.SetRate(cap);  // 4R = 6Mbps < 12Mbps -> peak = 6Mbps
    check("peak = min(4R,12Mbps) when 4R<12Mbps", L.peak_bytes_per_s() == 6'000'000 / 8);
    BurstLedger H; H.SetRate(6'000'000);  // 4R = 24Mbps -> capped to 12Mbps
    check("peak = 12Mbps cap when 4R>12Mbps", H.peak_bytes_per_s() == 12'000'000 / 8);
  }

  // --- THE window invariant: a clamp scenario (light deltas + periodic large IDRs). With the ledger
  //     every 2s window stays <= 2r; the IDR bursts only into the freed budget. -------------------
  const uint64_t frameUs = 33'333;                 // 30fps
  const uint64_t deltaBytes = (r * frameUs / 1'000'000) * 3 / 10;  // light deltas (clamp: many skipped)
  const uint64_t idrBytes = 200'000;               // a large clamped IDR
  {
    BurstLedger L; L.SetRate(cap);
    uint64_t maxWindow = 0, idrBacklog = 0;
    for (int tick = 0; tick < 450; ++tick) {  // 15s
      const uint64_t t = static_cast<uint64_t>(tick) * frameUs;
      L.Record(t, deltaBytes);
      if (tick > 0 && tick % 30 == 0) {  // a clamped self-IDR ~every 1s
        const uint64_t g = L.GrantForIdr(t, idrBytes);
        if (g > 0) { L.Record(t, g); L.DebitGrant(g); }
        idrBacklog += (idrBytes - g);  // the part over the grant is paced later, within budget
      }
      if (idrBacklog > 0) {  // drain the IDR backlog paced, never beyond the window budget
        const uint64_t room =
            std::min<uint64_t>({idrBacklog, r * frameUs / 1'000'000, L.window_remaining(t)});
        if (room > 0) { L.Record(t, room); idrBacklog -= room; }
      }
      maxWindow = std::max(maxWindow, L.window_bytes(t));
    }
    // <= 2r plus at most one datagram of counting error.
    check("window invariant: every 2s window <= 2r (with the ledger)", maxWindow <= twoR + 1500,
          "maxWindow=" + std::to_string(maxWindow) + " 2r=" + std::to_string(twoR));
  }
  // --- NEGATIVE CONTROL: remove the ledger (IDR sends its full bytes, no window cap) -> the same 2s
  //     invariant is VIOLATED. This is what makes the window ledger load-bearing. -----------------
  {
    BurstLedger L; L.SetRate(cap);  // used only as the 2s-window summer here; no grant cap applied
    uint64_t maxWindow = 0;
    for (int tick = 0; tick < 450; ++tick) {
      const uint64_t t = static_cast<uint64_t>(tick) * frameUs;
      L.Record(t, deltaBytes);
      if (tick > 0 && tick % 30 == 0) L.Record(t, idrBytes);  // FULL IDR, no GrantForIdr window check
      maxWindow = std::max(maxWindow, L.window_bytes(t));
    }
    check("[neg] ledger removed (full IDR, no window cap) VIOLATES 2r", maxWindow > twoR,
          "maxWindow=" + std::to_string(maxWindow) + " 2r=" + std::to_string(twoR));
  }

  if (gFailures == 0) { std::printf("host_burst_ledger_test: PASS\n"); return 0; }
  std::printf("host_burst_ledger_test: FAIL (%d)\n", gFailures);
  return 1;
}
