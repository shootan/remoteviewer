// Stage 2: desktop backend request, demotion recovery and climb-back promotion.
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

Flow stage_backend(HostContext& hx, TickContext& tc) {
  auto& powerKeepalive = hx.powerKeepalive;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& backend = hx.backend;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  auto& seq = tc.seq;
  if (backend.reqPending.exchange(false, std::memory_order_acq_rel)) {
    const uint32_t reqSeq = backend.reqSeq.load(std::memory_order_acquire);
    DesktopCaptureBackend nextRequested = backend.requested;
    const uint16_t requestedCode = backend.reqValue.load(std::memory_order_acquire);
    if (desktop_capture_backend_from_code(requestedCode, &nextRequested)) {
      backend.requested = nextRequested;
      const bool desktopActive = !capture.windowModeActive.load(std::memory_order_acquire);
      const bool restartNeeded = desktopActive && backend.active != backend.requested;
      if (restartNeeded) {
        const DesktopCaptureBackend prevActiveBackend = backend.active;
        backend.active = backend.requested;
        if (!restart_capture_session(hx)) {
          backend.active = prevActiveBackend;
          std::cerr << "[native-video-host][control] desktop-backend-apply failed seq=" << reqSeq
                    << " requested=" << desktop_capture_backend_name(backend.requested)
                    << " active=" << desktop_capture_backend_name(backend.active)
                    << "\n";
        } else {
          ++capture.restartCount;
          capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
          capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
          capture.lastCallbackUs.store(0, std::memory_order_release);
          encoder.ResetTimelineAnchors(capture);
          encoder.forceKeyNext = true;
          capture.FlushCapturePipelineState(res, frameGating, stats, "desktop-backend-switch");
          std::cout << "[native-video-host][control] desktop-backend-applied seq=" << reqSeq
                    << " requested=" << desktop_capture_backend_name(backend.requested)
                    << " active=" << desktop_capture_backend_name(backend.active)
                    << " desktopActive=1\n";
        }
      } else {
        std::cout << "[native-video-host][control] desktop-backend-stored seq=" << reqSeq
                  << " requested=" << desktop_capture_backend_name(backend.requested)
                  << " active=" << desktop_capture_backend_name(backend.active)
                  << " desktopActive=" << (desktopActive ? 1 : 0)
                  << "\n";
      }
    }
  }
  // A DXGI worker can lose duplication during a fullscreen/desktop transition after the
  // viewer has already marked the stream inactive. Process recovery before the inactive
  // early-return; otherwise the request remains stuck and the next selection intermittently
  // times out with DXGI_ERROR_ACCESS_LOST/E_ACCESSDENIED.
  // The active check comes before the exchange so an inactive stream does not consume the
  // request: with no client watching, restarting capture here would be exactly the leak this
  // lifecycle exists to close -- an RDP connect moves the desktop, the fallback fires, and a
  // clientless host starts capturing the RDP session at full rate. While the stream is
  // inactive the request either survives until the client returns (processed then, one
  // iteration after the active edge) or is cleared by the idle detach, whose reattach
  // re-resolves the backend from scratch anyway.
  if (clientSession.streamControlActive.load(std::memory_order_acquire) &&
      capture.dxgiFallbackRequested.exchange(false, std::memory_order_acq_rel) &&
      !capture.windowModeActive.load(std::memory_order_acquire)) {
    powerKeepalive.SetStreaming(true, true);
    if (capture.dxgiStarted) {
      res.dxgiCaptureSession.Stop();
      capture.dxgiStarted = false;
    }
    backend.active = DesktopCaptureBackend::Wgc;
    const std::string fallbackReason = capture.CopyDxgiFallbackReason();
    std::cout << "[native-video-host] fallback_reason="
              << (fallbackReason.empty() ? "dxgi_runtime_fallback" : fallbackReason)
              << "\n";
    if (!restart_capture_session(hx)) {
      std::cerr << "[native-video-host] capture fallback restart failed; retrying\n";
      capture.dxgiFallbackRequested.store(true, std::memory_order_release);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return Flow::Continue;
    }
    ++capture.restartCount;
    capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
    capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
    capture.lastCallbackUs.store(0, std::memory_order_release);
    encoder.ResetTimelineAnchors(capture);
    encoder.forceKeyNext = true;
    capture.FlushCapturePipelineState(res, frameGating, stats, "dxgi-runtime-fallback");
  }
  if (clientSession.streamControlActive.load(std::memory_order_acquire) &&
      capture.gdiFallbackRequested.exchange(false, std::memory_order_acq_rel) &&
      !capture.windowModeActive.load(std::memory_order_acquire)) {
    powerKeepalive.SetStreaming(true, true);
    if (capture.gdiStarted) {
      res.gdiCaptureProcess.Stop();
      capture.gdiStarted = false;
    }
    backend.active = DesktopCaptureBackend::Wgc;
    const std::string fallbackReason = capture.CopyGdiFallbackReason();
    std::cout << "[native-video-host] fallback_reason="
              << (fallbackReason.empty() ? "gdi_runtime_fallback" : fallbackReason)
              << "\n";
    if (!restart_capture_session(hx)) {
      std::cerr << "[native-video-host] GDI capture fallback restart failed; retrying\n";
      capture.gdiFallbackRequested.store(true, std::memory_order_release);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return Flow::Continue;
    }
    ++capture.restartCount;
    capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
    capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
    capture.lastCallbackUs.store(0, std::memory_order_release);
    encoder.ResetTimelineAnchors(capture);
    encoder.forceKeyNext = true;
    capture.FlushCapturePipelineState(res, frameGating, stats, "gdi-runtime-fallback");
  }

  // Climb back to the requested backend once whatever forced the demotion has passed.
  //
  // A demotion used to be permanent: backend.active was set to Wgc and the only way back
  // was an explicit request from the client, which then failed again for the same reason. So a
  // single UAC prompt or RDP connect left the session on WGC for good, and the picture stayed
  // degraded long after the cause was gone. That is the "everything is slower after a UAC
  // prompt" report.
  //
  // Both causes are temporary by nature. The secure desktop goes away when the prompt is
  // answered, and the desktop returns to the physical adapter when RDP disconnects, so simply
  // trying again is what was missing.
  if (backend.active != backend.requested &&
      !capture.windowModeActive.load(std::memory_order_acquire) &&
      clientSession.streamControlActive.load(std::memory_order_acquire)) {
    const uint64_t nowUs = qpc_now_us();
    if (backend.demotionSinceUs == 0) backend.demotionSinceUs = nowUs;
    // Probe the interactive-desktop state at a bounded cadence (OpenInputDesktop is a syscall).
    // A secure desktop resets the stability clock; the default desktop starts or continues it.
    // The uncached query is deliberate: the shared cached one is refreshed by input/pong callers
    // and can hand a stale "default" reading to a promotion decision the moment a UAC prompt rose.
    if (nowUs >= backend.defaultProbeAtUs) {
      backend.defaultProbeAtUs = nowUs + kDesktopDefaultProbeIntervalUs;
      if (interactive_desktop_is_default_uncached()) {
        if (backend.defaultStableSinceUs == 0) backend.defaultStableSinceUs = nowUs;
      } else {
        backend.defaultStableSinceUs = 0;
        backend.secureProbeFalseTotal.fetch_add(1, std::memory_order_relaxed);
      }
    }
    const bool defaultStable =
        backend.defaultStableSinceUs != 0 &&
        (nowUs - backend.defaultStableSinceUs) >= kDesktopDefaultStableUs;
    if (backend.retryAtUs == 0) {
      backend.retryAtUs = nowUs + kDesktopBackendRetryMinUs;
    } else if (nowUs >= backend.retryAtUs) {
      // The retry deadline is due. Promote only if the default desktop has been up for the whole
      // settle window AND one final uncached check confirms it is still up right now -- otherwise
      // a UAC prompt that reappeared since the last cadence probe would still eat a restart+IDR.
      bool finalDefault = defaultStable;
      if (finalDefault && !interactive_desktop_is_default_uncached()) {
        finalDefault = false;
        backend.defaultStableSinceUs = 0;  // secure again: restart the settle clock
        backend.secureProbeFalseTotal.fetch_add(1, std::memory_order_relaxed);
      }
      if (!finalDefault) {
        // Deferred by the secure gate. Latch so this counts once per deadline episode, not once
        // per main-loop iteration -- the deadline stays due until we actually attempt.
        if (!backend.promotionDeferredForCurrentDeadline) {
          backend.promotionDeferredForCurrentDeadline = true;
          backend.promotionDeferredSecureTotal.fetch_add(1, std::memory_order_relaxed);
          std::cout << "[native-video-host] desktop-promotion-deferred reason=secure-desktop\n";
        }
      } else {
        backend.promotionDeferredForCurrentDeadline = false;
        backend.promotionAttempts.fetch_add(1, std::memory_order_relaxed);
        const DesktopCaptureBackend demoted = backend.active;
        backend.active = backend.requested;
        const bool restarted = restart_capture_session(hx);
        // restart_capture_session(hx) reports that *a* session started, not that it started on the
        // backend we asked for. When the requested one is still unavailable it falls back
        // internally, puts backend.active back where it was, and returns success anyway.
        // The backend the restart actually left behind is the only honest test.
        const bool promoted = restarted && backend.active == backend.requested;
        if (restarted) {
          // A restart replaces the capture session whether or not the backend moved, so the
          // timeline still has to be re-anchored and the next frame still has to be a keyframe.
          ++capture.restartCount;
          capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(),
                                     std::memory_order_release);
          capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
          capture.lastCallbackUs.store(0, std::memory_order_release);
          encoder.ResetTimelineAnchors(capture);
          encoder.forceKeyNext = true;
          capture.FlushCapturePipelineState(res, frameGating, stats, promoted ? "desktop-backend-restored"
                                                : "desktop-backend-retry-failed");
        }
        if (promoted) {
          backend.promotionSuccess.fetch_add(1, std::memory_order_relaxed);
          if (backend.demotionSinceUs != 0 && nowUs >= backend.demotionSinceUs) {
            backend.lastPromotionWaitUs.store(nowUs - backend.demotionSinceUs, std::memory_order_relaxed);
          }
          std::cout << "[native-video-host] desktop-backend-restored from="
                    << desktop_capture_backend_name(demoted)
                    << " to=" << desktop_capture_backend_name(backend.active) << "\n";
          backend.retryAtUs = 0;
          backend.retryDelayUs = kDesktopBackendRetryMinUs;
          backend.demotionSinceUs = 0;
        } else {
          // A real promotion failure with the default desktop up (e.g. RDP: primary duplication
          // still unavailable). This is not the secure-desktop case, so back off -- a machine
          // that genuinely cannot use the requested backend must not restart every few seconds.
          backend.promotionFail.fetch_add(1, std::memory_order_relaxed);
          backend.active = demoted;
          backend.retryDelayUs =
              std::min<uint64_t>(backend.retryDelayUs * 2, kDesktopBackendRetryMaxUs);
          backend.retryAtUs = nowUs + backend.retryDelayUs;
        }
        // Any attempt consumes the current stability evidence; the next one must gather fresh
        // proof that the default desktop is up before it may fire.
        backend.defaultStableSinceUs = 0;
        backend.defaultProbeAtUs = 0;
      }
    }
  } else {
    backend.retryAtUs = 0;
    backend.retryDelayUs = kDesktopBackendRetryMinUs;
    backend.defaultStableSinceUs = 0;
    backend.defaultProbeAtUs = 0;
    backend.demotionSinceUs = 0;
    backend.promotionDeferredForCurrentDeadline = false;
  }

  return Flow::Next;
}

}  // namespace remote60::native_poc
