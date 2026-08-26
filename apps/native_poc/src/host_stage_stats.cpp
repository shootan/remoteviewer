// Stage 12: 1s stats tick, readback drain watchdog, ABR / M9 decisions.
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

Flow stage_stats(HostContext& hx, TickContext& tc) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& useRaw = hx.useRaw;
  auto& transport = hx.transport;
  auto& startUs = hx.startUs;
  auto& streamActiveSinceUs = hx.streamActiveSinceUs;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& kick = hx.kick;
  auto& clientMetrics = hx.clientMetrics;
  auto& backend = hx.backend;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& w = tc.w;
  auto& h = tc.h;
  const uint64_t t = qpc_now_us();
  if (t >= stats.nextAtUs) {
    ++stats.ticks;
    const bool statsPrintDue = (stats.ticks % stats.printEverySec) == 0;
    const double mbps = (sender.sentBytes * 8.0) / (1000.0 * 1000.0);
    std::string targetProcessName;
    {
      std::lock_guard<std::mutex> lk(capture.metaMu);
      targetProcessName = capture.targetProcess;
    }
    const uint64_t queuePushPerSec =
        (stats.queuePushCount >= stats.queuePushCountLastSample) ? (stats.queuePushCount - stats.queuePushCountLastSample) : 0;
    stats.queuePushCountLastSample = stats.queuePushCount;
    stats.queuePushPerSecLatest = queuePushPerSec;
    const uint64_t callbackFramesPerSec = stats.callbackFrames.load(std::memory_order_relaxed);
    const uint64_t idleHoldPerSec =
        (useH264 &&
         capture.sessionReady.load(std::memory_order_acquire) &&
         clientSession.streamControlActive.load(std::memory_order_acquire) &&
         callbackFramesPerSec == 0) ? 1ULL : 0ULL;
    stats.idleHoldTotal += idleHoldPerSec;
    const bool gdiLowPushFallbackEnabled =
        !capture.windowModeActive && backend.active == DesktopCaptureBackend::Gdi;
    if (useH264 &&
        capture.sessionReady.load(std::memory_order_acquire) &&
        clientSession.streamControlActive.load(std::memory_order_acquire) &&
        gdiLowPushFallbackEnabled) {
      const bool warmupDone =
          (watchdog.inputStallWarmupSec == 0 ||
           t >= (startUs + static_cast<uint64_t>(watchdog.inputStallWarmupSec) * 1000000ULL));
      if (warmupDone) {
        if (callbackFramesPerSec < static_cast<uint64_t>(watchdog.inputMinPushPerSec)) {
          watchdog.inputLowPushStreakSec += 1;
        } else {
          watchdog.inputLowPushStreakSec = 0;
        }
        const bool restartCooldownDone =
            (watchdog.lastCaptureRestartUs == 0 ||
             t >= (watchdog.lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
        if (watchdog.inputLowPushStreakSec >= watchdog.inputStallConsecutiveSec && restartCooldownDone) {
          watchdog.lastCaptureRestartUs = t;
          const bool fallbackFromGdi =
              !capture.windowModeActive && backend.active == DesktopCaptureBackend::Gdi;
          if (fallbackFromGdi) {
            backend.active = DesktopCaptureBackend::Wgc;
            capture.SetGdiFallbackReason("gdi_low_capture_rate");
            std::cout << "[native-video-host] fallback_reason=gdi_low_capture_rate"
                      << " callbackFramesPerSec=" << callbackFramesPerSec
                      << " minPushPerSec=" << watchdog.inputMinPushPerSec << "\n";
          }
          const bool restarted = restart_capture_session(hx);
          if (restarted) {
            ++capture.restartCount;
            ++watchdog.deadRestartCount;
            capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
            capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
            capture.lastCallbackUs.store(0, std::memory_order_release);
            encoder.ResetTimelineAnchors(capture);
            encoder.forceKeyNext = true;
            watchdog.inputLowPushStreakSec = 0;
            std::cout << "[native-video-host] capture session restarted reason="
                      << (fallbackFromGdi ? "gdi-low-push-fallback" : "capture-input-stall")
                      << " restartCount=" << capture.restartCount
                      << " captureDeadRestartCount=" << watchdog.deadRestartCount
                      << " callbackFramesPerSec=" << callbackFramesPerSec
                      << " minPushPerSec=" << watchdog.inputMinPushPerSec
                      << " stallStreakSec=" << watchdog.inputStallConsecutiveSec
                      << "\n";
          } else {
            std::cerr << "[native-video-host] capture session restart failed reason=capture-input-stall"
                      << " callbackFramesPerSec=" << callbackFramesPerSec
                      << " minPushPerSec=" << watchdog.inputMinPushPerSec
                      << " streakSec=" << watchdog.inputLowPushStreakSec
                      << "\n";
          }
        }
      }
    }
    // Readback-throughput soft watchdog (DXGI/WGC). Runs every stats tick on per-1s-window
    // deltas: the frozen-ring block above already accumulated this window's oldest-pending peak
    // at loop frequency. Gated to the same live desktop-capture surface the frozen-ring watchdog
    // uses, plus a warmup and a secure-desktop check, so a legitimately static desktop, a
    // just-restarted session, or a lock screen cannot trip it. This is the ONLY new rebuild
    // trigger; the frozen-ring 2s hard path and session rollover behavior are unchanged.
    {
      const bool drainStreamActive = clientSession.streamControlActive.load(std::memory_order_acquire);
      if (drainStreamActive && !watchdog.drainPrevStreamActive) {
        streamActiveSinceUs = t;  // client (re)attach edge; anchors the warmup below
      }
      watchdog.drainPrevStreamActive = drainStreamActive;

      // Per-1s-window deltas. AcceptContentCount / BusyDrops / SupersededDrops are all lifetime
      // cumulative (superseded especially -- it is never reset), so diff, never read absolute.
      // The snapshots are advanced every tick regardless of whether the watchdog is eligible, so
      // an eligible second always sees exactly that second's increment.
      // AcceptContentCount is a plain uint64 the capture-callback thread mutates under
      // capture.cadenceMu; snapshot it under the same lock. The watchdog now restarts capture on
      // this value, so an unlocked read is a real data race, not just a stale display. (BusyDrops
      // and SupersededDrops are std::atomic, so they need no lock.)
      uint64_t acceptedNow;
      {
        std::lock_guard<std::mutex> lk(capture.cadenceMu);
        acceptedNow = capture.cadenceGate.AcceptContentCount();
      }
      const uint64_t busyNow = res.captureReadback.BusyDrops();
      const uint64_t supersededNow = res.captureReadback.SupersededDrops();
      const uint64_t acceptedDelta =
          (acceptedNow >= watchdog.drainPrevAccepted) ? (acceptedNow - watchdog.drainPrevAccepted) : 0;
      const uint64_t busyDelta =
          (busyNow >= watchdog.drainPrevBusyDrops) ? (busyNow - watchdog.drainPrevBusyDrops) : 0;
      const uint64_t supersededDelta =
          (supersededNow >= watchdog.drainPrevSuperseded) ? (supersededNow - watchdog.drainPrevSuperseded) : 0;
      watchdog.drainPrevAccepted = acceptedNow;
      watchdog.drainPrevBusyDrops = busyNow;
      watchdog.drainPrevSuperseded = supersededNow;
      // published = callbackFramesPerSec: the readback worker's publish count for this second,
      // already reset each tick, so it is a true per-window delta as-is.
      const uint64_t drainPendingPeakUs = watchdog.drainOldestPendingPeakUs;
      watchdog.drainOldestPendingPeakUs = 0;  // window closes here

      const bool drainSurfaceEligible =
          useH264 &&
          capture.sessionReady.load(std::memory_order_acquire) &&
          drainStreamActive &&
          !capture.windowModeActive.load(std::memory_order_acquire) &&
          backend.active != DesktopCaptureBackend::Gdi;
      // Warmup after the latest of: capture session start, any capture restart, or client
      // reattach -- so the first seconds of a fresh pipeline (encoder spin-up, first IDR) never
      // read as a drain.
      uint64_t drainWarmupAnchorUs = capture.sessionStartedUs;
      if (watchdog.lastCaptureRestartUs > drainWarmupAnchorUs) drainWarmupAnchorUs = watchdog.lastCaptureRestartUs;
      if (streamActiveSinceUs > drainWarmupAnchorUs) drainWarmupAnchorUs = streamActiveSinceUs;
      const bool drainWarmupDone = (t >= drainWarmupAnchorUs + kReadbackDrainWarmupUs);
      // accepted >= max(5, fps/4): a static/quiet desktop accepts almost nothing (pointer-only
      // offers never advance this count), so it stays well below the floor and cannot trip.
      const uint32_t drainAcceptFloor =
          std::max<uint32_t>(5u, std::max<uint32_t>(1u, encoder.activeFps) / 4u);
      const uint64_t drainPublishCeil = std::max<uint64_t>(1u, acceptedDelta / 10u);
      // Cheap arithmetic first; the uncached secure-desktop syscall runs only when a stall is
      // already indicated, so the healthy path pays no per-second OpenInputDesktop cost.
      const bool drainMetricsStalled =
          drainSurfaceEligible && drainWarmupDone &&
          acceptedDelta >= drainAcceptFloor &&
          callbackFramesPerSec <= drainPublishCeil &&
          (drainPendingPeakUs >= kReadbackDrainPendingAgeUs ||
           (busyDelta + supersededDelta) >= kReadbackDrainDropBurstMin);
      const bool drainStarved =
          drainMetricsStalled && interactive_desktop_is_default_uncached();

      if (drainStarved) {
        ++watchdog.drainConsecutiveSec;
      } else {
        watchdog.drainConsecutiveSec = 0;
      }

      const bool drainRestartCooldownDone =
          (watchdog.lastCaptureRestartUs == 0 ||
           t >= (watchdog.lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
      if (watchdog.drainConsecutiveSec >= kReadbackDrainConsecutiveSecMin && drainRestartCooldownDone) {
        watchdog.drainConsecutiveSec = 0;
        // First trip: restart_capture_session(hx) runs create_staging -> captureReadback
        // Shutdown/Initialize, rebuilding the capture backend and the readback ring on the same
        // device. A recurrence inside the same 60s window the frozen-ring refreeze uses means the
        // device itself is wedged; match that path and exit code 3 so the supervisor rebuilds the
        // process with a fresh D3D device.
        const bool drainRecurred =
            watchdog.lastDrainRestartUs != 0 &&
            t < (watchdog.lastDrainRestartUs + kCaptureFrozenEscalationWindowUs);
        watchdog.lastDrainRestartUs = t;
        if (drainRecurred) {
          const uint64_t drainLastPubUs = capture.lastPublishUs.load(std::memory_order_acquire);
          const uint64_t drainLastPubAgeUs =
              (drainLastPubUs > 0 && t > drainLastPubUs) ? t - drainLastPubUs : 0;
          std::cerr << "[native-video-host] capture readback drain recurred within "
                    << (kCaptureFrozenEscalationWindowUs / 1000000)
                    << "s acceptedDelta=" << acceptedDelta
                    << " published=" << callbackFramesPerSec
                    << "; exiting for a full process restart\n";
          std::cout << "[native-video-host] capture-recovery reason=readback-drain-recurrence"
                    << " action=process-restart exitCode=3"
                    << " acceptedDelta=" << acceptedDelta
                    << " published=" << callbackFramesPerSec
                    << " oldestPendingPeakUs=" << drainPendingPeakUs
                    << " busyDelta=" << busyDelta
                    << " supersededDelta=" << supersededDelta
                    << " readbackDrainRestarts=" << watchdog.drainRestartCount
                    << " captureRestarts=" << capture.restartCount
                    << " lastPublishAgeUs=" << drainLastPubAgeUs
                    << " backend=" << desktop_capture_backend_name(backend.active)
                    << " windowSec=" << (kCaptureFrozenEscalationWindowUs / 1000000)
                    << "\n";
          std::cout.flush();
          std::cerr.flush();
          { hx.exitCode = 3; return Flow::Return; }
        }
        watchdog.lastCaptureRestartUs = t;
        const bool restarted = restart_capture_session(hx);
        if (restarted) {
          ++capture.restartCount;
          capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
          capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
          capture.lastCallbackUs.store(0, std::memory_order_release);
          encoder.ResetTimelineAnchors(capture);
          encoder.forceKeyNext = true;
          ++watchdog.deadRestartCount;
          ++watchdog.drainRestartCount;
          std::cout << "[native-video-host] capture session restarted reason=readback-drain count="
                    << capture.restartCount
                    << " captureDeadRestartCount=" << watchdog.deadRestartCount
                    << " readbackDrainRestarts=" << watchdog.drainRestartCount
                    << " acceptedDelta=" << acceptedDelta
                    << " published=" << callbackFramesPerSec
                    << " oldestPendingPeakUs=" << drainPendingPeakUs
                    << " busyDelta=" << busyDelta
                    << " supersededDelta=" << supersededDelta
                    << "\n";
        } else {
          std::cerr << "[native-video-host] readback-drain restart failed acceptedDelta="
                    << acceptedDelta << " published=" << callbackFramesPerSec << "\n";
        }
      }
    }
    if (useRaw) {
      if (statsPrintDue) {
      std::cout << "[native-video-host] sentFrames=" << sender.sentFrames
                << " queuePushCount=" << stats.queuePushCount
                << " queuePopCount=" << stats.queuePopCount
                << " queuePushPerSec=" << stats.queuePushPerSecLatest
                << " idleHoldPerSec=" << idleHoldPerSec
                << " idleHoldTotal=" << stats.idleHoldTotal
                << " captureInputLowPushStreakSec=" << watchdog.inputLowPushStreakSec
                << " captureDeadRestartCount=" << watchdog.deadRestartCount
                << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
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
                << " keyReqDropTotal=" << clientMetrics.keyFrameRequestDropped.load()
                << " callbackFrames=" << callbackFramesPerSec
                << " skippedByOverwrite=" << stats.skippedByOverwrite
                << " frameGatingMode=" << (frameGating.staticMode ? "static" : "motion")
                << " frameGatingSkips=" << frameGating.skipCount
                << " frameGatingStaticSkips=" << frameGating.staticSkipCount
                << " mbps=" << mbps
                << " size=" << w << "x" << h
                << "\n";
      }
    } else {
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
                << " captureOfferContent=" << capture.cadenceGate.OfferContentCount()
                << " captureOfferPointer=" << capture.cadenceGate.OfferPointerCount()
                << " captureGateDropContent=" << capture.cadenceGate.GateDropContentCount()
                << " captureGateDropPointer=" << capture.cadenceGate.GateDropPointerCount()
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

      const uint64_t metricsUpdatedUs = clientMetrics.updatedUs.load();
      const bool metricsFresh =
          (metricsUpdatedUs > 0) && (t >= metricsUpdatedUs) && ((t - metricsUpdatedUs) <= 3000000ULL);
      const uint64_t clAvgLatencyUs = metricsFresh ? clientMetrics.avgLatencyUs.load() : 0;
      const uint64_t clAvgDecodeTailUs = metricsFresh ? clientMetrics.avgDecodeTailUs.load() : 0;
      const uint32_t clDecodedFpsX100 = metricsFresh ? clientMetrics.decodedFpsX100.load() : 0;
      const uint32_t clRecvMbpsX1000 = metricsFresh ? clientMetrics.recvMbpsX1000.load() : 0;
      const uint32_t clWidth = metricsFresh ? clientMetrics.width.load() : 0;
      const uint32_t clHeight = metricsFresh ? clientMetrics.height.load() : 0;
      const uint32_t clCongestionState = metricsFresh ? clientMetrics.congestionState.load() : 0;
      const uint32_t clCongestionTransitions = metricsFresh ? clientMetrics.congestionTransitions.load() : 0;
      const uint32_t clCongestionRecoveryCount = metricsFresh ? clientMetrics.congestionRecoveryCount.load() : 0;
      const uint32_t clCongestionRecoveryReq = metricsFresh ? clientMetrics.congestionRecoveryReq.load() : 0;
      const uint32_t clCongestionRecoveryMaxUs = metricsFresh ? clientMetrics.congestionRecoveryMaxUs.load() : 0;
      const uint32_t clQueueDepthMax = metricsFresh ? clientMetrics.queueDepthMax.load() : 0;
      const uint32_t clQueueDepthH4p = metricsFresh ? clientMetrics.queueDepthH4p.load() : 0;
      const uint32_t clUdpDropPm = metricsFresh ? clientMetrics.udpAssemblyDropPm.load() : 0;

      if (rate.abrEnabled && !encoder.tuneManualOverride && !rate.m9Apply) {
        // The current target, not the one the process started with. A runtime FPS tune moves
        // encoder.activeFps, and judging against the startup args.fps would compare the client's rate
        // to a target that no longer exists -- after a tune to 20, a healthy 20 fps reads as
        // 66% of 30 and trips a demote; after a tune to 60, a struggling 20 fps reads as fine.
        // ABR only runs when no manual override or M9 is lowering encoder.activeFps, so here it is the
        // authoritative target. All four thresholds and the sparse floor share it.
        const uint32_t abrExpectedFps = std::max<uint32_t>(1, encoder.activeFps);
        const uint32_t minGoodFpsX100 = abrExpectedFps * (rate.abrQualityFirst ? 95u : 93u);
        const uint32_t minOkayFpsX100 = abrExpectedFps * (rate.abrQualityFirst ? 90u : 85u);
        const uint32_t minDegradeFpsX100 = abrExpectedFps * (rate.abrQualityFirst ? 55u : 45u);
        const uint32_t minSevereFpsX100 = abrExpectedFps * (rate.abrQualityFirst ? 45u : 35u);
        const bool abrWarmupDone = (t >= (startUs + 4000000ULL));

        // A second in which the host offered almost no frames carries no usable evidence
        // either way. The client's relative-lag metric is a delay-variation estimate over
        // that second's samples, and 2-4 samples let a single outlier -- or the decoder
        // holding output across a sparse cadence -- read as latency the network never had.
        // A static desktop (frame gating) is the common case: the picture was still, the
        // client decoded a handful of frames, and the old code took that for congestion and
        // demoted, then recovered on motion, then demoted again -- the quality seen flapping
        // between sharp and soft while simply reading the screen. sender.sentFrames is this tick's
        // real send cadence (reset each stats second), which is what the discarded
        // queuePushPerSec never was. When evidence is this thin, hold the profile and let a
        // second with real motion decide against the unchanged thresholds.
        const bool hostOfferSparse =
            (sender.sentFrames < std::max<uint64_t>(2, static_cast<uint64_t>(abrExpectedFps) / 2)) ||
            frameGating.staticMode;

        const uint64_t severeLatencyUs = rate.abrQualityFirst ? 170000ULL : 150000ULL;
        const uint64_t severeTailUs = rate.abrQualityFirst ? 140000ULL : 110000ULL;
        const uint64_t moderateLatencyUs = rate.abrQualityFirst ? 145000ULL : 125000ULL;
        const uint64_t moderateTailUs = rate.abrQualityFirst ? 120000ULL : 90000ULL;
        const uint64_t emergencyLatencyUs = rate.abrQualityFirst ? 260000ULL : 220000ULL;
        const uint64_t emergencyTailUs = rate.abrQualityFirst ? 190000ULL : 160000ULL;

        const bool severeDownByClient =
            metricsFresh &&
            (clAvgLatencyUs > severeLatencyUs ||
             clAvgDecodeTailUs > severeTailUs ||
             (clDecodedFpsX100 < minSevereFpsX100 &&
              (clAvgLatencyUs > (severeLatencyUs - 30000ULL) || clAvgDecodeTailUs > (severeTailUs - 40000ULL))));
        const bool moderateDownByClient =
            metricsFresh &&
            (clAvgLatencyUs > moderateLatencyUs ||
             clAvgDecodeTailUs > moderateTailUs ||
             (clDecodedFpsX100 < minDegradeFpsX100 &&
              (clAvgLatencyUs > (moderateLatencyUs - 50000ULL) ||
               clAvgDecodeTailUs > (moderateTailUs - 30000ULL))));
        const bool emergencyDownByClient =
            metricsFresh &&
            (clAvgLatencyUs > emergencyLatencyUs ||
             clAvgDecodeTailUs > emergencyTailUs);
        const bool severeDownByHost = (!metricsFresh && cb2eAvgUs > (rate.abrQualityFirst ? 110000ULL : 90000ULL));
        const bool moderateDownByHost = (!metricsFresh && cb2eAvgUs > (rate.abrQualityFirst ? 90000ULL : 70000ULL));
        // !hostOfferSparse on every up/down verdict: a sparse second neither degrades nor
        // recovers the profile. The pressure and good counters below fall to their else
        // branch and reset, so the profile holds until a second with real cadence arrives.
        const bool severeDown =
            abrWarmupDone && !hostOfferSparse && (severeDownByClient || severeDownByHost);
        const bool moderateDown =
            abrWarmupDone && !hostOfferSparse && (moderateDownByClient || moderateDownByHost);
        const bool emergencyDown = abrWarmupDone && !hostOfferSparse && emergencyDownByClient;

        if (severeDown) {
          ++rate.abrSeverePressureSeconds;
        } else {
          rate.abrSeverePressureSeconds = 0;
        }
        if (moderateDown) {
          ++rate.abrModeratePressureSeconds;
        } else {
          rate.abrModeratePressureSeconds = 0;
        }

        const bool goodForLowToMid =
            metricsFresh && !hostOfferSparse &&
            (clAvgLatencyUs < 90000ULL) &&
            (clAvgDecodeTailUs < 65000ULL) &&
            (clDecodedFpsX100 >= minOkayFpsX100);
        const bool goodForMidToHigh =
            metricsFresh && !hostOfferSparse &&
            (clAvgLatencyUs < 75000ULL) &&
            (clAvgDecodeTailUs < 50000ULL) &&
            (clDecodedFpsX100 >= minGoodFpsX100);

        int targetProfile = rate.abrProfile;
        const char* abrReason = "none";
        if (t >= rate.abrCooldownUntilUs) {
          const uint32_t highToMidSevereSec = rate.abrQualityFirst ? 3u : 2u;
          const uint32_t highToMidModerateSec = rate.abrQualityFirst ? 6u : 4u;
          const uint32_t midToLowSevereSec = rate.abrQualityFirst ? 4u : 3u;
          const uint32_t midToLowModerateSec = rate.abrQualityFirst ? 8u : 5u;
          const uint32_t lowToMidGoodSec = rate.abrQualityFirst ? 8u : 5u;
          const uint32_t midToHighGoodSec = rate.abrQualityFirst ? 12u : 8u;

          if (rate.abrProfile == 0) {
            if (emergencyDown && rate.abrHasLowProfile && rate.abrSeverePressureSeconds >= 1) {
              targetProfile = 2;
              abrReason = "client_emergency";
            } else if ((rate.abrSeverePressureSeconds >= highToMidSevereSec) || (rate.abrModeratePressureSeconds >= highToMidModerateSec)) {
              if (rate.abrHasMidProfile) {
                targetProfile = 1;
                abrReason = (rate.abrSeverePressureSeconds >= highToMidSevereSec) ? "high_to_mid_severe" : "high_to_mid_moderate";
              } else if (rate.abrHasLowProfile) {
                targetProfile = 2;
                abrReason = (rate.abrSeverePressureSeconds >= highToMidSevereSec) ? "high_to_low_severe" : "high_to_low_moderate";
              }
            }
            rate.abrGoodSeconds = 0;
          } else if (rate.abrProfile == 1) {
            if (emergencyDown && rate.abrHasLowProfile) {
              targetProfile = 2;
              abrReason = "client_emergency";
              rate.abrGoodSeconds = 0;
            } else if ((rate.abrSeverePressureSeconds >= midToLowSevereSec || rate.abrModeratePressureSeconds >= midToLowModerateSec) && rate.abrHasLowProfile) {
              targetProfile = 2;
              abrReason = (rate.abrSeverePressureSeconds >= midToLowSevereSec) ? "mid_to_low_severe" : "mid_to_low_moderate";
              rate.abrGoodSeconds = 0;
            } else {
              if (goodForMidToHigh) {
                ++rate.abrGoodSeconds;
              } else {
                rate.abrGoodSeconds = 0;
              }
              if (rate.abrGoodSeconds >= midToHighGoodSec) {
                targetProfile = 0;
                abrReason = "client_stable_high";
              }
            }
          } else {  // rate.abrProfile == 2
            if (goodForLowToMid) {
              ++rate.abrGoodSeconds;
            } else {
              rate.abrGoodSeconds = 0;
            }
            if (rate.abrGoodSeconds >= lowToMidGoodSec) {
              targetProfile = rate.abrHasMidProfile ? 1 : 0;
              abrReason = "client_stable_mid";
            }
          }
        }

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
          rate.encodeLadderReduced = ladderChoice.reduced;

          rate.abrProfile = targetProfile;
          rate.abrGoodSeconds = 0;
          rate.abrModeratePressureSeconds = 0;
          rate.abrSeverePressureSeconds = 0;
          rate.abrCooldownUntilUs = t + 4000000ULL;
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

      if (rate.m9Enabled && !encoder.tuneManualOverride) {
        const bool downByClient =
            metricsFresh &&
            (clCongestionState == 2 ||
             clDecodedFpsX100 < rate.m9DecodedFpsFloorX100 ||
             clQueueDepthMax >= rate.m9QueueDepthHighFrames ||
             clUdpDropPm >= rate.m9UdpDropPmHigh ||
             clAvgLatencyUs >= rate.m9LatencyHighUs ||
             clAvgDecodeTailUs >= rate.m9TailHighUs);
        const bool downByHostFallback =
            (!metricsFresh && cb2eAvgUs >= rate.m9TailHighUs);
        const bool downPressure = downByClient || downByHostFallback;
        const bool upPressure =
            metricsFresh &&
            clCongestionState == 0 &&
            clDecodedFpsX100 >= rate.m9DecodedFpsRecoverX100 &&
            clQueueDepthMax <= rate.m9QueueDepthLowFrames &&
            clUdpDropPm <= rate.m9UdpDropPmLow &&
            clAvgLatencyUs <= rate.m9LatencyLowUs &&
            clAvgDecodeTailUs <= rate.m9TailLowUs;

        if (downPressure) {
          ++rate.m9DownPressureSeconds;
        } else {
          rate.m9DownPressureSeconds = 0;
        }
        if (upPressure) {
          ++rate.m9UpPressureSeconds;
        } else {
          rate.m9UpPressureSeconds = 0;
        }

        int targetLevel = rate.m9Level;
        const char* m9Reason = "hold";
        if (t >= rate.m9CooldownUntilUs) {
          if (downPressure && rate.m9DownPressureSeconds >= rate.m9DownRequireSec && targetLevel < 3) {
            ++targetLevel;
            m9Reason = downByClient ? "client_pressure" : "host_fallback_pressure";
          } else if (upPressure && rate.m9UpPressureSeconds >= rate.m9UpRequireSec && targetLevel > 0) {
            --targetLevel;
            m9Reason = "client_recovered";
          }
        }


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
          rate.m9Level = targetLevel;
          rate.m9CooldownUntilUs = t + static_cast<uint64_t>(rate.m9CooldownSec) * 1000000ULL;
          rate.m9DownPressureSeconds = 0;
          rate.m9UpPressureSeconds = 0;
        }
      }
    }
    sender.sentFrames = 0;
    encoder.encodedFrames = 0;
    sender.sentBytes = 0;
    stats.rawEquivalentBytes = 0;
    sender.udpTxFrames = 0;
    sender.udpTxChunks = 0;
    sender.udpTxBytes = 0;
    sender.udpTxFail = 0;
    sender.udpTxNoPeer = 0;
    stats.skippedByOverwrite = 0;
    stats.stalePreEncodeDropCount = 0;
    stats.staleEncodedDropCount = 0;
    encoder.resetCount = 0;
    stats.callbackFrames = 0;
    stats.captureAgeSumUs = 0;
    stats.captureAgeMaxUs = 0;
    stats.callbackToEncodeStartSumUs = 0;
    stats.callbackToEncodeStartMaxUs = 0;
    stats.gpuScaleAttempts = 0;
    stats.gpuScaleSuccess = 0;
    stats.gpuScaleFail = 0;
    stats.gpuScaleCpuFallback = 0;
    stats.captureReadbackSamples = 0;
    stats.captureD3DWaitSumUs = 0;
    stats.captureD3DWaitMaxUs = 0;
    stats.captureCopyMapSumUs = 0;
    stats.captureCopyMapMaxUs = 0;
    stats.captureMemcpySumUs = 0;
    stats.captureMemcpyMaxUs = 0;
    stats.captureUnmapWaitSumUs = 0;
    stats.captureUnmapWaitMaxUs = 0;
    // The frozen-ring peaks must span the whole print interval, not a single tick. Everything
    // else here resets every second and is sampled once per print, but a freeze can spike in any
    // of the ~30 ticks between prints (stats.printEverySec defaults to 30), so a per-second reset
    // would throw those windows away and the peak would only ever show the last second before a
    // print. Reset them only once the value has actually been printed. (Codex.)
    if (statsPrintDue) {
      watchdog.oldestGpuPendingPeakUs = 0;
      watchdog.gpuPendingCountPeak = 0;
      // Per print-interval rates: reset only once printed so they span the whole interval
      // (matching the peak resets above). firstKey*/lastKeyAu* are per media epoch and are
      // reset by the rollover transaction instead, so they persist across prints.
      encoder.forceKeyInputCount = 0;
      sender.nonKeyAuWhileWaiting = 0;
    }
    stats.captureUnmapSumUs = 0;
    stats.captureUnmapMaxUs = 0;
    stats.gpuScaleTimedCount = 0;
    stats.gpuScaleD3DWaitSumUs = 0;
    stats.gpuScaleD3DWaitMaxUs = 0;
    stats.gpuScaleCopyMapSumUs = 0;
    stats.gpuScaleCopyMapMaxUs = 0;
    stats.gpuScaleMemcpySumUs = 0;
    stats.gpuScaleMemcpyMaxUs = 0;
    stats.gpuScaleUnmapWaitSumUs = 0;
    stats.gpuScaleUnmapWaitMaxUs = 0;
    stats.gpuScaleUnmapSumUs = 0;
    stats.gpuScaleUnmapMaxUs = 0;
    frameGating.skipCount = 0;
    frameGating.staticSkipCount = 0;
    frameGating.changePermilleSum = 0;
    frameGating.changePermilleCount = 0;
    stats.nextAtUs += 1000000ULL;
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc
