#pragma once

// Encoder management state (Nv12PendingRelease, EncoderState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "host_capture_session.hpp"
#include "host_frame_gate.hpp"
#include "mf_h264_codec.hpp"
#include "host_epoch_gate.hpp"

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
  uint32_t activeBitrate = 0;
  uint32_t keyintOverride = 0;      // REMOTE60_NATIVE_KEYINT_OVERRIDE (0 = off)
  uint32_t activeKeyint = 0;
  uint64_t activeFrameIntervalUs = 0;
  uint64_t activePacingFrameIntervalUs = 0;
  // Force-key: next input must be an IDR; submit latch so one request forces one input.
  bool forceKeyNext = true;
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
    if (!codec.initialize(activeEncodeW, activeEncodeH, activeFps, activeBitrate, activeKeyint)) {
      ++provenanceResyncFailed;
      return false;  // still invalid, still pending, gate still closed
    }
    ResetTimelineAnchors(capture);  // a new epoch: the gate re-judges and waits for its IDR
    ResetStarvationEpisode();
    forceKeySubmittedAtUs = 0;
    ++resetCount;
    ++provenanceResyncCount;
    consecutiveStaleFrames = 0;
    forceKeyNext = true;
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
  }
  // The single choke point every encoder parameter change goes through (runtime tune, capture-UI
  // overview/focus, ABR/M9 refit): fits the box to the source aspect, rebuilds or re-tunes the MFT,
  // publishes the input domain and the UDP pacing budget.
  bool ApplyTarget(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                   InputRouterState& inputRouter, SenderState& sender, uint32_t targetW, uint32_t targetH,
                   uint32_t targetFps, uint32_t targetBitrate, uint32_t targetKeyint);
  // A confirmed source-size change: re-fit the encode target to the new geometry immediately.
  void ApplyConfirmedCaptureGeometry(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                                     InputRouterState& inputRouter, SenderState& sender, uint32_t newW,
                                     uint32_t newH, const char* reason, bool allowWindowOverride = false);
  // Capture-UI overview (lower bitrate/fps/size) vs focus mode, derived from the live ceilings.
  bool ApplyCaptureUiQualityMode(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                                 InputRouterState& inputRouter, SenderState& sender, RateControlState& rate,
                                 bool useH264, bool overviewMode, uint64_t nowUs);
};

}  // namespace remote60::native_poc
