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
};
struct ForceKeyDecision {
  bool keyWanted = false;
  bool forceKeyFrame = false;
};
inline ForceKeyDecision decide_force_key(const ForceKeyInputs& in) {
  ForceKeyDecision d;
  d.keyWanted = in.forceKeyNext || in.firstFrame || in.scheduledKey;
  d.forceKeyFrame = d.keyWanted && !in.forceKeyInFlight;
  return d;
}

}  // namespace remote60::native_poc
