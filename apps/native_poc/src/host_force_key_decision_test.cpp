// Unit tests for the forced-key decision + rejection back-off/repair (stutter-keyframe r2 F1).
// Pure functions, no MFT: drives decide_force_key + decide_force_key_post_encode + the real
// decide_encode_admission through the rejection sequences Codex named, proving a persistently-
// rejecting ForceKeyFrame cannot turn every input into an admit-always exception under the cap.

#include "host_force_key_decision.hpp"
#include "host_encode_admission.hpp"

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

// A full-queue, cap-on delta admission -- the state F1 must not let a rejected force bypass.
EncodeAdmission admit(bool keyWanted) {
  EncodeAdmissionInputs a;
  a.wireCapActive = true;
  a.keyWanted = keyWanted;      // == forceKeyFrame this tick
  a.servedBootstrap = false;
  a.senderQueueDepth = 5;       // >= senderQueueMax(2): the wire is backlogged
  return decide_encode_admission(a);
}
}  // namespace

int main() {
  std::printf("host_force_key_decision_test\n");

  // --- base decision: force only when wanted, not in flight, not backing off --------------------
  check("first frame forces", decide_force_key({false, true, false, false, false}).forceKeyFrame);
  check("request forces", decide_force_key({true, false, false, false, false}).forceKeyFrame);
  check("in-flight suppresses the force (rides as delta)",
        !decide_force_key({true, false, false, true, false}).forceKeyFrame &&
            decide_force_key({true, false, false, true, false}).keyWanted);
  check("retry back-off suppresses the force (rides as delta)",
        !decide_force_key({true, false, false, false, true}).forceKeyFrame &&
            decide_force_key({true, false, false, false, true}).keyWanted);

  // --- post-encode: S_OK arms the real latch, resets the streak ---------------------------------
  {
    const ForceKeyPostEncodeDecision d =
        decide_force_key_post_encode({true, true, /*setValueOk=*/true, 1'000'000, 4});
    check("S_OK accept arms the in-flight latch", d.armInFlightLatch && !d.setRetryBackoff);
    check("S_OK accept clears the reject streak", d.newRejectStreak == 0 && !d.triggerRepair);
  }
  // --- post-encode: rejection backs off, does NOT arm the latch ---------------------------------
  {
    const ForceKeyPostEncodeDecision d =
        decide_force_key_post_encode({true, true, /*setValueOk=*/false, 1'000'000, 0});
    check("rejection sets the back-off, not the latch",
          d.setRetryBackoff && !d.armInFlightLatch && d.retryAtUs == 1'000'000 + kForceKeyRetryBackoffUs);
    check("rejection increments the streak", d.newRejectStreak == 1 && !d.triggerRepair);
  }
  // --- post-encode: persistent rejection escalates to a repair at the threshold -----------------
  {
    const ForceKeyPostEncodeDecision d =
        decide_force_key_post_encode({true, true, false, 2'000'000, kForceKeyRejectRepairStreak - 1});
    check("rejection streak at threshold triggers a repair", d.triggerRepair);
    check("repair resets the streak", d.newRejectStreak == 0);
  }
  // --- no attempt carries the streak unchanged --------------------------------------------------
  {
    const ForceKeyPostEncodeDecision d = decide_force_key_post_encode({false, false, false, 0, 3});
    check("no force attempt leaves the streak unchanged", d.newRejectStreak == 3 && !d.armInFlightLatch);
  }

  // --- THE F1 scenario: a persistently-rejecting MFT over 5s of 60fps ticks. Attempts must be
  //     BOUNDED by the back-off (not every tick), deltas between attempts must be gated normally
  //     (SkipOverloaded, NOT admit-always), and a repair must fire. ------------------------------
  {
    const uint64_t tickUs = 16'667;  // 60fps
    uint64_t now = 0, retryAt = 0;
    uint32_t streak = 0;
    int attempts = 0, repairs = 0, gatedDeltas = 0, admitAlwaysDeltas = 0;
    for (int i = 0; i < 300; ++i) {  // ~5s
      const bool retryPending = (retryAt != 0 && now < retryAt);
      const ForceKeyDecision fk = decide_force_key({/*forceKeyNext=*/true, false, false, false, retryPending});
      if (fk.forceKeyFrame) {
        ++attempts;
        (void)admit(fk.forceKeyFrame);  // a real force attempt is admitted (correct) -- that is fine
        const ForceKeyPostEncodeDecision pe =
            decide_force_key_post_encode({true, true, /*setValueOk=*/false, now, streak});  // always reject
        streak = pe.newRejectStreak;
        if (pe.setRetryBackoff) retryAt = pe.retryAtUs;
        if (pe.triggerRepair) ++repairs;
      } else {
        // a delta while backing off: its admission must be the NORMAL backlog gate
        if (admit(fk.forceKeyFrame) == EncodeAdmission::SkipOverloaded) ++gatedDeltas;
        else ++admitAlwaysDeltas;
      }
      now += tickUs;
    }
    // ~5s / 300ms back-off => ~16-17 attempts, NOT 300. The regression was every-tick admit-always.
    check("persistent rejection: force attempts are bounded by the back-off (<= 25, not 300)",
          attempts <= 25, "attempts=" + std::to_string(attempts));
    check("persistent rejection: it still keeps retrying (>= 10 attempts)", attempts >= 10,
          "attempts=" + std::to_string(attempts));
    check("persistent rejection: deltas between attempts are gated normally (SkipOverloaded)",
          gatedDeltas > 0 && admitAlwaysDeltas == 0,
          "gated=" + std::to_string(gatedDeltas) + " admitAlways=" + std::to_string(admitAlwaysDeltas));
    check("persistent rejection: escalates to a repair", repairs >= 1,
          "repairs=" + std::to_string(repairs));
  }

  if (gFailures == 0) { std::printf("host_force_key_decision_test: PASS\n"); return 0; }
  std::printf("host_force_key_decision_test: FAIL (%d)\n", gFailures);
  return 1;
}
