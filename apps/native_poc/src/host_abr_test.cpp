// Pins the ABR profile and M9 level decisions of RateControlState (host_abr.hpp): the 4s warmup, the
// sparse-offer / static-scene hold, the pressure and good-second counters, the cooldowns and the
// up/down thresholds in both tuning modes. Time is an argument, so a session is a loop of one-second
// ticks with hand-made client metrics; nothing here touches the encoder or the network.
//
// Host split refactor Phase 2-T1 (2026-08-26).

#include "host_abr.hpp"
#include "host_client_metrics.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using remote60::native_poc::AbrDecision;
using remote60::native_poc::AbrInputs;
using remote60::native_poc::M9Decision;
using remote60::native_poc::M9Inputs;
using remote60::native_poc::RateControlState;
using namespace remote60::native_poc;

namespace {

int gFailures = 0;

void expect(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++gFailures;
  }
}

constexpr uint64_t kSec = 1000000ULL;
constexpr uint64_t kStartUs = 10 * kSec;
constexpr uint32_t kFps = 30;

RateControlState MakeAbr(bool qualityFirst) {
  RateControlState r;
  r.abrEnabled = true;
  r.abrQualityFirst = qualityFirst;
  r.abrHasMidProfile = true;
  r.abrHasLowProfile = true;
  r.abrHasLowerResolution = true;
  r.abrHighBitrate = 10000000;
  r.abrMidBitrate = 7500000;
  r.abrLowBitrate = 5500000;
  return r;
}

// A second in which the client decoded the full rate with low latency. A viewer that reports its
// decode side (the Windows viewer): the session has seen reports, with decode fields.
AbrInputs Healthy() {
  AbrInputs in;
  in.metricsFresh = true;
  in.clientEverReported = true;
  in.clientDecodeReported = true;
  in.clDecodedFpsX100 = kFps * 100;
  in.clAvgLatencyUs = 40000;
  in.clAvgDecodeTailUs = 20000;
  in.cb2eAvgUs = 5000;
  in.sentFrames = kFps;
  in.staticMode = false;
  in.activeFps = kFps;
  in.startUs = kStartUs;
  return in;
}
// Clearly over the severe thresholds of both modes (150/170ms latency, 110/140ms tail).
AbrInputs Severe() {
  AbrInputs in = Healthy();
  in.clAvgLatencyUs = 180000;
  in.clAvgDecodeTailUs = 145000;
  in.clDecodedFpsX100 = kFps * 40;
  return in;
}
// Over the emergency thresholds (220/260ms latency).
AbrInputs Emergency() {
  AbrInputs in = Severe();
  in.clAvgLatencyUs = 270000;
  in.clAvgDecodeTailUs = 200000;
  return in;
}

// Runs `seconds` ticks of `in` starting at `t0`, committing every switch the way the stage does; returns
// the profile after the last tick and records the tick (1-based) of the first switch in *switchAt.
int RunAbr(RateControlState& r, const AbrInputs& in, uint64_t t0, int seconds, int* switchAt = nullptr,
           std::string* firstReason = nullptr) {
  if (switchAt) *switchAt = 0;
  for (int i = 1; i <= seconds; ++i) {
    const uint64_t t = t0 + static_cast<uint64_t>(i) * kSec;
    const AbrDecision d = r.DecideAbrProfile(in, t);
    if (d.targetProfile != r.abrProfile) {
      if (switchAt && *switchAt == 0) {
        *switchAt = i;
        if (firstReason) *firstReason = d.reason;
      }
      r.CommitAbrProfile(d.targetProfile, false, t);
    }
  }
  return r.abrProfile;
}

void TestAbrWarmupHoldsEverything() {
  RateControlState r = MakeAbr(false);
  // Ticks at start+1s..start+3s are inside the 4s warmup: severe evidence must not move the profile.
  const int p = RunAbr(r, Severe(), kStartUs, 3);
  expect(p == 0, "abr: warmup holds high under severe pressure");
  expect(r.abrSeverePressureSeconds == 0, "abr: warmup accumulates no pressure");
}

void TestAbrHighToMidSevereThenMidToLow() {
  RateControlState r = MakeAbr(false);
  int at = 0;
  std::string reason;
  // Warm: ticks at start+4s, +5s. Default mode demotes after 2 consecutive severe seconds.
  int p = RunAbr(r, Severe(), kStartUs + 3 * kSec, 2, &at, &reason);
  expect(p == 1 && at == 2 && reason == "high_to_mid_severe", "abr: high->mid after 2 severe seconds (default)");
  const uint64_t switchedAtUs = kStartUs + 5 * kSec;
  expect(r.abrCooldownUntilUs == switchedAtUs + 4 * kSec, "abr: 4s cooldown armed at the switch");
  expect(r.abrSeverePressureSeconds == 0 && r.abrGoodSeconds == 0, "abr: counters reset by the commit");
  // Cooldown: severe seconds at +6..+8 accumulate pressure but cannot switch; +9s (== cooldown end)
  // sees 4 severe seconds >= midToLowSevereSec(3) and demotes to low.
  p = RunAbr(r, Severe(), switchedAtUs, 4, &at, &reason);
  expect(p == 2 && at == 4 && reason == "mid_to_low_severe", "abr: mid->low only once the cooldown has passed");
}

void TestAbrQualityFirstNeedsThreeSevereSeconds() {
  RateControlState r = MakeAbr(true);
  int at = 0;
  std::string reason;
  const int p = RunAbr(r, Severe(), kStartUs + 3 * kSec, 3, &at, &reason);
  expect(p == 1 && at == 3 && reason == "high_to_mid_severe", "abr: quality-first demotes after 3 severe seconds");
}

void TestAbrSparseAndStaticSecondsHold() {
  RateControlState r = MakeAbr(false);
  AbrInputs sparse = Severe();
  sparse.sentFrames = 1;  // < max(2, fps/2): no usable evidence this second
  int p = RunAbr(r, sparse, kStartUs + 3 * kSec, 10);
  expect(p == 0 && r.abrSeverePressureSeconds == 0, "abr: sparse host offer neither demotes nor accumulates");
  AbrInputs still = Severe();
  still.staticMode = true;
  p = RunAbr(r, still, kStartUs + 13 * kSec, 10);
  expect(p == 0 && r.abrModeratePressureSeconds == 0, "abr: static scene holds the profile");
  // A sparse second in the middle of real pressure resets the streak.
  RunAbr(r, Severe(), kStartUs + 23 * kSec, 1);
  expect(r.abrSeverePressureSeconds == 1, "abr: one severe second counted");
  RunAbr(r, sparse, kStartUs + 24 * kSec, 1);
  expect(r.abrSeverePressureSeconds == 0 && r.abrProfile == 0, "abr: a sparse second resets the severe streak");
}

void TestAbrEmergencyGoesStraightToLow() {
  RateControlState r = MakeAbr(false);
  int at = 0;
  std::string reason;
  const int p = RunAbr(r, Emergency(), kStartUs + 3 * kSec, 1, &at, &reason);
  expect(p == 2 && at == 1 && reason == "client_emergency", "abr: emergency latency drops high->low in one second");
}

void TestAbrHostFallbackEvidence() {
  RateControlState r = MakeAbr(false);
  AbrInputs in = Healthy();
  in.metricsFresh = false;  // no client metrics: host-side callback->encode age is the only evidence
  in.cb2eAvgUs = 95000;     // > 90ms severe (default mode)
  int at = 0;
  std::string reason;
  const int p = RunAbr(r, in, kStartUs + 3 * kSec, 2, &at, &reason);
  expect(p == 1 && at == 2 && reason == "high_to_mid_severe", "abr: stale metrics fall back to host evidence");
}

void TestAbrRecoversMidToHighAfterGoodSeconds() {
  RateControlState r = MakeAbr(false);
  r.abrProfile = 1;
  int at = 0;
  std::string reason;
  // Default mode: 8 good seconds; the 8th tick switches.
  int p = RunAbr(r, Healthy(), kStartUs + 3 * kSec, 8, &at, &reason);
  expect(p == 0 && at == 8 && reason == "client_stable_high", "abr: mid->high after 8 good seconds (default)");
  RateControlState q = MakeAbr(true);
  q.abrProfile = 1;
  p = RunAbr(q, Healthy(), kStartUs + 3 * kSec, 12, &at, &reason);
  expect(p == 0 && at == 12, "abr: quality-first needs 12 good seconds");
  // A single bad second in between restarts the good streak.
  RateControlState s = MakeAbr(false);
  s.abrProfile = 1;
  RunAbr(s, Healthy(), kStartUs + 3 * kSec, 5);
  expect(s.abrGoodSeconds == 5, "abr: five good seconds counted");
  AbrInputs meh = Healthy();
  meh.clAvgLatencyUs = 80000;  // not good enough for mid->high (needs < 75ms)
  RunAbr(s, meh, kStartUs + 8 * kSec, 1);
  expect(s.abrGoodSeconds == 0 && s.abrProfile == 1, "abr: a mediocre second resets the good streak");
}

void TestAbrLowToMidAndLowToHighWithoutMid() {
  RateControlState r = MakeAbr(false);
  r.abrProfile = 2;
  int at = 0;
  std::string reason;
  AbrInputs ok = Healthy();
  ok.clAvgLatencyUs = 85000;      // good enough for low->mid (< 90ms) but not mid->high
  ok.clAvgDecodeTailUs = 60000;   // < 65ms
  ok.clDecodedFpsX100 = kFps * 86; // >= 85% of target
  int p = RunAbr(r, ok, kStartUs + 3 * kSec, 5, &at, &reason);
  expect(p == 1 && at == 5 && reason == "client_stable_mid", "abr: low->mid after 5 good seconds");
  RateControlState noMid = MakeAbr(false);
  noMid.abrHasMidProfile = false;
  noMid.abrProfile = 2;
  p = RunAbr(noMid, ok, kStartUs + 3 * kSec, 5, &at, &reason);
  expect(p == 0 && at == 5, "abr: without a mid profile low recovers straight to high");
}

void TestAbrWithoutLowerProfilesHolds() {
  RateControlState r = MakeAbr(false);
  r.abrHasMidProfile = false;
  r.abrHasLowProfile = false;
  const int p = RunAbr(r, Emergency(), kStartUs + 3 * kSec, 6);
  expect(p == 0, "abr: nothing to demote to -> high holds");
}

// P4: a low profile entered during motion must climb back on a still desktop with a clean link,
// otherwise text stays soft at 720p forever while reading. A still desktop with a bad link must not.
void TestAbrStaticRecoveryPromotesFromLow() {
  RateControlState r = MakeAbr(false);
  r.abrProfile = 2;
  AbrInputs still = Healthy();
  still.staticMode = true;  // sparse: no down verdict, but P4 must still recover on a clean link
  int at = 0;
  std::string reason;
  const int p = RunAbr(r, still, kStartUs + 3 * kSec, 8, &at, &reason);
  expect(p == 1 && at == 8 && reason == "static_recovery",
         "abr(P4): static screen recovers low->mid on a clean link after 8s");

  RateControlState bad = MakeAbr(false);
  bad.abrProfile = 2;
  AbrInputs stillBad = Healthy();
  stillBad.staticMode = true;
  stillBad.clAvgLatencyUs = 200000;  // congested: sparseHealthy false, no recovery
  const int pb = RunAbr(bad, stillBad, kStartUs + 3 * kSec, 12);
  expect(pb == 2, "abr(P4): static screen with high latency does not recover");
  RateControlState stale = MakeAbr(false);
  stale.abrProfile = 2;
  still.metricsFresh = false;
  expect(RunAbr(stale, still, kStartUs + 3 * kSec, 20) == 2,
         "abr: missing feedback is not evidence to increase traffic on a quiet link");
}

// P6: sustained client packet loss is congestion evidence on its own, even when latency and fps
// still read fine (loss shows up before the queue backs up).
void TestAbrClientLossTriggersDemotion() {
  RateControlState r = MakeAbr(false);
  AbrInputs lossy = Healthy();
  lossy.clUdpDropPm = 150;  // > severeDropPm (100): severe by loss alone
  int at = 0;
  std::string reason;
  const int p = RunAbr(r, lossy, kStartUs + 3 * kSec, 2, &at, &reason);
  expect(p == 1 && at == 2 && reason == "high_to_mid_severe",
         "abr(P6): sustained packet loss demotes even with healthy latency/fps");
}

// P7: client feedback goes silent (metricsFresh=false) while the host is still actively sending --
// the relay-collapse signature -- must demote, because neither client metrics nor cb2e can show it.
// An idle (sparse) stale second must not demote.
void TestAbrStaleFeedbackDuringActiveSendDemotes() {
  RateControlState r = MakeAbr(false);
  AbrInputs stale = Healthy();
  stale.metricsFresh = false;  // client went silent under congestion
  stale.cb2eAvgUs = 5000;      // host encoding still fine -> only P7 catches this
  int at = 0;
  std::string reason;
  // 2 stale seconds arm staleActiveCongestion, then 2 severe seconds (highToMidSevereSec) demote:
  // first severe second is tick 2, second is tick 3 -> switch at tick 3.
  const int p = RunAbr(r, stale, kStartUs + 3 * kSec, 3, &at, &reason);
  expect(p == 1 && at == 3 && reason == "high_to_mid_severe",
         "abr(P7): stale feedback while actively sending demotes");

  RateControlState idle = MakeAbr(false);
  AbrInputs staleIdle = stale;
  staleIdle.sentFrames = 1;  // sparse: not actively sending -> not treated as congestion
  const int pi = RunAbr(idle, staleIdle, kStartUs + 3 * kSec, 6);
  expect(pi == 0, "abr(P7): stale feedback while idle/sparse does not demote");
}

// P7: the measured 14:46 collapse -- fresh metrics, but the client decodes far below target while
// the host sends a full cadence (relay dropping most frames). The few frames that arrive look
// low-latency, so this must demote on the fps shortfall alone.
void TestAbrLowClientFpsDemotesDespiteLowLatency() {
  RateControlState r = MakeAbr(false);
  AbrInputs lying = Healthy();
  lying.clDecodedFpsX100 = kFps * 8;  // ~8% of target: most frames lost on the wire
  lying.clAvgLatencyUs = 40000;       // the few that arrive are fine
  lying.clAvgDecodeTailUs = 20000;
  int at = 0;
  std::string reason;
  const int p = RunAbr(r, lying, kStartUs + 3 * kSec, 2, &at, &reason);
  expect(p == 1 && at == 2 && reason == "high_to_mid_severe",
         "abr(P7): low client fps under active send demotes despite low latency");

  // Interactive/low-motion use: the client decodes 20 of 60 fps with fine latency. That is NOT a
  // collapse (the floor is ~12%) and must hold, or ABR flaps high<->mid on a text window. (#345)
  RateControlState ok = MakeAbr(false);
  AbrInputs interactive = Healthy();
  interactive.clDecodedFpsX100 = kFps * 33;  // ~33% of target: normal, not congestion
  const int pi = RunAbr(ok, interactive, kStartUs + 3 * kSec, 6);
  expect(pi == 0, "abr(P7): a 20-of-60 fps interactive dip does not demote");
}

// ---------------- M9 ----------------

RateControlState MakeM9() {
  RateControlState r;
  r.m9Enabled = true;
  r.m9Apply = true;
  r.m9CooldownSec = 4;
  r.m9DownRequireSec = 2;
  r.m9UpRequireSec = 3;
  r.m9DecodedFpsFloorX100 = 2000;
  r.m9DecodedFpsRecoverX100 = 2500;
  r.m9QueueDepthHighFrames = 4;
  r.m9QueueDepthLowFrames = 1;
  r.m9UdpDropPmHigh = 120;
  r.m9UdpDropPmLow = 30;
  r.m9LatencyHighUs = 140000;
  r.m9LatencyLowUs = 90000;
  r.m9TailHighUs = 110000;
  r.m9TailLowUs = 70000;
  return r;
}
M9Inputs M9Good() {
  M9Inputs in;
  in.metricsFresh = true;
  in.clCongestionState = 0;
  in.clDecodedFpsX100 = 3000;
  in.clQueueDepthMax = 1;
  in.clUdpDropPm = 10;
  in.clAvgLatencyUs = 50000;
  in.clAvgDecodeTailUs = 30000;
  in.cb2eAvgUs = 5000;
  return in;
}
M9Inputs M9Congested() {
  M9Inputs in = M9Good();
  in.clCongestionState = 2;
  return in;
}
// Neither down nor up pressure: fine on every axis except the queue, which is too deep to recover.
M9Inputs M9Neutral() {
  M9Inputs in = M9Good();
  in.clQueueDepthMax = 2;
  return in;
}

int RunM9(RateControlState& r, const M9Inputs& in, uint64_t t0, int seconds, int* switchAt = nullptr,
          std::string* firstReason = nullptr) {
  if (switchAt) *switchAt = 0;
  for (int i = 1; i <= seconds; ++i) {
    const uint64_t t = t0 + static_cast<uint64_t>(i) * kSec;
    const M9Decision d = r.DecideM9Level(in, t);
    if (d.targetLevel != r.m9Level) {
      if (switchAt && *switchAt == 0) {
        *switchAt = i;
        if (firstReason) *firstReason = d.reason;
      }
      r.CommitM9Level(d.targetLevel, t);
    }
  }
  return r.m9Level;
}

void TestM9DownRequiresConsecutiveSecondsAndCooldown() {
  RateControlState r = MakeM9();
  int at = 0;
  std::string reason;
  int level = RunM9(r, M9Congested(), kStartUs, 2, &at, &reason);
  expect(level == 1 && at == 2 && reason == "client_pressure", "m9: one level down after m9DownRequireSec seconds");
  expect(r.m9CooldownUntilUs == kStartUs + 2 * kSec + 4 * kSec, "m9: cooldown = m9CooldownSec after the switch");
  // Pressure through the cooldown does not switch again until it ends; then 2 more seconds each.
  level = RunM9(r, M9Congested(), kStartUs + 2 * kSec, 3, &at);
  expect(level == 1 && at == 0, "m9: no switch inside the cooldown");
  level = RunM9(r, M9Congested(), kStartUs + 5 * kSec, 20);
  expect(level == 3, "m9: keeps stepping down to the last level");
  level = RunM9(r, M9Congested(), kStartUs + 25 * kSec, 20);
  expect(level == 3, "m9: never below level 3");
}

void TestM9PressureStreakResets() {
  RateControlState r = MakeM9();
  RunM9(r, M9Congested(), kStartUs, 1);
  expect(r.m9DownPressureSeconds == 1, "m9: one pressure second counted");
  RunM9(r, M9Neutral(), kStartUs + kSec, 1);
  expect(r.m9DownPressureSeconds == 0 && r.m9Level == 0, "m9: a neutral second resets the down streak");
}

void TestM9UpAfterRecoverySeconds() {
  RateControlState r = MakeM9();
  r.m9Level = 3;
  int at = 0;
  std::string reason;
  int level = RunM9(r, M9Good(), kStartUs, 3, &at, &reason);
  expect(level == 2 && at == 3 && reason == "client_recovered", "m9: one level up after m9UpRequireSec good seconds");
  level = RunM9(r, M9Good(), kStartUs + 3 * kSec, 30);
  expect(level == 0, "m9: recovers all the way to level 0");
  level = RunM9(r, M9Good(), kStartUs + 33 * kSec, 10);
  expect(level == 0, "m9: never above level 0");
}

void TestM9HostFallbackWhenMetricsStale() {
  RateControlState r = MakeM9();
  M9Inputs in = M9Good();
  in.metricsFresh = false;
  in.cb2eAvgUs = 120000;  // >= m9TailHighUs
  int at = 0;
  std::string reason;
  const int level = RunM9(r, in, kStartUs, 2, &at, &reason);
  expect(level == 1 && reason == "host_fallback_pressure", "m9: stale metrics use the host tail as pressure");
  // Stale metrics never count as recovery.
  RateControlState u = MakeM9();
  u.m9Level = 2;
  M9Inputs stale = M9Good();
  stale.metricsFresh = false;
  RunM9(u, stale, kStartUs, 10);
  expect(u.m9Level == 2 && u.m9UpPressureSeconds == 0, "m9: no recovery without fresh metrics");
}

void TestM9EachAxisTriggersDown() {
  const char* names[] = {"decodedFps", "queueDepth", "udpDrop", "latency", "tail"};
  for (int axis = 0; axis < 5; ++axis) {
    RateControlState r = MakeM9();
    M9Inputs in = M9Good();
    switch (axis) {
      case 0: in.clDecodedFpsX100 = 1999; break;
      case 1: in.clQueueDepthMax = 4; break;
      case 2: in.clUdpDropPm = 120; break;
      case 3: in.clAvgLatencyUs = 140000; break;
      default: in.clAvgDecodeTailUs = 110000; break;
    }
    const int level = RunM9(r, in, kStartUs, 2);
    expect(level == 1, std::string("m9: axis alone steps down: ") + names[axis]);
  }
}

// ---- quality r1: evidence validity ------------------------------------------------------------

// What the Android APK (<= 0.2.21) sends: a fresh ControlClientMetrics every second with only the
// present* block, so every decode field is 0. Measured: 6000/30 and 3000/30 both fell to the lowest
// rung within 5-11 s with "clientDecodedFps=0 clientAvgLatUs=0 clientMbps=0" and the host fine.
AbrInputs PresentOnly() {
  AbrInputs in = Healthy();
  in.clientDecodeReported = false;
  in.clDecodedFpsX100 = 0;
  in.clAvgLatencyUs = 0;
  in.clAvgDecodeTailUs = 0;
  in.clUdpDropPm = 0;
  return in;
}

void TestAbrPresentOnlyReportsDoNotDemote() {
  RateControlState r = MakeAbr(false);
  const AbrInputs in = PresentOnly();
  const int p = RunAbr(r, in, kStartUs + 3 * kSec, 30);
  expect(p == 0, "abr(r1): present-only reports (decode fields never sent) hold high for 30 s");
  const AbrDecision d = r.DecideAbrProfile(in, kStartUs + 40 * kSec);
  expect((d.evidence & kAbrEvidenceSevereClient) == 0 && !d.clientDecodeValid,
         "abr(r1): ...their zeros are not client evidence");
  expect((d.evidence & kAbrEvidenceDecodeUnreported) != 0,
         "abr(r1): ...and the decision line says the decode side was never reported");

  RateControlState q = MakeAbr(true);
  expect(RunAbr(q, in, kStartUs + 3 * kSec, 30) == 0, "abr(r1): same in quality-first mode");
}

void TestAbrNeverReportedDoesNotDemote() {
  RateControlState r = MakeAbr(false);
  AbrInputs in = Healthy();
  in.metricsFresh = false;
  in.clientEverReported = false;
  in.clientDecodeReported = false;
  const int p = RunAbr(r, in, kStartUs + 3 * kSec, 30);
  expect(p == 0, "abr(r1): a client that never reported is not 'feedback lost' -- holds high 30 s");
  expect(r.abrStaleActiveSeconds == 0, "abr(r1): ...and no stale-active seconds accumulate");
  const AbrDecision d = r.DecideAbrProfile(in, kStartUs + 40 * kSec);
  expect((d.evidence & kAbrEvidenceNeverReported) != 0 && (d.evidence & kAbrEvidenceStaleActive) == 0,
         "abr(r1): ...the decision line says never_reported, not stale_active");
}

void TestAbrHostPressureStillDemotesWithoutClientEvidence() {
  // Kept on purpose (Codex 2): the host's own encode pressure demotes whether the client's evidence
  // is missing (never reported) or meaningless (present-only). Before r1 the present-only case had
  // this switched off, because it only applied when the report was stale.
  for (int variant = 0; variant < 2; ++variant) {
    RateControlState r = MakeAbr(false);
    AbrInputs in = variant == 0 ? PresentOnly() : Healthy();
    if (variant == 1) {
      in.metricsFresh = false;
      in.clientEverReported = false;
      in.clientDecodeReported = false;
    }
    in.cb2eAvgUs = 100000;  // over the 90 ms severe host threshold
    int at = 0;
    std::string reason;
    const int p = RunAbr(r, in, kStartUs + 3 * kSec, 2, &at, &reason);
    expect(p == 1 && at == 2 && reason == "high_to_mid_severe",
           std::string("abr(r1): host encode pressure still demotes -- ") +
               (variant == 0 ? "present-only client" : "never-reported client"));
  }
}

void TestAbrFreshZeroFromADecodeReporterIsStillEvidence() {
  // A viewer that does report decode values and now says 0 decoded under a full send: that is the
  // relay-collapse signal and stays one.
  RateControlState r = MakeAbr(false);
  AbrInputs in = Healthy();
  in.clDecodedFpsX100 = 0;
  int at = 0;
  std::string reason;
  const int p = RunAbr(r, in, kStartUs + 3 * kSec, 2, &at, &reason);
  expect(p == 1 && at == 2 && reason == "high_to_mid_severe",
         "abr(r1): a fresh 0 fps from a decode-reporting viewer still demotes");
}

void TestAbrReportedThenSilentStillDemotes() {
  // The P7 case with the r1 input spelled out: reports existed, then stopped under a full send.
  RateControlState r = MakeAbr(false);
  AbrInputs in = PresentOnly();  // even a present-only reporter that goes silent is feedback lost
  in.metricsFresh = false;
  int at = 0;
  std::string reason;
  const int p = RunAbr(r, in, kStartUs + 3 * kSec, 3, &at, &reason);
  expect(p == 1 && at == 3 && reason == "high_to_mid_severe",
         "abr(r1): a client that reported and then went silent still demotes (P7 kept)");
}

void TestClientMetricsSessionStickyFields() {
  ClientMetricsSnapshot snap;
  ViewerMetrics presentOnly;
  presentOnly.updatedUs = 1000;
  snap.Publish(presentOnly);
  ViewerMetrics v = snap.Snapshot();
  expect(v.firstUs == 1000 && v.reports == 1 && !v.decodeReported && v.firstDecodeUs == 0,
         "metrics(r1): a present-only report counts as reported, not as decode-reported");
  ViewerMetrics full;
  full.updatedUs = 2000;
  full.width = 1280;
  full.decodedFpsX100 = 3000;
  snap.Publish(full);
  presentOnly.updatedUs = 3000;
  snap.Publish(presentOnly);
  v = snap.Snapshot();
  expect(v.firstUs == 1000 && v.firstDecodeUs == 2000 && v.decodeReported && v.reports == 3,
         "metrics(r1): decode-reported is sticky for the session; first times are kept");
  snap.Reset();
  v = snap.Snapshot();
  expect(v.firstUs == 0 && !v.decodeReported && v.reports == 0,
         "metrics(r1): a new session starts with nothing reported");
  ViewerMetrics zeros;
  zeros.updatedUs = 5000;
  expect(!viewer_report_has_decode_fields(zeros) && viewer_report_has_decode_fields(full),
         "metrics(r1): decode fields are recognised by value");
}

// ---- quality r2: the fps ABR runs each profile at, and which captures get DesktopText ----

void TestTextPriorityFpsAndScope() {
  RateControlState r = MakeAbr(false);
  r.userFpsCeiling = 30;
  const auto T = EncodePriority::DesktopText;
  const auto S = EncodePriority::Standard;
  expect(r.AbrProfileFps(0, T) == 30 && r.AbrProfileFps(1, T) == 20 && r.AbrProfileFps(2, T) == 20,
         "r2: desktop text at 30 fps runs high 30 / mid 20 / low 20");
  expect(r.AbrProfileFps(0, S) == 30 && r.AbrProfileFps(1, S) == 30 && r.AbrProfileFps(2, S) == 30,
         "r2: standard keeps the ceiling at every profile (unchanged)");
  r.userFpsCeiling = 60;
  expect(r.AbrProfileFps(1, T) == 40, "r2: 60 fps -> mid 40");
  r.userFpsCeiling = 20;
  expect(r.AbrProfileFps(1, T) == 15, "r2: 20 fps -> mid floors at 15");
  r.userFpsCeiling = 10;
  expect(r.AbrProfileFps(1, T) == 10, "r2: a ceiling under the floor is never raised");
  expect(r.PriorityFor(false) == T && r.PriorityFor(true) == S,
         "r2: desktop capture is text priority, window capture keeps the old ladder");
  r.textPriorityEnabled = false;
  expect(r.PriorityFor(false) == S, "r2: the rollback switch turns it off");
}

// Pressure and recovery, end to end through the ABR's own timers: what the encoder is told at each
// profile for a 4K desktop at the user's 3000/30. Resolution below 1080p only at low; recovering to
// high restores both the fps and the floor.
void TestTextPriorityPressureAndRecovery() {
  RateControlState r = MakeAbr(false);
  r.userFpsCeiling = 30;
  const auto T = EncodePriority::DesktopText;
  const auto plan = [&](int profile) {
    const uint32_t bitrate = profile == 0 ? 3000000u : (profile == 1 ? 2250000u : 1650000u);
    const auto size = choose_abr_profile_size(profile, bitrate, 3840, 2160, true, T);
    return std::to_string(size.width) + "x" + std::to_string(size.height) + "@" +
           std::to_string(r.AbrProfileFps(profile, T));
  };
  expect(plan(r.abrProfile) == "1920x1080@30", "r2: starts 1920x1080@30, got " + plan(r.abrProfile));
  // Healthy seconds never demote.
  RunAbr(r, Healthy(), kStartUs + 3 * kSec, 20);
  expect(r.abrProfile == 0, "r2: healthy seconds hold high");
  // Real pressure: high -> mid gives up frames, not pixels.
  RunAbr(r, Severe(), kStartUs + 23 * kSec, 2);
  expect(r.abrProfile == 1 && plan(1) == "1920x1080@20", "r2: first pressure step keeps 1080p, fps 20: " + plan(1));
  // Sustained: mid -> low is the only step that takes 720p.
  RunAbr(r, Severe(), kStartUs + 25 * kSec, 4);
  const std::string lowPlan = plan(2);
  expect(r.abrProfile == 2 && lowPlan.rfind("1280x720", 0) == 0, "r2: sustained pressure reaches 720p: " + lowPlan);
  // Recovery takes its hold time (low->mid 5 s, mid->high 8 s, plus cooldowns) and comes back whole.
  RunAbr(r, Healthy(), kStartUs + 29 * kSec, 4);
  expect(r.abrProfile == 2, "r2: no instant recovery (hysteresis)");
  RunAbr(r, Healthy(), kStartUs + 33 * kSec, 30);
  expect(r.abrProfile == 0 && plan(0) == "1920x1080@30", "r2: recovered to 1920x1080@30");
}

// ---- quality r3: the box follows what is captured now, not how the session got there ----

void TestBoxRepickIsHistoryIndependent() {
  const auto T = EncodePriority::DesktopText;
  const auto S = EncodePriority::Standard;
  // When to re-choose.
  expect(RateControlState::NeedsBoxRepick(S, T, false, false), "r3: window box, desktop capture -> re-choose");
  expect(RateControlState::NeedsBoxRepick(T, S, false, false), "r3: desktop box, window capture -> re-choose");
  expect(!RateControlState::NeedsBoxRepick(T, T, false, false), "r3: same priority -> keep");
  expect(!RateControlState::NeedsBoxRepick(S, T, true, false), "r3: a user-set --encode box is never re-chosen");
  expect(!RateControlState::NeedsBoxRepick(S, T, false, true), "r3: the picker overview owns its box");

  // What it re-chooses: only (profile, bitrate, source, priority). The measured bug: desktop 6000
  // -> window 480x270 -> tune 3000 in the window (box = 480x270) -> desktop stayed 480x270.
  RateControlState r = MakeAbr(false);
  r.userFpsCeiling = 30;
  const auto fresh = r.PlanBox(0, 3000000, 1920, 1080, T);
  RateControlState afterWindow = MakeAbr(false);
  afterWindow.userFpsCeiling = 30;
  (void)afterWindow.PlanBox(0, 6000000, 480, 270, S);   // the window, before the tune
  (void)afterWindow.PlanBox(0, 3000000, 480, 270, S);   // the tune while on the window
  const auto back = afterWindow.PlanBox(0, 3000000, 1920, 1080, T);
  expect(back.width == fresh.width && back.height == fresh.height && back.fps == fresh.fps &&
             back.width == 1920 && back.height == 1080 && back.fps == 30,
         "r3: window -> tune -> desktop plans the same box as a fresh desktop: " +
             std::to_string(back.width) + "x" + std::to_string(back.height) + "@" + std::to_string(back.fps));
  // 4K desktop at 3000: 1080p area; the same 4K as a window (Standard): the old ladder's 720p.
  const auto desk4k = r.PlanBox(0, 3000000, 3840, 2160, T);
  const auto win4k = r.PlanBox(0, 3000000, 3840, 2160, S);
  expect(desk4k.width == 1920 && win4k.width == 1280, "r3: desktop -> 1080p, window -> old ladder 720p");
  // Mid profile on the desktop: fps, not pixels; the same profile as a window: ceiling fps.
  const auto deskMid = r.PlanBox(1, 2250000, 3840, 2160, T);
  const auto winMid = r.PlanBox(1, 2250000, 3840, 2160, S);
  expect(deskMid.width == 1920 && deskMid.fps == 20 && winMid.fps == 30,
         "r3: at mid the desktop plans 1080p@20, a window keeps its fps");
}

}  // namespace

int main() {
  TestAbrWarmupHoldsEverything();
  TestAbrHighToMidSevereThenMidToLow();
  TestAbrQualityFirstNeedsThreeSevereSeconds();
  TestAbrSparseAndStaticSecondsHold();
  TestAbrEmergencyGoesStraightToLow();
  TestAbrHostFallbackEvidence();
  TestAbrRecoversMidToHighAfterGoodSeconds();
  TestAbrLowToMidAndLowToHighWithoutMid();
  TestAbrWithoutLowerProfilesHolds();
  TestAbrStaticRecoveryPromotesFromLow();
  TestAbrClientLossTriggersDemotion();
  TestAbrStaleFeedbackDuringActiveSendDemotes();
  TestAbrLowClientFpsDemotesDespiteLowLatency();
  TestAbrPresentOnlyReportsDoNotDemote();
  TestAbrNeverReportedDoesNotDemote();
  TestAbrHostPressureStillDemotesWithoutClientEvidence();
  TestAbrFreshZeroFromADecodeReporterIsStillEvidence();
  TestAbrReportedThenSilentStillDemotes();
  TestClientMetricsSessionStickyFields();
  TestTextPriorityFpsAndScope();
  TestTextPriorityPressureAndRecovery();
  TestBoxRepickIsHistoryIndependent();
  TestM9DownRequiresConsecutiveSecondsAndCooldown();
  TestM9PressureStreakResets();
  TestM9UpAfterRecoverySeconds();
  TestM9HostFallbackWhenMetricsStale();
  TestM9EachAxisTriggersDown();
  if (gFailures == 0) {
    std::printf("host_abr_test: PASS\n");
    return 0;
  }
  std::printf("host_abr_test: FAIL (%d)\n", gFailures);
  return 1;
}
