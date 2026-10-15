#pragma once

// Trailing-edge kick / static refresh state (KickState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <cstdint>
#include <algorithm>
#include <memory>
#include <vector>

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

  // --- latest-content catch-up (motion-settle r8, Codex r8 s3) ---
  // The wire-cap input gate can skip the LAST real capture of a change; nothing else re-serves it
  // promptly (the trailing kick only flushes an input the MFT already holds; the static refresh is
  // 1 Hz), so the final picture arrives up to a second late. A skip opens a DEBT bound to the picture
  // (its pixels' buffer, content version) and to the session/input epoch and stream generation it
  // belongs to. Two frame periods after the last skip the main loop re-serves the cached picture once
  // as an ordinary P: no admit-always, no forced key, no QP change; it waits for an empty sender queue
  // and never goes ahead of a newer real capture. The debt closes only when the encoder ACCEPTS an
  // input carrying that picture or a newer one (real, kick, refresh or this catch-up) -- not when a
  // tick runs, a cache copy succeeds or the gate lets it through. An identity change cancels it; a
  // re-rejection keeps it; at most kCatchupMaxAttempts served attempts, then the 1 Hz refresh is the
  // fallback. No new data, no new debt: there is no periodic catch-up. REMOTE60_NATIVE_CATCHUP=0 off.
  struct CatchupDebt {
    bool owed = false;
    uint64_t dueUs = 0;
    uint64_t sessionEpoch = 0, inputEpoch = 0, streamGeneration = 0;
    uint64_t version = 0;                            // content version of the skipped real capture
    std::shared_ptr<std::vector<uint8_t>> payload;   // its pixels (null on a path without a CPU copy)
    uint32_t attempts = 0;                           // served attempts for this debt
  };
  static constexpr uint32_t kCatchupMaxAttempts = 8;
  bool catchupEnabled = true;
  CatchupDebt catchup;
  // telemetry: debts opened / attempts served / closed by the catch-up's own accepted input / closed by
  // another accepted input / attempts the encoder did not accept / dropped right before submission
  // because a newer real frame was waiting / cancelled on an identity change / given up after the
  // attempt bound / served with a newer cache than the skipped picture
  uint64_t catchupOwed = 0, catchupServed = 0, catchupAccepted = 0, catchupClosedByOther = 0,
           catchupNotAccepted = 0, catchupDroppedForNewer = 0, catchupCancelledIdentity = 0,
           catchupGaveUp = 0, catchupServedNewerCache = 0, catchupReRejected = 0;
  static uint64_t CatchupDelayUs(uint32_t fps) {
    return fps > 0 ? std::clamp<uint64_t>(2000000ULL / fps, 15000, kTrailingKickDelayUs) : kTrailingKickDelayUs;
  }
  // The input gate refused an input at atUs. A real capture opens (or re-targets) the debt to ITS
  // picture; a refused catch-up keeps the debt it was serving. Either way: due two frame periods on.
  void NoteAdmissionSkip(uint64_t atUs, uint32_t fps, bool catchupInput, uint64_t sessionEpoch,
                         uint64_t inputEpoch, uint64_t streamGeneration, uint64_t version,
                         std::shared_ptr<std::vector<uint8_t>> payload) {
    if (!catchupEnabled) return;
    if (catchupInput) {
      if (!catchup.owed) return;  // its debt was closed or cancelled meanwhile
      ++catchupReRejected;
    } else {
      if (!catchup.owed) ++catchupOwed;
      catchup.owed = true;
      catchup.sessionEpoch = sessionEpoch;
      catchup.inputEpoch = inputEpoch;
      catchup.streamGeneration = streamGeneration;
      catchup.version = version;
      catchup.payload = std::move(payload);
      catchup.attempts = 0;
    }
    catchup.dueUs = atUs + CatchupDelayUs(fps);
  }
  bool CatchupDue(uint64_t nowUs) const { return catchup.owed && nowUs >= catchup.dueUs; }
  // The debt still belongs to what the session watches now.
  bool CatchupIdentityMatches(uint64_t sessionEpoch, uint64_t inputEpoch, uint64_t streamGeneration) const {
    return catchup.sessionEpoch == sessionEpoch && catchup.inputEpoch == inputEpoch &&
           catchup.streamGeneration == streamGeneration;
  }
  void CancelCatchup(uint64_t* counter) {
    if (!catchup.owed) return;
    catchup = CatchupDebt{};
    if (counter) ++*counter;
  }
  // A catch-up attempt was handed to the encode stage: count it and push the next attempt out.
  void NoteCatchupServed(uint64_t nowUs, uint32_t fps) {
    ++catchup.attempts;
    ++catchupServed;
    catchup.dueUs = nowUs + CatchupDelayUs(fps);
  }
  // The encoder ACCEPTED an input (accepted-input counter moved). Every accepted input carries the
  // newest picture the host has (a real capture newer than the skipped one, or the cache that kick /
  // refresh / catch-up re-serve), so any of them pays the debt.
  void NoteInputAccepted(bool catchupInput) {
    if (!catchup.owed) return;
    catchup = CatchupDebt{};
    ++(catchupInput ? catchupAccepted : catchupClosedByOther);
  }

  // --- behaviour (Phase 2-1: former main() lambdas arm_trailing_kick / cancel_trailing_kick) ---
  static constexpr uint64_t kTrailingKickDelayUs = 150000;  // 150ms trailing edge
  // (Re)arm the trailing-edge kick to fire kTrailingKickDelayUs after atUs. Raw mode has no
  // encoder to flush, so it is a no-op there.
  void Arm(uint64_t atUs, bool useH264, uint32_t fps = 0) {
    if (!useH264) return;
    pending = true;
    // Two frame periods distinguish an idle encoder from normal continuous input. A fixed
    // 150ms debounce made every final typed character wait that long on asynchronous MFTs.
    const uint64_t delayUs = fps > 0
        ? std::clamp<uint64_t>(2000000ULL / fps, 15000, kTrailingKickDelayUs)
        : kTrailingKickDelayUs;
    dueAtUs = atUs + delayUs;
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
