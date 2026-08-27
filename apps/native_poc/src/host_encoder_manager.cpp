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
#include <iostream>

#include "encode_resolution_ladder.hpp"
#include "host_abr.hpp"
#include "host_bgra_scale.hpp"
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
  if (encoder.keyintOverride != 0) targetKeyint = encoder.keyintOverride;
  // Callers pass the nominal box for the current ABR/M9 level. Remember it so a later
  // source-size change can be re-fitted against the same budget instead of ratcheting down.
  encoder.nominalEncodeW = targetW;
  encoder.nominalEncodeH = targetH;
  fit_size_preserving_aspect(encoder.encodeSourceW, encoder.encodeSourceH, targetW, targetH, &targetW, &targetH);

  const bool keyintChanged = (targetKeyint != encoder.activeKeyint);
  const bool fpsChanged = (targetFps != encoder.activeFps);
  const bool resizeChanged = (targetW != encoder.activeEncodeW || targetH != encoder.activeEncodeH);
  const bool bitrateChanged = (targetBitrate != encoder.activeBitrate);

  if (keyintChanged || fpsChanged || resizeChanged) {
    encoder.codec.shutdown();
    // The shutdown flushed the MFT, so every in-flight surface is released.
    for (const auto& pending : encoder.nv12PendingReleases) {
      res.captureReadback.ReleaseNv12Slot(pending.slot, pending.generation);
    }
    encoder.nv12PendingReleases.clear();
    encoder.surfaceEncodeHealthy = true;
    if (!encoder.codec.initialize(targetW, targetH, targetFps, targetBitrate, targetKeyint)) {
      return false;
    }
    encoder.ResetTimelineAnchors(capture);
    encoder.ResetStarvationEpisode();
    // shutdown+initialize discarded any pending key input; a stale latch here would delay the
    // fresh encoder's needed IDR by up to the 300ms retry window.
    encoder.forceKeySubmittedAtUs = 0;
  } else if (bitrateChanged) {
    if (!encoder.codec.reconfigure_bitrate(targetBitrate)) {
      encoder.codec.shutdown();
      if (!encoder.codec.initialize(targetW, targetH, targetFps, targetBitrate, targetKeyint)) {
        return false;
      }
      encoder.ResetTimelineAnchors(capture);
      encoder.ResetStarvationEpisode();
      // Same contract as the other reinit sites: shutdown discarded any pending key input.
      encoder.forceKeySubmittedAtUs = 0;
    }
  }

  encoder.activeEncodeW = targetW;
  encoder.activeEncodeH = targetH;
  encoder.activeFps = targetFps;
  encoder.activeBitrate = targetBitrate;
  encoder.activeKeyint = targetKeyint;
  inputRouter.domainW.store(encoder.activeEncodeW, std::memory_order_release);
  inputRouter.domainH.store(encoder.activeEncodeH, std::memory_order_release);
  // The pacing budget follows the active bitrate. It used to be computed once at startup,
  // so after an ABR downshift frames kept leaving at the launch rate (bursts the network
  // just asked us to stop), and after an upshift sends were throttled below the new rate.
  const uint64_t pacePeakBps = sender.noPacingH264
                                   ? 0ULL
                                   : std::max<uint64_t>(
                                         sender.udpPacePeakFloorBps,
                                         (static_cast<uint64_t>(encoder.activeBitrate) *
                                          sender.udpPacePeakPercent) /
                                             100ULL);
  const uint32_t pacePeakBpsClamped =
      static_cast<uint32_t>(std::min<uint64_t>(pacePeakBps, 4000000000ULL));
  if (sender.pacePeakBps.load(std::memory_order_relaxed) != pacePeakBpsClamped) {
    sender.pacePeakBps.store(pacePeakBpsClamped, std::memory_order_relaxed);
    std::cout << "[native-video-host] pacing update udpPacePeakBps=" << pacePeakBpsClamped
              << " bitrate=" << encoder.activeBitrate << "\n";
  }
  res.captureReadback.SetOutputSize(encoder.activeEncodeW, encoder.activeEncodeH);
  encoder.RefreshFrameIntervals(capture, frameGating);
  return true;
}

void EncoderState::ApplyConfirmedCaptureGeometry(CaptureState& capture, CaptureResources& res,
                                                 FrameGatingState& frameGating, InputRouterState& inputRouter,
                                                 SenderState& sender, uint32_t newW, uint32_t newH,
                                                 const char* reason, bool allowWindowOverride) {
  EncoderState& encoder = *this;
  // An interactive window DRAG keeps the 0.4s settle path (per-frame MFT re-init would thrash),
  // so it bails here. A CONFIRMED window selection passes allowWindowOverride=true so the encode
  // target is re-fit to the final window geometry immediately -- otherwise the first IDR goes out
  // at the pre-selection encode size and a second, new-size IDR follows a frame later, forcing the
  // client to reconfigure twice and fire a keyframe-request storm.
  if (capture.windowModeActive && !allowWindowOverride) return;
  if (newW < 2 || newH < 2) return;
  if (newW == encoder.encodeSourceW && newH == encoder.encodeSourceH) return;  // already fit to this source
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
  if (encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, encoder.nominalEncodeW, encoder.nominalEncodeH, encoder.activeFps, encoder.activeBitrate, encoder.activeKeyint)) {
    encoder.forceKeyNext = true;
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
  const auto sizeChoice = remote60::native_poc::choose_abr_profile_size(
      overviewMode ? 2 : 0, targetBitrate, capture.width, capture.height, rate.encodeLadderReduced);
  const uint32_t targetKeyint =
      overviewMode ? std::max<uint32_t>(rate.userKeyintCeiling, 60u) : rate.userKeyintCeiling;
  if (!encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, sizeChoice.width, sizeChoice.height, targetFps, targetBitrate,
                            targetKeyint)) {
    return false;
  }
  rate.encodeLadderReduced = sizeChoice.reduced;
  encoder.tuneManualOverride = true;
  rate.abrCooldownUntilUs = nowUs + 3000000ULL;
  rate.abrGoodSeconds = 0;
  rate.abrModeratePressureSeconds = 0;
  rate.abrSeverePressureSeconds = 0;
  rate.m9Level = overviewMode ? 3 : 0;
  rate.m9CooldownUntilUs = nowUs + static_cast<uint64_t>(rate.m9CooldownSec) * 1000000ULL;
  rate.m9DownPressureSeconds = 0;
  rate.m9UpPressureSeconds = 0;
  encoder.forceKeyNext = true;
  return true;
}

}  // namespace remote60::native_poc
