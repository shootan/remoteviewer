#pragma once

// encoder-rc-budget C2/C3/C4: the rate-control PLAN an encoder instance is given, chosen by the
// identity of the MFT that was actually activated, and what the host may still do to that instance
// at runtime.
//
// Role:    pure policy. RcPlan is the explicit set {mode, mean, peak, VBV bytes, MaxQP set?/value}
//          that every initialize() -- first, fps/geometry change, error recovery, provenance resync
//          -- hands to the new transform. Which plan, and whether it goes before or after the media
//          types, is decided by RcProfile, and RcProfile is decided by the MFT's vendor id as read
//          from its activation attributes -- never from a friendly-name guess and never from the
//          backend mode (mft_enum_hw is "whatever hardware MFT came first", not a vendor).
// Thread:  pure; used from the encoder's initialize() and the host's stats tick.
// Callers: H264Encoder (mf_h264_codec.cpp), EncoderState/host_stage_stats_h264 (governor config),
//          host_encoder_budget_test.
//
// Why two profiles (Home PC NVIDIA probe v2 r5, 2026-10-11; AMD measured on this PC):
//   - On the NVIDIA H.264 Encoder MFT (VEN_10DE), ICodecAPI SetValue AFTER the media types returns
//     S_OK and is not applied: the readback keeps mode 0 / peak 15552000 / VBV 15552000 / MaxQP 51
//     and only the mean follows. Fed BEFORE the types the same values are applied, and a motion IDR
//     went from 536 KB to 21 KB. Runtime SetValue on that configured instance changed the readback
//     of the mean only and nothing observable in the output within the probe's window -- so for this
//     profile a running instance is treated as not runtime-controllable: a new mean needs a new
//     instance (the host's existing safe re-initialisation path), and the quantiser ceiling is
//     static for the instance's life. MaxQP 32 fed pre-types made NVIDIA over-produce like AMD does
//     (x5.11); 44 held x1.13 with a PSNR 0.12 dB from 40. 44 is the first candidate, with vbr_peak
//     and a 200 ms VBV (100 ms under-shot to x0.70, 500 ms over-shot to x1.28).
//   - On the AMD MFT (this PC) the post-types path is read back as set and live changes take: the
//     existing initialise order, 32 ceiling and governor stay exactly as they were (Legacy). An
//     unidentified MFT is Legacy too: no name guessing into the NVIDIA policy.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

#include "host_rate_governor.hpp"

namespace remote60::native_poc {

// Who the activated MFT is, as read from the IMFActivate the enumeration handed us (or from the
// transform's own attribute store when there was no activate: the CLSID fallback, a test transform).
// "?" is the honest absent value; it is never inferred from a name.
struct MftIdentity {
  std::string friendlyName = "?";
  std::string clsid = "?";
  std::string vendorId = "?";  // MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, e.g. "VEN_10DE"
};

enum class RcProfile {
  Legacy,          // today's path: RC after the media types, live mean/MaxQP changes, governor 32..40
  NvidiaPreTypes,  // RC before the media types, static for the instance; a new mean = a new instance
};

inline const char* rc_profile_name(RcProfile p) {
  switch (p) {
    case RcProfile::Legacy: return "legacy";
    case RcProfile::NvidiaPreTypes: return "nvidia_pre_types";
  }
  return "?";
}

inline bool vendor_id_equals(const std::string& a, const char* b) {
  if (!b || a.size() != std::char_traits<char>::length(b)) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const char x = static_cast<char>(std::toupper(static_cast<unsigned char>(a[i])));
    const char y = static_cast<char>(std::toupper(static_cast<unsigned char>(b[i])));
    if (x != y) return false;
  }
  return true;
}

/** The profile for an activated MFT. Vendor id only; the friendly name is logged, not decided on. */
inline RcProfile rc_profile_for(const MftIdentity& id) {
  if (vendor_id_equals(id.vendorId, "VEN_10DE")) return RcProfile::NvidiaPreTypes;
  return RcProfile::Legacy;
}

struct RcPlan {
  uint32_t rcMode = 1;       // eAVEncCommonRateControlMode value (1 = PeakConstrainedVBR, 0 = CBR)
  uint32_t meanBps = 0;      // E
  uint32_t peakBps = 0;
  uint32_t vbvBytes = 0;     // AVEncCommonBufferSize is in BYTES
  bool maxQpSet = false;     // false: the MaxQP SetValue is not issued at all (NOT "no limit")
  uint32_t maxQp = 0;
  bool operator==(const RcPlan& o) const {
    return rcMode == o.rcMode && meanBps == o.meanBps && peakBps == o.peakBps && vbvBytes == o.vbvBytes &&
           maxQpSet == o.maxQpSet && (!maxQpSet || maxQp == o.maxQp);
  }
  bool operator!=(const RcPlan& o) const { return !(*this == o); }
};

// Inputs the profile turns into a plan. The legacy values are exactly what apply_rate_control
// computed before this change; they are passed in so the policy stays pure.
struct RcPlanInputs {
  uint32_t meanBps = 0;              // E
  uint32_t peakPercent = 200;        // REMOTE60_NATIVE_PEAK_BITRATE_PERCENT (vbr_peak default 200)
  bool cbr = false;                  // REMOTE60_NATIVE_RATE_CONTROL=cbr
  uint32_t rcModeOverride = 0xFFFFFFFFu;  // REMOTE60_NATIVE_RC_MODE_NUM diagnostic, 0xFFFFFFFF = none
  uint32_t legacyVbvBytes = 0;       // low_latency_vbv_bytes / stable_text_vbv_bytes of the mean
  uint32_t legacyMaxQp = 32;         // the governor's current ceiling, or configured_max_qp (0 = unset)
};

constexpr uint32_t kNvidiaFirstCandidateMaxQp = 44;
constexpr uint32_t kNvidiaVbvMilliseconds = 200;

inline uint32_t peak_for(uint32_t meanBps, uint32_t peakPercent) {
  const uint64_t peak = static_cast<uint64_t>(meanBps) * std::max<uint32_t>(100u, peakPercent) / 100ULL;
  return static_cast<uint32_t>(std::min<uint64_t>(peak, 200000000ULL));
}

/** The plan a new instance of `profile` gets for mean E. */
inline RcPlan make_rc_plan(RcProfile profile, const RcPlanInputs& in) {
  RcPlan plan;
  plan.meanBps = in.meanBps;
  plan.peakBps = peak_for(in.meanBps, in.peakPercent);
  if (profile == RcProfile::NvidiaPreTypes) {
    // vbr_peak, the current peak multiplier, 200 ms of VBV in bytes, an EXPLICIT 44 ceiling. The
    // diagnostic mode override is honoured so a field experiment can still pick CBR here.
    plan.rcMode = in.rcModeOverride != 0xFFFFFFFFu ? in.rcModeOverride : 1u;
    plan.vbvBytes = static_cast<uint32_t>(
        std::max<uint64_t>(16384ULL, static_cast<uint64_t>(in.meanBps) / 8ULL * kNvidiaVbvMilliseconds / 1000ULL));
    plan.maxQpSet = true;
    plan.maxQp = kNvidiaFirstCandidateMaxQp;
    return plan;
  }
  plan.rcMode = in.rcModeOverride != 0xFFFFFFFFu ? in.rcModeOverride : (in.cbr ? 0u : 1u);
  plan.vbvBytes = in.legacyVbvBytes;
  plan.maxQpSet = in.legacyMaxQp > 0 && in.legacyMaxQp <= 51;
  plan.maxQp = plan.maxQpSet ? in.legacyMaxQp : 0;
  return plan;
}

// What a RUNNING instance of the profile accepts. Legacy: the mean and the ceiling move live (AMD,
// read back as set). NvidiaPreTypes: neither is trusted -- S_OK is returned and nothing observable
// follows -- so the host must not call SetValue on the instance and must not count a call as applied.
struct RcRuntimeControls {
  bool liveMean = true;
  bool liveMaxQp = true;
};
inline RcRuntimeControls rc_runtime_controls_for(RcProfile profile) {
  if (profile == RcProfile::NvidiaPreTypes) return RcRuntimeControls{false, false};
  return RcRuntimeControls{true, true};
}

/**
 * The governor's config for a profile (C4). Legacy keeps its ladder from the configured ceiling.
 * For a static-ceiling profile the quantiser stage is pinned (base == cap == the plan's ceiling) so
 * the loop never asks for a step it cannot take and never "restores" 32: its remaining lever, the
 * frame rate, engages directly when the generated payload stays over the target at the cap.
 */
inline RateGovernorConfig governor_config_for(RcProfile profile, uint32_t configuredMaxQp,
                                              uint32_t staticMaxQp) {
  RateGovernorConfig cfg{configuredMaxQp};
  if (profile == RcProfile::NvidiaPreTypes) {
    cfg.baseQp = staticMaxQp;
    cfg.capQp = staticMaxQp;
  }
  return cfg;
}

// ----------------------------------------------------------------------- C3: retarget coalescing
//
// On a profile whose mean is not live, a new E means a new instance: one IDR and the frames in
// flight. ABR, M9, the governor's fps stage and the user's tune can each ask within the same second,
// so requests are COALESCED: the first one past the minimum interval is adopted now; one that arrives
// inside the interval is deferred to the interval's end and the LATEST target wins there. Same target
// twice = nothing. The interval is the knob below (2 s: longer than the ABR/M9 hysteresis tick, short
// enough that a user's bitrate change is visible within two seconds). Failure retry is NOT here: that
// stays with ApplyTarget's existing bounded back-off.
struct EncoderRetargetScheduler {
  uint64_t minIntervalUs = 2'000'000;
  uint64_t lastReinitUs = 0;      // qpc of the last encoder (re)initialisation, 0 = never
  bool deferred = false;
  uint64_t deferredUntilUs = 0;

  enum class Verdict { NoChange, ReinitNow, Defer };

  /** `desired` vs `applied` E on the live instance at `nowUs`. */
  Verdict Decide(uint64_t nowUs, uint32_t desiredBps, uint32_t appliedBps) {
    if (desiredBps == appliedBps) {
      deferred = false;
      return Verdict::NoChange;
    }
    if (lastReinitUs == 0 || nowUs >= lastReinitUs + minIntervalUs) {
      deferred = false;
      return Verdict::ReinitNow;
    }
    deferred = true;
    deferredUntilUs = lastReinitUs + minIntervalUs;
    return Verdict::Defer;
  }
  /** Every (re)initialisation, for whatever reason, restarts the interval. */
  void NoteReinit(uint64_t nowUs) {
    lastReinitUs = nowUs;
    deferred = false;
    deferredUntilUs = 0;
  }
  bool Due(uint64_t nowUs) const { return deferred && nowUs >= deferredUntilUs; }
};

}  // namespace remote60::native_poc
