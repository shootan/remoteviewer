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
};

}  // namespace remote60::native_poc
