#pragma once

// Trailing-edge kick / static refresh state (KickState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <cstdint>

namespace remote60::native_poc {

// Trailing-edge kick + periodic static refresh + selection-first-keyframe fence (Phase 1-8 state
// struct). The kick resubmits the cached last raw frame once, 150ms after the last real capture,
// so a held frame leaves the encoder.codec on a still screen; the static refresh re-serves it at a low
// cadence so a quiet viewer does not look dead. See the comment block above arm_trailing_kick in
// main() for the full rationale. thread: main encode loop only.
struct KickState {
  bool pending = false;                        // trailing kick armed
  uint64_t dueAtUs = 0;                        // when it fires
  uint64_t lastSeenBootstrapEpoch = 0;         // session epoch we last armed for
  uint64_t lastSeenStreamGeneration = 0;       // capture generation last armed for (window-select/reattach)
  uint64_t lastRealInputCaptureUs = 0;         // capture ts of the most recent real frame fed to the MFT
  uint64_t lastEmittedAuCaptureUs = 0;         // capture ts seen on the most recent emitted AU (encoder.codec output)
  uint64_t lastKickedForInputCaptureUs = 0;    // one-kick-per-held-input guard
  uint64_t count = 0;                          // telemetry: total trailing-edge kicks served
  uint64_t lastSourceAgeUs = 0;                // telemetry: cached-frame age at the last kick
  // Periodic static refresh cadence (0 = off), REMOTE60_NATIVE_STATIC_REFRESH_MS.
  uint64_t staticRefreshIntervalUs = 0;
  uint64_t staticRefreshCount = 0;             // telemetry: refresh SUBMITS (not wire AUs)
  uint64_t lastStaticRefreshAttemptUs = 0;     // attempt-side cadence anchor; see the refresh block
  // A window/monitor selection must open with an IDR: non-key AUs of that generation are dropped.
  uint64_t selectionFirstKeyframePendingGeneration = 0;
  uint64_t selectionFirstKeyframeDropCount = 0;
  // Encoder/wire capture-stamp monotonic guard (0.2.97). A kick/refresh is stamped "now" (a synthetic
  // frame has no capture time of its own), but a REAL frame that a slow readback publishes late (GPU
  // copy 100-300ms under contention; common right after a UAC/secure-desktop backend restart) then
  // reaches the encoder AFTER that kick carrying an OLDER stamp. The MFT input-timestamp FIFO hands
  // the reversal to the wire, the viewer reads it as a stale reference (behind latest / behind
  // presented) and answers with a decoder reset + IDR -- once per cooldown, i.e. a ~1 Hz IDR storm
  // that decodes 2-4 fps (field: "slow after a UAC"; NAS host 8ec6ecb1 / viewer 68f79d01, 09-05
  // 12:21-12:33). Clamp the real stamp to strictly after the last stamp handed to the encoder; the
  // raw captureUs/callbackUs stay untouched so telemetry still shows the true readback delay.
  uint64_t lastEncoderStampUs = 0;      // last capture stamp handed to the encoder (real or synthetic)
  uint64_t lastSyntheticStampUs = 0;    // last kick/refresh stamp (telemetry: what the clamp chases)
  uint64_t stampClampCount = 0;         // telemetry: real frames whose stamp was moved forward
  uint64_t stampClampMaxUs = 0;         // telemetry: largest forward move
  uint64_t stampClampLastLogUs = 0;     // 1 Hz log rate limit

  // --- behaviour (Phase 2-1: former main() lambdas arm_trailing_kick / cancel_trailing_kick) ---
  static constexpr uint64_t kTrailingKickDelayUs = 150000;  // 150ms trailing edge
  // (Re)arm the trailing-edge kick to fire kTrailingKickDelayUs after atUs. Raw mode has no
  // encoder to flush, so it is a no-op there.
  void Arm(uint64_t atUs, bool useH264) {
    if (!useH264) return;
    pending = true;
    dueAtUs = atUs + kTrailingKickDelayUs;
  }
  void Cancel() {
    pending = false;
    dueAtUs = 0;
  }

  // --- decisions (Phase 2-T3: the former stage_pop_frame conditions, pure on this state) ---
  // The armed trailing edge has arrived.
  bool Due(uint64_t nowUs) const { return pending && nowUs >= dueAtUs; }
  // Whether the trailing edge needs a kick: the barrier still wants a key, or the newest real input is
  // still held in the encoder and has not been kicked yet.
  bool NeedKick(bool barrierClosed) const {
    const KickState& kick = *this;
    // The latest real input is "stuck" until its capture timestamp is observed on an emitted AU;
    // on the async MFT it sits there until the next input, which on a still screen never comes.
    const bool latestInputStuck = (kick.lastRealInputCaptureUs > kick.lastEmittedAuCaptureUs);
    // One kick per distinct held input: never resubmit the same held frame twice on a P-frame
    // trailing edge. A closed barrier overrides this -- it must keep kicking until an IDR lands.
    const bool alreadyKickedThisInput =
        (kick.lastRealInputCaptureUs != 0 && kick.lastKickedForInputCaptureUs == kick.lastRealInputCaptureUs);
    return barrierClosed || (latestInputStuck && !alreadyKickedThisInput);
  }
  // One-shot per held input: remember which input the kick just re-served.
  void MarkKickedForCurrentInput() { lastKickedForInputCaptureUs = lastRealInputCaptureUs; }
  // Stamp a REAL frame must carry so the encoder/wire timeline never runs backwards past a stamp
  // already fed (a kick's "now"): strictly after it, else its own. Pure; host_kick_test.cpp.
  static uint64_t ClampRealStamp(uint64_t realStampUs, uint64_t lastEncoderStampUs) {
    return (lastEncoderStampUs != 0 && realStampUs <= lastEncoderStampUs) ? lastEncoderStampUs + 1
                                                                            : realStampUs;
  }
  // Record the stamp actually handed to the encoder (after any clamp). Only ever moves forward.
  void NoteEncoderStamp(uint64_t stampUs, bool synthetic) {
    if (stampUs > lastEncoderStampUs) lastEncoderStampUs = stampUs;
    if (synthetic && stampUs > lastSyntheticStampUs) lastSyntheticStampUs = stampUs;
  }
  // Periodic static refresh cadence (the kick-side half of the stage condition): no kick pending, an AU
  // has been seen, and both the emitted-AU and the attempt anchors are at least one interval old.
  bool StaticRefreshDue(uint64_t nowUs) const {
    const KickState& kick = *this;
    return !kick.pending &&
           kick.lastEmittedAuCaptureUs != 0 &&
           nowUs >= kick.lastEmittedAuCaptureUs + kick.staticRefreshIntervalUs &&
           nowUs >= kick.lastStaticRefreshAttemptUs + kick.staticRefreshIntervalUs;
  }
};

}  // namespace remote60::native_poc
