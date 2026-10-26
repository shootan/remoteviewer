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
#include "prelogin_logon_desktop.hpp"
#include "host_backend_owner.hpp"
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

namespace {
uint16_t backend_wire_code(DesktopCaptureBackend b) {
  switch (b) {
    case DesktopCaptureBackend::Dxgi: return 1;
    case DesktopCaptureBackend::Wgc: return 2;
    case DesktopCaptureBackend::Gdi: return 3;
    default: return 0;
  }
}
// capture-lifecycle r1 (UA-2): what the control thread answers a status request with (90).
uint64_t backend_status_packed(HostContext& hx) {
  auto& backend = hx.backend;
  auto& capture = hx.capture;
  uint16_t flags = kDesktopBackendStatusFlagSessionScoped;
  if (backend.dxgiQuarantined) flags |= kDesktopBackendStatusFlagDxgiQuarantined;
  if (capture.windowModeActive.load(std::memory_order_acquire)) flags |= kDesktopBackendStatusFlagWindowTarget;
  if (!capture.preloginServingDesktop.empty()) flags |= kDesktopBackendStatusFlagBeforeLogin;
  const uint64_t packed = static_cast<uint64_t>(backend_wire_code(backend.requested)) |
                          (static_cast<uint64_t>(backend_wire_code(backend.EffectiveRequested())) << 16) |
                          (static_cast<uint64_t>(backend_wire_code(backend.active)) << 32) |
                          (static_cast<uint64_t>(flags) << 48);
  return packed;
}
void publish_backend_status(HostContext& hx) {
  // r3 (Codex N3-host): with its owner, in one snapshot.
  hx.capture.backendStatus.Publish(hx.backend.owner.ownerEpoch, backend_status_packed(hx));
}
}  // namespace

Flow stage_backend(HostContext& hx, TickContext& tc) {
  auto& powerKeepalive = hx.powerKeepalive;
  auto& frameGating = hx.frameGating;
  auto& backend = hx.backend;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  auto& seq = tc.seq;
#ifdef REMOTE60_STREAM_TEST_SEAM
  // TEST BUILD ONLY (r10 T2): what the DXGI worker does when the input desktop is no longer the one served
  // (capture_backend_dxgi attach_input_desktop -> request_fallback), made to happen this long into the run
  // (GNLINK_STREAM_TEST_FORCE_DXGI_FALLBACK_MS) -- the test session has no desktop switch to make.
  {
    static const ULONGLONG forceAt = [] {
      const char* v = std::getenv("GNLINK_STREAM_TEST_FORCE_DXGI_FALLBACK_MS");
      return v ? GetTickCount64() + std::strtoull(v, nullptr, 10) : 0ull;
    }();
    static bool forced = false;
    if (forceAt != 0 && !forced && GetTickCount64() >= forceAt) {
      forced = true;
      std::cout << "[native-video-host][test-seam] dxgi fallback forced: prelogin_desktop_not_served\n";
      capture.SetDxgiFallbackReason("prelogin_desktop_not_served");
      capture.dxgiFallbackRequested.store(true, std::memory_order_release);
    }
  }
  // TEST BUILD ONLY (capture-lifecycle r1, UA): while the test shows the input desktop as a secure one
  // (GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT, desktop_name_seam.hpp), a DXGI duplication would lose access to
  // it (ACCESS_LOST / DENIED on the real secure desktop) and its worker would ask for the fallback -- the
  // test build asks for exactly that fallback. After login only (before it, T2 above is the rule).
  if (capture.preloginServingDesktop.empty() && backend.active == DesktopCaptureBackend::Dxgi && capture.dxgiStarted &&
      !capture.dxgiFallbackRequested.load(std::memory_order_acquire) &&
      SeamInputDesktopName(L"Default") == L"Winlogon") {
    std::cout << "[native-video-host][test-seam] secure desktop shown: dxgi access lost (as on a real secure desktop)\n";
    capture.SetDxgiFallbackReason("dxgi_access_lost_secure_desktop(test-seam)");
    capture.dxgiFallbackRequested.store(true, std::memory_order_release);
  }
#endif
  // r10 T2: before login a capture that cannot go on ends the worker -- never another backend.
  if (!capture.preloginServingDesktop.empty() &&
      (capture.dxgiFallbackRequested.load(std::memory_order_acquire) ||
       capture.gdiFallbackRequested.load(std::memory_order_acquire))) {
    const std::string why = capture.SnapshotFallbackReasons().dxgi;
    remote60::native_poc::prelogin::PreloginStopServing(why.empty() ? std::string("capture fallback requested") : why);
  }
  const bool streamActiveNow = clientSession.streamControlActive.load(std::memory_order_acquire);
  const bool desktopMode = !capture.windowModeActive.load(std::memory_order_acquire);
  const bool beforeLogin = !capture.preloginServingDesktop.empty();
  // The bookkeeping every capture restart in this stage shares.
  auto restarted_capture = [&](const char* reason) {
    ++capture.restartCount;
    capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
    capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
    capture.lastCallbackUs.store(0, std::memory_order_release);
    encoder.ResetTimelineAnchors(capture);
    encoder.RequestKey(kHostKeyReasonCaptureRestart);
    capture.FlushCapturePipelineState(res, frameGating, stats, reason);
  };

  // capture-lifecycle r1 (UA-2, Codex §1): a backend a viewer asked for belongs to the logical control
  // session that asked (its epoch). A NEW session starts from this host's own default -- a phone's WGC
  // choice is not inherited by the PC viewer that connects next (10-22..10-23 field: it was, for days).
  // A resume / a re-sent Hello / a selection of the same session keeps the epoch, and so the choice.
  // Only the request is reset here: the climb-back below applies it while a stream is active, a reattach
  // starts from it -- no capture work for a stream nobody watches. Before login nothing is asked anyway.
  // r2 (Codex N2): ONE epoch read for this tick -- the owner it settles is the one every backend request below is
  // taken and marked under; its real end (backendSessionEnded) puts the choice back too, once, and that ended
  // Serve()'s requests -- queued or late -- are dropped (r3).
  const uint64_t curEpoch = clientSession.epoch.load(std::memory_order_acquire);
  {
    const BackendOwnerStep ownerStep = StepBackendOwner(
        backend.owner, curEpoch, clientSession.backendSessionEnded.load(std::memory_order_acquire));
    if (ownerStep.ownerChanged) {
      std::cout << "[native-video-host][control] backend-owner epoch=" << curEpoch
                << " requested=" << desktop_capture_backend_name(backend.requested)
                << " active=" << desktop_capture_backend_name(backend.active) << "\n";
    }
    if (ownerStep.ownerChanged || ownerStep.ownerEnded) {
      if (!beforeLogin && backend.requested != backend.startupRequested) {
        std::cout << "[native-video-host][control] desktop-backend-reset "
                  << (ownerStep.ownerChanged ? "new-session" : "session-ended") << " epoch=" << curEpoch
                  << " requested=" << desktop_capture_backend_name(backend.requested)
                  << " -> host-default=" << desktop_capture_backend_name(backend.startupRequested)
                  << " effective=" << desktop_capture_backend_name(
                                          backend.dxgiQuarantined && backend.startupRequested == DesktopCaptureBackend::Dxgi
                                              ? DesktopCaptureBackend::Wgc
                                              : backend.startupRequested)
                  << " active=" << desktop_capture_backend_name(backend.active) << "\n";
        backend.requested = backend.startupRequested;
      }
    }
  }

  // capture-lifecycle r1 (PS-A / UA-1, Codex §4 -- one contract): the input desktop tracker, owned here.
  // A confirmed change clears the bootstrap cache and moves the desktop generation.
  if (capture.TrackInputDesktop(qpc_now_us())) {
    // r2 (Codex N1): the media fence comes FIRST, whatever a rebuild below does or fails to do -- the input
    // epoch moves (the sender drops every AU encoded before it), the raw frame waiting for the encoder goes,
    // and a key is owed. Nothing captured on the desktop that was crosses the edge; the attachment's later
    // frames are refused at adoption (PublishFrame: their attachment's desktop generation is stale).
    capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
    capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
    capture.lastCallbackUs.store(0, std::memory_order_release);
    encoder.ResetTimelineAnchors(capture);
    encoder.RequestKey(kHostKeyReasonCaptureRestart);
    capture.FlushCapturePipelineState(res, frameGating, stats, "desktop-edge");
    std::cout << "[native-video-host][desktop] edge fence desktopGen="
              << capture.desktopGeneration.load(std::memory_order_acquire) << " atUs=" << qpc_now_us() << "\n";
  }
  // UA-1 / r2 N1: the running capture is rebuilt after every confirmed desktop change, whatever backend --
  // the 10-23 field: requested=active=WGC never crossed a UAC prompt (no frames of it, then black after it),
  // and two reconnects were what rebuilt it. The rule is the provenance itself: the attachment running was
  // made under an older desktop generation. Detached (idle) -> nothing to do, the reattach is made under the
  // current one. No stream -> nothing now (no capture work for a stream nobody watches); a resume rebuilds.
  // Bounded per desktop generation, backoff between attempts.
  // r3 (Codex N1-a): a rebuild that FAILED has already detached (DetachCaptureSession stamps the generation the
  // next attachment would have had), so "stale" alone would call it done. The rebuild is open until a restart
  // of this generation succeeds: stale, or this generation's rebuild failed and nothing has attached since
  // (restartPending, owned by the rebuild -- the generic retry leaves it alone). Counted per (generation,
  // control session); a newer generation or a new session starts its own count.
  {
    const uint64_t dgen = capture.desktopGeneration.load(std::memory_order_acquire);
    const bool stale = capture.attachmentDesktopGen.load(std::memory_order_acquire) != dgen;
    const bool failedHere = capture.restartPending && capture.restartOwner == CaptureState::RestartOwner::DesktopRebuild &&
                            backend.desktopRebuildGen == dgen;
    if ((stale || failedHere) && streamActiveNow && !capture.idleDetached) {
      if (backend.desktopRebuildGen != dgen || backend.desktopRebuildEpoch != curEpoch) {
        backend.desktopRebuildGen = dgen;
        backend.desktopRebuildEpoch = curEpoch;
        backend.desktopRebuildAttempts = 0;
        backend.desktopRebuildRetryAtUs = 0;
      }
      const uint64_t nowRebuildUs = qpc_now_us();
      // (Never before the restart wrapper's own backoff: an attempt it would refuse unseen is no attempt.)
      if (backend.desktopRebuildAttempts < DesktopBackendState::kMaxDesktopRebuildAttempts &&
          nowRebuildUs >= backend.desktopRebuildRetryAtUs &&
          (!capture.restartPending || nowRebuildUs >= capture.restartRetryAtUs)) {
        ++backend.desktopRebuildAttempts;
        powerKeepalive.SetStreaming(true, true);
        const bool ok = restart_capture_session(hx, CaptureState::RestartOwner::DesktopRebuild);
        if (ok) restarted_capture("desktop-edge-rebuild");
        std::cout << "[native-video-host][desktop] rebuild desktopGen=" << dgen << " attempt="
                  << backend.desktopRebuildAttempts << " ok=" << (ok ? 1 : 0)
                  << " active=" << desktop_capture_backend_name(backend.active) << "\n";
        if (!ok) {
          backend.desktopRebuildRetryAtUs = nowRebuildUs + 500000ULL * backend.desktopRebuildAttempts;
          if (backend.desktopRebuildAttempts >= DesktopBackendState::kMaxDesktopRebuildAttempts) {
            // Fail closed: the old attachment's frames stay refused (no picture of the desktop that was);
            // any later attach (reconnect, idle reattach, backend change) is made under this generation.
            std::cout << "[native-video-host][desktop] rebuild gave up desktopGen=" << dgen << "\n";
          }
        }
      }
    }
  }
  // PS-A: a still WGC desktop with nothing cached gives no frame by itself; the pop stage asked (bounded)
  // for a fresh session, whose first frame is the whole picture.
  if (capture.wgcFullFrameRestartWanted.exchange(false, std::memory_order_acq_rel) && streamActiveNow && desktopMode &&
      !beforeLogin && backend.active == DesktopCaptureBackend::Wgc && !capture.idleDetached && !capture.restartPending) {
    powerKeepalive.SetStreaming(true, true);
    // r2/r3 (Codex N4/N1-a): the episode's own reacquire keeps its budget -- and so does its retry, if it fails.
    const bool ok = restart_capture_session(hx, CaptureState::RestartOwner::FullFrame);
    if (ok) restarted_capture("full-frame-wgc-restart");
    std::cout << "[native-video-host] full-frame wgc restart ok=" << (ok ? 1 : 0) << "\n";
  }

  publish_backend_status(hx);  // capture-lifecycle r1 (UA-2): whatever changed since the last tick
  if (const auto backendReq = hx.mailbox.TakeBackendRequestForOwner(backend.owner)) {
    const uint32_t reqSeq = backendReq->seq;
    // capture-lifecycle r1 (UA-2): handled (applied, stored or refused) once this block ends -- the control
    // thread answers a status request then. A struct with its destructor at the block's end, every path.
    struct AppliedMark {
      HostContext& hx;
      uint64_t key;
      ~AppliedMark() {
        // r3 (Codex N3-host): the handled key with the status it left, in one publish.
        hx.capture.backendStatus.PublishHandled(hx.backend.owner.ownerEpoch, backend_status_packed(hx), key);
      }
    } appliedMark{hx, BackendRequestKey(backendReq->epoch, backend.owner.ownerEpoch, reqSeq)};  // r2 N2: its own epoch
    DesktopCaptureBackend nextRequested = backend.requested;
    const uint16_t requestedCode = static_cast<uint16_t>(backendReq->backend);
    DesktopCaptureBackend asked = backend.requested;
    const bool decoded = desktop_capture_backend_from_code(requestedCode, &asked);
    if (decoded && !capture.preloginServingDesktop.empty() && asked != DesktopCaptureBackend::Dxgi) {
      // r10 T2: a viewer's backend choice does not apply before login (only DXGI on the logon desktop).
      std::cout << "[native-video-host][prelogin] desktop backend request ignored (only dxgi before login)\n";
    } else if (decoded) {
      nextRequested = asked;
      backend.requested = nextRequested;
      const bool desktopActive = !capture.windowModeActive.load(std::memory_order_acquire);
      // dxgi-wedge-fix r1 (Codex B5): the request is STORED as the client's wish either way; what may
      // run is EffectiveRequested(), so an older APK's dxgi-on-connect cannot lift the quarantine.
      const DesktopCaptureBackend effective = backend.EffectiveRequested();
      // r2 (Codex N2): no capture work for a stream nobody watches -- inactive / detached, the choice is only
      // stored; the next active attach (reattach: EffectiveRequested) or the climb-back applies it.
      const bool restartNeeded =
          desktopActive && streamActiveNow && !capture.idleDetached && backend.active != effective;
      if (backend.dxgiQuarantined && backend.requested == DesktopCaptureBackend::Dxgi) {
        std::cout << "[native-video-host][capture-quarantine] request seq=" << reqSeq
                  << " requested=dxgi effective=" << desktop_capture_backend_name(effective)
                  << " reason=" << backend.dxgiQuarantineReason << " (kept quarantined)\n";
      }
      if (restartNeeded) {
        const DesktopCaptureBackend prevActiveBackend = backend.active;
        backend.active = effective;
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
          encoder.RequestKey(kHostKeyReasonCaptureRestart);
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
    const std::string fallbackReason = capture.SnapshotFallbackReasons().dxgi;
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
    encoder.RequestKey(kHostKeyReasonCaptureRestart);
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
    const std::string fallbackReason = capture.SnapshotFallbackReasons().gdi;
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
    encoder.RequestKey(kHostKeyReasonCaptureRestart);
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
  // dxgi-wedge-fix r1: "back to what was requested" means back to EffectiveRequested() -- while DXGI
  // is quarantined a WGC that was asked for as DXGI is not a demotion, and must not be climbed out of.
  if (backend.active != backend.EffectiveRequested() &&
      !capture.windowModeActive.load(std::memory_order_acquire) &&
      clientSession.streamControlActive.load(std::memory_order_acquire)) {
    const uint64_t nowUs = qpc_now_us();
    backend.NoteDemotionEpisode(nowUs);
    // Probe the interactive-desktop state at a bounded cadence (OpenInputDesktop is a syscall).
    // A secure desktop resets the stability clock; the default desktop starts or continues it.
    // The uncached query is deliberate: the shared cached one is refreshed by input/pong callers
    // and can hand a stale "default" reading to a promotion decision the moment a UAC prompt rose.
    if (backend.DefaultProbeDue(nowUs)) {
      backend.NoteDefaultProbe(nowUs, interactive_desktop_is_default_uncached());
    }
    const bool defaultStable = backend.DefaultStable(nowUs);
    if (backend.RetryDue(nowUs)) {
      // The retry deadline is due. Promote only if the default desktop has been up for the whole
      // settle window AND one final uncached check confirms it is still up right now -- otherwise
      // a UAC prompt that reappeared since the last cadence probe would still eat a restart+IDR.
      bool finalDefault = defaultStable;
      if (finalDefault && !interactive_desktop_is_default_uncached()) {
        finalDefault = false;
        backend.NoteSecureAtDeadline();
      }
      if (!finalDefault) {
        // Deferred by the secure gate. Latch so this counts once per deadline episode, not once
        // per main-loop iteration -- the deadline stays due until we actually attempt.
        if (backend.NoteDeferredForSecure()) {
          std::cout << "[native-video-host] desktop-promotion-deferred reason=secure-desktop\n";
        }
      } else {
        backend.NotePromotionAttempt();
        const DesktopCaptureBackend demoted = backend.active;
        const DesktopCaptureBackend target = backend.EffectiveRequested();
        backend.active = target;
        const bool restarted = restart_capture_session(hx);
        // restart_capture_session(hx) reports that *a* session started, not that it started on the
        // backend we asked for. When the requested one is still unavailable it falls back
        // internally, puts backend.active back where it was, and returns success anyway.
        // The backend the restart actually left behind is the only honest test.
        const bool promoted = restarted && backend.active == target;
        if (restarted) {
          // A restart replaces the capture session whether or not the backend moved, so the
          // timeline still has to be re-anchored and the next frame still has to be a keyframe.
          ++capture.restartCount;
          capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(),
                                     std::memory_order_release);
          capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
          capture.lastCallbackUs.store(0, std::memory_order_release);
          encoder.ResetTimelineAnchors(capture);
          encoder.RequestKey(kHostKeyReasonCaptureRestart);
          capture.FlushCapturePipelineState(res, frameGating, stats, promoted ? "desktop-backend-restored"
                                                : "desktop-backend-retry-failed");
        }
        if (promoted) {
          backend.NotePromotionSuccess(nowUs);
          std::cout << "[native-video-host] desktop-backend-restored from="
                    << desktop_capture_backend_name(demoted)
                    << " to=" << desktop_capture_backend_name(backend.active) << "\n";
        } else {
          // A real promotion failure with the default desktop up (e.g. RDP: primary duplication
          // still unavailable). This is not the secure-desktop case, so back off -- a machine
          // that genuinely cannot use the requested backend must not restart every few seconds.
          backend.active = demoted;
          backend.NotePromotionFailure(nowUs);
        }
        // Any attempt consumes the current stability evidence; the next one must gather fresh
        // proof that the default desktop is up before it may fire.
        backend.ConsumeStabilityEvidence();
      }
    }
  } else {
    backend.ResetPromotionGate();
  }

  return Flow::Next;
}

}  // namespace remote60::native_poc
