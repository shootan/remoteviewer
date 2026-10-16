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

  // --- awaiting the catch-up's AU (motion-settle r9, Codex r9 s2) ---
  // ACCEPTED is not DELIVERED: an async MFT may take the lone catch-up input and return its AU only
  // inside a later encode call (r8: 0.4-0.6 s, released together with the next kick / 1 Hz refresh, 7 of
  // 9 runs). Nothing re-armed the trailing kick for a synthetic input, so the picture waited for the
  // refresh. Acceptance closes the submission debt above (never re-submitted per tick) and opens this
  // separate output-progress state, bound to the codec instance, the session / input epoch, the stream
  // generation and the accepted input's ordinal / content version. It closes when an AU of the same
  // instance and input epoch with that ordinal or a later one comes out (outputs follow accepted order);
  // otherwise kAwaitKickDelayUs after acceptance ONE extra input -- an ordinary trailing-edge kick, gated
  // like any delta -- lets the encode call collect it. Another input accepted meanwhile already advanced
  // the encoder: the extra kick is dropped (merged). Still nothing after the kick: exhausted, the 1 Hz
  // refresh is the fallback and it is not counted as delivered. The kick never re-opens this state.
  // r10 (Codex r10 s1): r8 showed the AU was usually READY and simply never collected (the host collects
  // only inside an encode call). While the await is open, every main-loop tick first COLLECTS what the
  // MFT has, without an input (H264Encoder::collect_outputs); an AU that satisfies the await ends it
  // there and no kick input is ever made. Only an await still unsatisfied at kAwaitKickDelayUs gets the
  // one kick (AwaitWantsKick). AwaitMode::CollectOnly (experiment builds only) never kicks -- the
  // diagnostic arm that separates "nobody collected" from "the MFT needed another input".
  enum class AwaitEnd : uint8_t { Observed, Exhausted, Deferred, Cancelled };
  struct CatchupAwait {
    bool active = false;
    uint64_t codecInstance = 0, sessionEpoch = 0, inputEpoch = 0, streamGeneration = 0;
    uint64_t ordinal = 0;             // accepted-input ordinal of the catch-up input
    uint64_t version = 0;             // content version it carried
    uint64_t acceptedUs = 0;
    uint64_t kickDueUs = 0;           // when the one extra input may go
    uint64_t kickDeadlineUs = 0;      // gate / queue may delay that input this long, then deferred
    bool kickSubmitted = false;       // the extra input was handed to the encode stage
    bool kickAccepted = false;        // ... and the encoder took it
    bool advancedByOther = false;     // another input was accepted after the catch-up (kick merged)
  };
  enum class AwaitMode : uint8_t { Kick = 0, CollectOnly = 1 };
  AwaitMode awaitMode = AwaitMode::Kick;
  static constexpr uint64_t kAwaitKickDelayUs = 150000;      // Codex r9: fixed at the existing 150 ms
  static constexpr uint64_t kAwaitKickDeadlineUs = 1000000;  // bounded wait for the gate / queue
  CatchupAwait await;
  // telemetry: opened / AU in the catch-up's own call (no await) / AU collected later, by which call /
  // extra kicks submitted / accepted / gate-refused / dropped because another input advanced / ended
  // without the AU after the kick (exhausted) / deferred to the refresh / cancelled (identity, instance,
  // pause)
  uint64_t awaitOpened = 0, awaitOutputOwnCall = 0, awaitObservedByKick = 0, awaitObservedByOther = 0,
           awaitKickSubmitted = 0, awaitKickAccepted = 0, awaitKickRefused = 0, awaitKickMerged = 0,
           awaitExhausted = 0, awaitDeferred = 0, awaitCancelled = 0;
  // r10: satisfied by an output-only collect (no input) / collect calls made / AUs they drained / AUs of
  // an exhausted or deferred await seen later (its real delivery time is logged, never counted as
  // the await's success)
  uint64_t awaitObservedByCollect = 0, awaitCollectCalls = 0, awaitCollectAus = 0, awaitLateOutputs = 0;
  // r10: after Exhausted / Deferred the ordinal is still watched so its actual output time is known.
  struct AwaitLateWatch {
    bool active = false;
    uint64_t codecInstance = 0, inputEpoch = 0, ordinal = 0, acceptedUs = 0, endedUs = 0;
    AwaitEnd how = AwaitEnd::Exhausted;
  };
  static constexpr uint64_t kAwaitLateWatchUs = 5000000;  // stop watching 5 s after the await ended
  AwaitLateWatch lateWatch;
  // The one kick is wanted: the await is still open at its due time and this build kicks at all.
  bool AwaitWantsKick(uint64_t nowUs) const { return awaitMode == AwaitMode::Kick && AwaitKickDue(nowUs); }
  // An AU the late watch was waiting for (same rules as AwaitSatisfiedBy).
  bool LateWatchSatisfiedBy(uint64_t codecInstance, uint64_t auInputEpoch, uint64_t auOrdinal) const {
    return lateWatch.active && auOrdinal != 0 && codecInstance == lateWatch.codecInstance &&
           auInputEpoch == lateWatch.inputEpoch && auOrdinal >= lateWatch.ordinal;
  }
  bool LateWatchExpired(uint64_t nowUs) const {
    return lateWatch.active && nowUs >= lateWatch.endedUs + kAwaitLateWatchUs;
  }
  // The catch-up input was accepted (ordinal) and its own call returned no AU for it.
  void OpenAwait(uint64_t codecInstance, uint64_t sessionEpoch, uint64_t inputEpoch, uint64_t streamGeneration,
                 uint64_t ordinal, uint64_t version, uint64_t nowUs) {
    await = CatchupAwait{};
    await.active = true;
    await.codecInstance = codecInstance;
    await.sessionEpoch = sessionEpoch;
    await.inputEpoch = inputEpoch;
    await.streamGeneration = streamGeneration;
    await.ordinal = ordinal;
    await.version = version;
    await.acceptedUs = nowUs;
    await.kickDueUs = nowUs + kAwaitKickDelayUs;
    await.kickDeadlineUs = nowUs + kAwaitKickDelayUs + kAwaitKickDeadlineUs;
    ++awaitOpened;
  }
  // An AU came out. Only an output of the SAME codec instance and input epoch whose accepted ordinal
  // is the catch-up's or later counts (an older instance's late output or a pre-epoch AU never does).
  bool AwaitSatisfiedBy(uint64_t codecInstance, uint64_t auInputEpoch, uint64_t auOrdinal) const {
    return await.active && auOrdinal != 0 && codecInstance == await.codecInstance &&
           auInputEpoch == await.inputEpoch && auOrdinal >= await.ordinal;
  }
  bool AwaitIdentityMatches(uint64_t codecInstance, uint64_t sessionEpoch, uint64_t inputEpoch,
                            uint64_t streamGeneration) const {
    return await.codecInstance == codecInstance && await.sessionEpoch == sessionEpoch &&
           await.inputEpoch == inputEpoch && await.streamGeneration == streamGeneration;
  }
  // The extra kick may be tried now (it still has to pass the identity / queue / gate checks).
  bool AwaitKickDue(uint64_t nowUs) const {
    return await.active && !await.kickSubmitted && !await.advancedByOther && nowUs >= await.kickDueUs;
  }
  // Unresolved past the deadline (the kick could not go -- gate refused it, queue busy -- or its call
  // never reached the acceptance check): hand over to the refresh. Bounds the state in every case.
  bool AwaitKickExpired(uint64_t nowUs) const {
    return await.active && nowUs >= await.kickDeadlineUs;
  }
  void EndAwait(AwaitEnd how, uint64_t nowUs = 0) {
    if (!await.active) return;
    if (how == AwaitEnd::Exhausted || how == AwaitEnd::Deferred) {
      lateWatch = AwaitLateWatch{true, await.codecInstance, await.inputEpoch, await.ordinal, await.acceptedUs,
                                 nowUs, how};
    }
    switch (how) {
      case AwaitEnd::Observed: break;  // the caller counts by whom
      case AwaitEnd::Exhausted: ++awaitExhausted; break;
      case AwaitEnd::Deferred: ++awaitDeferred; break;
      case AwaitEnd::Cancelled: ++awaitCancelled; break;
    }
    await = CatchupAwait{};
  }
  // An encode call accepted an input while the AU is awaited. awaitKickInput: it is the extra kick.
  void NoteAwaitInputAccepted(bool awaitKickInput) {
    if (!await.active) return;
    if (awaitKickInput) {
      await.kickAccepted = true;
      ++awaitKickAccepted;
    } else if (!await.advancedByOther) {
      await.advancedByOther = true;  // that input moves the encoder on; no extra kick (merged)
      if (!await.kickSubmitted) ++awaitKickMerged;
    }
  }
  // End of an encode call that did NOT bring the awaited AU: the kick's call ends it as exhausted; a
  // call of another input leaves it to that input's own trailing edge / the refresh (deferred).
  void NoteAwaitCallEnded(bool awaitKickInput, bool inputAccepted, uint64_t nowUs = 0) {
    if (!await.active || !inputAccepted) return;
    if (awaitKickInput) {
      EndAwait(AwaitEnd::Exhausted, nowUs);
    } else if (await.advancedByOther) {
      EndAwait(AwaitEnd::Deferred, nowUs);
    }
  }

  // --- behaviour (Phase 2-1: former main() lambdas arm_trailing_kick / cancel_trailing_kick) ---
  static constexpr uint64_t kTrailingKickDelayUs = 150000;  // 150ms trailing edge (see kAwaitKickDelayUs)
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
