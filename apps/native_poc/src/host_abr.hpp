#pragma once

// ABR profile ladder + M9 level ladder state (RateControlState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

// Rate control (Phase 1-6 state struct): the ABR profile ladder (high/mid/low bitrate + optional
// lower resolution) and the M9 four-level ladder, their env config, the user's fps/keyint
// ceilings, and the pressure/cooldown counters the 1s stats tick drives them with.
// thread: main encode loop only (reads ClientMetrics snapshots written by the control thread).
// Inputs of the once-a-second ABR profile decision: what stats_tick_h264 measured this second.
struct AbrInputs {
  bool metricsFresh = false;      // client metrics arrived this second (else host evidence only)
  uint32_t clDecodedFpsX100 = 0;  // client decoded fps x100
  uint64_t clAvgLatencyUs = 0;    // client average end-to-end latency
  uint64_t clAvgDecodeTailUs = 0; // client average decode tail
  uint64_t cb2eAvgUs = 0;         // host callback->encode-start average (fallback evidence)
  uint64_t sentFrames = 0;        // frames the sender actually sent this second
  bool staticMode = false;        // frame gating is in static mode
  uint32_t activeFps = 0;         // the encoder's current fps target
  uint64_t startUs = 0;           // stream start (warmup anchor)
};
struct AbrDecision {
  int targetProfile = 0;          // 0 high, 1 mid, 2 low (== abrProfile: hold)
  const char* reason = "none";
};
// Inputs of the once-a-second M9 level decision.
struct M9Inputs {
  bool metricsFresh = false;
  uint32_t clCongestionState = 0;
  uint32_t clDecodedFpsX100 = 0;
  uint32_t clQueueDepthMax = 0;
  uint32_t clUdpDropPm = 0;
  uint64_t clAvgLatencyUs = 0;
  uint64_t clAvgDecodeTailUs = 0;
  uint64_t cb2eAvgUs = 0;
};
struct M9Decision {
  int targetLevel = 0;            // 0..3 (== m9Level: hold)
  const char* reason = "hold";
};

struct RateControlState {
  // Env config (REMOTE60_NATIVE_ABR_* / M9_*), fixed after startup.
  bool abrEnabled = false;
  bool abrQualityFirst = false;
  bool m9Enabled = false;
  bool m9Apply = false;
  uint32_t m9CooldownSec = 0;
  uint32_t m9DownRequireSec = 0;
  uint32_t m9UpRequireSec = 0;
  uint32_t m9DecodedFpsFloorX100 = 0;
  uint32_t m9DecodedFpsRecoverX100 = 0;
  uint32_t m9QueueDepthHighFrames = 0;
  uint32_t m9QueueDepthLowFrames = 0;
  uint32_t m9UdpDropPmHigh = 0;
  uint32_t m9UdpDropPmLow = 0;
  uint32_t m9LatencyHighUs = 0;
  uint32_t m9LatencyLowUs = 0;
  uint32_t m9TailHighUs = 0;
  uint32_t m9TailLowUs = 0;
  // Ladder geometry/bitrates, derived once from the initial encode size and args.
  bool autoFallback720 = false;
  bool encodeLadderReduced = false;
  uint32_t abrHighW = 0;
  uint32_t abrHighH = 0;
  uint32_t abrMidW = 0;
  uint32_t abrMidH = 0;
  uint32_t abrLowW = 0;
  uint32_t abrLowH = 0;
  bool abrHasLowerResolution = false;
  uint32_t abrHighBitrate = 0;
  uint32_t abrMidBitrate = 0;
  uint32_t abrLowBitrate = 0;
  bool abrHasMidProfile = false;
  bool abrHasLowProfile = false;
  uint32_t m9BitrateLevel0 = 0;
  uint32_t m9BitrateLevel1 = 0;
  uint32_t m9BitrateLevel2 = 0;
  uint32_t m9BitrateLevel3 = 0;
  uint32_t m9FpsLevel0 = 0;
  uint32_t m9FpsLevel1 = 0;
  uint32_t m9FpsLevel2 = 0;
  uint32_t m9FpsLevel3 = 0;
  uint32_t m9WidthLevel0 = 0;
  uint32_t m9WidthLevel1 = 0;
  uint32_t m9WidthLevel2 = 0;
  uint32_t m9WidthLevel3 = 0;
  uint32_t m9HeightLevel0 = 0;
  uint32_t m9HeightLevel1 = 0;
  uint32_t m9HeightLevel2 = 0;
  uint32_t m9HeightLevel3 = 0;
  int abrProfile = 0;  // 0: high, 1: mid, 2: low
  // What the user asked for (ceilings a runtime tune may lower but ABR must not exceed).
  uint32_t userFpsCeiling = 0;
  uint32_t userKeyintCeiling = 0;
  // Runtime decision state, advanced by the 1s stats tick.
  uint64_t abrCooldownUntilUs = 0;
  uint32_t abrGoodSeconds = 0;
  uint32_t abrModeratePressureSeconds = 0;
  uint32_t abrSeverePressureSeconds = 0;
  int m9Level = 0;
  uint64_t m9CooldownUntilUs = 0;
  uint32_t m9DownPressureSeconds = 0;
  uint32_t m9UpPressureSeconds = 0;

  // --- behaviour (Phase 2-1: former main() lambdas m9_level_bitrate/fps/w/h) ---
  uint32_t M9LevelBitrate(int level) const {
    if (level <= 0) return m9BitrateLevel0;
    if (level == 1) return m9BitrateLevel1;
    if (level == 2) return m9BitrateLevel2;
    return m9BitrateLevel3;
  }
  uint32_t M9LevelFps(int level) const {
    if (level <= 0) return m9FpsLevel0;
    if (level == 1) return m9FpsLevel1;
    if (level == 2) return m9FpsLevel2;
    return m9FpsLevel3;
  }
  uint32_t M9LevelW(int level) const {
    if (level <= 0) return m9WidthLevel0;
    if (level == 1) return m9WidthLevel1;
    if (level == 2) return m9WidthLevel2;
    return m9WidthLevel3;
  }
  uint32_t M9LevelH(int level) const {
    if (level <= 0) return m9HeightLevel0;
    if (level == 1) return m9HeightLevel1;
    if (level == 2) return m9HeightLevel2;
    return m9HeightLevel3;
  }

  // --- decisions (Phase 2-T1: the former stats_tick_h264 blocks, pure on this state + `in` + t) ---
  // ABR profile for this second: updates the pressure / good-second counters and returns the profile
  // to run (== abrProfile means hold). The caller applies it to the encoder and, if that succeeds,
  // calls CommitAbrProfile.
  AbrDecision DecideAbrProfile(const AbrInputs& in, uint64_t t) {
    RateControlState& rate = *this;
    const uint32_t abrExpectedFps = std::max<uint32_t>(1, in.activeFps);
    const uint32_t minGoodFpsX100 = abrExpectedFps * (rate.abrQualityFirst ? 95u : 93u);
    const uint32_t minOkayFpsX100 = abrExpectedFps * (rate.abrQualityFirst ? 90u : 85u);
    const uint32_t minDegradeFpsX100 = abrExpectedFps * (rate.abrQualityFirst ? 55u : 45u);
    const uint32_t minSevereFpsX100 = abrExpectedFps * (rate.abrQualityFirst ? 45u : 35u);
    const bool abrWarmupDone = (t >= (in.startUs + 4000000ULL));

    // A second in which the host offered almost no frames carries no usable evidence
    // either way. The client's relative-lag metric is a delay-variation estimate over
    // that second's samples, and 2-4 samples let a single outlier -- or the decoder
    // holding output across a sparse cadence -- read as latency the network never had.
    // A static desktop (frame gating) is the common case: the picture was still, the
    // client decoded a handful of frames, and the old code took that for congestion and
    // demoted, then recovered on motion, then demoted again -- the quality seen flapping
    // between sharp and soft while simply reading the screen. in.sentFrames is this tick's
    // real send cadence (reset each stats second), which is what the discarded
    // queuePushPerSec never was. When evidence is this thin, hold the profile and let a
    // second with real motion decide against the unchanged thresholds.
    const bool hostOfferSparse =
        (in.sentFrames < std::max<uint64_t>(2, static_cast<uint64_t>(abrExpectedFps) / 2)) ||
        in.staticMode;

    const uint64_t severeLatencyUs = rate.abrQualityFirst ? 170000ULL : 150000ULL;
    const uint64_t severeTailUs = rate.abrQualityFirst ? 140000ULL : 110000ULL;
    const uint64_t moderateLatencyUs = rate.abrQualityFirst ? 145000ULL : 125000ULL;
    const uint64_t moderateTailUs = rate.abrQualityFirst ? 120000ULL : 90000ULL;
    const uint64_t emergencyLatencyUs = rate.abrQualityFirst ? 260000ULL : 220000ULL;
    const uint64_t emergencyTailUs = rate.abrQualityFirst ? 190000ULL : 160000ULL;

    const bool severeDownByClient =
        in.metricsFresh &&
        (in.clAvgLatencyUs > severeLatencyUs ||
         in.clAvgDecodeTailUs > severeTailUs ||
         (in.clDecodedFpsX100 < minSevereFpsX100 &&
          (in.clAvgLatencyUs > (severeLatencyUs - 30000ULL) || in.clAvgDecodeTailUs > (severeTailUs - 40000ULL))));
    const bool moderateDownByClient =
        in.metricsFresh &&
        (in.clAvgLatencyUs > moderateLatencyUs ||
         in.clAvgDecodeTailUs > moderateTailUs ||
         (in.clDecodedFpsX100 < minDegradeFpsX100 &&
          (in.clAvgLatencyUs > (moderateLatencyUs - 50000ULL) ||
           in.clAvgDecodeTailUs > (moderateTailUs - 30000ULL))));
    const bool emergencyDownByClient =
        in.metricsFresh &&
        (in.clAvgLatencyUs > emergencyLatencyUs ||
         in.clAvgDecodeTailUs > emergencyTailUs);
    const bool severeDownByHost = (!in.metricsFresh && in.cb2eAvgUs > (rate.abrQualityFirst ? 110000ULL : 90000ULL));
    const bool moderateDownByHost = (!in.metricsFresh && in.cb2eAvgUs > (rate.abrQualityFirst ? 90000ULL : 70000ULL));
    // !hostOfferSparse on every up/down verdict: a sparse second neither degrades nor
    // recovers the profile. The pressure and good counters below fall to their else
    // branch and reset, so the profile holds until a second with real cadence arrives.
    const bool severeDown =
        abrWarmupDone && !hostOfferSparse && (severeDownByClient || severeDownByHost);
    const bool moderateDown =
        abrWarmupDone && !hostOfferSparse && (moderateDownByClient || moderateDownByHost);
    const bool emergencyDown = abrWarmupDone && !hostOfferSparse && emergencyDownByClient;

    if (severeDown) {
      ++rate.abrSeverePressureSeconds;
    } else {
      rate.abrSeverePressureSeconds = 0;
    }
    if (moderateDown) {
      ++rate.abrModeratePressureSeconds;
    } else {
      rate.abrModeratePressureSeconds = 0;
    }

    const bool goodForLowToMid =
        in.metricsFresh && !hostOfferSparse &&
        (in.clAvgLatencyUs < 90000ULL) &&
        (in.clAvgDecodeTailUs < 65000ULL) &&
        (in.clDecodedFpsX100 >= minOkayFpsX100);
    const bool goodForMidToHigh =
        in.metricsFresh && !hostOfferSparse &&
        (in.clAvgLatencyUs < 75000ULL) &&
        (in.clAvgDecodeTailUs < 50000ULL) &&
        (in.clDecodedFpsX100 >= minGoodFpsX100);

    int targetProfile = rate.abrProfile;
    const char* abrReason = "none";
    if (t >= rate.abrCooldownUntilUs) {
      const uint32_t highToMidSevereSec = rate.abrQualityFirst ? 3u : 2u;
      const uint32_t highToMidModerateSec = rate.abrQualityFirst ? 6u : 4u;
      const uint32_t midToLowSevereSec = rate.abrQualityFirst ? 4u : 3u;
      const uint32_t midToLowModerateSec = rate.abrQualityFirst ? 8u : 5u;
      const uint32_t lowToMidGoodSec = rate.abrQualityFirst ? 8u : 5u;
      const uint32_t midToHighGoodSec = rate.abrQualityFirst ? 12u : 8u;

      if (rate.abrProfile == 0) {
        if (emergencyDown && rate.abrHasLowProfile && rate.abrSeverePressureSeconds >= 1) {
          targetProfile = 2;
          abrReason = "client_emergency";
        } else if ((rate.abrSeverePressureSeconds >= highToMidSevereSec) || (rate.abrModeratePressureSeconds >= highToMidModerateSec)) {
          if (rate.abrHasMidProfile) {
            targetProfile = 1;
            abrReason = (rate.abrSeverePressureSeconds >= highToMidSevereSec) ? "high_to_mid_severe" : "high_to_mid_moderate";
          } else if (rate.abrHasLowProfile) {
            targetProfile = 2;
            abrReason = (rate.abrSeverePressureSeconds >= highToMidSevereSec) ? "high_to_low_severe" : "high_to_low_moderate";
          }
        }
        rate.abrGoodSeconds = 0;
      } else if (rate.abrProfile == 1) {
        if (emergencyDown && rate.abrHasLowProfile) {
          targetProfile = 2;
          abrReason = "client_emergency";
          rate.abrGoodSeconds = 0;
        } else if ((rate.abrSeverePressureSeconds >= midToLowSevereSec || rate.abrModeratePressureSeconds >= midToLowModerateSec) && rate.abrHasLowProfile) {
          targetProfile = 2;
          abrReason = (rate.abrSeverePressureSeconds >= midToLowSevereSec) ? "mid_to_low_severe" : "mid_to_low_moderate";
          rate.abrGoodSeconds = 0;
        } else {
          if (goodForMidToHigh) {
            ++rate.abrGoodSeconds;
          } else {
            rate.abrGoodSeconds = 0;
          }
          if (rate.abrGoodSeconds >= midToHighGoodSec) {
            targetProfile = 0;
            abrReason = "client_stable_high";
          }
        }
      } else {  // rate.abrProfile == 2
        if (goodForLowToMid) {
          ++rate.abrGoodSeconds;
        } else {
          rate.abrGoodSeconds = 0;
        }
        if (rate.abrGoodSeconds >= lowToMidGoodSec) {
          targetProfile = rate.abrHasMidProfile ? 1 : 0;
          abrReason = "client_stable_mid";
        }
      }
    }
    return AbrDecision{targetProfile, abrReason};
  }
  // The encoder accepted the new profile: record it and start the 4s cooldown.
  void CommitAbrProfile(int targetProfile, bool ladderReduced, uint64_t t) {
    RateControlState& rate = *this;
    rate.encodeLadderReduced = ladderReduced;
    rate.abrProfile = targetProfile;
    rate.abrGoodSeconds = 0;
    rate.abrModeratePressureSeconds = 0;
    rate.abrSeverePressureSeconds = 0;
    rate.abrCooldownUntilUs = t + 4000000ULL;
  }
  // M9 level for this second: updates the down / up pressure counters and returns the level to run
  // (== m9Level means hold). The caller logs / applies it and calls CommitM9Level.
  M9Decision DecideM9Level(const M9Inputs& in, uint64_t t) {
    RateControlState& rate = *this;
    const bool downByClient =
        in.metricsFresh &&
        (in.clCongestionState == 2 ||
         in.clDecodedFpsX100 < rate.m9DecodedFpsFloorX100 ||
         in.clQueueDepthMax >= rate.m9QueueDepthHighFrames ||
         in.clUdpDropPm >= rate.m9UdpDropPmHigh ||
         in.clAvgLatencyUs >= rate.m9LatencyHighUs ||
         in.clAvgDecodeTailUs >= rate.m9TailHighUs);
    const bool downByHostFallback =
        (!in.metricsFresh && in.cb2eAvgUs >= rate.m9TailHighUs);
    const bool downPressure = downByClient || downByHostFallback;
    const bool upPressure =
        in.metricsFresh &&
        in.clCongestionState == 0 &&
        in.clDecodedFpsX100 >= rate.m9DecodedFpsRecoverX100 &&
        in.clQueueDepthMax <= rate.m9QueueDepthLowFrames &&
        in.clUdpDropPm <= rate.m9UdpDropPmLow &&
        in.clAvgLatencyUs <= rate.m9LatencyLowUs &&
        in.clAvgDecodeTailUs <= rate.m9TailLowUs;

    if (downPressure) {
      ++rate.m9DownPressureSeconds;
    } else {
      rate.m9DownPressureSeconds = 0;
    }
    if (upPressure) {
      ++rate.m9UpPressureSeconds;
    } else {
      rate.m9UpPressureSeconds = 0;
    }

    int targetLevel = rate.m9Level;
    const char* m9Reason = "hold";
    if (t >= rate.m9CooldownUntilUs) {
      if (downPressure && rate.m9DownPressureSeconds >= rate.m9DownRequireSec && targetLevel < 3) {
        ++targetLevel;
        m9Reason = downByClient ? "client_pressure" : "host_fallback_pressure";
      } else if (upPressure && rate.m9UpPressureSeconds >= rate.m9UpRequireSec && targetLevel > 0) {
        --targetLevel;
        m9Reason = "client_recovered";
      }
    }
    return M9Decision{targetLevel, m9Reason};
  }
  void CommitM9Level(int targetLevel, uint64_t t) {
    RateControlState& rate = *this;
    rate.m9Level = targetLevel;
    rate.m9CooldownUntilUs = t + static_cast<uint64_t>(rate.m9CooldownSec) * 1000000ULL;
    rate.m9DownPressureSeconds = 0;
    rate.m9UpPressureSeconds = 0;
  }
};

}  // namespace remote60::native_poc
