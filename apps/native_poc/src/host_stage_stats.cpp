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
  // Persistent, not tc.w/tc.h: this stage now runs first in the tick, before a frame has been
  // popped, so the per-tick values would print 0x0. lastFrameW/H is the last size actually
  // popped, which is what the old placement printed anyway. (Ledger H-10.)
  const uint32_t w = stats.lastFrameW;
  const uint32_t h = stats.lastFrameH;
  (void)tc;
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
    // One lock-correct read of the cadence counters for the whole tick: the drain watchdog's
    // accepted delta below and the stats line both come from it. (Ledger H-05.)
    const CaptureCadenceGate::Counters cadence = capture.SnapshotCadenceCounters();
    const uint64_t queuePushPerSec =
        (stats.queuePushCount >= stats.queuePushCountLastSample) ? (stats.queuePushCount - stats.queuePushCountLastSample) : 0;
    stats.queuePushCountLastSample = stats.queuePushCount;
    stats.queuePushPerSecLatest = queuePushPerSec;
    // exchange, not load-then-zero-at-the-end: the readback worker keeps publishing while this
  // tick runs, and the old `= 0` at the bottom threw away every increment that landed in
  // between. Those lost frames biased `published` downward -- and `published` is what the
  // readback-drain and GDI low-push watchdogs restart capture on. (Ledger H-12.)
    const uint64_t callbackFramesPerSec = stats.callbackFrames.exchange(0, std::memory_order_relaxed);
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

      // Per-1s-window deltas. acceptContent / BusyDrops / SupersededDrops are all lifetime
      // cumulative (superseded especially -- it is never reset), so diff, never read absolute.
      // The snapshots are advanced every tick regardless of whether the watchdog is eligible, so
      // an eligible second always sees exactly that second's increment.
      // acceptContent comes from `cadence` above, taken under capture.cadenceMu -- the mutex the
      // capture-callback thread mutates the gate under. The watchdog restarts capture on this
      // value, so an unlocked read is a real data race, not just a stale display. (BusyDrops and
      // SupersededDrops are std::atomic, so they need no lock.)
      const uint64_t acceptedNow = cadence.acceptContent;
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
      const Flow h264Flow = stats_tick_h264(hx, tc, t, statsPrintDue, mbps, targetProcessName,
                                            queuePushPerSec, callbackFramesPerSec, idleHoldPerSec,
                                            cadence);
      if (h264Flow != Flow::Next) return h264Flow;
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
    // Clamped, so a tick that ran late cannot leave nextAtUs in the past and fire once per
    // loop iteration until it catches up. Those catch-up windows measured ~0 seconds, read
    // callbackFramesPerSec as 0, and drove inputLowPushStreakSec to its threshold within a
    // few milliseconds -- a capture restart with nothing wrong. Phase is preserved in the
    // ordinary case; a real gap re-anchors. Every window is now >= 1s by construction, which
    // is also what makes the low-push streak count genuinely separated observations.
    // (Ledger H-11.)
    if (stats.nextAtUs <= t) stats.nextAtUs = t + 1000000ULL;
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc
