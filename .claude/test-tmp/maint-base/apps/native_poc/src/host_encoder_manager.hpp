#pragma once

// Encoder management state (Nv12PendingRelease, EncoderState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>

#include "host_capture_session.hpp"
#include "host_encoder_budget.hpp"
#include "host_frame_gate.hpp"
#include "host_gop_clamp_detector.hpp"
#include "mf_h264_codec.hpp"
#include "poc_protocol.hpp"
#include "host_epoch_gate.hpp"
#include "host_rate_governor.hpp"
#include "encode_resolution_ladder.hpp"
#include "host_keyframe_reason.hpp"

namespace remote60::native_poc {

struct InputRouterState;
struct RateControlState;
struct SenderState;

// Zero-copy encode bookkeeping. A surface handed to the MFT stays reserved until the
// encoder's total output count passes the frame's submission -- only then is its texture
// provably no longer being read.
struct Nv12PendingRelease {
  int32_t slot = -1;
  uint64_t generation = 0;
  uint64_t requiredOutputs = 0;
};

// Encoder management (Phase 1-5 state struct): the MFT wrapper, the active/nominal encode
// geometry and rate parameters (what the encoder runs at right now, as opposed to what the user
// asked for -- see RateControlState ceilings), the runtime-tune request from the control thread,
// the keyframe-request token bucket, the force-key submit latch, the zero-copy NV12 surface
// bookkeeping, the output-liveness (starvation) heartbeat, and the stats-interval encode counters.
// See the comment blocks in main() (refit debounce, force-key latch, starvation heartbeat) for
// the rationale of each group.
// thread: main encode loop owns everything except the keyframe-request token bucket, which the
// control thread refills/consumes and the main loop resets on reconnect. (Runtime tune requests
// moved to MainLoopMailbox in Phase 4.) The bucket is guarded by keyReqMu rather than made atomic
// field by field,
// because refill/check/consume is one transaction: three separate atomics would still let a reset
// land in the middle of it. (Ledger H-04.)
struct EncoderState {
  H264Encoder codec;
  // quality r5: the closed loop on the quantiser ceiling (host_rate_governor.hpp). Off when the
  // configured ceiling is 0 or REMOTE60_NATIVE_RATE_GOVERNOR=0.
  RateGovernor rateGovernor{RateGovernorConfig{H264Encoder::configured_max_qp()}};
  uint64_t rateGovernorLastTxBytes = 0;
  uint32_t rateGovernorBitrate = 0;
  uint32_t rateGovernorUserFps = 0;  // r6: the user's frame rate the loop was started against
  // Governor assist for the hard wire cap (bitrate-hard-cap r1): the governor judges the GENERATED
  // load (bytes sent + the bytes the gate skipped to meet the cap), normalised to the real tick
  // length -- not just the sent bytes, which the cap pins at the target so the loop would never
  // step the quality down under motion. These track the previous tick's cumulative values.
  uint64_t rateGovernorLastTxFrames = 0;
  uint64_t rateGovernorLastSkips = 0;
  uint64_t rateGovernorLastTickUs = 0;
  // Last tick's average sent-frame size, so a tick in which a single large AU spanned the whole
  // interval (framesDelta == 0) does not value the gate's skipped frames at 0 bytes and misread an
  // overloaded stream as calm. (bitrate-hard-cap r2.)
  uint64_t rateGovernorLastAvgFrameBytes = 0;
  struct PendingTarget { uint32_t w=0, h=0, fps=0, bitrate=0, keyint=0; } retryTarget;
  bool targetPending = false;
  bool codecNeedsInit = false;
  uint64_t targetRetryAtUs = 0;
  uint32_t targetFailures = 0;
  uint64_t encodeErrorSinceUs = 0;
  uint64_t repairWindowStartUs = 0;
  uint32_t repairsInWindow = 0;
  bool repairExhausted = false;

  void RequestOutputRepair(uint64_t nowUs) {
    if (targetPending) return;
    if (!repairWindowStartUs || nowUs - repairWindowStartUs > 60000000ULL) {
      repairWindowStartUs = nowUs; repairsInWindow = 0;
    }
    if (++repairsInWindow > 3) { repairExhausted = true; return; }
    retryTarget = {nominalEncodeW, nominalEncodeH, activeFps, activeBitrate, activeKeyint};
    codecNeedsInit = targetPending = true;
    targetRetryAtUs = nowUs;
  }
  bool mfStarted = false;
  bool experimentEnabled = false;   // REMOTE60_NATIVE_ENCODED_EXPERIMENT(_FORCE)
  std::string tuneMode;             // REMOTE60_NATIVE_ENCODER_TUNE_MODE (default low_latency)
  // Viewer keyframe-request token bucket (REMOTE60_NATIVE_KEYREQ_*). The three config fields are
  // fixed after startup; the three live fields below are cross-thread -- see keyReqMu.
  uint32_t keyReqMinIntervalUs = 0;
  uint32_t keyReqTokenRefillUs = 0;
  uint32_t keyReqTokenCapacity = 0;
  std::mutex keyReqMu;
  double keyReqTokens = 0.0;
  uint64_t keyReqLastRefillUs = 0;
  uint64_t keyReqNextAllowedUs = 0;

  // Control thread: refill by elapsed time, then take one token if the minimum interval has also
  // passed. Returns whether the request is allowed; *outTokens is the post-decision level, for the
  // throttle log. Formerly open-coded in the ControlRequestKeyFrame handler, where it raced the
  // main loop's reconnect reset on plain double/uint64 fields.
  bool TryTakeKeyRequestToken(uint64_t nowUs, double* outTokens) {
    std::lock_guard<std::mutex> lk(keyReqMu);
    if (keyReqLastRefillUs == 0) keyReqLastRefillUs = nowUs;
    if (nowUs > keyReqLastRefillUs) {
      const double refill = static_cast<double>(nowUs - keyReqLastRefillUs) /
                            static_cast<double>(keyReqTokenRefillUs);
      if (refill > 0.0) {
        keyReqTokens = std::min<double>(static_cast<double>(keyReqTokenCapacity), keyReqTokens + refill);
        keyReqLastRefillUs = nowUs;
      }
    }
    const bool minIntervalOk = (keyReqNextAllowedUs == 0 || nowUs >= keyReqNextAllowedUs);
    const bool allowed = (keyReqTokens >= 1.0) && minIntervalOk;
    if (allowed) {
      keyReqTokens -= 1.0;
      keyReqNextAllowedUs = nowUs + keyReqMinIntervalUs;
    }
    if (outTokens) *outTokens = keyReqTokens;
    return allowed;
  }
  // Main loop: a new session starts with a full bucket.
  void ResetKeyRequestBucket() {
    std::lock_guard<std::mutex> lk(keyReqMu);
    keyReqTokens = static_cast<double>(keyReqTokenCapacity);
    keyReqLastRefillUs = 0;
    keyReqNextAllowedUs = 0;
  }
  // The runtime tune request used to live here as five atomics the control thread stored one by
  // one before raising tunePending. It is a MainLoopMailbox request now. (Phase 4.)
  bool tuneManualOverride = false;
  // Encode geometry: initial fit, active (running), nominal (pre-aspect-fit box of the quality
  // level), the source size the active size was fitted against, and the refit debounce.
  uint32_t encodeW = 0;
  uint32_t encodeH = 0;
  uint32_t activeEncodeW = 0;
  uint32_t activeEncodeH = 0;
  uint32_t nominalEncodeW = 0;
  uint32_t nominalEncodeH = 0;
  uint32_t encodeSourceW = 0;
  uint32_t encodeSourceH = 0;
  uint32_t pendingRefitW = 0;
  uint32_t pendingRefitH = 0;
  uint64_t pendingRefitSinceUs = 0;
  // Active rate parameters.
  uint32_t activeFps = 0;
  // encoder-rc-budget C1: R and E are separate state, with one owner each.
  //   activeBitrate      -- R, the WIRE budget the user / ABR / M9 allow for this stream (bps). The hard
  //                         wire cap, the burst ledger, the pacing peak and the governor's target
  //                         follow it; it is what every ApplyTarget caller passes as `targetBitrate`.
  //   desiredEncoderBps  -- E, the payload mean the encoder should run at for the current R
  //                         (EncoderBudgetFor: R minus parity, headers, NACK reserve, overshoot slack).
  //   codec.configured_bitrate() -- the E the LIVE instance actually holds (applied_encoder_bps()).
  //                         Set only by a successful initialize() or an Applied live reconfigure, never
  //                         by an S_OK that the profile does not trust (C3).
  // The conversion R -> E happens in EncoderBudgetFor and nowhere else; nothing downstream subtracts
  // headroom a second time.
  uint32_t activeBitrate = 0;
  uint32_t desiredEncoderBps = 0;
  uint32_t wireMtu = 1200;  // clamp_udp_mtu(args.udpMtu), set at startup; the header-cost input of the budget
  // C3: coalescing of E changes that need a new instance (a profile without a live mean). Its 2 s
  // interval is a policy value awaiting field verification on the NVIDIA profile, not a measured optimum.
  EncoderRetargetScheduler encoderRetarget;
  // C4: the governor's current ceiling, pinned in the codec for the next legacy rebuild
  // (codec.set_initial_max_qp) instead of being restored late with a post-type SetValue.
  uint64_t rateGovernorCodecInstance = 0;  // the codec instance the governor's ladder was configured for
  // r3 (Codex S2): the governor's desired ceiling, the LIVE instance's ceiling and the NEXT instance's
  // seed are reconciled in one place after every decision that moves the loop (Reset, OnSecond, a
  // failed fps apply) -- the policy is "the loop's current ceiling is the ceiling": a Reset brings the
  // live legacy instance back to the floor too, not only the next instance. Bounded: a ceiling the MFT
  // keeps refusing or holding is retried kGovernorQpApplyMaxFailures times, then held as degraded
  // (logged, instance left as it is) until the desired value changes. A static-ceiling profile is never
  // poked: its plan owns the ceiling and the seed is ignored by its plan.
  uint32_t governorQpApplyFailures = 0;
  uint32_t governorLastDesiredQp = 0;
  uint64_t governorFailInstance = 0;  // r4 Q1c: the failure budget belongs to (instance, desired)
  static constexpr uint32_t kGovernorQpApplyMaxFailures = 3;
  struct CeilingReconcile {
    bool attempted = false;                                // a SetValue was issued to the live instance
    RcRuntimeResult result = RcRuntimeResult::Unchanged;   // its answer (Unchanged when nothing was issued)
    uint32_t desired = 0;
    uint32_t codecBefore = 0;                              // the instance's requested ceiling before
    bool degraded = false;                                 // held after the bounded retries
  };
  CeilingReconcile ReconcileGovernorCeiling(uint32_t desiredQp, uint32_t configuredQp) {
    CeilingReconcile out;
    out.desired = desiredQp;
    out.codecBefore = codec.plan_max_qp();
    codec.set_initial_max_qp(desiredQp == configuredQp ? 0u : desiredQp);  // the next instance's seed
    if (desiredQp != governorLastDesiredQp || codec.instance_id() != governorFailInstance) {
      // r4 Q1c: a new target OR a new instance gets its own bounded attempts; the old instance's spent
      // budget does not follow the same desired onto its successor.
      governorLastDesiredQp = desiredQp;
      governorFailInstance = codec.instance_id();
      governorQpApplyFailures = 0;
    }
    if (!codec.started() || !codec.runtime_max_qp_live()) return out;  // no instance, or the plan owns it
    // r4 Q1b: judged on the CEILING'S OWN field. Another field's mismatch (a stuck mean) is not a reason
    // to re-issue a confirmed ceiling -- that was a whole-plan rewrite every tick that the budget never saw.
    if (codec.plan_max_qp() == desiredQp && codec.rc_max_qp_field_applied()) {
      governorQpApplyFailures = 0;  // confirmed in force: any older failure state is stale
      return out;
    }
    if (governorQpApplyFailures >= kGovernorQpApplyMaxFailures) {
      out.degraded = true;
      return out;
    }
    out.attempted = true;
    out.result = codec.set_max_qp(desiredQp == configuredQp ? 0u : desiredQp);
    if (out.result == RcRuntimeResult::Applied || out.result == RcRuntimeResult::Unchanged) governorQpApplyFailures = 0;
    else ++governorQpApplyFailures;
    out.degraded = governorQpApplyFailures >= kGovernorQpApplyMaxFailures;
    return out;
  }
  static EncoderBudgetPolicy BudgetPolicy() {
    EncoderBudgetPolicy p;
    // Field tuning levers for the two allowances (percent). Not test switches: the computed budget is
    // the product behaviour; these move its margins without a rebuild.
    if (const char* v = std::getenv("REMOTE60_NATIVE_NACK_RESERVE_PERCENT"); v && *v) p.nackReservePercent = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    if (const char* v = std::getenv("REMOTE60_NATIVE_ENCODER_OVERSHOOT_PERCENT"); v && *v) p.encoderOvershootPercent = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    return p;
  }
  WireGeometry WireGeometryNow() const {
    WireGeometry g;
    g.mtuBytes = wireMtu;
    g.chunkHeaderBytes = static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
    g.fecGroupSize = kUdpVideoFecGroupSize;
    return g;
  }
  /**
   * E for a wire budget R under the live geometry at the frame rate the instance will run: the ONE
   * conversion point (C1). The frame rate matters because the per-AU cost (a parity datagram per
   * group, headers per datagram, ceil at every step) is paid per frame -- at 1.5 Mbps / 60 fps an AU
   * of ~2 KB is two chunks and a parity, far above the large-AU asymptote (r2, Codex).
   */
  uint32_t EncoderBudgetFor(uint32_t wireBps, uint32_t fps) const {
    return encoder_budget_for(wireBps, WireGeometryNow(), BudgetPolicy(), fps).encoderBps;
  }
  /**
   * The PAYLOAD the wire can carry at R (R minus the nominal margin, divided by the parity and header
   * cost at this frame rate) -- the unit the rate governor's generated-payload figure is compared
   * against (r2, Codex R4). E = this / (1 + overshoot allowance); the governor's over/under bands sit
   * on this number, not on R (a wire budget) and not on E (a target with slack removed).
   */
  uint32_t PayloadBudgetFor(uint32_t wireBps, uint32_t fps) const {
    return encoder_budget_for(wireBps, WireGeometryNow(), BudgetPolicy(), fps).payloadWireBps;
  }
  /** What a (re)initialize of the codec outside ApplyTarget hands it as the mean: E for the active R. */
  uint32_t encoder_bitrate_for_init() const {
    return desiredEncoderBps != 0 ? desiredEncoderBps : EncoderBudgetFor(activeBitrate, activeFps);
  }
  /**
   * The E the live instance was GIVEN (its configured mean), 0 when there is none. Whether the MFT
   * confirmed the plan is a separate state (codec.rc_plan_state / rc_plan_effective): a mismatch is
   * reported, it does not make the instance "unconfigured" -- that would re-build it every interval.
   */
  uint32_t applied_encoder_bps() const { return codec.started() ? codec.configured_bitrate() : 0; }
  uint32_t keyintOverride = 0;      // REMOTE60_NATIVE_KEYINT_OVERRIDE (0 = off)
  uint32_t activeKeyint = 0;
  uint64_t activeFrameIntervalUs = 0;
  uint64_t activePacingFrameIntervalUs = 0;
  // Force-key: next input must be an IDR; submit latch so one request forces one input.
  bool forceKeyNext = true;
  // Why forceKeyNext is set (host_keyframe_reason.hpp), accumulated until the key AU is
  // accepted and logged. Diagnostic only; nothing decides on it.
  uint32_t keyReasons = kHostKeyReasonInitial;
  // quality r3: which priority the nominal box was last chosen for. A capture that switches
  // between window and desktop re-chooses the box for its own priority (ApplyConfirmedCapture-
  // Geometry) instead of inheriting one picked for the other -- so the size depends on what is
  // captured now, not on the order things happened. manualEncodeBox: --encode-width/height set a
  // box the user chose, never re-chosen. uiOverviewActive: the picker overview owns the box.
  EncodePriority boxPriority = EncodePriority::Standard;
  bool manualEncodeBox = false;
  bool uiOverviewActive = false;
  uint64_t lastKeyAcceptedUs = 0;  // qpc of the previous key AU accepted for sending (diagnostic)
  // quality r4: real (non-kick) inputs handed to the encoder since the last key AU was accepted --
  // of ANY reason. The periodic key is due when this reaches activeKeyint. It used to be
  // `captureSeq % keyint`, which ignored every other key: a tune or selection IDR at seq 280 was
  // followed by a "scheduled" one at 282, and a kick frame that pushed the encoder's own GOP over
  // its boundary made the MFT emit an IDR the host then repeated (reasons=none, then scheduled).
  uint32_t realInputsSinceKey = 0;
  // stutter-keyframe r1 (observe-1): snapshot of stats.wireOverloadSkipCount at the previous key AU.
  // The delta of wireOverloadSkipCount between two keys is how many capture frames the cap-gate
  // dropped during the window that ended with this IDR -- the periodic-stutter signal, so each
  // [keyframe] line carries the skips its own period cost.
  uint64_t wireSkipAtLastKey = 0;
  void RequestKey(uint32_t reason) {
    forceKeyNext = true;
    keyReasons |= reason;
  }
  uint64_t forceKeySubmittedAtUs = 0;
  // Epoch gate (P11, host_epoch_gate.hpp): after a flush, nothing encoded from a pre-flush input
  // goes out and the first AU sent is the new epoch's IDR.
  EpochGate epochGate;
  // A02/A06: the codec latched "provenance invalid" and only a rebuild re-synchronises its input
  // FIFO with what the MFT still holds. Stays true until one succeeds, so the retry does not
  // depend on a new frame arriving (a static screen would never bring one).
  bool provenanceResyncPending = false;
  uint64_t provenanceResyncCount = 0;
  uint64_t provenanceResyncFailed = 0;
  uint64_t provenanceDeferLogUs = 0;  // last "resync deferred" line (time-throttled)
  /**
   * Records what the codec reported about its input provenance after an encode call -- whatever
   * that call returned. An encode that fails LATE (the MFT accepted the input, then a drain or
   * ProcessOutput failed) can be the very call that overflowed the FIFO, and the stage returns
   * early on failure: without this the gate would stay open and the rebuild would never be
   * asked for, because the tick retry only looks at `provenanceResyncPending`. Idempotent.
   * Returns true the first time it latches, so the caller can log it once.
   */
  bool NoteProvenance(bool codecProvenanceInvalid) {
    if (!codecProvenanceInvalid || epochGate.provenanceInvalid) return false;
    epochGate.provenanceInvalid = true;
    provenanceResyncPending = true;
    return true;
  }

  /** What one encode call did: its own answer, and whether it was the call that lost provenance. */
  struct EncodeCallResult {
    bool ok = false;                 // what encode_frame_* returned
    bool provenanceLatched = false;  // NoteProvenance latched here (first time only: log once)
  };

  /**
   * One encode call and the provenance handling that must follow it. This is a
   * BEHAVIOUR-PRESERVING EXTRACTION of what the encode stage did inline, not a pure function:
   * it drives the encoder and moves this object's state. It exists so that no exit from an
   * encode call can skip the provenance check -- an encode that fails AFTER the MFT accepted the
   * input may be the very call that overflowed the accepted-input FIFO, and the stage returns
   * early on failure, so a check placed after the failure branch was never reached (A06). Every
   * encode path -- surface, BGRA, and the test's -- goes through here, so the order
   * `encode -> provenance -> the caller's failure handling` is a property of the code, not of
   * remembering to repeat it.
   */
  template <typename EncodeFn>
  EncodeCallResult RunEncodeCall(EncodeFn&& encodeCall) {
    EncodeCallResult result{};
    result.ok = encodeCall();
    result.provenanceLatched = NoteProvenance(codec.provenance_invalid());
    return result;
  }

  /** The stage's BGRA path, with the provenance handling it must not skip. */
  EncodeCallResult EncodeBgraWithProvenance(const uint8_t* bgra, uint32_t width, uint32_t height,
                                            uint32_t stride, bool forceKeyFrame,
                                            int64_t inputSampleTimeHns,
                                            std::vector<H264AccessUnit>* outUnits,
                                            H264EncodeFrameStats* encodeStats) {
    return RunEncodeCall([&] {
      return codec.encode_frame_bgra(bgra, width, height, stride, forceKeyFrame,
                                     inputSampleTimeHns, outUnits, encodeStats);
    });
  }

  /** The stage's zero-copy path, same contract. */
  EncodeCallResult EncodeSurfaceWithProvenance(ID3D11Texture2D* texture, bool forceKeyFrame,
                                               int64_t inputSampleTimeHns,
                                               std::vector<H264AccessUnit>* outUnits,
                                               H264EncodeFrameStats* encodeStats) {
    return RunEncodeCall([&] {
      return codec.encode_frame_surface(texture, forceKeyFrame, inputSampleTimeHns, outUnits,
                                        encodeStats);
    });
  }
  /**
   * A02/A06: rebuilds the encoder because its accepted-input FIFO lost provenance. Only a
   * rebuild helps -- clearing the FIFO alone would let the outputs the MFT still holds consume
   * the tags of new inputs and desynchronise again -- and the codec's latch clears exactly on a
   * successful initialize(). Takes one rebuild from the epoch gate's budget (3 per 10 s), so
   * this cannot become a re-initialisation loop; when the budget is spent, or the rebuild fails,
   * the request stays pending, the gate stays closed and the caller retries on a later tick
   * (a static screen never brings a frame to retry on). Returns true when the encoder is new.
   */
  bool TryProvenanceResync(CaptureState& capture, uint64_t nowUs) {
    if (!provenanceResyncPending) return false;
    if (!epoch_gate_take_reset_budget(epochGate, nowUs)) return false;  // budget spent: stay pending
    codec.shutdown();
    if (!codec.initialize(activeEncodeW, activeEncodeH, activeFps, encoder_bitrate_for_init(), activeKeyint)) {
      ++provenanceResyncFailed;
      return false;  // still invalid, still pending, gate still closed
    }
    encoderRetarget.NoteReinit(nowUs);
    ResetTimelineAnchors(capture);  // a new epoch: the gate re-judges and waits for its IDR
    ResetStarvationEpisode();
    forceKeySubmittedAtUs = 0;
    ++resetCount;
    ++provenanceResyncCount;
    consecutiveStaleFrames = 0;
    RequestKey(kHostKeyReasonProvenance);
    epochGate.provenanceInvalid = false;
    epoch_gate_note_reset(epochGate, nowUs);
    provenanceResyncPending = false;
    return true;
  }
  // Zero-copy NV12 surfaces reserved until the encoder has provably consumed them.
  std::deque<Nv12PendingRelease> nv12PendingReleases;
  bool surfaceEncodeHealthy = true;
  uint64_t nv12SurfaceEncodeCount = 0;
  uint32_t surfaceEncodeProbeCount = 0;
  uint64_t surfaceEncodeProbeSumUs = 0;
  // Output-liveness heartbeat (a starved async MFT returns empty on every call).
  uint64_t outputSamplesTotal = 0;
  uint64_t inputAcceptedTotal = 0;       // encode calls that handed a frame to the MFT
  uint64_t realInputAccepted = 0;        // ... of which carried a real captured frame
  uint64_t syntheticInputAccepted = 0;   // ... trailing-edge/bootstrap synthetic kicks
  uint64_t outputAuTotal = 0;            // cumulative output access units produced
  uint64_t lastOutputUs = 0;             // qpc of the last produced output (0 = never yet)
  uint64_t noOutputSinceUs = 0;          // qpc the current no-output streak began
  uint32_t acceptedNoOutputStreak = 0;   // consecutive accepted-input calls with no output
  uint64_t lastStarvationLogUs = 0;      // rate-limits the anomaly line to <=1/s
  uint64_t starveNeedInputAccum = 0;
  uint64_t starveHaveOutputAccum = 0;
  uint64_t starveNoEventAccum = 0;
  uint64_t starveNotAcceptingAccum = 0;
  uint64_t starveNeedMoreAccum = 0;
  uint64_t starveNeedInputOnlyCalls = 0;
  // Stats-interval encode counters.
  uint64_t encodedFrames = 0;
  uint64_t forceKeyInputCount = 0;       // key inputs handed to the encoder
  // stutter-keyframe r1/r2 F1: forced-key requests the MFT REJECTED (AVEncVideoForceKeyFrame SetValue
  // failed). A rejection does NOT arm the real in-flight latch; it sets a bounded retry back-off
  // (forceKeyRetryAtUs) so the next force is retried after an interval rather than every tick (which
  // would make every input ride the admit-always key gate under the cap), and a bounded streak
  // escalates to a repair. forceKeyRejectedCount is cumulative (diagnostic); forceKeyRejectStreak is
  // the consecutive run, reset on an accepted force or a key AU.
  uint64_t forceKeyRejectedCount = 0;
  uint64_t forceKeyRetryAtUs = 0;      // do not re-attempt a forced key before this qpc (back-off)
  uint32_t forceKeyRejectStreak = 0;   // consecutive rejections; triggers a repair at the threshold
  // stutter-keyframe r3 B / r4 G: runtime GOP-clamp detection from the real NAL cadence, fed by
  // PER-AU provenance (accepted-input ordinal + forced flag + raw NAL5), not host globals. The
  // last self-IDR's ordinal/epoch are the baseline the next interval is measured from; reset on a
  // fresh encoder init (ordinals restart) so no interval spans codec instances.
  GopClampDetector clampDetector;
  uint64_t lastSelfIdrOrdinal = 0;
  uint64_t lastSelfIdrEpoch = 0;
  uint64_t clampDetectorCodecId = 0;  // r4 G1: the codec instance the detector state belongs to
  uint32_t encodedSeq = 0;
  uint64_t encodeFailCount = 0;
  uint64_t resetCount = 0;
  uint32_t consecutiveStaleFrames = 0;

  // --- behaviour (Phase 2-1: former main() lambdas reset_encoder_starvation_episode /
  //     refresh_frame_intervals) ---
  void ResetStarvationEpisode() {
    noOutputSinceUs = 0;
    acceptedNoOutputStreak = 0;
    lastStarvationLogUs = 0;
    starveNeedInputAccum = starveHaveOutputAccum = starveNoEventAccum = 0;
    starveNotAcceptingAccum = starveNeedMoreAccum = starveNeedInputOnlyCalls = 0;
  }
  // Recompute the frame intervals from activeFps and push them to the capture submit limiter and
  // the static-frame gate.
  void RefreshFrameIntervals(CaptureState& capture, FrameGatingState& frameGating) {
    activeFrameIntervalUs =
        std::max<uint64_t>(1, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, activeFps)));
    // Encoded capture is callback-clocked below. Raw mode uses the main tick at the exact
    // requested cadence.
    activePacingFrameIntervalUs = activeFrameIntervalUs;
    capture.submitMinIntervalUs.store(activeFrameIntervalUs, std::memory_order_release);
    frameGating.staticIntervalUs =
        std::max<uint64_t>(activeFrameIntervalUs, std::max<uint64_t>(1, 1000000ULL / frameGating.staticFps));
  }
  // AU timeline anchor (paired with CaptureState::timelineOriginUs; -1 = not yet anchored).
  int64_t auTimelineOriginUs = -1;

  // --- behaviour (Phase 2-5: former main() lambdas; bodies in host_encoder_manager.cpp except the
  //     one-liner) ---
  // Forget both timeline anchors so the next frame re-anchors capture vs AU timing.
  void ResetTimelineAnchors(CaptureState& capture) {
    capture.timelineOriginUs = -1;
    auTimelineOriginUs = -1;
    // Every caller is a flush / restart / confirmed geometry change / encoder reset: the boundary
    // after which an AU from an older input is pre-flush. Most callers also force the next input
    // to be an IDR; the epoch gate re-forces it itself if one does not (P11).
    capture.inputEpoch.fetch_add(1, std::memory_order_acq_rel);
    // stutter-keyframe r3 S2: the forced-key rejection back-off/streak belong to the previous encoder
    // and input epoch. Clear them at THIS boundary (codec reinit / flush / geometry) so a stale
    // 300ms back-off or a carried-over streak cannot suppress or near-repair the new epoch's selection/
    // first-frame force. This is a real codec/input-epoch boundary, not a per-request reset, so it does
    // not let an ordinary viewer re-request bypass the back-off of a still-rejecting same-epoch encoder.
    forceKeyRetryAtUs = 0;
    forceKeyRejectStreak = 0;
  }
  // The single choke point every encoder parameter change goes through (runtime tune, capture-UI
  // overview/focus, ABR/M9 refit): fits the box to the source aspect, rebuilds or re-tunes the MFT,
  // publishes the input domain and the UDP pacing budget.
  bool ApplyTarget(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                   InputRouterState& inputRouter, SenderState& sender, uint32_t targetW, uint32_t targetH,
                   uint32_t targetFps, uint32_t targetBitrate, uint32_t targetKeyint);
  // encoder-rc-budget C1: the wire side of a target -- pacing peak and the hard wire cap follow R at
  // once, before (and independently of) any encoder transition. Logs the same-clock apply marker.
  void PublishWireBudget(SenderState& sender, uint32_t wireBps);
  // A confirmed source-size change: re-fit the encode target to the new geometry immediately.
  // `rate` (r3): when given, a change of capture priority (window <-> desktop) re-chooses the box
  // from the ladder for the new priority at the current ABR profile and bitrate.
  void ApplyConfirmedCaptureGeometry(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                                     InputRouterState& inputRouter, SenderState& sender, uint32_t newW,
                                     uint32_t newH, const char* reason, bool allowWindowOverride = false,
                                     RateControlState* rate = nullptr);
  // Capture-UI overview (lower bitrate/fps/size) vs focus mode, derived from the live ceilings.
  bool ApplyCaptureUiQualityMode(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                                 InputRouterState& inputRouter, SenderState& sender, RateControlState& rate,
                                 bool useH264, bool overviewMode, uint64_t nowUs);
};

}  // namespace remote60::native_poc
