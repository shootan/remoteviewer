// See host_encoder_manager.hpp for the module summary. The member bodies below are the former
// apply_encoder_target / apply_confirmed_capture_geometry / apply_capture_ui_quality_mode lambdas
// of native_video_host_main.cpp, moved verbatim (host split refactor Phase 2-5): "encoder"
// aliases *this so the text reads unchanged; cross-calls were rewritten to member calls.

#include <winsock2.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "encode_resolution_ladder.hpp"
#include "host_abr.hpp"
#include "host_bgra_scale.hpp"
#include "host_key_period_policy.hpp"
#include "host_capture_session.hpp"
#include "host_encoded_sender.hpp"
#include "host_encoder_manager.hpp"
#include "host_frame_gate.hpp"
#include "host_input_router.hpp"
#include "host_net_io.hpp"

namespace remote60::native_poc {

bool EncoderState::ApplyTarget(CaptureState& capture, CaptureResources& res, FrameGatingState& frameGating,
                               InputRouterState& inputRouter, SenderState& sender, uint32_t targetW,
                               uint32_t targetH, uint32_t targetFps, uint32_t targetBitrate,
                               uint32_t targetKeyint) {
  EncoderState& encoder = *this;
  // The keyint A/B env override is enforced HERE, the single choke point every caller passes
  // (runtime tune, capture-UI overview/focus, ABR/M9 refit) -- pinning it in just one caller
  // let another quietly revert the override with its own cached keyint. Ceiling bookkeeping
  // upstream stays based on what the CLIENT actually requested.
  // stutter-keyframe r2 (step 4b): raise the periodic key period to the >=10s floor so periodic IDRs
  // -- the stutter source serialized under the hard cap -- become rare. Applied HERE, the single
  // choke point every caller passes, so the encoder's own GOP (codec.initialize below) AND the host
  // scheduled key (activeKeyint) extend together; removing only one would let the other key instead
  // (Codex contract 1). An explicit REMOTE60_NATIVE_KEYINT_OVERRIDE still wins verbatim (compute_
  // effective_keyint handles it). Forced keys -- recovery, first frame, selection/epoch -- bypass
  // activeKeyint, so this never delays them; the recovery roundtrip at a long GOP is proven by
  // host_recovery_roundtrip_test and viewer_udp_recovery_test scenario_long_gop_forced_recovery.
  {
    const uint32_t requestedKeyint = targetKeyint;
    targetKeyint = compute_effective_keyint({requestedKeyint, targetFps, encoder.keyintOverride});
    if (targetKeyint != requestedKeyint && targetKeyint != encoder.activeKeyint) {
      std::cout << "[native-video-host] key-period configured=" << requestedKeyint
                << " effective=" << targetKeyint << " fps=" << targetFps
                << " override=" << encoder.keyintOverride << " floorSec=" << kKeyPeriodSecondsFloor
                << "\n";
    }
  }
  const uint64_t nowUs = qpc_now_us();
  const auto& previousTarget = encoder.retryTarget;
  if (encoder.targetPending && nowUs < encoder.targetRetryAtUs &&
      previousTarget.w == targetW && previousTarget.h == targetH &&
      previousTarget.fps == targetFps && previousTarget.bitrate == targetBitrate &&
      previousTarget.keyint == targetKeyint) return false;
  encoder.retryTarget = {targetW, targetH, targetFps, targetBitrate, targetKeyint};
  const auto failed = [&] {
    encoder.targetPending = encoder.codecNeedsInit = true;
    encoder.targetFailures = std::min<uint32_t>(encoder.targetFailures + 1, 5);
    encoder.targetRetryAtUs = qpc_now_us() + (250000ULL << encoder.targetFailures);
    return false;
  };
  // Callers pass the nominal box for the current ABR/M9 level. Remember it so a later
  // source-size change can be re-fitted against the same budget instead of ratcheting down.
  encoder.nominalEncodeW = targetW;
  encoder.nominalEncodeH = targetH;
  fit_size_preserving_aspect(encoder.encodeSourceW, encoder.encodeSourceH, targetW, targetH, &targetW, &targetH);

  const bool keyintChanged = (targetKeyint != encoder.activeKeyint);
  const bool fpsChanged = (targetFps != encoder.activeFps);
  const bool resizeChanged = (targetW != encoder.activeEncodeW || targetH != encoder.activeEncodeH);
  // encoder-rc-budget C1: `targetBitrate` is R, the wire budget. E is derived from it here -- the one
  // conversion point -- and is what the encoder gets. The two are no longer the same number, and they
  // no longer move together: a lower E never lowers the wire cap, and a lower R reaches the limiter
  // now, before any encoder transition (which may be deferred, below).
  const bool wireChanged = (targetBitrate != encoder.activeBitrate);
  const uint32_t desiredE = encoder.EncoderBudgetFor(targetBitrate, targetFps);
  if (wireChanged ||
      (sender.wireCapEnabled && sender.wireCapBps.load(std::memory_order_relaxed) != static_cast<uint64_t>(targetBitrate))) {
    encoder.PublishWireBudget(sender, targetBitrate);
  }
  encoder.desiredEncoderBps = desiredE;
  const uint32_t appliedBefore = encoder.applied_encoder_bps();
  const uint64_t instanceBefore = encoder.codec.instance_id();
  // r2 (Codex R1): a deferral is judged against the CURRENT desired E on every call, including the
  // one where E has come back to what the instance runs -- that is the cancellation. Done up front so
  // no branch below can leave a stale deferral that the tick would keep waking up on.
  if (desiredE == appliedBefore) (void)encoder.encoderRetarget.Decide(nowUs, desiredE, appliedBefore);

  // One re-initialisation sequence for every reason (geometry/fps/keyint, a failed live change, a
  // new E on a profile without a live mean). The fresh instance is told E and the governor's current
  // legacy ceiling (codec.set_initial_max_qp, kept current by the stats tick) and gets its RC plan
  // inside initialize() -- before the media types where the profile needs that (C2).
  const auto reinitCodec = [&]() -> bool {
    encoder.codecNeedsInit = true;
    encoder.codec.shutdown();
    // The shutdown flushed the MFT, so every in-flight surface is released.
    for (const auto& pending : encoder.nv12PendingReleases) {
      res.captureReadback.ReleaseNv12Slot(pending.slot, pending.generation);
    }
    encoder.nv12PendingReleases.clear();
    encoder.surfaceEncodeHealthy = true;
    if (!encoder.codec.initialize(targetW, targetH, targetFps, desiredE, targetKeyint)) return false;
    encoder.encoderRetarget.NoteReinit(qpc_now_us());
    encoder.ResetTimelineAnchors(capture);
    encoder.ResetStarvationEpisode();
    encoder.clampDetector.Reset();  // r3 B: a fresh transform may clamp differently; re-detect
    encoder.lastSelfIdrOrdinal = 0;  // r4 G: ordinals restart per codec instance
    encoder.lastSelfIdrEpoch = 0;
    // shutdown+initialize discarded any pending key input; a stale latch here would delay the
    // fresh encoder's needed IDR by up to the 300ms retry window.
    encoder.forceKeySubmittedAtUs = 0;
    return true;
  };

  const char* action = "none";
  if (encoder.codecNeedsInit || keyintChanged || fpsChanged || resizeChanged) {
    if (!reinitCodec()) return failed();
    action = "reinit";
  } else if (desiredE != appliedBefore) {
    // C3: the live instance runs a different E. Ask it first; what it answers decides the path.
    const RcRuntimeResult live = encoder.codec.reconfigure_bitrate(desiredE);
    if (live == RcRuntimeResult::Applied) {
      action = "live";
    } else if (live == RcRuntimeResult::Unchanged) {
      action = "none";
    } else if (live == RcRuntimeResult::Unproven) {
      // This profile does not take a new mean on a running instance: a new E is a new instance, on
      // the same safe path as a geometry change -- but COALESCED: inside the minimum interval since the
      // last (re)initialisation the request is deferred to the interval's end, where the latest target
      // wins (the tick calls back in). R was published above regardless, so the wire is protected
      // while E lags. The forced key comes with the instance when it is built, not now.
      switch (encoder.encoderRetarget.Decide(nowUs, desiredE, appliedBefore)) {
        case EncoderRetargetScheduler::Verdict::ReinitNow:
          if (!reinitCodec()) return failed();
          action = "reinit";
          break;
        case EncoderRetargetScheduler::Verdict::Defer:
          action = "deferred";
          break;
        case EncoderRetargetScheduler::Verdict::NoChange:
          action = "none";
          break;
      }
    } else {
      // The live change was issued and rejected: the same rebuild as before this change.
      if (!reinitCodec()) return failed();
      action = "reinit-after-rejected-live";
    }
  }

  encoder.activeEncodeW = targetW;
  encoder.targetPending = encoder.codecNeedsInit = false;
  encoder.targetRetryAtUs = encoder.encodeErrorSinceUs = 0;
  encoder.targetFailures = 0;
  // The forced key an ENCODER change owes the viewer: a new instance (its stream starts over) or a
  // live mean change (its output changes). Not a pure wire-budget change, not a repeated target, not
  // a deferral -- a deferred change owes it when the new instance is built, and forcing one now would
  // send a large IDR at the very moment the wire budget was lowered, with the encoder still on the
  // old E. The keys a selection, a recovery or a new epoch demand are requested by those callers with
  // their own reasons and are untouched here (C4). (r2, Codex R1: the previous rule requested one on
  // every non-deferred call, so a cancelled deferral made the tick ask for a key every time.)
  const bool encoderOutputChanged = encoder.codec.instance_id() != instanceBefore || std::strcmp(action, "live") == 0;
  if (encoderOutputChanged) encoder.RequestKey(kHostKeyReasonEncoderTarget);
  encoder.activeEncodeH = targetH;
  encoder.activeFps = targetFps;
  encoder.activeBitrate = targetBitrate;
  encoder.activeKeyint = targetKeyint;
  inputRouter.domainW.store(encoder.activeEncodeW, std::memory_order_release);
  inputRouter.domainH.store(encoder.activeEncodeH, std::memory_order_release);
  if (wireChanged || std::strcmp(action, "none") != 0) {
    const EncoderBudgetBreakdown b = encoder_budget_for(targetBitrate, encoder.WireGeometryNow(), EncoderState::BudgetPolicy(), targetFps);
    std::cout << "[native-video-host] encoder-budget requestedWire=" << targetBitrate
              << " effectiveWire=" << sender.wireCapBps.load(std::memory_order_relaxed)
              << " desiredEncoder=" << desiredE << " appliedEncoder=" << encoder.applied_encoder_bps()
              << " rcEffective=" << (encoder.codec.rc_plan_effective() ? 1 : 0)
              << " payloadBudget=" << b.payloadWireBps
              << " action=" << action << " profile=" << rc_profile_name(encoder.codec.rc_profile())
              << " fecX1000=" << b.fecFactorX1000 << " headerX1000=" << b.headerFactorX1000
              << " auBytes=" << b.auBytesAtFps << " wireAtFpsBps=" << b.wireAtFpsBps
              << " keyRequested=" << (encoderOutputChanged ? 1 : 0)
              << " deferredUntilUs=" << (encoder.encoderRetarget.deferred ? encoder.encoderRetarget.deferredUntilUs : 0ULL)
              << "\n";
  }
  res.captureReadback.SetOutputSize(encoder.activeEncodeW, encoder.activeEncodeH);
  encoder.RefreshFrameIntervals(capture, frameGating);
  return true;
}

void EncoderState::PublishWireBudget(SenderState& sender, uint32_t wireBps) {
  // The pacing budget follows the WIRE budget. It used to be computed once at startup, so after an
  // ABR downshift frames kept leaving at the launch rate (bursts the network just asked us to stop),
  // and after an upshift sends were throttled below the new rate.
  const uint64_t pacePeakBps = sender.noPacingH264
                                   ? 0ULL
                                   : std::max<uint64_t>(sender.udpPacePeakFloorBps,
                                                        (static_cast<uint64_t>(wireBps) * sender.udpPacePeakPercent) / 100ULL);
  const uint32_t pacePeakBpsClamped = static_cast<uint32_t>(std::min<uint64_t>(pacePeakBps, 4000000000ULL));
  if (sender.pacePeakBps.load(std::memory_order_relaxed) != pacePeakBpsClamped) {
    sender.pacePeakBps.store(pacePeakBpsClamped, std::memory_order_relaxed);
    std::cout << "[native-video-host] pacing update udpPacePeakBps=" << pacePeakBpsClamped << " bitrate=" << wireBps << "\n";
  }
  // The hard wire cap follows R (ABR / user downshift lowers it; an upshift raises it) -- and only R:
  // the encoder's E is a different number and never reaches here (C1). Changing the rate preserves
  // the bucket's credit -- it is not a refill lane. (r1)
  if (sender.wireCapEnabled && sender.wireCapBps.load(std::memory_order_relaxed) != static_cast<uint64_t>(wireBps)) {
    sender.UpdateWireCap(static_cast<uint64_t>(wireBps));
    // r5 G4: a same-clock apply marker so a test (or field log) can pin the EXACT instant the wire cap
    // changed -- the independent apply point, not a rate observed later. qpc here is the sender/receiver
    // clock on loopback. Diagnostic only.
    std::cout << "[native-video-host] wirecap applied bitrate=" << wireBps << " appliedQpcUs=" << qpc_now_us() << "\n";
  }
}

void EncoderState::ApplyConfirmedCaptureGeometry(CaptureState& capture, CaptureResources& res,
                                                 FrameGatingState& frameGating, InputRouterState& inputRouter,
                                                 SenderState& sender, uint32_t newW, uint32_t newH,
                                                 const char* reason, bool allowWindowOverride,
                                                 RateControlState* rate) {
  EncoderState& encoder = *this;
  // An interactive window DRAG keeps the 0.4s settle path (per-frame MFT re-init would thrash),
  // so it bails here. A CONFIRMED window selection passes allowWindowOverride=true so the encode
  // target is re-fit to the final window geometry immediately -- otherwise the first IDR goes out
  // at the pre-selection encode size and a second, new-size IDR follows a frame later, forcing the
  // client to reconfigure twice and fire a keyframe-request storm.
  if (capture.windowModeActive && !allowWindowOverride) return;
  if (newW < 2 || newH < 2) return;
  // quality r3: the priority of what is captured NOW. If the box was chosen for the other one --
  // a window at 3000 chose the old ladder's box (or the window's own size), then the desktop came
  // back -- choose again, even when the source size did not change.
  EncodePriority want = encoder.boxPriority;
  bool repick = false;
  if (rate) {
    want = rate->PriorityFor(capture.windowModeActive.load(std::memory_order_acquire));
    repick = RateControlState::NeedsBoxRepick(encoder.boxPriority, want, encoder.manualEncodeBox,
                                              encoder.uiOverviewActive);
  }
  if (!repick && newW == encoder.encodeSourceW && newH == encoder.encodeSourceH) return;  // already fit to this source
  encoder.encodeSourceW = newW;
  encoder.encodeSourceH = newH;
  encoder.pendingRefitW = 0;
  encoder.pendingRefitH = 0;
  encoder.pendingRefitSinceUs = 0;
  const uint32_t prevEncW = encoder.activeEncodeW;
  const uint32_t prevEncH = encoder.activeEncodeH;
  // Confirmed change: no aspectClose skip. A smaller same-aspect source must still shrink
  // activeEncode to avoid upscaling. Passing the current nominal box re-fits activeEncode from
  // the new encodeSource aspect and rebuilds the MFT immediately, instead of after the 0.4s settle.
  uint32_t boxW = encoder.nominalEncodeW;
  uint32_t boxH = encoder.nominalEncodeH;
  uint32_t fps = encoder.activeFps;
  RateControlState::BoxPlan picked{};
  if (repick) {
    picked = rate->PlanBox(rate->abrProfile, encoder.activeBitrate, newW, newH, want);
    boxW = picked.width;
    boxH = picked.height;
    if (picked.fps > 0) fps = picked.fps;
  }
  if (encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, boxW, boxH, fps, encoder.activeBitrate, encoder.activeKeyint)) {
    if (repick) {
      rate->encodeLadderReduced = picked.reduced;
      std::cout << "[native-video-host] encode-box-repick reason=" << reason << " priority="
                << (want == EncodePriority::DesktopText ? "desktop_text" : "standard")
                << " from=" << (encoder.boxPriority == EncodePriority::DesktopText ? "desktop_text" : "standard")
                << " profile=" << rate->abrProfile << " bitrate=" << encoder.activeBitrate
                << " box=" << boxW << "x" << boxH << " fps=" << fps << "\n";
      encoder.boxPriority = want;
    }
    encoder.RequestKey(kHostKeyReasonGeometry);
    encoder.ResetTimelineAnchors(capture);
    std::cout << "[native-video-host] capture-geometry-confirmed reason=" << reason
              << " source=" << newW << "x" << newH
              << " encode=" << prevEncW << "x" << prevEncH
              << "->" << encoder.activeEncodeW << "x" << encoder.activeEncodeH << "\n";
  }
}

bool EncoderState::ApplyCaptureUiQualityMode(CaptureState& capture, CaptureResources& res,
                                             FrameGatingState& frameGating, InputRouterState& inputRouter,
                                             SenderState& sender, RateControlState& rate, bool useH264,
                                             bool overviewMode, uint64_t nowUs) {
  EncoderState& encoder = *this;
  if (!useH264) return true;
  // Derived from the live ceiling, not from the m9 level constants: those are frozen at
  // encoder initialization, so a host born at 3 Mbps regressed to its birth bitrate and
  // size every time the client left overview mode -- and set the manual override, which
  // kept ABR from ever repairing it. Same freeze as the ABR profiles, one more door in.
  // (The m9 adaptive levels themselves are still the frozen constants; that ladder is off
  // by default and needs its own pass before it can be trusted with live values.)
  const uint32_t focusBitrate = rate.abrHighBitrate;
  const uint32_t targetBitrate =
      overviewMode
          ? std::min<uint32_t>(focusBitrate,
                               std::max<uint32_t>(900000u, (focusBitrate * 50u) / 100u))
          : focusBitrate;
  const uint32_t targetFps =
      overviewMode ? std::max<uint32_t>(15u, (rate.userFpsCeiling * 67u) / 100u) : rate.userFpsCeiling;
  // r3: the focus box is the capture's own priority's box -- leaving the picker on a desktop at
  // 3000 used to land on the old ladder's 720p.
  const EncodePriority priority =
      rate.PriorityFor(capture.windowModeActive.load(std::memory_order_acquire));
  const auto sizeChoice = remote60::native_poc::choose_abr_profile_size(
      overviewMode ? 2 : 0, targetBitrate, capture.width, capture.height, rate.encodeLadderReduced,
      priority);
  const uint32_t targetKeyint =
      overviewMode ? std::max<uint32_t>(rate.userKeyintCeiling, 60u) : rate.userKeyintCeiling;
  if (!encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, sizeChoice.width, sizeChoice.height, targetFps, targetBitrate,
                            targetKeyint)) {
    return false;
  }
  rate.encodeLadderReduced = sizeChoice.reduced;
  encoder.boxPriority = priority;
  encoder.uiOverviewActive = overviewMode;
  encoder.tuneManualOverride = true;
  rate.abrCooldownUntilUs = nowUs + 3000000ULL;
  rate.abrGoodSeconds = 0;
  rate.abrModeratePressureSeconds = 0;
  rate.abrSeverePressureSeconds = 0;
  rate.m9Level = overviewMode ? 3 : 0;
  rate.m9CooldownUntilUs = nowUs + static_cast<uint64_t>(rate.m9CooldownSec) * 1000000ULL;
  rate.m9DownPressureSeconds = 0;
  rate.m9UpPressureSeconds = 0;
  encoder.RequestKey(kHostKeyReasonUiQualityMode);
  return true;
}

}  // namespace remote60::native_poc
