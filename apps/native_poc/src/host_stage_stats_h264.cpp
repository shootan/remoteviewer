// Stage 12 (H.264 arm of the 1s tick): the stats line, encoder-starvation summary, ABR profile and
// M9 level decisions.
//
// Host split refactor Phase 3.5b: the `} else {` arm of the former stage_stats tick, verbatim; the
// seven per-tick values it reads are passed in. See host_main_loop.hpp for HostContext / Flow.

//


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

Flow stats_tick_h264(HostContext& hx, TickContext& tc, uint64_t t, bool statsPrintDue, double mbps,
                     const std::string& targetProcessName, uint64_t queuePushPerSec,
                     uint64_t callbackFramesPerSec, uint64_t idleHoldPerSec,
                     const CaptureCadenceGate::Counters& cadence) {
  auto& transport = hx.transport;
  auto& startUs = hx.startUs;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& kick = hx.kick;
  auto& clientMetrics = hx.clientMetrics;
  auto& backend = hx.backend;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  const uint64_t capAgeAvgUs = (encoder.encodedFrames > 0) ? (stats.captureAgeSumUs / encoder.encodedFrames) : 0;
  const uint64_t cb2eAvgUs = (encoder.encodedFrames > 0) ? (stats.callbackToEncodeStartSumUs / encoder.encodedFrames) : 0;
  const uint64_t captureD3DWaitAvgUs =
      (stats.captureReadbackSamples > 0) ? (stats.captureD3DWaitSumUs / stats.captureReadbackSamples) : 0;
  const uint64_t captureCopyMapAvgUs =
      (stats.captureReadbackSamples > 0) ? (stats.captureCopyMapSumUs / stats.captureReadbackSamples) : 0;
  const uint64_t captureMemcpyAvgUs =
      (stats.captureReadbackSamples > 0) ? (stats.captureMemcpySumUs / stats.captureReadbackSamples) : 0;
  const uint64_t captureUnmapWaitAvgUs =
      (stats.captureReadbackSamples > 0) ? (stats.captureUnmapWaitSumUs / stats.captureReadbackSamples) : 0;
  const uint64_t captureUnmapAvgUs =
      (stats.captureReadbackSamples > 0) ? (stats.captureUnmapSumUs / stats.captureReadbackSamples) : 0;
  const uint64_t gpuScaleD3DWaitAvgUs =
      (stats.gpuScaleTimedCount > 0) ? (stats.gpuScaleD3DWaitSumUs / stats.gpuScaleTimedCount) : 0;
  const uint64_t gpuScaleCopyMapAvgUs =
      (stats.gpuScaleTimedCount > 0) ? (stats.gpuScaleCopyMapSumUs / stats.gpuScaleTimedCount) : 0;
  const uint64_t gpuScaleMemcpyAvgUs =
      (stats.gpuScaleTimedCount > 0) ? (stats.gpuScaleMemcpySumUs / stats.gpuScaleTimedCount) : 0;
  const uint64_t gpuScaleUnmapWaitAvgUs =
      (stats.gpuScaleTimedCount > 0) ? (stats.gpuScaleUnmapWaitSumUs / stats.gpuScaleTimedCount) : 0;
  const uint64_t gpuScaleUnmapAvgUs =
      (stats.gpuScaleTimedCount > 0) ? (stats.gpuScaleUnmapSumUs / stats.gpuScaleTimedCount) : 0;
  const uint64_t frameGatingChangeAvgPm =
      (frameGating.changePermilleCount > 0)
          ? (frameGating.changePermilleSum / frameGating.changePermilleCount)
          : frameGating.changePermilleLast;
  const double rawEquivMbps = (stats.rawEquivalentBytes * 8.0) / (1000.0 * 1000.0);
  const uint64_t encRatioX100 =
      (sender.sentBytes > 0) ? ((stats.rawEquivalentBytes * 100ULL) / sender.sentBytes) : 0;
  // The sender thread owns the UDP wire counters now.
  if (transport == VideoTransport::Udp) {
    sender.udpTxFrames = sender.txFrames.load(std::memory_order_relaxed);
    sender.udpTxChunks = sender.txChunks.load(std::memory_order_relaxed);
    sender.udpTxBytes = sender.txBytes.load(std::memory_order_relaxed);
    sender.udpTxNoPeer += sender.txNoPeer.exchange(0, std::memory_order_relaxed);
  }
  const uint64_t udpTxChunkPerFrameX100 =
      (sender.udpTxFrames > 0) ? ((sender.udpTxChunks * 100ULL) / sender.udpTxFrames) : 0;
  const uint64_t senderSendCountNow = sender.sendCount.load(std::memory_order_relaxed);
  const uint64_t senderSendDurAvgUs =
      (senderSendCountNow > 0)
          ? (sender.sendDurSumUs.load(std::memory_order_relaxed) / senderSendCountNow)
          : 0;
  if (statsPrintDue) {
  // Age of the last frame published to the encoder -- diagnostic only. A frozen ring shows
  // this climbing in lockstep with watchdog.oldestGpuPendingPeakUs. Per Codex: report it, but never
  // drive the watchdog off it, since a static change-driven desktop is legitimately silent.
  const uint64_t statsNowUs = qpc_now_us();
  const uint64_t lastPublishAtUs = capture.lastPublishUs.load(std::memory_order_acquire);
  const uint64_t lastPublishAgeUs =
      (lastPublishAtUs > 0 && statsNowUs > lastPublishAtUs) ? statsNowUs - lastPublishAtUs : 0;
  std::cout << "[native-video-host] encodedFrames=" << encoder.encodedFrames
            << " sentFrames=" << sender.sentFrames
            << " queuePushCount=" << stats.queuePushCount
            << " queuePopCount=" << stats.queuePopCount
            << " queuePushPerSec=" << stats.queuePushPerSecLatest
            << " idleHoldPerSec=" << idleHoldPerSec
            << " idleHoldTotal=" << stats.idleHoldTotal
            << " captureInputLowPushStreakSec=" << watchdog.inputLowPushStreakSec
            << " captureDeadRestartCount=" << watchdog.deadRestartCount
            << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
            << " queueDepthWindowMax=" << stats.queueDepthWindowMax.load(std::memory_order_relaxed)
            << " queueWaitTimeoutCount=" << stats.queueWaitTimeoutCount
            << " queueWaitNoWorkCount=" << stats.queueWaitNoWorkCount
            << " captureRestarts=" << capture.restartCount
            << " wgcContentSizeMismatchDrops=" << capture.wgcContentSizeMismatchDrops.load(std::memory_order_relaxed)
            << " wgcPoolRecreates=" << capture.wgcPoolRecreates
            << " captureWindowRebindCount=" << capture.rebindCount.load(std::memory_order_relaxed)
            << " captureTargetPid=" << capture.targetPid.load(std::memory_order_relaxed)
            << " captureTargetProc=" << targetProcessName
            << " captureTargetHwnd=0x" << std::hex
            << capture.targetHwnd.load(std::memory_order_relaxed) << std::dec
            << " callbackFrames=" << callbackFramesPerSec
            << " skippedByOverwrite=" << stats.skippedByOverwrite
            << " stalePreEncodeDrops=" << stats.stalePreEncodeDropCount
            << " staleEncodedDrops=" << stats.staleEncodedDropCount
            << " encoderResets=" << encoder.resetCount
            << " keyReqTotal=" << clientMetrics.keyFrameRequestCount.load()
            << " keyReqDropTotal=" << clientMetrics.keyFrameRequestDropped.load()
            << " inputEvents=" << inputRouter.events.load()
            << " secureInputAttempts=" << inputRouter.secureAttempts.load()
            << " secureInputDelivered=" << inputRouter.secureDelivered.load()
            << " secureInputBrokerFailed=" << inputRouter.secureBrokerFailed.load()
            << " secureInputSkipWindowMode=" << inputRouter.secureSkipWindowMode.load()
            << " secureInputSkipUnauth=" << inputRouter.secureSkipUnauthenticated.load()
            << " desktopPromo=" << backend.promotionAttempts.load() << "/"
            << backend.promotionSuccess.load() << "/" << backend.promotionFail.load()
            << " desktopPromoDeferSecure=" << backend.promotionDeferredSecureTotal.load()
            << " desktopSecureProbeFalse=" << backend.secureProbeFalseTotal.load()
            << " lastPromoWaitUs=" << backend.lastPromotionWaitUs.load()
            << " inputIgnoredMove=" << inputRouter.ignoredMove.load(std::memory_order_relaxed)
            << " inputNoTarget=" << inputRouter.noTarget.load(std::memory_order_relaxed)
            << " inputUnsupported=" << inputRouter.unsupported.load(std::memory_order_relaxed)
            << " inputInjectFail=" << inputRouter.injectFail.load(std::memory_order_relaxed)
            << " inputFreshProbeSecure=" << inputRouter.freshProbeSecure.load(std::memory_order_relaxed)
            << " inputFreshProbeReroute=" << inputRouter.freshProbeReroute.load(std::memory_order_relaxed)
            << " inputInjectFailDefault=" << inputRouter.injectFailDefault.load(std::memory_order_relaxed)
            << " inputFailSetCursorPos=" << inputRouter.failSetCursorPos.load(std::memory_order_relaxed)
            << " inputFailSendInputMouse=" << inputRouter.failSendInputMouse.load(std::memory_order_relaxed)
            << " inputFailSendInputKey=" << inputRouter.failSendInputKey.load(std::memory_order_relaxed)
            << " inputFailPostMessage=" << inputRouter.failPostMessage.load(std::memory_order_relaxed)
            << " inputDefaultBrokerFallback=" << inputRouter.defaultBrokerFallback.load(std::memory_order_relaxed)
            << " inputDefaultBrokerQueued=" << inputRouter.defaultBrokerQueued.load(std::memory_order_relaxed)
            << " inputDefaultBrokerPipeFail=" << inputRouter.defaultBrokerPipeFail.load(std::memory_order_relaxed)
            << " capAgeAvgUs=" << capAgeAvgUs
            << " capAgeMaxUs=" << stats.captureAgeMaxUs
            << " cb2eAvgUs=" << cb2eAvgUs
            << " cb2eMaxUs=" << stats.callbackToEncodeStartMaxUs
            << " captureReadbackSamples=" << stats.captureReadbackSamples
            << " captureStagingBusyDrops=" << res.captureReadback.BusyDrops()
            << " captureSupersededDrops=" << res.captureReadback.SupersededDrops()
            << " captureCpuBufferReuse=" << res.captureReadback.BufferReuseCount()
            << " capturePreprocessed=" << res.captureReadback.PreprocessCount()
            << " capturePreprocessFallbacks=" << res.captureReadback.PreprocessFallbacks()
            << " nv12Converted=" << res.captureReadback.Nv12Converted()
            << " nv12RingBusy=" << res.captureReadback.Nv12RingBusy()
            << " nv12SurfaceFrames=" << encoder.nv12SurfaceEncodeCount
            << " captureD3DWaitAvgUs=" << captureD3DWaitAvgUs
            << " captureD3DWaitMaxUs=" << stats.captureD3DWaitMaxUs
            << " captureCopyMapAvgUs=" << captureCopyMapAvgUs
            << " captureCopyMapMaxUs=" << stats.captureCopyMapMaxUs
            << " captureMemcpyAvgUs=" << captureMemcpyAvgUs
            << " captureMemcpyMaxUs=" << stats.captureMemcpyMaxUs
            << " captureUnmapWaitAvgUs=" << captureUnmapWaitAvgUs
            << " captureUnmapWaitMaxUs=" << stats.captureUnmapWaitMaxUs
            << " oldestGpuPendingPeakUs=" << watchdog.oldestGpuPendingPeakUs
            << " oldestGpuPendingNowUs=" << res.captureReadback.OldestGpuPendingAgeUs()
            << " gpuPendingCount=" << res.captureReadback.GpuPendingCount()
            << " gpuPendingCountPeak=" << watchdog.gpuPendingCountPeak
            << " frozenRingRestarts=" << watchdog.frozenRingRestartCount
            << " readbackDrainRestarts=" << watchdog.drainRestartCount
            << " readbackDrainSec=" << watchdog.drainConsecutiveSec
            << " lastPublishAgeUs=" << lastPublishAgeUs
            << " captureUnmapAvgUs=" << captureUnmapAvgUs
            << " captureUnmapMaxUs=" << stats.captureUnmapMaxUs
            << " mbps=" << mbps
            << " rawEquivMbps=" << rawEquivMbps
            << " encRatioX100=" << encRatioX100
            << " udpTxFrames=" << sender.udpTxFrames
            << " udpTxChunks=" << sender.udpTxChunks
            << " udpTxChunkPerFrameX100=" << udpTxChunkPerFrameX100
            << " udpTxBytes=" << sender.udpTxBytes
            << " udpTxFail=" << sender.udpTxFail
            << " udpTxNoPeer=" << sender.udpTxNoPeer
            << " senderQueueDrops=" << sender.dropCount.load(std::memory_order_relaxed)
            // Frames the queue policy withheld: the direct measure of how long a viewer
            // was looking at a frozen picture.
            << " senderHeldFrames=" << sender.heldFrames
            << " senderSendDurAvgUs=" << senderSendDurAvgUs
            << " senderSendDurMaxUs=" << sender.sendDurMaxUs.load(std::memory_order_relaxed)
            << " bitrateTarget=" << encoder.activeBitrate
            << " fpsTarget=" << encoder.activeFps
            << " keyintTarget=" << encoder.activeKeyint
            << " size=" << encoder.activeEncodeW << "x" << encoder.activeEncodeH
            << " gpuScaleReq=" << (capture.gpuScalerRequested ? 1 : 0)
            << " gpuScaleReady=" << (capture.gpuScalerHealthy ? 1 : 0)
            << " gpuScaleAttempts=" << stats.gpuScaleAttempts
            << " gpuScaleSuccess=" << stats.gpuScaleSuccess
            << " gpuScaleFail=" << stats.gpuScaleFail
            << " gpuScaleCpuFallback=" << stats.gpuScaleCpuFallback
            << " gpuScaleTimedCount=" << stats.gpuScaleTimedCount
            << " gpuScaleD3DWaitAvgUs=" << gpuScaleD3DWaitAvgUs
            << " gpuScaleD3DWaitMaxUs=" << stats.gpuScaleD3DWaitMaxUs
            << " gpuScaleCopyMapAvgUs=" << gpuScaleCopyMapAvgUs
            << " gpuScaleCopyMapMaxUs=" << stats.gpuScaleCopyMapMaxUs
            << " gpuScaleMemcpyAvgUs=" << gpuScaleMemcpyAvgUs
            << " gpuScaleMemcpyMaxUs=" << stats.gpuScaleMemcpyMaxUs
            << " gpuScaleUnmapWaitAvgUs=" << gpuScaleUnmapWaitAvgUs
            << " gpuScaleUnmapWaitMaxUs=" << stats.gpuScaleUnmapWaitMaxUs
            << " gpuScaleUnmapAvgUs=" << gpuScaleUnmapAvgUs
            << " gpuScaleUnmapMaxUs=" << stats.gpuScaleUnmapMaxUs
            << " abrProfile=" << ((rate.abrProfile == 0) ? "high" : ((rate.abrProfile == 1) ? "mid" : "low"))
            << " abrModSec=" << rate.abrModeratePressureSeconds
            << " abrSevSec=" << rate.abrSeverePressureSeconds
            << " abrGoodSec=" << rate.abrGoodSeconds
            << " abrOverride=" << (encoder.tuneManualOverride ? 1 : 0)
            << " frameGatingMode=" << (frameGating.staticMode ? "static" : "motion")
            << " frameGatingSkips=" << frameGating.skipCount
            << " frameGatingStaticSkips=" << frameGating.staticSkipCount
            << " frameGatingChangePm=" << frameGating.changePermilleLast
            << " frameGatingChangeAvgPm=" << frameGatingChangeAvgPm
            << " captureOfferContent=" << cadence.offerContent
            << " captureOfferPointer=" << cadence.offerPointer
            << " captureGateDropContent=" << cadence.gateDropContent
            << " captureGateDropPointer=" << cadence.gateDropPointer
            << " trailingKickCount=" << kick.count
            << " staticRefreshCount=" << kick.staticRefreshCount
            << " lastKickSourceAgeUs=" << kick.lastSourceAgeUs
            << " mediaEpoch=" << sender.mediaSessionEpoch.load(std::memory_order_acquire)
            << " forceKeyInputCount=" << encoder.forceKeyInputCount
            << " nonKeyAuWhileWaiting=" << sender.nonKeyAuWhileWaiting
            << " barrierRearm=" << sender.barrierRearmCount.load(std::memory_order_relaxed)
            << " firstKeyEnqueuedUs=" << sender.firstKeyEnqueuedUs
            << " firstKeyWireUs=" << sender.firstKeyWireUs.load(std::memory_order_relaxed)
            << " lastKeyAuBytes=" << sender.lastKeyAuBytes.load(std::memory_order_relaxed)
            << " lastKeyAuChunks=" << sender.lastKeyAuChunks.load(std::memory_order_relaxed)
            << "\n";
  }

  // One copy for the whole tick -- the freshness gate and every value below then describe the
  // same report. (Phase 4: ClientMetricsSnapshot.)
  const ViewerMetrics viewer = clientMetrics.Snapshot();
  const uint64_t metricsUpdatedUs = viewer.updatedUs;
  const bool metricsFresh =
      (metricsUpdatedUs > 0) && (t >= metricsUpdatedUs) && ((t - metricsUpdatedUs) <= 3000000ULL);
  const uint64_t clAvgLatencyUs = metricsFresh ? viewer.avgLatencyUs : 0;
  const uint64_t clAvgDecodeTailUs = metricsFresh ? viewer.avgDecodeTailUs : 0;
  const uint32_t clDecodedFpsX100 = metricsFresh ? viewer.decodedFpsX100 : 0;
  const uint32_t clRecvMbpsX1000 = metricsFresh ? viewer.recvMbpsX1000 : 0;
  const uint32_t clWidth = metricsFresh ? viewer.width : 0;
  const uint32_t clHeight = metricsFresh ? viewer.height : 0;
  const uint32_t clCongestionState = metricsFresh ? viewer.congestionState : 0;
  const uint32_t clCongestionTransitions = metricsFresh ? viewer.congestionTransitions : 0;
  const uint32_t clCongestionRecoveryCount = metricsFresh ? viewer.congestionRecoveryCount : 0;
  const uint32_t clCongestionRecoveryReq = metricsFresh ? viewer.congestionRecoveryReq : 0;
  const uint32_t clCongestionRecoveryMaxUs = metricsFresh ? viewer.congestionRecoveryMaxUs : 0;
  const uint32_t clQueueDepthMax = metricsFresh ? viewer.queueDepthMax : 0;
  const uint32_t clQueueDepthH4p = metricsFresh ? viewer.queueDepthH4p : 0;
  const uint32_t clUdpDropPm = metricsFresh ? viewer.udpAssemblyDropPm : 0;

  // Hold ABR/M9 for a tick when an explicit tune is already queued. stage_stats runs BEFORE
  // stage_runtime_tune now (the 1s tick had to move to the front so it stops being skipped -- see
  // native_video_host_main.cpp), and the old ordering was the other way round: a tune arriving on
  // a second boundary set tuneManualOverride first, which suppressed ABR for that tick. Without
  // this peek ABR would re-init the encoder from pre-tune state and the tune would re-init it
  // again a few stages later -- two teardowns and a visible gap where there used to be one.
  // (Ledger H-26d.)
  const bool tuneQueued = hx.mailbox.TuneEncoderPending();
  if (rate.abrEnabled && !encoder.tuneManualOverride && !rate.m9Apply && !tuneQueued) {
    // The current target, not the one the process started with. A runtime FPS tune moves
    // encoder.activeFps, and judging against the startup args.fps would compare the client's rate
    // to a target that no longer exists -- after a tune to 20, a healthy 20 fps reads as
    // 66% of 30 and trips a demote; after a tune to 60, a struggling 20 fps reads as fine.
    // ABR only runs when no manual override or M9 is lowering encoder.activeFps, so here it is the
    // authoritative target. All four thresholds and the sparse floor share it.
    const AbrInputs abrIn{metricsFresh, clDecodedFpsX100, clAvgLatencyUs, clAvgDecodeTailUs, cb2eAvgUs,
                          sender.sentFrames, frameGating.staticMode, encoder.activeFps, startUs};
    const AbrDecision abrDecision = rate.DecideAbrProfile(abrIn, t);
    const int targetProfile = abrDecision.targetProfile;
    const char* abrReason = abrDecision.reason;

    if (targetProfile != rate.abrProfile) {
      uint32_t targetBitrate = rate.abrHighBitrate;
      if (targetProfile == 1) {
        targetBitrate = rate.abrMidBitrate;
      } else if (targetProfile == 2) {
        targetBitrate = rate.abrLowBitrate;
      }
      // Derived at transition time, never read from the profile: frozen profile sizes
      // are the bug that put "profile=high encode=1256x706 bitrate=12000000" in a live
      // log. Deriving from the ladder also tracks capture-size changes (monitor
      // switches, RDP) that a frozen value never could. Runtime tuning does the same
      // already, and the hysteresis state is shared so the two cannot fight.
      const auto ladderChoice = remote60::native_poc::choose_abr_profile_size(
          targetProfile, targetBitrate, capture.width, capture.height, rate.encodeLadderReduced);
      uint32_t targetW = ladderChoice.width;
      uint32_t targetH = ladderChoice.height;

      if (!encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, targetW, targetH, encoder.activeFps, targetBitrate, encoder.activeKeyint)) {
        std::cerr << "[native-video-host][abr] encoder profile apply failed\n";
        return Flow::Break;
      }
      // Committed only once the encoder accepted the target, so a failed reinit cannot
      // leave the hysteresis state describing an encoder that does not exist.
      rate.CommitAbrProfile(targetProfile, ladderChoice.reduced, t);
      encoder.forceKeyNext = true;

      std::cout << "[native-video-host][abr] profile="
                << ((rate.abrProfile == 0) ? "high" : ((rate.abrProfile == 1) ? "mid" : "low"))
                << " encode=" << encoder.activeEncodeW << "x" << encoder.activeEncodeH
                << " bitrate=" << encoder.activeBitrate
                << " reason=" << abrReason
                << " clientSize=" << clWidth << "x" << clHeight
                << " clientDecodedFps=" << (clDecodedFpsX100 / 100.0)
                << " clientAvgLatUs=" << clAvgLatencyUs
                << " clientAvgTailUs=" << clAvgDecodeTailUs
                << " clientMbps=" << (clRecvMbpsX1000 / 1000.0)
                << "\n";
    }
  }

  if (rate.m9Enabled && !encoder.tuneManualOverride && !tuneQueued) {
    const M9Inputs m9In{metricsFresh, clCongestionState, clDecodedFpsX100, clQueueDepthMax, clUdpDropPm,
                        clAvgLatencyUs, clAvgDecodeTailUs, cb2eAvgUs};
    const M9Decision m9Decision = rate.DecideM9Level(m9In, t);
    const int targetLevel = m9Decision.targetLevel;
    const char* m9Reason = m9Decision.reason;


    if (targetLevel != rate.m9Level) {
      const char* action = (targetLevel > rate.m9Level) ? "down" : "up";
      const uint32_t targetBitrate = rate.M9LevelBitrate(targetLevel);
      const uint32_t targetFps = rate.M9LevelFps(targetLevel);
      const uint32_t targetW = rate.M9LevelW(targetLevel);
      const uint32_t targetH = rate.M9LevelH(targetLevel);
      std::cout << "[native-video-host][m9] action=" << action
                << " mode=" << (rate.m9Apply ? "apply" : "dryrun")
                << " fromLevel=" << rate.m9Level
                << " toLevel=" << targetLevel
                << " reason=" << m9Reason
                << " targetBitrate=" << targetBitrate
                << " targetFps=" << targetFps
                << " targetSize=" << targetW << "x" << targetH
                << " decodedFps=" << (clDecodedFpsX100 / 100.0)
                << " avgLatUs=" << clAvgLatencyUs
                << " avgTailUs=" << clAvgDecodeTailUs
                << " queueDepthMax=" << clQueueDepthMax
                << " queueDepthH4p=" << clQueueDepthH4p
                << " udpDropPm=" << clUdpDropPm
                << " congState=" << clCongestionState
                << " congTrans=" << clCongestionTransitions
                << " congRecCnt=" << clCongestionRecoveryCount
                << " congRecReq=" << clCongestionRecoveryReq
                << " congRecMaxUs=" << clCongestionRecoveryMaxUs
                << "\n";
      if (rate.m9Apply) {
        if (!encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, targetW, targetH, targetFps, targetBitrate, encoder.activeKeyint)) {
          std::cerr << "[native-video-host][m9] encoder target apply failed level=" << targetLevel << "\n";
          return Flow::Break;
        }
        encoder.forceKeyNext = true;
      }
      rate.CommitM9Level(targetLevel, t);
    }
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc
