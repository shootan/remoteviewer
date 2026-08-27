// Stage 4: runtime encoder configuration requests from the viewer.
//
// Host split refactor Phase 3.5: moved verbatim out of host_main_loop.cpp so each stage reads on its
// own; see host_main_loop.hpp for the loop, HostContext / TickContext and Flow.


#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture_backend_dxgi.hpp"
#include "d3d_capture_readback.hpp"
#include "encode_resolution_ladder.hpp"
#include "gdi_capture_process.hpp"
#include "host_abr.hpp"
#include "host_args.hpp"
#include "host_backend_policy.hpp"
#include "host_bgra_scale.hpp"
#include "host_bottleneck.hpp"
#include "host_capture_device.hpp"
#include "host_capture_session.hpp"
#include "host_client_metrics.hpp"
#include "host_control_session.hpp"
#include "host_encoded_sender.hpp"
#include "host_encoder_manager.hpp"
#include "host_frame_gate.hpp"
#include "host_frame_state.hpp"
#include "host_gpu_scaler.hpp"
#include "host_input_inject.hpp"
#include "host_input_router.hpp"
#include "host_kick.hpp"
#include "host_log.hpp"
#include "host_main_loop.hpp"
#include "host_net_io.hpp"
#include "host_session.hpp"
#include "host_stats.hpp"
#include "host_string_util.hpp"
#include "host_watchdog.hpp"
#include "host_window_enum.hpp"
#include "mf_h264_codec.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::host::DesktopCaptureBackend;
using remote60::host::DxgiDesktopCaptureConfig;
using remote60::host::DxgiDesktopCaptureSession;

namespace remote60::native_poc {

Flow stage_runtime_tune(HostContext& hx, TickContext& tc) {
  auto& useH264 = hx.useH264;
  auto& windowSelectionTxn = hx.windowSelectionTxn;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& backend = hx.backend;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  auto& seq = tc.seq;
  const auto tuneReq = useH264 ? hx.mailbox.TakeTuneEncoder() : std::nullopt;
  if (tuneReq) {
    const uint32_t reqSeq = tuneReq->seq;
    const uint32_t requestedBitrate = tuneReq->bitrate;
    const bool bitrateExplicit = requestedBitrate >= 100000;
    uint32_t targetBitrate = requestedBitrate;
    uint32_t targetKeyint = tuneReq->keyint;
    uint32_t targetFps = tuneReq->fps;
    // Explicitness is recorded before the fallbacks fill the gaps: the fallbacks are the
    // CURRENT values, and only what the user actually asked for may move a ceiling. A
    // bitrate-only tune sent while overview mode has encoder.activeFps lowered would otherwise
    // write that lowered value into rate.userFpsCeiling -- the exact contamination the ceiling
    // exists to prevent, back in through a side door.
    const bool fpsExplicit = targetFps >= 1;
    const bool keyintExplicit = targetKeyint >= 1;
    if (targetBitrate < 100000) targetBitrate = encoder.activeBitrate;
    if (targetKeyint < 1) targetKeyint = encoder.activeKeyint;
    if (targetFps < 1) targetFps = encoder.activeFps;
    const bool bitrateChanged = (targetBitrate != encoder.activeBitrate);
    const bool keyintChanged = (targetKeyint != encoder.activeKeyint);
    const bool fpsChanged = (targetFps != encoder.activeFps);
    // A request can match the ACTIVE value while changing the CEILING: with ABR sitting on
    // its low profile at 6.6 Mbps, a user lowering the ceiling from 12M to exactly 6.6M
    // changes nothing active -- and used to be dropped whole, leaving the profiles, the
    // ladder, and the manual-override reset all unrun. The ceiling comparisons catch what
    // the active comparisons cannot; apply_encoder_target is a no-op for identical targets,
    // so entering the block for a ceiling-only change costs no encoder restart.
    const bool bitrateCeilingChanged = bitrateExplicit && (targetBitrate != rate.abrHighBitrate);
    const bool fpsCeilingChanged = fpsExplicit && (targetFps != rate.userFpsCeiling);
    const bool keyintCeilingChanged = keyintExplicit && (targetKeyint != rate.userKeyintCeiling);
    if (bitrateChanged || keyintChanged || fpsChanged || bitrateCeilingChanged ||
        fpsCeilingChanged || keyintCeilingChanged) {
      if (bitrateExplicit) {
        // The UI bitrate is the top quality ceiling, not an instruction to disable
        // adaptation. A 20 Mbps request may start there, but the host must still step down
        // when the client's decoded FPS/latency says the Wi-Fi path cannot sustain it.
        rate.abrHighBitrate = targetBitrate;
        rate.abrMidBitrate = std::min<uint32_t>(
            rate.abrHighBitrate,
            std::max<uint32_t>(2000000u, (rate.abrHighBitrate * 75u) / 100u));
        rate.abrLowBitrate = std::min<uint32_t>(
            rate.abrHighBitrate,
            std::max<uint32_t>(1500000u, (rate.abrHighBitrate * 55u) / 100u));
        rate.abrHasMidProfile = rate.abrMidBitrate < rate.abrHighBitrate;
        rate.abrHasLowProfile = rate.abrHasLowerResolution || rate.abrLowBitrate < rate.abrMidBitrate;
        rate.abrProfile = 0;
      }
      // The resolution follows the bitrate, because the bitrate is a budget for the whole
      // frame: the same 3 Mbps buys four times as much per pixel at 720p. Switching to mobile
      // has to take the picture size down with it, or the encoder spends the difference
      // predicting badly every time the screen changes at once.
      uint32_t ladderW = encoder.nominalEncodeW;
      uint32_t ladderH = encoder.nominalEncodeH;
      bool ladderReducedNext = rate.encodeLadderReduced;
      if (bitrateExplicit) {
        const auto choice = remote60::native_poc::choose_encode_resolution(
            targetBitrate, capture.width, capture.height, rate.encodeLadderReduced);
        ladderReducedNext = choice.reduced;
        ladderW = choice.width;
        ladderH = choice.height;
        if (ladderW != encoder.nominalEncodeW || ladderH != encoder.nominalEncodeH) {
          std::cout << "[native-video-host][control] encode ladder " << encoder.nominalEncodeW << "x"
                    << encoder.nominalEncodeH << " -> " << ladderW << "x" << ladderH
                    << " for " << (targetBitrate / 1000) << "kbps\n";
        }
      }
      // Pass the nominal box, not the fitted activeEncode size: apply_encoder_target
      // records its width/height arguments as the new nominal budget, and feeding the
      // already-fitted size back in would permanently shrink the box for every later
      // target switch.
      if (!encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, ladderW, ladderH, targetFps, targetBitrate, targetKeyint)) {
        std::cerr << "[native-video-host][control] runtime-config apply failed seq=" << reqSeq << "\n";
        return Flow::Break;
      }
      rate.encodeLadderReduced = ladderReducedNext;
      if (fpsExplicit) rate.userFpsCeiling = targetFps;
      if (keyintExplicit) rate.userKeyintCeiling = targetKeyint;
      encoder.tuneManualOverride = false;
      rate.abrCooldownUntilUs = nowUs + 3000000ULL;
      rate.abrGoodSeconds = 0;
      rate.abrModeratePressureSeconds = 0;
      rate.abrSeverePressureSeconds = 0;
      encoder.forceKeyNext = true;
      if (fpsChanged && !capture.windowModeActive.load(std::memory_order_acquire) &&
          backend.active == DesktopCaptureBackend::Gdi) {
        if (!restart_capture_session(hx)) {
          std::cerr << "[native-video-host][control] GDI fps restart failed seq="
                    << reqSeq << "\n";
          return Flow::Break;
        }
        ++capture.restartCount;
        capture.FlushCapturePipelineState(res, frameGating, stats, "gdi-fps-change");
      }
      std::cout << "[native-video-host][control] runtime-config-applied seq=" << reqSeq
                << " bitrate=" << encoder.activeBitrate
                << " keyint=" << encoder.activeKeyint
                << " fps=" << encoder.activeFps
                // Was hardcoded "abrOverride=1", which misreported the ABR ladder as pinned --
                // the actual flag is cleared just above, so print the real state.
                << " abrOverride=" << (encoder.tuneManualOverride ? 1 : 0) << "\n";
    }
  }
  {
    uint32_t reqSeq = 0;
    uint64_t requestedWindowId = 0;
    bool hasWindowSelectRequest = false;
    {
      std::lock_guard<std::mutex> lk(windowSelectionTxn.mu);
      if (windowSelectionTxn.pending) {
        reqSeq = windowSelectionTxn.reqSeq;
        requestedWindowId = windowSelectionTxn.requestedWindowId;
        hasWindowSelectRequest = true;
        windowSelectionTxn.pending = false;
      }
    }
    if (hasWindowSelectRequest) {
      uint32_t responseFlags = 0;
      uint64_t responseWindowId = requestedWindowId;
      uint64_t responseStreamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
      std::string responseReason;
      std::string responseTitle;
      const bool applied = apply_selected_window_capture(hx, 
          requestedWindowId, nowUs, &responseFlags, &responseWindowId, &responseStreamGeneration,
          &responseReason, &responseTitle);
      {
        std::lock_guard<std::mutex> lk(windowSelectionTxn.mu);
        windowSelectionTxn.responseFlags = responseFlags;
        windowSelectionTxn.responseWindowId = responseWindowId;
        windowSelectionTxn.responseStreamGeneration = responseStreamGeneration;
        windowSelectionTxn.responseReason = responseReason;
        windowSelectionTxn.responseTitle = responseTitle;
        windowSelectionTxn.completed = true;
      }
      windowSelectionTxn.cv.notify_all();

      std::cout << "[native-video-host][control] window-select seq=" << reqSeq
                << " requestedId=" << requestedWindowId
                << " applied=" << (applied ? 1 : 0)
                << " selectedId=" << responseWindowId
                << " streamGen=" << responseStreamGeneration
                << " reason=" << (responseReason.empty() ? "none" : responseReason)
                << " title=" << (responseTitle.empty() ? "<empty>" : responseTitle)
                << "\n";
    }
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc
