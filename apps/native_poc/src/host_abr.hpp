#pragma once

// ABR profile ladder + M9 level ladder state (RateControlState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <cstdint>

namespace remote60::native_poc {

// Rate control (Phase 1-6 state struct): the ABR profile ladder (high/mid/low bitrate + optional
// lower resolution) and the M9 four-level ladder, their env config, the user's fps/keyint
// ceilings, and the pressure/cooldown counters the 1s stats tick drives them with.
// thread: main encode loop only (reads ClientMetrics snapshots written by the control thread).
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
};

}  // namespace remote60::native_poc
