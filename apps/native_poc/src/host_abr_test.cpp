// Pins the ABR profile and M9 level decisions of RateControlState (host_abr.hpp): the 4s warmup, the
// sparse-offer / static-scene hold, the pressure and good-second counters, the cooldowns and the
// up/down thresholds in both tuning modes. Time is an argument, so a session is a loop of one-second
// ticks with hand-made client metrics; nothing here touches the encoder or the network.
//
// Host split refactor Phase 2-T1 (2026-08-26).

#include "host_abr.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using remote60::native_poc::AbrDecision;
using remote60::native_poc::AbrInputs;
using remote60::native_poc::M9Decision;
using remote60::native_poc::M9Inputs;
using remote60::native_poc::RateControlState;

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

// A second in which the client decoded the full rate with low latency.
AbrInputs Healthy() {
  AbrInputs in;
  in.metricsFresh = true;
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
