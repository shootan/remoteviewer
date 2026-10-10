#pragma once

// The periodic-key period policy for the stutter fix (stutter-keyframe r2, step 4b).
//
// The periodic stutter is a periodic IDR serialized under the hard wire-rate cap: while the IDR
// drains, the sender queue stays full and decide_encode_admission skips every delta -- a 0.25-0.58s
// freeze once per GOP (host_encode_admission.hpp). The fewer periodic IDRs, the fewer freezes. This
// policy raises the MAXIMUM key period so periodic IDRs become rare.
//
// Both the encoder's own GOP (H264Encoder::keyint_ -> AVEncMPVGOPSize) and the host's scheduled key
// (EncoderState::realInputsSinceKey >= activeKeyint, host_stage_encode_send_h264.cpp) ride the SAME
// value through EncoderState::ApplyTarget (host_encoder_manager.cpp: codec.initialize(... targetKeyint)
// and activeKeyint = targetKeyint). Removing only the encoder's auto-GOP would just let the host
// scheduled key fire instead (Codex contract 1), so both must extend together -- which this one value
// does.
//
// This is a MAXIMUM GOP, not a guarantee:
//   - scene changes still emit an IDR sooner (expected);
//   - a clamping MFT (the company PC clamps GOP to its frame rate) still emits every ~fps frames --
//     that case is NOT solved here; it needs step 4a (pre/post-type ordering) or step 5 (IDR cost);
//   - forced keys -- recovery (forceKeyNext), the first frame (encodedSeq==0), selection/epoch
//     transitions -- are immediate and do NOT go through activeKeyint, so this never delays them. The
//     recovery contract was proven to resume on the forced IDR alone up to a 40s GOP
//     (viewer_udp_recovery_test scenario_long_gop_forced_recovery_resumes, r1).
//
// Pure, so every branch is a unit test (host_key_period_policy_test.cpp); the thresholds are here.

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

// The floor for the periodic key, in seconds. 10s matches the contract target (>= 10s) and is far
// inside the recovery envelope proven in the rig. The period is only ever RAISED to this floor, never
// lowered: a caller that already asks for a longer key period keeps it.
inline constexpr uint32_t kKeyPeriodSecondsFloor = 10;

// Frame ceiling: a high fps cannot ask for an unbounded GOP. Matches the existing
// REMOTE60_NATIVE_KEYINT_OVERRIDE clamp (host_startup_graphics.cpp) so the two agree.
inline constexpr uint32_t kKeyPeriodFramesMax = 600;

struct KeyPeriodInputs {
  uint32_t requestedKeyint = 0;  // keyint the caller asked for (runtime config / ABR / overview), frames
  uint32_t fps = 30;             // the active frame rate
  uint32_t overrideKeyint = 0;   // REMOTE60_NATIVE_KEYINT_OVERRIDE (0 = off): an explicit A/B value wins
};

// Returns the effective keyint (max GOP, in frames) to drive BOTH the encoder GOP and the host
// scheduled key. An explicit override wins verbatim (clamped); otherwise the requested keyint is
// raised to the seconds-floor and clamped to the frame ceiling.
inline uint32_t compute_effective_keyint(const KeyPeriodInputs& in) {
  if (in.overrideKeyint != 0) {
    return std::clamp(in.overrideKeyint, 1u, kKeyPeriodFramesMax);  // explicit A/B choice bypasses the floor
  }
  const uint32_t fps = std::max<uint32_t>(1u, in.fps);
  const uint64_t floorFrames = static_cast<uint64_t>(fps) * kKeyPeriodSecondsFloor;
  const uint64_t requested = std::max<uint32_t>(1u, in.requestedKeyint);
  const uint64_t want = std::max<uint64_t>(requested, floorFrames);
  return static_cast<uint32_t>(std::clamp<uint64_t>(want, 1u, kKeyPeriodFramesMax));
}

}  // namespace remote60::native_poc
