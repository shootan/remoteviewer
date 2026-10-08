#pragma once

// The two forced-key decision points of the host main loop, extracted as pure functions so the
// recovery-roundtrip test (stutter-keyframe r2, step 2) drives the IDENTICAL product logic the stages
// use rather than a copy. Behaviour-preserving: the bodies are moved verbatim from
// host_stage_time_limit.cpp (the mailbox reason mapping) and host_stage_encode_send_h264.cpp (the
// force-key decision).

#include <cstdint>

#include "host_keyframe_reason.hpp"    // kHostKeyReason* (encoder-side bits)
#include "host_main_loop_mailbox.hpp"  // kKeyframeReason* (viewer/mailbox-side bits)

namespace remote60::native_poc {

// Map the mailbox's viewer-facing keyframe reasons to the encoder's host-reason bits. From
// host_stage_time_limit.cpp:158-161.
inline uint32_t map_keyframe_reasons_to_host(uint32_t viewerReasons) {
  uint32_t hostReasons = kHostKeyReasonNone;
  if ((viewerReasons & kKeyframeReasonViewer) != 0) hostReasons |= kHostKeyReasonViewer;
  if ((viewerReasons & kKeyframeReasonSenderBarrier) != 0) hostReasons |= kHostKeyReasonSenderBarrier;
  if ((viewerReasons & kKeyframeReasonSenderBacklog) != 0) hostReasons |= kHostKeyReasonSenderBacklog;
  return hostReasons;
}

// The forced-key decision. A key is WANTED if a request is pending (forceKeyNext, set by RequestKey),
// it is the first frame, or the periodic schedule is due. It is FORCED this tick only if no key of
// this session is already in flight -- otherwise a keyWanted frame rides as a delta so it does not
// overflow the sender queue into a resync-IDR loop while the real key is on its way (r3 F2). From
// host_stage_encode_send_h264.cpp:289/294.
struct ForceKeyInputs {
  bool forceKeyNext = false;      // a RequestKey is pending (viewer/barrier/backlog/refit/target/...)
  bool firstFrame = false;        // encoder.encodedSeq == 0
  bool scheduledKey = false;      // !servedBootstrap && activeKeyint>0 && realInputsSinceKey>=activeKeyint
  bool forceKeyInFlight = false;  // a key of this (media,gen,input) epoch is queued/on-wire or latched
  // stutter-keyframe r2 F1: a prior ForceKeyFrame SetValue was REJECTED and we are inside the retry
  // back-off. The request stays wanted, but we do NOT force (and so do NOT ride the admit-always key
  // gate) this tick -- the frame is an ordinary delta, gated normally -- until the back-off elapses.
  bool forceRetryPending = false;
};
struct ForceKeyDecision {
  bool keyWanted = false;
  bool forceKeyFrame = false;
};
inline ForceKeyDecision decide_force_key(const ForceKeyInputs& in) {
  ForceKeyDecision d;
  d.keyWanted = in.forceKeyNext || in.firstFrame || in.scheduledKey;
  // Force only if no key is in flight AND we are not inside a rejection back-off. During the back-off
  // the frame rides as a delta (adm.keyWanted = forceKeyFrame = false => the normal backlog gate), so
  // a persistently-rejecting MFT cannot turn every input into an admit-always exception (r2 F1).
  d.forceKeyFrame = d.keyWanted && !in.forceKeyInFlight && !in.forceRetryPending;
  return d;
}

// The back-off / escalation after an encode that attempted a forced key (r2 F1). The MFT's
// AVEncVideoForceKeyFrame SetValue is honoured only on S_OK; a persistent rejection must neither arm
// the real in-flight latch (it would fake a key that is not coming) nor re-force every tick. Instead
// it sets a bounded retry back-off, and after a bounded streak escalates to a repair (encoder rebuild)
// so recovery cannot stall for ever. Pure, so every branch is a unit test.
inline constexpr uint64_t kForceKeyRetryBackoffUs = 300'000;  // match the old in-flight latch spacing
inline constexpr uint32_t kForceKeyRejectRepairStreak = 10;   // ~3s of 300ms retries -> repair

struct ForceKeyPostEncodeInputs {
  bool attemptedForce = false;   // forceKeyFrame was true this tick (a force was submitted to the MFT)
  bool setValueRequested = false;// H264EncodeFrameStats.forceKeyRequested (the setter ran)
  bool setValueOk = false;       // the SetValue returned S_OK (armed), not a rejection
  uint64_t nowUs = 0;            // encode-call start qpc
  uint32_t rejectStreak = 0;     // consecutive rejections so far
};
struct ForceKeyPostEncodeDecision {
  bool armInFlightLatch = false; // arm forceKeySubmittedAtUs: a real key input is in flight (S_OK)
  bool setRetryBackoff = false;  // set forceKeyRetryAtUs: rejected, wait before retrying
  uint64_t retryAtUs = 0;        // when setRetryBackoff
  uint32_t newRejectStreak = 0;  // the updated streak (reset to 0 on accept)
  bool triggerRepair = false;    // persistent rejection -> bounded repair (encoder rebuild)
};
inline ForceKeyPostEncodeDecision decide_force_key_post_encode(const ForceKeyPostEncodeInputs& in) {
  ForceKeyPostEncodeDecision d;
  if (!in.attemptedForce) {
    d.newRejectStreak = in.rejectStreak;  // nothing attempted; carry the streak unchanged
    return d;
  }
  const bool accepted = !in.setValueRequested || in.setValueOk;  // S_OK (or no setter ran) = armed
  if (accepted) {
    d.armInFlightLatch = true;
    d.newRejectStreak = 0;  // a clean attempt clears the streak
    return d;
  }
  // Rejected: back off, do not arm the latch, keep the request; escalate on a bounded streak.
  d.setRetryBackoff = true;
  d.retryAtUs = in.nowUs + kForceKeyRetryBackoffUs;
  d.newRejectStreak = in.rejectStreak + 1;
  d.triggerRepair = d.newRejectStreak >= kForceKeyRejectRepairStreak;
  if (d.triggerRepair) d.newRejectStreak = 0;  // one repair per streak, then start counting again
  return d;
}

}  // namespace remote60::native_poc
