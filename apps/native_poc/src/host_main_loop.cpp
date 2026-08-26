// See host_main_loop.hpp for the module summary. Everything below is the former main loop and
// helper lambdas of native_video_host_main.cpp, moved verbatim (host split refactor Phase 3):
// each stage body is unchanged apart from loop-level continue/break/return becoming Flow
// results, per-iteration locals living in TickContext, and the moved helpers taking the context.
// The alias lines at the top of each function bind the old local names to the context.

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

// ---------------------------------------------------------------------------------------------
// Helpers the loop calls (former main() lambdas).
// ---------------------------------------------------------------------------------------------

bool restart_capture_session(HostContext& hx) {
  auto& useH264 = hx.useH264;
  auto& stop = hx.stop;
  auto& item = hx.item;
  auto& token = hx.token;
  auto& frameGating = hx.frameGating;
  auto& backend = hx.backend;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  auto& res = hx.res;
  watchdog.EnterMainPhase(MainLoopPhase::CaptureRestart);
  // A restarted session invalidates the held pointer sample even when the stream generation
  // survives (some size-changes keep it): a stale position against the new capture geometry
  // would misplace the remote cursor until the next real mouse update.
  capture.dxgiPointerUpdateUs.store(0, std::memory_order_release);
  if (!capture.RestartCaptureSessionImpl(res, backend, clientSession, encoder, stop, useH264, item, token)) return false;
  uint32_t finalW = 0, finalH = 0;
  {
    std::lock_guard<std::mutex> lk(capture.resourceMu);
    finalW = capture.width;
    finalH = capture.height;
  }
  encoder.ApplyConfirmedCaptureGeometry(capture, res, frameGating, inputRouter, sender, finalW, finalH, "capture-restart");
  return true;
}

void pump_cursor_forward(HostContext& hx, uint64_t nowUs) {
  auto& transport = hx.transport;
  auto& backend = hx.backend;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& capture = hx.capture;
  // Field verdict: the remote-cursor marker reads as clutter, not signal -- the user asked for
  // it gone. Default OFF on both ends; the reviewed machinery (generation fence, overlay) stays
  // dormant behind this env for future reconsideration.
  static const bool remoteCursorEnabled = env_truthy("REMOTE60_NATIVE_REMOTE_CURSOR");
  if (!remoteCursorEnabled) return;
  if (transport != VideoTransport::Udp || !sender.udpPeerReady) return;
  if (!clientSession.streamControlActive.load(std::memory_order_acquire)) return;
  if (capture.windowModeActive.load(std::memory_order_acquire)) return;
  if (backend.active != DesktopCaptureBackend::Dxgi) return;
  if (capture.dxgiPointerUpdateUs.load(std::memory_order_acquire) == 0) return;
  if (nowUs < inputRouter.cursorSendLastUs + 33'000) return;  // <=30Hz
  // Generation fence: a sample captured under the previous target/attachment must never be
  // sent as if it belonged to the current one (stale desktop cursor over a fresh window).
  const uint64_t sampleGen = capture.dxgiPointerGeneration.load(std::memory_order_relaxed);
  if (sampleGen != capture.streamGenerationState.load(std::memory_order_acquire)) return;
  const int32_t px = capture.dxgiPointerX.load(std::memory_order_acquire);
  const int32_t py = capture.dxgiPointerY.load(std::memory_order_acquire);
  const bool visible = capture.dxgiPointerVisible.load(std::memory_order_acquire);
  const bool changed = px != inputRouter.cursorSentX || py != inputRouter.cursorSentY || visible != inputRouter.cursorSentVisible;
  if (!changed && (!visible || nowUs < inputRouter.cursorSendLastUs + 250'000)) return;
  remote60::native_poc::UdpCursorPosPacket pkt{};
  if (visible) pkt.flags |= 0x1u;
  pkt.x = px;
  pkt.y = py;
  pkt.streamGeneration = sampleGen;
  {
    std::lock_guard<std::mutex> lk(capture.resourceMu);
    pkt.captureW = capture.width;
    pkt.captureH = capture.height;
  }
  pkt.hostQpcUs = nowUs;
  (void)sendto(clientSession.clientSock, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
               reinterpret_cast<const sockaddr*>(&sender.udpPeer), sizeof(sender.udpPeer));
  inputRouter.cursorSendLastUs = nowUs;
  inputRouter.cursorSentX = px;
  inputRouter.cursorSentY = py;
  inputRouter.cursorSentVisible = visible;
}

bool reconnect_tcp_data_session(HostContext& hx, const char* reason) {
  auto& args = hx.args;
  auto& transport = hx.transport;
  auto& stop = hx.stop;
  auto& frameGating = hx.frameGating;
  auto& kick = hx.kick;
  auto& clientMetrics = hx.clientMetrics;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& res = hx.res;
  if (transport != VideoTransport::Tcp) return false;
  if (args.seconds > 0) return false;
  if (clientSession.listenSock == INVALID_SOCKET) return false;
  if (clientSession.clientSock != INVALID_SOCKET) {
    shutdown(clientSession.clientSock, SD_BOTH);
    closesocket(clientSession.clientSock);
    clientSession.clientSock = INVALID_SOCKET;
  }
  std::cout << "[native-video-host] data disconnected reason="
            << (reason ? reason : "unknown")
            << " waiting reconnect\n";
  while (!stop.load()) {
    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    SOCKET accepted = accept(clientSession.listenSock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    if (accepted == INVALID_SOCKET) {
      if (stop.load()) return false;
      Sleep(50);
      continue;
    }
    clientSession.clientSock = accepted;
    int noDelay = 1;
    setsockopt(clientSession.clientSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    if (args.tcpSendBufKb > 0) {
      const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
      setsockopt(clientSession.clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
    }
    int effectiveSendBuf = 0;
    int effectiveSendBufLen = sizeof(effectiveSendBuf);
    (void)getsockopt(clientSession.clientSock, SOL_SOCKET, SO_SNDBUF,
                     reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
    std::cout << "[native-video-host] client reconnected transport=tcp sndbuf="
              << effectiveSendBuf << " bytes\n";
    clientMetrics.updatedUs = 0;
    clientMetrics.congestionState = 0;
    clientMetrics.congestionTransitions = 0;
    clientMetrics.congestionRecoveryCount = 0;
    clientMetrics.congestionRecoveryReq = 0;
    clientMetrics.congestionRecoveryMaxUs = 0;
    clientMetrics.queueDepthMax = 0;
    clientMetrics.queueDepthH4p = 0;
    clientMetrics.udpAssemblyDropPm = 0;
    clientMetrics.requestedKeyFrame = false;
    clientMetrics.keyFrameReason = 0;
    encoder.tunePending = false;
    encoder.tuneBitrate = 0;
    encoder.tuneKeyint = 0;
    encoder.tuneFps = 0;
    encoder.tuneSeq = 0;
    encoder.keyReqTokens = static_cast<double>(encoder.keyReqTokenCapacity);
    encoder.keyReqLastRefillUs = 0;
    encoder.keyReqNextAllowedUs = 0;
    encoder.forceKeyNext = true;
    kick.selectionFirstKeyframeDropCount = 0;
    encoder.encodedSeq = 0;
    stats.lastSendStartUs = 0;
    frameGating.lastSentUs = 0;
    {
      std::lock_guard<std::mutex> lk(res.frame.mu);
      stats.lastVersionSent = res.frame.version;
    }
    return true;
  }
  return false;
}

bool apply_selected_window_capture(HostContext& hx, uint64_t requestedWindowId, uint64_t nowUs,
                                   uint32_t* outFlags, uint64_t* outWindowId,
                                   uint64_t* outStreamGeneration,
                                   std::string* outReason, std::string* outTitle) {
  auto& useH264 = hx.useH264;
  auto& captureWindowRebindIntervalUs = hx.captureWindowRebindIntervalUs;
  auto& nextCaptureWindowCheckUs = hx.nextCaptureWindowCheckUs;
  auto& item = hx.item;
  auto& frameGating = hx.frameGating;
  auto& kick = hx.kick;
  auto& backend = hx.backend;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  if (outFlags) *outFlags = 0;
  if (outWindowId) *outWindowId = requestedWindowId;
  if (outStreamGeneration) {
    *outStreamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
  }
  if (outReason) outReason->clear();
  if (outTitle) outTitle->clear();
  if (capture.windowSelectionLocked.load(std::memory_order_acquire)) {
    if (outFlags) *outFlags |= 0x2u;
    if (outReason) *outReason = "selection_locked_by_config";
    if (requestedWindowId == 0 && outTitle) *outTitle = "desktop";
    return false;
  }

  const auto prevItem = item;
  const bool prevCaptureWindowModeActive = capture.windowModeActive.load(std::memory_order_acquire);
  const bool prevCaptureWindowClientOnlyActive =
      capture.windowClientOnlyActive.load(std::memory_order_acquire);
  const auto prevCaptureWindowCriteria = capture.windowCriteria;
  const auto prevCaptureWindowInfo = capture.windowInfo;
  const uint64_t prevSelectedWindowId = capture.selectedWindowId.load(std::memory_order_acquire);
  const uint64_t prevCaptureStreamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
  const uint32_t prevHostCaptureFlags = capture.targetFlags.load(std::memory_order_acquire);
  const uint32_t prevHostCapturePid = capture.targetPid.load(std::memory_order_acquire);
  const uint64_t prevHostCaptureHwnd = capture.targetHwnd.load(std::memory_order_acquire);
  const uint32_t prevHostCaptureRebindCount = capture.rebindCount.load(std::memory_order_acquire);
  std::string prevHostCaptureProcess;
  std::string prevHostCaptureTitle;
  {
    std::lock_guard<std::mutex> lk(capture.metaMu);
    prevHostCaptureProcess = capture.targetProcess;
    prevHostCaptureTitle = capture.targetTitle;
  }

  auto restore_previous_target = [&]() {
    item = prevItem;
    capture.windowModeActive.store(prevCaptureWindowModeActive, std::memory_order_release);
    capture.windowClientOnlyActive.store(prevCaptureWindowClientOnlyActive, std::memory_order_release);
    capture.windowCriteria = prevCaptureWindowCriteria;
    capture.windowInfo = prevCaptureWindowInfo;
    capture.selectedWindowId.store(prevSelectedWindowId, std::memory_order_release);
    capture.streamGenerationState.store(prevCaptureStreamGeneration, std::memory_order_release);
    capture.targetFlags.store(prevHostCaptureFlags, std::memory_order_release);
    capture.targetPid.store(prevHostCapturePid, std::memory_order_release);
    capture.targetHwnd.store(prevHostCaptureHwnd, std::memory_order_release);
    capture.rebindCount.store(prevHostCaptureRebindCount, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lk(capture.metaMu);
      capture.targetProcess = prevHostCaptureProcess;
      capture.targetTitle = prevHostCaptureTitle;
    }
  };

  winrt::Windows::Graphics::Capture::GraphicsCaptureItem nextItem{nullptr};
  CaptureWindowInfo nextCaptureWindowInfo{};
  bool nextCaptureWindowModeActive = false;
  bool nextCaptureWindowClientOnlyActive = false;
  CaptureWindowCriteria nextCaptureWindowCriteria{};
  uint64_t nextSelectedWindowId = requestedWindowId;
  std::string nextReason = "ok";
  std::string nextTitle;
  std::string nextProcess = "monitor";
  uint32_t nextPid = 0;
  uint64_t nextHwnd = 0;
  uint32_t nextFlags = 0;
  const uint64_t nextCaptureStreamGeneration = prevCaptureStreamGeneration + 1;

  if (requestedWindowId == 0) {
    if (backend.requested == DesktopCaptureBackend::Wgc ||
        backend.active == DesktopCaptureBackend::Wgc) {
      nextItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(window-select-desktop)");
      if (!nextItem) {
        if (outReason) *outReason = "desktop_capture_item_failed";
        if (outTitle) *outTitle = "desktop";
        return false;
      }
    } else {
      nextItem = nullptr;
    }
    nextReason = "desktop_mode_selected";
    nextTitle = "desktop";
  } else {
    const auto selected = find_window_by_id(requestedWindowId);
    if (!selected.has_value()) {
      if (outReason) *outReason = "window_not_found_or_not_shareable";
      return false;
    }
    nextItem = CreateItemForPrimaryMonitor(selected->hwnd, "CreateForWindow(window-select)");
    if (!nextItem) {
      if (outReason) *outReason = "window_capture_item_failed";
      if (outTitle) *outTitle = selected->title;
      return false;
    }
    nextCaptureWindowModeActive = true;
    nextCaptureWindowClientOnlyActive = false;
    nextCaptureWindowInfo.hwnd = selected->hwnd;
    nextCaptureWindowInfo.pid = selected->pid;
    nextCaptureWindowInfo.processName = get_window_process_name(selected->hwnd, nullptr);
    nextCaptureWindowInfo.title = utf8_to_wide(selected->title);
    nextSelectedWindowId = selected->id;
    nextReason = "ok";
    nextTitle = selected->title;
    nextProcess = nextCaptureWindowInfo.processName.empty() ? "unknown" : nextCaptureWindowInfo.processName;
    nextPid = selected->pid;
    nextHwnd = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(selected->hwnd));
    nextFlags = 0x1u;
  }

  item = nextItem;
  capture.windowModeActive.store(nextCaptureWindowModeActive, std::memory_order_release);
  capture.windowClientOnlyActive.store(nextCaptureWindowClientOnlyActive, std::memory_order_release);
  capture.windowCriteria = nextCaptureWindowCriteria;
  capture.windowInfo = nextCaptureWindowInfo;
  capture.selectedWindowId.store(nextSelectedWindowId, std::memory_order_release);
  capture.streamGenerationState.store(nextCaptureStreamGeneration, std::memory_order_release);
  capture.targetFlags.store(nextFlags, std::memory_order_release);
  capture.targetPid.store(nextPid, std::memory_order_release);
  capture.targetHwnd.store(nextHwnd, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lk(capture.metaMu);
    capture.targetProcess = nextProcess;
    capture.targetTitle = nextTitle == "desktop" ? std::string{} : nextTitle;
  }
  nextCaptureWindowCheckUs = nowUs + captureWindowRebindIntervalUs;
  watchdog.lastCaptureRestartUs = nowUs;
  if (!restart_capture_session(hx)) {
    restore_previous_target();
    if (outReason) *outReason = "capture_restart_failed";
    if (outTitle) *outTitle = nextTitle;
    return false;
  }

  capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
  capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
  capture.lastCallbackUs.store(0, std::memory_order_release);
  encoder.ResetTimelineAnchors(capture);
  // Confirmed window selection: re-fit the encoder to the FINAL window geometry now (before the
  // selection first-frame gate opens), so the first IDR is already at the final size. Without this,
  // apply_confirmed_capture_geometry (called inside restart_capture_session) bails for window mode
  // and the encoder stays at the pre-selection size -- the client would get an old-size IDR, then a
  // new-size IDR a frame later, and reconfigure twice. A window DRAG still returns early there and
  // keeps the 0.4s settle. The desktop selection already re-fit through the non-window path.
  if (nextCaptureWindowModeActive && useH264) {
    uint32_t finalW = 0;
    uint32_t finalH = 0;
    {
      std::lock_guard<std::mutex> lk(capture.resourceMu);
      finalW = capture.width;
      finalH = capture.height;
    }
    encoder.ApplyConfirmedCaptureGeometry(capture, res, frameGating, inputRouter, sender, finalW, finalH, "window-select", /*allowWindowOverride=*/true);
  }
  encoder.forceKeyNext = true;
  kick.selectionFirstKeyframePendingGeneration = nextCaptureStreamGeneration;
  kick.selectionFirstKeyframeDropCount = 0;
  ++capture.restartCount;
  capture.FlushCapturePipelineState(res, frameGating, stats, "window-select");

  if (outFlags) *outFlags = 0x1u;
  if (outWindowId) *outWindowId = nextSelectedWindowId;
  if (outStreamGeneration) *outStreamGeneration = nextCaptureStreamGeneration;
  if (outReason) *outReason = nextReason;
  if (outTitle) *outTitle = nextTitle;
  return true;
}

// ---------------------------------------------------------------------------------------------
// The twelve stages of one main-loop tick, in call order.
// ---------------------------------------------------------------------------------------------

Flow stage_time_limit(HostContext& hx, TickContext& tc) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& transport = hx.transport;
  auto& startUs = hx.startUs;
  auto& kick = hx.kick;
  auto& backend = hx.backend;
  auto& watchdog = hx.watchdog;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  auto& nowUs = tc.nowUs;
  watchdog.MarkMainProgress(MainLoopPhase::Loop);
  nowUs = qpc_now_us();
 
  if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
    return Flow::Break;
  }
  if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
    return Flow::Break;
  }
  sender.PumpUdpHello(transport, encoder);
  pump_cursor_forward(hx, nowUs);
  // Arm the trailing-edge kick on a fresh viewer/decoder (bumped session epoch) and on a capture
  // identity change (a new stream generation -- window select, reattach, backend change). The kick
  // fires 150ms later only if no real frame has arrived and been flushed out; a real callback
  // fills the cold cache first, so even the first kick has pixels to resubmit.
  {
    const uint64_t curEpoch = clientSession.epoch.load(std::memory_order_acquire);
    if (curEpoch != kick.lastSeenBootstrapEpoch) {
      kick.lastSeenBootstrapEpoch = curEpoch;
      kick.Arm(nowUs, useH264);
    }
    const uint64_t curGen = capture.streamGenerationState.load(std::memory_order_acquire);
    if (curGen != kick.lastSeenStreamGeneration) {
      kick.lastSeenStreamGeneration = curGen;
      kick.Arm(nowUs, useH264);
    }
  }
  // Barrier recovery: the sender thread re-armed sender.waitingForKey after a same-epoch send
  // failure and cannot itself produce an IDR (encoder.forceKeyNext is main-thread-owned, and on a static
  // desktop no new frame arrives to carry sender.requestKey). Consume the flag here, before the
  // frame wait, and both force the next encode to be a key AND arm the trailing kick so the kick
  // resubmits the cached raw frame when the screen is not changing -- otherwise a re-armed barrier
  // on a still desktop would never open.
  if (sender.recoveryPending.exchange(false, std::memory_order_acq_rel)) {
    encoder.forceKeyNext = true;
    kick.Arm(nowUs, useH264);
  }
  return Flow::Next;
}

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

Flow stage_stream_active(HostContext& hx, TickContext& tc) {
  auto& useH264 = hx.useH264;
  auto& streamActiveApplied = hx.streamActiveApplied;
  auto& powerKeepalive = hx.powerKeepalive;
  auto& token = hx.token;
  auto& frameGating = hx.frameGating;
  auto& kick = hx.kick;
  auto& backend = hx.backend;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  const bool streamActive = clientSession.streamControlActive.load(std::memory_order_acquire);
  if (!streamActive) {
    if (streamActiveApplied) {
      capture.FlushCapturePipelineState(res, frameGating, stats, "stream-inactive");
      streamActiveApplied = false;
      powerKeepalive.SetStreaming(false);
      capture.idleDetachAtUs = qpc_now_us() + kCaptureIdleDetachDelayUs;
      std::cout << "[native-video-host] stream inactive\n";
    }
    if (!capture.idleDetached && qpc_now_us() >= capture.idleDetachAtUs) {
      capture.DetachCaptureSession(res, token);
      // Stale by construction: whatever forced a fallback while nobody was watching is
      // re-evaluated from scratch when the reattach picks its backend.
      capture.dxgiFallbackRequested.store(false, std::memory_order_release);
      capture.gdiFallbackRequested.store(false, std::memory_order_release);
      capture.idleDetached = true;
      std::cout << "[native-video-host] capture detached (idle)\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return Flow::Continue;
  }
  if (!streamActiveApplied) {
    if (capture.idleDetached) {
      // Reattach before declaring the stream applied, and only declare it on success:
      // marking it applied with no capture running would serve black frames with no path
      // back. Failure retries with backoff -- the desktop may still be mid-transition
      // (RDP disconnecting, a secure desktop closing) when the client returns.
      const uint64_t nowUs = qpc_now_us();
      if (nowUs < capture.reattachRetryAtUs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return Flow::Continue;
      }
      if (!capture.windowModeActive.load(std::memory_order_acquire)) {
        // Fresh resolution, not the backend the last session was demoted to. This is also
        // what frees a host parked on WGC by an RDP visit: the desktop is back on the real
        // adapter by now, and starting from the requested backend finds it.
        backend.active = backend.requested;
        // Reattach bypasses the climb-back secure gate, so honour the same rule here: if the
        // requested backend is DXGI but the desktop is currently secure (UAC/lock), attaching
        // DXGI would just take an immediate E_ACCESSDENIED and demote. Start on WGC instead so
        // the returning viewer gets a picture now, and let the climb-back promote to DXGI once
        // the default desktop settles. A requested WGC/GDI backend is respected as-is.
        if (backend.requested == DesktopCaptureBackend::Dxgi &&
            !interactive_desktop_is_default_uncached()) {
          backend.active = DesktopCaptureBackend::Wgc;
        }
      }
      if (!restart_capture_session(hx)) {
        capture.reattachRetryDelayUs =
            std::min<uint64_t>(capture.reattachRetryDelayUs * 2, kCaptureReattachRetryMaxUs);
        capture.reattachRetryAtUs = nowUs + capture.reattachRetryDelayUs;
        std::cerr << "[native-video-host] capture reattach failed; retrying in "
                  << (capture.reattachRetryDelayUs / 1000) << "ms\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return Flow::Continue;
      }
      capture.idleDetached = false;
      capture.reattachRetryAtUs = 0;
      capture.reattachRetryDelayUs = kCaptureReattachRetryMinUs;
      ++capture.restartCount;
      capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
      capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
      capture.lastCallbackUs.store(0, std::memory_order_release);
      encoder.ResetTimelineAnchors(capture);
      capture.FlushCapturePipelineState(res, frameGating, stats, "capture-reattached");
      std::cout << "[native-video-host] capture reattached backend="
                << desktop_capture_backend_name(backend.active) << "\n";
    }
    streamActiveApplied = true;
    encoder.forceKeyNext = true;
    // A returning viewer on a still desktop needs a picture too; arm the trailing-edge kick for
    // the current epoch (coalesces with any arm from the epoch/generation edges above). This also
    // covers the stream-inactive->active edge and a capture reattach, which both land here.
    kick.Arm(qpc_now_us(), useH264);
    powerKeepalive.SetStreaming(true, true);
    // A stream-inactive->active edge starts a fresh streaming episode; drop any no-output streak
    // left from before so the inactive gap is not mistaken for encoder starvation.
    encoder.ResetStarvationEpisode();
    std::cout << "[native-video-host] stream active; forcing keyframe\n";
  }
  return Flow::Next;
}

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
  if (useH264 && encoder.tunePending.exchange(false, std::memory_order_acq_rel)) {
    const uint32_t reqSeq = encoder.tuneSeq.load(std::memory_order_acquire);
    const uint32_t requestedBitrate = encoder.tuneBitrate.load(std::memory_order_acquire);
    const bool bitrateExplicit = requestedBitrate >= 100000;
    uint32_t targetBitrate = requestedBitrate;
    uint32_t targetKeyint = encoder.tuneKeyint.load(std::memory_order_acquire);
    uint32_t targetFps = encoder.tuneFps.load(std::memory_order_acquire);
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

Flow stage_selection(HostContext& hx, TickContext& tc) {
  auto& useH264 = hx.useH264;
  auto& captureWindowRebindIntervalUs = hx.captureWindowRebindIntervalUs;
  auto& nextCaptureWindowCheckUs = hx.nextCaptureWindowCheckUs;
  auto& item = hx.item;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  auto& seq = tc.seq;
  // Switching screens is the same operation as switching to desktop mode, aimed at a particular
  // monitor. Done here rather than on the control thread because the capture item belongs to
  // this loop, exactly like the window and capture-mode selections above.
  if (capture.monitorSelectPending.exchange(false, std::memory_order_acq_rel)) {
    const uint32_t requestedId = capture.monitorSelectRequested.load(std::memory_order_acquire);
    const auto monitors = enumerate_monitors();
    if (requestedId >= monitors.size()) {
      std::cerr << "[native-video-host][control] monitor-select out of range id=" << requestedId
                << " count=" << monitors.size() << "\n";
    } else {
      const auto& target = monitors[requestedId];
      auto nextItem = CreateItemForPrimaryMonitor(nullptr, nullptr, target.handle);
      if (!nextItem) {
        std::cerr << "[native-video-host][control] monitor-select capture failed id="
                  << requestedId << "\n";
      } else {
        item = nextItem;
        capture.selectedMonitorId.store(requestedId, std::memory_order_release);
        // A monitor is a desktop target, so any window selection it replaces has to go.
        capture.windowModeActive = false;
        capture.windowCriteria.processNamesLower.clear();
        capture.windowCriteria.titleNeedleLower.clear();
        capture.selectedWindowId.store(0u, std::memory_order_release);
        capture.targetFlags.store(0u, std::memory_order_release);
        capture.targetPid.store(0u, std::memory_order_release);
        capture.targetHwnd.store(0u, std::memory_order_release);
        {
          std::lock_guard<std::mutex> lk(capture.metaMu);
          capture.targetProcess = "monitor";
          capture.targetTitle = target.name;
        }
        watchdog.lastCaptureRestartUs = nowUs;
        if (restart_capture_session(hx)) {
          ++capture.restartCount;
          capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
          capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
          capture.lastCallbackUs.store(0, std::memory_order_release);
          encoder.ResetTimelineAnchors(capture);
          encoder.forceKeyNext = true;
          std::cout << "[native-video-host][control] monitor-select applied id=" << requestedId
                    << " " << target.width << "x" << target.height
                    << " at " << target.x << "," << target.y << "\n";
        } else {
          std::cerr << "[native-video-host][control] monitor-select restart failed id="
                    << requestedId << "\n";
        }
      }
    }
  }

  if (capture.modeReqPending.exchange(false, std::memory_order_acq_rel)) {
    const uint16_t reqMode = capture.modeReqMode.load(std::memory_order_acquire);
    const uint32_t reqSeq = capture.modeReqSeq.load(std::memory_order_acquire);
    const uint32_t reqXPermille = std::min<uint32_t>(10000u, capture.modeReqXPermille.load(std::memory_order_acquire));
    const uint32_t reqYPermille = std::min<uint32_t>(10000u, capture.modeReqYPermille.load(std::memory_order_acquire));
    if (reqMode == 1) {
      auto nextItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(control-overview)");
      if (!nextItem) {
        std::cerr << "[native-video-host][control] capture-mode overview failed seq=" << reqSeq << "\n";
      } else {
        item = nextItem;
        capture.windowModeActive = false;
        capture.windowCriteria.processNamesLower.clear();
        capture.windowCriteria.titleNeedleLower.clear();
        capture.selectedWindowId.store(0u, std::memory_order_release);
        capture.targetFlags.store(0u, std::memory_order_release);
        capture.targetPid.store(0u, std::memory_order_release);
        capture.targetHwnd.store(0u, std::memory_order_release);
        {
          std::lock_guard<std::mutex> lk(capture.metaMu);
          capture.targetProcess = "monitor";
          capture.targetTitle.clear();
        }
        watchdog.lastCaptureRestartUs = nowUs;
        if (restart_capture_session(hx)) {
          ++capture.restartCount;
          capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
          capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
          capture.lastCallbackUs.store(0, std::memory_order_release);
          encoder.ResetTimelineAnchors(capture);
          if (!encoder.ApplyCaptureUiQualityMode(capture, res, frameGating, inputRouter, sender, rate, useH264, true, nowUs)) {
            std::cerr << "[native-video-host][control] capture-mode overview quality apply failed seq=" << reqSeq
                      << "\n";
            return Flow::Break;
          }
          std::cout << "[native-video-host][control] capture-mode applied seq=" << reqSeq
                    << " mode=overview"
                    << " bitrate=" << encoder.activeBitrate
                    << " fps=" << encoder.activeFps
                    << " encode=" << encoder.activeEncodeW << "x" << encoder.activeEncodeH
                    << "\n";
        } else {
          std::cerr << "[native-video-host][control] capture-mode overview restart failed seq=" << reqSeq << "\n";
        }
      }
    } else if (reqMode == 2) {
      HMONITOR primaryMon = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
      MONITORINFO monInfo{};
      monInfo.cbSize = sizeof(monInfo);
      if (!GetMonitorInfo(primaryMon, &monInfo)) {
        monInfo.rcMonitor.left = 0;
        monInfo.rcMonitor.top = 0;
        monInfo.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
        monInfo.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
      }
      const int monW = std::max<int>(1, monInfo.rcMonitor.right - monInfo.rcMonitor.left);
      const int monH = std::max<int>(1, monInfo.rcMonitor.bottom - monInfo.rcMonitor.top);
      POINT focusPt{};
      focusPt.x = monInfo.rcMonitor.left +
                  static_cast<int>((static_cast<uint64_t>(reqXPermille) * static_cast<uint64_t>(monW - 1) + 5000ULL) /
                                   10000ULL);
      focusPt.y = monInfo.rcMonitor.top +
                  static_cast<int>((static_cast<uint64_t>(reqYPermille) * static_cast<uint64_t>(monH - 1) + 5000ULL) /
                                   10000ULL);
      CaptureWindowInfo selected{};
      if (!find_top_level_window_at_point(focusPt, &selected)) {
        std::cerr << "[native-video-host][control] capture-mode focus no-window seq=" << reqSeq
                  << " xPermille=" << reqXPermille
                  << " yPermille=" << reqYPermille
                  << "\n";
      } else {
        auto nextItem = CreateItemForPrimaryMonitor(selected.hwnd, "CreateForWindow(control-focus-point)");
        if (!nextItem) {
          std::cerr << "[native-video-host][control] capture-mode focus create-item failed seq=" << reqSeq << "\n";
        } else {
          item = nextItem;
          capture.windowModeActive = true;
          capture.windowClientOnlyActive = true;
          capture.windowCriteria.processNamesLower.clear();
          if (!selected.processName.empty()) {
            capture.windowCriteria.processNamesLower.insert(selected.processName);
          }
          capture.windowCriteria.titleNeedleLower.clear();
          capture.selectedWindowId.store(hwnd_to_id(selected.hwnd), std::memory_order_release);
          capture.targetFlags.store(0x1u | 0x2u, std::memory_order_release);
          capture.targetPid.store(selected.pid, std::memory_order_release);
          capture.targetHwnd.store(
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(selected.hwnd)), std::memory_order_release);
          {
            std::lock_guard<std::mutex> lk(capture.metaMu);
            capture.targetProcess = selected.processName.empty() ? "unknown" : selected.processName;
            capture.targetTitle = selected.title.empty() ? std::string{} : wide_to_utf8(selected.title);
          }
          nextCaptureWindowCheckUs = nowUs + captureWindowRebindIntervalUs;
          watchdog.lastCaptureRestartUs = nowUs;
          if (restart_capture_session(hx)) {
            ++capture.restartCount;
            capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
            capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
            capture.lastCallbackUs.store(0, std::memory_order_release);
            encoder.ResetTimelineAnchors(capture);
            if (!encoder.ApplyCaptureUiQualityMode(capture, res, frameGating, inputRouter, sender, rate, useH264, false, nowUs)) {
              std::cerr << "[native-video-host][control] capture-mode focus quality apply failed seq=" << reqSeq
                        << "\n";
              return Flow::Break;
            }
            std::cout << "[native-video-host][control] capture-mode applied seq=" << reqSeq
                      << " mode=focus-window"
                      << " pid=" << selected.pid
                      << " process=" << (selected.processName.empty() ? "unknown" : selected.processName)
                      << " title=" << (selected.title.empty() ? "<empty>" : wide_to_utf8(selected.title))
                      << " bitrate=" << encoder.activeBitrate
                      << " fps=" << encoder.activeFps
                      << " encode=" << encoder.activeEncodeW << "x" << encoder.activeEncodeH
                      << "\n";
          } else {
            std::cerr << "[native-video-host][control] capture-mode focus restart failed seq=" << reqSeq << "\n";
          }
        }
      }
    }
  }
  if (capture.windowModeActive && capture.windowCriteria.enabled() && nowUs >= nextCaptureWindowCheckUs) {
    nextCaptureWindowCheckUs = nowUs + captureWindowRebindIntervalUs;
    CaptureWindowInfo latestWindowInfo{};
    if (find_capture_window(capture.windowCriteria, &latestWindowInfo)) {
      const uintptr_t currentRaw = static_cast<uintptr_t>(capture.targetHwnd.load(std::memory_order_acquire));
      const uintptr_t nextRaw = reinterpret_cast<uintptr_t>(latestWindowInfo.hwnd);
      if (nextRaw != currentRaw) {
        const auto nextItem =
            CreateItemForPrimaryMonitor(latestWindowInfo.hwnd, "CreateForWindow(target-window-rebind)");
        if (nextItem) {
          item = nextItem;
          capture.selectedWindowId.store(hwnd_to_id(latestWindowInfo.hwnd), std::memory_order_release);
          capture.targetHwnd.store(static_cast<uint64_t>(nextRaw), std::memory_order_release);
          capture.targetPid.store(latestWindowInfo.pid, std::memory_order_release);
          {
            std::lock_guard<std::mutex> lk(capture.metaMu);
            capture.targetProcess =
                latestWindowInfo.processName.empty() ? std::string("unknown") : latestWindowInfo.processName;
            capture.targetTitle =
                latestWindowInfo.title.empty() ? std::string{} : wide_to_utf8(latestWindowInfo.title);
          }
          const uint32_t rebindCount = capture.rebindCount.fetch_add(1, std::memory_order_acq_rel) + 1;
          capture.targetFlags.store((capture.windowModeActive ? 0x1u : 0x0u) |
                                           ((capture.windowModeActive && capture.windowClientOnlyActive) ? 0x2u : 0x0u),
                                       std::memory_order_release);
          std::string targetProc = "unknown";
          std::string targetTitle;
          {
            std::lock_guard<std::mutex> lk(capture.metaMu);
            targetProc = capture.targetProcess;
            targetTitle = capture.targetTitle;
          }
          watchdog.lastCaptureRestartUs = nowUs;
          if (restart_capture_session(hx)) {
            ++capture.restartCount;
            capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
            capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
            capture.lastCallbackUs.store(0, std::memory_order_release);
            encoder.ResetTimelineAnchors(capture);
            encoder.forceKeyNext = true;
            std::cout << "[native-video-host] capture-window rebound hwnd=0x" << std::hex << nextRaw << std::dec
                      << " pid=" << capture.targetPid.load(std::memory_order_relaxed)
                      << " process=" << targetProc
                      << " title=" << (targetTitle.empty() ? "<empty>" : targetTitle)
                      << " rebindCount=" << rebindCount
                      << " restartCount=" << capture.restartCount
                      << "\n";
          } else {
            std::cerr << "[native-video-host] capture-window rebind restart failed\n";
          }
        }
      }
    }
  }
  return Flow::Next;
}

Flow stage_geometry(HostContext& hx, TickContext& tc) {
  auto& item = hx.item;
  auto& frameGating = hx.frameGating;
  auto& watchdog = hx.watchdog;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  // WGC ContentSize settle + main-thread pool recreate. The capture callback dropped frames whose
  // ContentSize != the pool geometry and recorded the pending content size here; during an
  // interactive window drag that size churns every frame. Wait for it to hold steady for a short
  // settle window, then recreate the pool + readback at the new size on THIS (main) thread --
  // the callback thread must never recreate capture resources. restart_capture_session(hx) rebuilds
  // the WGC pool at item.Size() (the settled window size) and create_staging at the new geometry.
  if (capture.wgcContentSizeMismatchPending.load(std::memory_order_acquire) != 0) {
    const uint32_t pendW = capture.wgcPendingContentW.load(std::memory_order_acquire);
    const uint32_t pendH = capture.wgcPendingContentH.load(std::memory_order_acquire);
    uint32_t curCapW = 0;
    uint32_t curCapH = 0;
    {
      std::lock_guard<std::mutex> lk(capture.resourceMu);
      curCapW = capture.width;
      curCapH = capture.height;
    }
    if (pendW < 2 || pendH < 2 || (pendW == curCapW && pendH == curCapH)) {
      // Content settled back to the current pool geometry -- nothing to recreate.
      capture.wgcContentSizeMismatchPending.store(0, std::memory_order_release);
      capture.wgcSettleTrackW = 0;
      capture.wgcSettleTrackH = 0;
      capture.wgcSettleSinceUs = 0;
    } else if (pendW != capture.wgcSettleTrackW || pendH != capture.wgcSettleTrackH) {
      // Size still moving: (re)arm the settle timer on the newest candidate.
      capture.wgcSettleTrackW = pendW;
      capture.wgcSettleTrackH = pendH;
      capture.wgcSettleSinceUs = nowUs;
    } else if (nowUs - capture.wgcSettleSinceUs >= kWgcContentSettleUs) {
      // Stable for the settle window: recreate the pool/readback at the new size on the main thread.
      capture.wgcContentSizeMismatchPending.store(0, std::memory_order_release);
      capture.wgcSettleTrackW = 0;
      capture.wgcSettleTrackH = 0;
      capture.wgcSettleSinceUs = 0;
      watchdog.lastCaptureRestartUs = nowUs;
      capture.FlushCapturePipelineState(res, frameGating, stats, "wgc-content-size");
      if (restart_capture_session(hx)) {
        ++capture.restartCount;
        ++capture.wgcPoolRecreates;
        capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
        capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
        capture.lastCallbackUs.store(0, std::memory_order_release);
        encoder.ResetTimelineAnchors(capture);
        // Force an IDR at the (now correct) geometry. An interactive drag still lets the encode
        // size catch up on the 0.4s refit path; only the capture pool was resized here.
        encoder.forceKeyNext = true;
        uint32_t newCapW = 0;
        uint32_t newCapH = 0;
        {
          std::lock_guard<std::mutex> lk(capture.resourceMu);
          newCapW = capture.width;
          newCapH = capture.height;
        }
        std::cout << "[native-video-host] wgc-content-size pool recreated content="
                  << pendW << "x" << pendH << " capture=" << newCapW << "x" << newCapH
                  << " poolRecreates=" << capture.wgcPoolRecreates
                  << " restartCount=" << capture.restartCount << "\n";
      } else {
        std::cerr << "[native-video-host] wgc-content-size pool recreate failed content="
                  << pendW << "x" << pendH << "\n";
      }
    }
  }
  if (capture.sizeChangePending.exchange(0, std::memory_order_acq_rel) != 0) {
    watchdog.lastCaptureRestartUs = nowUs;
    capture.FlushCapturePipelineState(res, frameGating, stats, "size-change");
    if (restart_capture_session(hx)) {
      ++capture.restartCount;
      capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
      capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
      capture.lastCallbackUs.store(0, std::memory_order_release);
      encoder.ResetTimelineAnchors(capture);
      encoder.forceKeyNext = true;
      std::cout << "[native-video-host] capture session restarted reason=size-change count="
                << capture.restartCount << "\n";
    } else {
      std::cerr << "[native-video-host] capture session restart failed reason=size-change\n";
    }
  }
  return Flow::Next;
}

Flow stage_watchdogs(HostContext& hx, TickContext& tc) {
  auto& rate = hx.rate;
  auto& backend = hx.backend;
  auto& watchdog = hx.watchdog;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  if (capture.sessionReady.load(std::memory_order_acquire) &&
      clientSession.streamControlActive.load(std::memory_order_acquire) &&
      !capture.windowModeActive.load(std::memory_order_acquire) &&
      backend.active == DesktopCaptureBackend::Gdi) {
    // GDI is clocked and must publish continuously. WGC/DXGI are change-driven and can
    // legitimately stay silent on a static desktop, so callback silence is not a stall for
    // those backends and must never trigger a restart loop.
    const uint64_t lastCbUs = capture.lastCallbackUs.load(std::memory_order_acquire);
    const uint64_t sessionStartUs = capture.sessionStartedUs;
    const uint64_t stallBaseUs = (lastCbUs > 0) ? lastCbUs : sessionStartUs;
    const bool restartCooldownDone =
        (watchdog.lastCaptureRestartUs == 0 ||
         nowUs >= (watchdog.lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
    if (stallBaseUs > 0 && nowUs >= (stallBaseUs + kCaptureCallbackStallRestartUs) &&
        restartCooldownDone) {
      const uint64_t stallUs = nowUs - stallBaseUs;
      watchdog.lastCaptureRestartUs = nowUs;
      const bool restarted = restart_capture_session(hx);
      if (restarted) {
        ++capture.restartCount;
        capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
        capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
        capture.lastCallbackUs.store(0, std::memory_order_release);
        encoder.ResetTimelineAnchors(capture);
        encoder.forceKeyNext = true;
        ++watchdog.deadRestartCount;
        std::cout << "[native-video-host] capture session restarted count=" << capture.restartCount
                  << " captureDeadRestartCount=" << watchdog.deadRestartCount
                  << " stallUs=" << stallUs
                  << " lastCallbackUs=" << lastCbUs
                  << "\n";
      } else {
        std::cerr << "[native-video-host] capture session restart failed stallUs=" << stallUs
                  << "\n";
      }
    }
  }
  // DXGI/WGC frozen-ring self-heal. The callback-stall watchdog above is GDI-only on purpose:
  // change-driven backends are silent on a static desktop, so silence there is not a stall. A
  // ring that has frozen under GPU contention is a different thing and it has a distinct signal
  // -- its oldest submit sits in GpuPending because the completion query never fires, while an
  // idle ring holds nothing pending at all. Restart on that age, over two consecutive polls so a
  // single slow readback does not trip it. This is the "it went dark and reconnecting shows
  // nothing" report from a host pinned by a GPU-heavy game; before this, DXGI/WGC had no path
  // back short of the user restarting the host.
  if (capture.sessionReady.load(std::memory_order_acquire) &&
      clientSession.streamControlActive.load(std::memory_order_acquire) &&
      !capture.windowModeActive.load(std::memory_order_acquire) &&
      backend.active != DesktopCaptureBackend::Gdi) {
    const uint64_t oldestPendingUs = res.captureReadback.OldestGpuPendingAgeUs();
    watchdog.oldestGpuPendingPeakUs = std::max(watchdog.oldestGpuPendingPeakUs, oldestPendingUs);
    // Same loop-rate sample feeds the readback-drain watchdog's per-1s-window peak; unlike the
    // frozen-ring peak above (reset per print interval) this one is reset every stats tick.
    watchdog.drainOldestPendingPeakUs = std::max(watchdog.drainOldestPendingPeakUs, oldestPendingUs);
    watchdog.gpuPendingCountPeak = std::max(watchdog.gpuPendingCountPeak, res.captureReadback.GpuPendingCount());
    if (oldestPendingUs >= kCaptureFrozenWarnUs) {
      watchdog.readbackSlowWindowPeakUs = std::max(watchdog.readbackSlowWindowPeakUs, oldestPendingUs);
    }
    // Advance the 1s window on the boundary regardless of whether it logs, so a peak from an
    // earlier slow episode never bleeds into a later warn (Codex 2026-08-25).
    if (watchdog.readbackSlowLastLogUs == 0) watchdog.readbackSlowLastLogUs = nowUs;
    if (nowUs - watchdog.readbackSlowLastLogUs >= 1'000'000) {
      if (watchdog.readbackSlowWindowPeakUs >= kCaptureFrozenWarnUs) {
        std::cout << "[native-video-host] capture readback slow oldestPendingUs=" << oldestPendingUs
                  << " peakUs=" << watchdog.readbackSlowWindowPeakUs << "\n";
      }
      watchdog.readbackSlowLastLogUs = nowUs;
      watchdog.readbackSlowWindowPeakUs = 0;
    }
    if (oldestPendingUs >= kCaptureFrozenRestartUs) {
      ++watchdog.frozenPollStreak;
    } else {
      watchdog.frozenPollStreak = 0;
    }
    const bool restartCooldownDone =
        (watchdog.lastCaptureRestartUs == 0 ||
         nowUs >= (watchdog.lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
    if (watchdog.frozenPollStreak >= kCaptureFrozenPollStreakMin && restartCooldownDone) {
      watchdog.frozenPollStreak = 0;
      // First freeze: a same-device capture restart clears a wedged duplication/WGC session.
      // A refreeze inside the window means the device itself is stuck -- restarting capture on
      // the same device will not clear it -- so exit and let the supervisor rebuild the process
      // with a fresh D3D device. main() runs under that supervisor; a non-zero return long after
      // startup reads to it as a restartable exit (ranMs >= 15s, so not counted as a crash-loop),
      // and it relaunches without the startup backoff.
      const bool refroze =
          watchdog.lastFrozenRestartUs != 0 &&
          nowUs < (watchdog.lastFrozenRestartUs + kCaptureFrozenEscalationWindowUs);
      watchdog.lastFrozenRestartUs = nowUs;
      if (refroze) {
        std::cerr << "[native-video-host] capture ring refroze within "
                  << (kCaptureFrozenEscalationWindowUs / 1000000)
                  << "s oldestPendingUs=" << oldestPendingUs
                  << "; exiting for a full process restart\n";
        // A machine-readable twin of the line above: an in-process escalation counter would be
        // pointless (the process exits before another stats print), so this single record carries
        // the last state the frozen ring reached and pairs with host_app.log's exit-code-3 line
        // to reconstruct a cross-process recovery across the restart. (Codex.)
        const uint64_t refreezeLastPubUs = capture.lastPublishUs.load(std::memory_order_acquire);
        const uint64_t refreezeLastPubAgeUs =
            (refreezeLastPubUs > 0 && nowUs > refreezeLastPubUs) ? nowUs - refreezeLastPubUs : 0;
        std::cout << "[native-video-host] capture-recovery reason=frozen-ring-refreeze"
                  << " action=process-restart exitCode=3"
                  << " oldestPendingUs=" << oldestPendingUs
                  << " gpuPendingCount=" << res.captureReadback.GpuPendingCount()
                  << " frozenRingRestarts=" << watchdog.frozenRingRestartCount
                  << " captureRestarts=" << capture.restartCount
                  << " lastPublishAgeUs=" << refreezeLastPubAgeUs
                  << " backend=" << desktop_capture_backend_name(backend.active)
                  << " windowSec=" << (kCaptureFrozenEscalationWindowUs / 1000000)
                  << "\n";
        std::cout.flush();
        std::cerr.flush();
        { hx.exitCode = 3; return Flow::Return; }
      }
      watchdog.lastCaptureRestartUs = nowUs;
      const bool restarted = restart_capture_session(hx);
      if (restarted) {
        ++capture.restartCount;
        capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(),
                                   std::memory_order_release);
        capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
        capture.lastCallbackUs.store(0, std::memory_order_release);
        encoder.ResetTimelineAnchors(capture);
        encoder.forceKeyNext = true;
        ++watchdog.deadRestartCount;
        ++watchdog.frozenRingRestartCount;
        std::cout << "[native-video-host] capture session restarted reason=frozen-ring count="
                  << capture.restartCount << " captureDeadRestartCount=" << watchdog.deadRestartCount
                  << " frozenRingRestarts=" << watchdog.frozenRingRestartCount
                  << " oldestPendingUs=" << oldestPendingUs << "\n";
      } else {
        std::cerr << "[native-video-host] frozen-ring restart failed oldestPendingUs="
                  << oldestPendingUs << "\n";
      }
    }
  }
  return Flow::Next;
}

Flow stage_pace(HostContext& hx, TickContext& tc) {
  auto& paceByTick = hx.paceByTick;
  auto& nextTickUs = hx.nextTickUs;
  auto& sender = hx.sender;
  auto& encoder = hx.encoder;
  auto& nowUs = tc.nowUs;
  auto& tickWaitUs = tc.tickWaitUs;
  if (paceByTick) {
    if (nowUs < nextTickUs) {
      const uint64_t paceWaitStartUs = qpc_now_us();
      // Reuse the high-resolution sender timer. sleep_for commonly overshoots a 60 Hz
      // deadline by 1-3ms on Windows; resetting the clock to that late wakeup on every
      // frame turned a requested 60fps into a stable 48-54fps.
      udp_pace_wait_until(nextTickUs);
      const uint64_t paceWaitDoneUs = qpc_now_us();
      tickWaitUs = (paceWaitDoneUs >= paceWaitStartUs) ? (paceWaitDoneUs - paceWaitStartUs) : 0;
      return Flow::Continue;
    }
    // Preserve the target phase after a normal sub-frame timer overshoot. Re-anchor only
    // when processing actually missed a whole frame, avoiding both drift and catch-up bursts.
    if (nowUs > nextTickUs + encoder.activePacingFrameIntervalUs) {
      nextTickUs = nowUs;
    }
    nextTickUs += encoder.activePacingFrameIntervalUs;
  }

  return Flow::Next;
}

Flow stage_pop_frame(HostContext& hx, TickContext& tc) {
  auto& useH264 = hx.useH264;
  auto& transport = hx.transport;
  auto& stop = hx.stop;
  auto& poppedNv12Slot = hx.poppedNv12Slot;
  auto& poppedNv12Generation = hx.poppedNv12Generation;
  auto& kick = hx.kick;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  auto& payload = tc.payload;
  auto& seq = tc.seq;
  auto& w = tc.w;
  auto& h = tc.h;
  auto& stride = tc.stride;
  auto& streamGeneration = tc.streamGeneration;
  auto& captureUs = tc.captureUs;
  auto& callbackUs = tc.callbackUs;
  auto& queuePushUs = tc.queuePushUs;
  auto& callbackIntervalUs = tc.callbackIntervalUs;
  auto& captureIntervalUs = tc.captureIntervalUs;
  auto& captureClockSkewUs = tc.captureClockSkewUs;
  auto& captureAgeAtCallbackUs = tc.captureAgeAtCallbackUs;
  auto& captureD3DWaitUs = tc.captureD3DWaitUs;
  auto& captureCopyMapUs = tc.captureCopyMapUs;
  auto& captureMemcpyUs = tc.captureMemcpyUs;
  auto& captureUnmapWaitUs = tc.captureUnmapWaitUs;
  auto& captureUnmapUs = tc.captureUnmapUs;
  auto& version = tc.version;
  auto& nv12Slot = tc.nv12Slot;
  auto& nv12Generation = tc.nv12Generation;
  auto& nv12W = tc.nv12W;
  auto& nv12H = tc.nv12H;
  auto& queueWaitReason = tc.queueWaitReason;
  auto& queueSelectStartUs = tc.queueSelectStartUs;
  auto& servedBootstrap = tc.servedBootstrap;
  auto& kickForcedKey = tc.kickForcedKey;
  auto& queueWaitUs = tc.queueWaitUs;
  auto& queueGapFrames = tc.queueGapFrames;
  auto& queueDepthAtPop = tc.queueDepthAtPop;
  auto& queuePopUs = tc.queuePopUs;
  auto& queueSelectWaitUs = tc.queueSelectWaitUs;
  auto& frameAgeAtSelectUs = tc.frameAgeAtSelectUs;
  auto& captureToCallbackUs = tc.captureToCallbackUs;
  auto& captureToQueueUs = tc.captureToQueueUs;
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
  queueSelectStartUs = qpc_now_us();
 
 
  if (kick.pending && nowUs >= kick.dueAtUs) {
    // A real frame already waiting in the ring is always better than a kick; fall through to the
    // normal pop (the encode below re-arms and records it). Otherwise decide whether the last real
    // input still needs flushing out of the MFT.
    bool realWaiting = false;
    {
      std::lock_guard<std::mutex> lk(res.frame.mu);
      realWaiting = (res.frame.version != stats.lastVersionSent) && res.frame.payload && !res.frame.payload->empty();
    }
    if (!realWaiting) {
      // Media barrier (UDP): a closed barrier means the epoch's first key AU has not reached the
      // wire, so a fresh/returning viewer still has no picture. TCP has no barrier (always open).
      bool barrierClosed = false;
      if (transport == VideoTransport::Udp) {
        std::lock_guard<std::mutex> lk(sender.mu);
        barrierClosed = sender.waitingForKey;
      }
      // The latest real input is "stuck" until its capture timestamp is observed on an emitted AU;
      // on the async MFT it sits there until the next input, which on a still screen never comes.
      const bool latestInputStuck = (kick.lastRealInputCaptureUs > kick.lastEmittedAuCaptureUs);
      // One kick per distinct held input: never resubmit the same held frame twice on a P-frame
      // trailing edge. A closed barrier overrides this -- it must keep kicking until an IDR lands.
      const bool alreadyKickedThisInput =
          (kick.lastRealInputCaptureUs != 0 && kick.lastKickedForInputCaptureUs == kick.lastRealInputCaptureUs);
      const bool needKick = barrierClosed || (latestInputStuck && !alreadyKickedThisInput);
      bool rearm = false;
      if (needKick && capture.KickTryFill(clientSession, kick, payload, w, h, stride, nowUs)) {
        servedBootstrap = true;
        // A closed barrier needs a real IDR; an ordinary trailing edge on an open stream can ride
        // the held frame as-is (a P-frame is fine). Leave any pre-existing encoder.forceKeyNext untouched.
        if (barrierClosed) {
          encoder.forceKeyNext = true;
          kickForcedKey = true;
        }
        seq = 0;
        version = stats.lastVersionSent;  // no real version consumed (keeps queue bookkeeping stable)
        streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
        captureUs = nowUs;     // fresh monotonic stamps: never reuse the stale capture time
        callbackUs = nowUs;
        queuePushUs = nowUs;
        kick.lastKickedForInputCaptureUs = kick.lastRealInputCaptureUs;  // one-shot per held input
        // Keep kicking on a still-closed barrier: each kick feeds a forced IDR, so the held frame
        // becomes an IDR within a couple of flushes and the cancel comes when it reaches the wire.
        rearm = barrierClosed;
      }
      // Otherwise one-shot: a failed fill (locked/secure/identity mismatch) leaves the screen black
      // rather than painting a wrong or stale picture, and a satisfied trailing edge stays quiet.
      if (rearm) {
        kick.Arm(nowUs, useH264);
      } else {
        kick.Cancel();
      }
    }
  }
  // Periodic static refresh (user requirement): on a still screen duplication offers no content
  // and the trailing kick is one-shot, so NOTHING is sent and the session looks frozen (the
  // field case: a static game map, revived only by dragging it). Re-serve the cached frame at a
  // low cadence (default 1Hz, REMOTE60_NATIVE_STATIC_REFRESH_MS, 0=off) as an ordinary P-frame.
  // The cadence anchors on BOTH the last emitted AU and the last refresh ATTEMPT: the async MFT
  // may legally return no output for a submitted input, and an emitted-only clock would then
  // retry on every loop iteration -- a tight input flood, the opposite of an idle 1Hz refresh.
  // The barrier must be open (a closed barrier is the kick's job and needs an IDR) and the
  // sender queue empty (stacking a synthetic frame onto a backlog helps nobody; the queue drains
  // within a few loop ticks). kick_try_fill re-validates identity/secure/size, so a lock screen
  // or a mid-switch target stays black rather than repainting a stale picture; a failed fill
  // also stamps the attempt clock so the (uncached) secure probe is not repeated every tick.
  if (!servedBootstrap && kick.staticRefreshIntervalUs > 0 && useH264 &&
      clientSession.streamControlActive.load(std::memory_order_acquire) && !kick.pending &&
      kick.lastEmittedAuCaptureUs != 0 &&
      nowUs >= kick.lastEmittedAuCaptureUs + kick.staticRefreshIntervalUs &&
      nowUs >= kick.lastStaticRefreshAttemptUs + kick.staticRefreshIntervalUs) {
    bool refreshBlocked = false;
    if (transport == VideoTransport::Udp) {
      std::lock_guard<std::mutex> lk(sender.mu);
      refreshBlocked = sender.waitingForKey || !sender.queue.empty();
    }
    if (!refreshBlocked) {
      // Stamped on the ATTEMPT, before the encode result is known -- see the cadence note.
      kick.lastStaticRefreshAttemptUs = nowUs;
      if (capture.KickTryFill(clientSession, kick, payload, w, h, stride, nowUs)) {
        servedBootstrap = true;
        seq = 0;
        version = stats.lastVersionSent;  // no real version consumed (keeps queue bookkeeping stable)
        streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
        captureUs = nowUs;
        callbackUs = nowUs;
        queuePushUs = nowUs;
        ++kick.staticRefreshCount;
      }
    }
  }
  bool queueReady = false;
  if (!servedBootstrap) {
    std::unique_lock<std::mutex> lk(res.frame.mu);
    queueReady = res.frame.cv.wait_for(lk, std::chrono::microseconds(capture.EffectiveQueueWaitTimeoutUs(encoder)), [&] {
      return stop.load() || res.frame.version != stats.lastVersionSent;
    });
    if (!queueReady && !stop.load()) {
      queueWaitReason = 1;
      ++stats.queueWaitTimeoutCount;
      return Flow::Continue;
    }
    if (stop.load()) return Flow::Break;
    if (res.frame.version == stats.lastVersionSent || !res.frame.payload || res.frame.payload->empty()) {
      queueWaitReason = 2;
      ++stats.queueWaitNoWorkCount;
      return Flow::Continue;
    }
    version = res.frame.version;
    payload = res.frame.payload;
    seq = res.frame.seq;
    w = res.frame.width;
    h = res.frame.height;
    stride = res.frame.stride;
    streamGeneration = res.frame.streamGeneration;
    captureUs = res.frame.captureUs;
    callbackUs = res.frame.callbackUs;
    callbackIntervalUs = res.frame.callbackIntervalUs;
    captureIntervalUs = res.frame.captureIntervalUs;
    queuePushUs = res.frame.queuePushUs;
    captureAgeAtCallbackUs = res.frame.captureAgeAtCallbackUs;
    captureClockSkewUs = res.frame.captureClockSkewUs;
    captureD3DWaitUs = res.frame.captureD3DWaitUs;
    captureCopyMapUs = res.frame.captureCopyMapUs;
    captureMemcpyUs = res.frame.captureMemcpyUs;
    captureUnmapWaitUs = res.frame.captureUnmapWaitUs;
    captureUnmapUs = res.frame.captureUnmapUs;
    nv12Slot = res.frame.nv12Slot;
    nv12Generation = res.frame.nv12Generation;
    nv12W = res.frame.nv12W;
    nv12H = res.frame.nv12H;
    res.frame.nv12Slot = -1;  // claimed; this loop now owns the release
  }
  // NB: a real frame pop deliberately does NOT cancel the kick. The pending timer is (re)armed and
  // kick.lastRealInputCaptureUs recorded once the frame is actually fed to the MFT (see below), so the
  // deadline trails the LAST real input; the kick then cancels only when that input is observed
  // coming out of the encoder, not merely because a frame was popped.
  if (poppedNv12Slot >= 0) {
    // The previous iteration bailed out before encoding (gating skip, stale drop);
    // release its claimed conversion now.
    res.captureReadback.ReleaseNv12Slot(poppedNv12Slot, poppedNv12Generation);
  }
  poppedNv12Slot = nv12Slot;
  poppedNv12Generation = nv12Generation;
queuePopUs = qpc_now_us();
queueSelectWaitUs =
    (queuePopUs >= queueSelectStartUs) ? (queuePopUs - queueSelectStartUs) : 0;
frameAgeAtSelectUs =
    (callbackUs > 0 && queuePopUs >= callbackUs) ? (queuePopUs - callbackUs) : 0;
captureToCallbackUs =
    (callbackUs > 0 && captureUs > 0)
        ? (callbackUs >= captureUs ? (callbackUs - captureUs) : (captureUs - callbackUs))
        : 0;
captureToQueueUs =
    (queuePushUs > 0 && captureUs > 0)
        ? (queuePushUs >= captureUs ? (queuePushUs - captureUs) : (captureUs - queuePushUs))
        : 0;
  ++stats.captureReadbackSamples;
  stats.captureD3DWaitSumUs += captureD3DWaitUs;
  stats.captureD3DWaitMaxUs = std::max(stats.captureD3DWaitMaxUs, captureD3DWaitUs);
  stats.captureCopyMapSumUs += captureCopyMapUs;
  stats.captureCopyMapMaxUs = std::max(stats.captureCopyMapMaxUs, captureCopyMapUs);
  stats.captureMemcpySumUs += captureMemcpyUs;
  stats.captureMemcpyMaxUs = std::max(stats.captureMemcpyMaxUs, captureMemcpyUs);
  stats.captureUnmapWaitSumUs += captureUnmapWaitUs;
  stats.captureUnmapWaitMaxUs = std::max(stats.captureUnmapWaitMaxUs, captureUnmapWaitUs);
  stats.captureUnmapSumUs += captureUnmapUs;
  stats.captureUnmapMaxUs = std::max(stats.captureUnmapMaxUs, captureUnmapUs);
  queueWaitUs =
      (queuePopUs > 0 && queuePushUs > 0 && queuePopUs >= queuePushUs) ? (queuePopUs - queuePushUs) : 0;
  queueGapFrames =
      (stats.lastVersionSent > 0 && version > stats.lastVersionSent) ? (version - stats.lastVersionSent - 1) : 0;
  ++stats.queuePopCount;
  const uint64_t lastPopVersionAtRead = capture.lastPopFrameVersion.load(std::memory_order_acquire);
  queueDepthAtPop = (version > lastPopVersionAtRead) ? (version - lastPopVersionAtRead) : 0;
  update_u64_max(stats.queueDepthMax, queueDepthAtPop);
  capture.lastPopFrameVersion.store(version, std::memory_order_release);
  return Flow::Next;
}

Flow stage_gate_static(HostContext& hx, TickContext& tc) {
  auto& useH264 = hx.useH264;
  auto& guardStalePreEncode = hx.guardStalePreEncode;
  auto& paceByTick = hx.paceByTick;
  auto& frameGating = hx.frameGating;
  auto& clientMetrics = hx.clientMetrics;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& payload = tc.payload;
  auto& w = tc.w;
  auto& h = tc.h;
  auto& stride = tc.stride;
  auto& captureUs = tc.captureUs;
  auto& callbackUs = tc.callbackUs;
  auto& version = tc.version;
  auto& servedBootstrap = tc.servedBootstrap;
  auto& captureStampUs = tc.captureStampUs;
  auto& queuePopUs = tc.queuePopUs;
  auto& frameAgeAtSelectUs = tc.frameAgeAtSelectUs;
  if (!servedBootstrap && frameGating.enabled && useH264 && payload && !payload->empty()) {
    if (frameGating.refPayload && !frameGating.refPayload->empty() &&
        frameGating.refW == w && frameGating.refH == h && frameGating.refStride == stride) {
      frameGating.changePermilleLast = estimate_bgra_change_permille(
          payload->data(), frameGating.refPayload->data(), payload->size(), frameGating.sampleTarget);
      frameGating.changePermilleSum += frameGating.changePermilleLast;
      ++frameGating.changePermilleCount;

      if (frameGating.changePermilleLast == 0) {
        frameGating.staticStreak = std::min<uint32_t>(frameGating.staticStreak + 1, 60000);
        frameGating.motionStreak = 0;
      } else {
        frameGating.motionStreak = std::min<uint32_t>(frameGating.motionStreak + 1, 60000);
        frameGating.staticStreak = 0;
      }
    } else {
      frameGating.staticStreak = 0;
      frameGating.motionStreak = 0;
      frameGating.changePermilleLast = 1000;
    }

    const bool prevStaticMode = frameGating.staticMode;
    // Any difference at all counts as motion. estimate_bgra_change_permille returns 0 only
    // for a byte-identical frame, so this both leaves static mode on the first changed
    // frame and never throttles an edit that is too small to move a percentage threshold.
    const bool motionNow = frameGating.changePermilleLast > 0;
    if (!frameGating.staticMode && frameGating.staticStreak >= frameGating.enterFrames) {
      frameGating.staticMode = true;
    } else if (frameGating.staticMode &&
               (motionNow || frameGating.motionStreak >= frameGating.exitFrames)) {
      frameGating.staticMode = false;
    }
    if (prevStaticMode != frameGating.staticMode) {
      std::cout << "[native-video-host] frame-gating mode="
                << (frameGating.staticMode ? "static" : "motion")
                << " changePm=" << frameGating.changePermilleLast
                << " staticStreak=" << frameGating.staticStreak
                << " motionStreak=" << frameGating.motionStreak
                << "\n";
    }

    const bool keyReqPending = clientMetrics.requestedKeyFrame.load(std::memory_order_acquire);
    const uint64_t targetIntervalUs = frameGating.staticMode ? frameGating.staticIntervalUs : encoder.activeFrameIntervalUs;
    // The static interval throttles idle scenes; it must never hold back a frame that
    // actually changed, or the first interaction after idle arrives late.
    // In paced motion mode the main tick already enforces encoder.activeFrameIntervalUs. Applying
    // the same interval here a second time makes a slightly-early capture timestamp skip
    // the entire tick (measured 1-6 lost frames/s at 60fps). Keep this limiter only for
    // static throttling or the explicitly unpaced throughput path.
    const bool needsGatingRateLimit = frameGating.staticMode || !paceByTick;
    if (needsGatingRateLimit && !keyReqPending && !motionNow &&
        frameGating.lastSentUs > 0 &&
        queuePopUs < (frameGating.lastSentUs + targetIntervalUs)) {
      ++frameGating.skipCount;
      if (frameGating.staticMode) ++frameGating.staticSkipCount;
      stats.lastVersionSent = version;
      return Flow::Continue;
    }
  }
  if (useH264 && guardStalePreEncode && frameAgeAtSelectUs > kMaxPreEncodeFrameAgeUs) {
    ++stats.stalePreEncodeDropCount;
    return Flow::Continue;
  }
  if (!servedBootstrap) {
    if (stats.lastVersionSent > 0 && version > stats.lastVersionSent + 1) {
      stats.skippedByOverwrite += (version - stats.lastVersionSent - 1);
    }
    stats.lastVersionSent = version;
  }
  captureStampUs = (callbackUs > 0) ? callbackUs : captureUs;

 
 
  return Flow::Next;
}

Flow stage_encode_send(HostContext& hx, TickContext& tc) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& useRaw = hx.useRaw;
  auto& transport = hx.transport;
  auto& guardStaleEncoded = hx.guardStaleEncoded;
  auto& guardStalePreEncode = hx.guardStalePreEncode;
  auto& poppedNv12Slot = hx.poppedNv12Slot;
  auto& item = hx.item;
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
  auto& lastUserFeedbackUs = hx.lastUserFeedbackUs;
  auto& tickWaitUs = tc.tickWaitUs;
  auto& payload = tc.payload;
  auto& seq = tc.seq;
  auto& w = tc.w;
  auto& h = tc.h;
  auto& stride = tc.stride;
  auto& streamGeneration = tc.streamGeneration;
  auto& captureUs = tc.captureUs;
  auto& callbackUs = tc.callbackUs;
  auto& callbackIntervalUs = tc.callbackIntervalUs;
  auto& captureIntervalUs = tc.captureIntervalUs;
  auto& captureClockSkewUs = tc.captureClockSkewUs;
  auto& captureAgeAtCallbackUs = tc.captureAgeAtCallbackUs;
  auto& captureD3DWaitUs = tc.captureD3DWaitUs;
  auto& captureCopyMapUs = tc.captureCopyMapUs;
  auto& captureMemcpyUs = tc.captureMemcpyUs;
  auto& captureUnmapWaitUs = tc.captureUnmapWaitUs;
  auto& captureUnmapUs = tc.captureUnmapUs;
  auto& version = tc.version;
  auto& nv12Slot = tc.nv12Slot;
  auto& nv12Generation = tc.nv12Generation;
  auto& nv12W = tc.nv12W;
  auto& nv12H = tc.nv12H;
  auto& queueWaitReason = tc.queueWaitReason;
  auto& servedBootstrap = tc.servedBootstrap;
  auto& kickForcedKey = tc.kickForcedKey;
  auto& queueWaitUs = tc.queueWaitUs;
  auto& queueGapFrames = tc.queueGapFrames;
  auto& queueDepthAtPop = tc.queueDepthAtPop;
  auto& captureStampUs = tc.captureStampUs;
  auto& sendFailed = tc.sendFailed;
  auto& queuePopUs = tc.queuePopUs;
  auto& queueSelectWaitUs = tc.queueSelectWaitUs;
  auto& frameAgeAtSelectUs = tc.frameAgeAtSelectUs;
  auto& captureToCallbackUs = tc.captureToCallbackUs;
  auto& captureToQueueUs = tc.captureToQueueUs;
  if (useRaw) {
    RawFrameHeader hdr{};
    hdr.header.magic = remote60::native_poc::kMagic;
    hdr.header.type = static_cast<uint16_t>(MessageType::RawFrameBgra);
    hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
    hdr.seq = seq;
    hdr.width = w;
    hdr.height = h;
    hdr.stride = stride;
    hdr.payloadSize = static_cast<uint32_t>(payload->size());
    hdr.streamGeneration = streamGeneration;
    hdr.captureQpcUs = captureStampUs;
    hdr.encodeStartQpcUs = captureStampUs;
    hdr.encodeEndQpcUs = captureStampUs;
    SendPathStats sendPathStats{};
    const uint64_t sendStartUs = qpc_now_us();
    const uint64_t sendIntervalUs =
        (stats.lastSendStartUs > 0 && sendStartUs >= stats.lastSendStartUs) ? (sendStartUs - stats.lastSendStartUs) : 0;
    const uint64_t sendIntervalErrUs =
        (encoder.activeFrameIntervalUs > 0 && sendIntervalUs > 0)
            ? ((sendIntervalUs >= encoder.activeFrameIntervalUs) ? (sendIntervalUs - encoder.activeFrameIntervalUs)
                                                         : (encoder.activeFrameIntervalUs - sendIntervalUs))
            : 0;
    const uint64_t queueToSendUs = (sendStartUs >= queuePopUs) ? (sendStartUs - queuePopUs) : 0;
    const uint64_t sendWaitUs = queueToSendUs;
    const uint64_t callbackToSendStartUs = (sendStartUs >= callbackUs) ? (sendStartUs - callbackUs) : 0;
    hdr.sendQpcUs = sendStartUs;
    const bool sentOk =
        (transport == VideoTransport::Tcp) &&
        send_all_timed(clientSession.clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                       &sendPathStats.headerCallCount) &&
        send_all_timed(clientSession.clientSock, payload->data(), payload->size(), &sendPathStats.payloadUs,
                       &sendPathStats.payloadCallCount);
    const uint64_t sendDoneUs = qpc_now_us();
    const uint64_t sendDurUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
    const uint64_t sendCallCount = sendPathStats.headerCallCount + sendPathStats.payloadCallCount;
    if (sentOk) {
      stats.lastSendStartUs = sendStartUs;
      capture.LogFirstSentGeneration(res, stats, "raw", streamGeneration, sendStartUs, hdr.captureQpcUs, hdr.width, hdr.height);
      if (frameGating.enabled && useH264 && payload && !payload->empty()) {
        frameGating.lastSentUs = sendStartUs;
        frameGating.refPayload = payload;
        frameGating.refW = w;
        frameGating.refH = h;
        frameGating.refStride = stride;
      }
    }

    if (!sentOk) {
      if (reconnect_tcp_data_session(hx, "raw_send_fail")) {
        return Flow::Continue;
      }
      std::cout << "[native-video-host] client disconnected\n";
      return Flow::Break;
    }
    ++sender.sentFrames;
    sender.sentBytes += payload->size();
      if (args.traceEvery > 0 && (seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || stats.tracePrinted < args.traceMax)) {
      ++stats.tracePrinted;
      const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
      const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
      const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
      const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
          queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
        std::cout << "[native-video-host][trace] seq=" << seq
                  << " captureUs=" << hdr.captureQpcUs
                  << " encodeStartUs=" << hdr.encodeStartQpcUs
                  << " encodeEndUs=" << hdr.encodeEndQpcUs
                  << " sendUs=" << hdr.sendQpcUs
                  << " bottleneckStageCode=" << bottleneck.code
                  << " bottleneckStageUs=" << bottleneck.us
                  << " bottleneckStageName=" << bottleneck.name
                  << " c2eUs=" << c2eUs
                  << " captureToCallbackUs=" << captureToCallbackUs
                  << " callbackIntervalUs=" << callbackIntervalUs
                  << " captureIntervalUs=" << captureIntervalUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " captureD3DWaitUs=" << captureD3DWaitUs
                  << " captureCopyMapUs=" << captureCopyMapUs
                  << " captureMemcpyUs=" << captureMemcpyUs
                  << " captureUnmapWaitUs=" << captureUnmapWaitUs
                  << " captureUnmapUs=" << captureUnmapUs
                  << " selectWaitUs=" << frameAgeAtSelectUs
                  << " queueSelectWaitUs=" << queueSelectWaitUs
                 << " queueGapFrames=" << queueGapFrames
                 << " queueDepth=" << queueDepthAtPop
                 << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
                 << " captureToQueueUs=" << captureToQueueUs
                 << " queueWaitUs=" << queueWaitUs
                 << " queueWaitReason=" << queueWaitReason
                 << " queueToSendUs=" << queueToSendUs
                 << " sendWaitUs=" << sendWaitUs
                 << " sendIntervalUs=" << sendIntervalUs
                 << " sendIntervalErrUs=" << sendIntervalErrUs
                 << " tickWaitUs=" << tickWaitUs
                 << " sendCallCount=" << sendCallCount
                 << " sendHeaderUs=" << sendPathStats.headerUs
                 << " sendPayloadUs=" << sendPathStats.payloadUs
                 << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                 << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                 << " sendChunkCount=" << sendPathStats.payloadChunkCount
                 << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                 << " sendStartUs=" << sendStartUs
                << " sendDoneUs=" << sendDoneUs
                << " sendDurUs=" << sendDurUs
                << " encUs=" << encUs
                << " e2sUs=" << e2sUs
                << " payloadBytes=" << hdr.payloadSize
                << "\n";
    }
    const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
    const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
    const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
    const uint64_t pipeUs = (hdr.sendQpcUs >= hdr.captureQpcUs) ? (hdr.sendQpcUs - hdr.captureQpcUs) : 0;
    const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
        queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
    if (pipeUs >= kHostUserFeedbackWarnUs &&
        (hdr.sendQpcUs >= lastUserFeedbackUs + kHostUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
      std::cout << "[native-video-host][user-feedback] seq=" << seq
                << " codec=" << "raw"
                << " pipeUs=" << pipeUs
                << " bottleneckStageCode=" << bottleneck.code
                << " bottleneckStageUs=" << bottleneck.us
                << " bottleneckStageName=" << bottleneck.name
                << " captureToCallbackUs=" << captureToCallbackUs
                << " callbackIntervalUs=" << callbackIntervalUs
                << " captureIntervalUs=" << captureIntervalUs
                << " captureClockSkewUs=" << captureClockSkewUs
                << " captureD3DWaitUs=" << captureD3DWaitUs
                << " captureCopyMapUs=" << captureCopyMapUs
                << " captureMemcpyUs=" << captureMemcpyUs
                << " captureUnmapWaitUs=" << captureUnmapWaitUs
                << " captureUnmapUs=" << captureUnmapUs
                << " selectWaitUs=" << frameAgeAtSelectUs
                << " queueSelectWaitUs=" << queueSelectWaitUs
                << " captureToQueueUs=" << captureToQueueUs
                 << " queueWaitUs=" << queueWaitUs
                 << " queueWaitReason=" << queueWaitReason
                  << " queueGapFrames=" << queueGapFrames
                  << " queueDepth=" << queueDepthAtPop
                  << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
                  << " queueToSendUs=" << queueToSendUs
                  << " sendIntervalUs=" << sendIntervalUs
                  << " sendIntervalErrUs=" << sendIntervalErrUs
                   << " captureClockSkewUs=" << captureClockSkewUs
                   << " sendWaitUs=" << sendWaitUs
                 << " tickWaitUs=" << tickWaitUs
                 << " sendCallCount=" << sendCallCount
                 << " sendHeaderUs=" << sendPathStats.headerUs
                 << " sendPayloadUs=" << sendPathStats.payloadUs
                 << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                 << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                 << " sendChunkCount=" << sendPathStats.payloadChunkCount
                 << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                 << " c2eUs=" << c2eUs
                << " cb2eUs=" << callbackToSendStartUs
                << " encUs=" << encUs
                << " e2sUs=" << e2sUs
                << " sendStartUs=" << sendStartUs
                << " sendDoneUs=" << sendDoneUs
                << " sendDurUs=" << sendDurUs
                << "\n";
      lastUserFeedbackUs = hdr.sendQpcUs;
    }
    } else {
      const uint8_t* encodeSrc = payload->data();
    uint32_t encodeSrcW = w;
    uint32_t encodeSrcH = h;
    uint32_t encodeSrcStride = stride;
    std::vector<uint8_t> scaledBgra;
    D3DReadbackTiming scaleReadbackTiming{};
    uint64_t preEncodePrepUs = 0;
    uint64_t scaleUs = 0;
    uint64_t nv12Us = 0;
    const uint64_t preEncodeStartUs = qpc_now_us();
    // A window selection or a resize changes the source geometry; re-fit the encode size
    // to the new aspect so the scaler never has to stretch. The source size changes on
    // EVERY frame of an interactive window drag, and apply_encoder_target tears the MFT
    // down, so two guards keep this from thrashing: the geometry must hold steady for a
    // settle period, and near-identical aspect (letterboxing under 2%) is left alone.
    if (!servedBootstrap && w > 0 && h > 0 && (w != encoder.encodeSourceW || h != encoder.encodeSourceH)) {
      const uint64_t nowRefitUs = qpc_now_us();
      if (w != encoder.pendingRefitW || h != encoder.pendingRefitH) {
        encoder.pendingRefitW = w;
        encoder.pendingRefitH = h;
        encoder.pendingRefitSinceUs = nowRefitUs;
      } else if (nowRefitUs - encoder.pendingRefitSinceUs >= kEncodeRefitSettleUs) {
        uint32_t refitW = encoder.activeEncodeW;
        uint32_t refitH = encoder.activeEncodeH;
        fit_size_preserving_aspect(w, h, encoder.nominalEncodeW, encoder.nominalEncodeH, &refitW, &refitH);
        const double activeAspect =
            static_cast<double>(encoder.activeEncodeW) / static_cast<double>(std::max(1u, encoder.activeEncodeH));
        const double refitAspect =
            static_cast<double>(refitW) / static_cast<double>(std::max(1u, refitH));
        const bool aspectClose =
            std::abs(refitAspect - activeAspect) <= activeAspect * 0.02;
        encoder.encodeSourceW = w;
        encoder.encodeSourceH = h;
        if ((refitW != encoder.activeEncodeW || refitH != encoder.activeEncodeH) && !aspectClose) {
          const uint32_t prevW = encoder.activeEncodeW;
          const uint32_t prevH = encoder.activeEncodeH;
          const uint32_t keepNominalW = encoder.nominalEncodeW;
          const uint32_t keepNominalH = encoder.nominalEncodeH;
          if (encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, keepNominalW, keepNominalH, encoder.activeFps, encoder.activeBitrate,
                                   encoder.activeKeyint)) {
            encoder.forceKeyNext = true;
            std::cout << "[native-video-host] encode-refit source=" << w << "x" << h
                      << " encode=" << prevW << "x" << prevH << " -> " << encoder.activeEncodeW << "x"
                      << encoder.activeEncodeH << "\n";
          } else {
            // apply_encoder_target already shut the encoder down; without a working encoder
            // every later frame fails silently, so treat this like the other callers do.
            std::cerr << "[native-video-host] encode-refit failed source=" << w << "x" << h
                      << "; stopping stream\n";
            return Flow::Break;
          }
        }
      }
    } else {
      encoder.pendingRefitW = 0;
      encoder.pendingRefitH = 0;
    }
    const bool wantSurfaceEncode = useH264 && nv12Slot >= 0 && encoder.surfaceEncodeHealthy &&
                                   nv12W == encoder.activeEncodeW && nv12H == encoder.activeEncodeH;
    if (!wantSurfaceEncode && (encoder.activeEncodeW != w || encoder.activeEncodeH != h)) {
      const uint64_t scaleStartUs = qpc_now_us();
      bool scaleOk = false;
      if (capture.gpuScalerHealthy) {
        ++stats.gpuScaleAttempts;
        scaleOk = res.gpuScaler.scale(payload->data(), w, h, stride, encoder.activeEncodeW, encoder.activeEncodeH,
                                  &scaledBgra, &scaleReadbackTiming);
        if (scaleOk) {
          ++stats.gpuScaleSuccess;
          ++stats.gpuScaleTimedCount;
          stats.gpuScaleD3DWaitSumUs += scaleReadbackTiming.d3dWaitUs;
          stats.gpuScaleD3DWaitMaxUs = std::max(stats.gpuScaleD3DWaitMaxUs, scaleReadbackTiming.d3dWaitUs);
          stats.gpuScaleCopyMapSumUs += scaleReadbackTiming.copyMapUs;
          stats.gpuScaleCopyMapMaxUs = std::max(stats.gpuScaleCopyMapMaxUs, scaleReadbackTiming.copyMapUs);
          stats.gpuScaleMemcpySumUs += scaleReadbackTiming.memcpyUs;
          stats.gpuScaleMemcpyMaxUs = std::max(stats.gpuScaleMemcpyMaxUs, scaleReadbackTiming.memcpyUs);
          stats.gpuScaleUnmapWaitSumUs += scaleReadbackTiming.unmapWaitUs;
          stats.gpuScaleUnmapWaitMaxUs = std::max(stats.gpuScaleUnmapWaitMaxUs, scaleReadbackTiming.unmapWaitUs);
          stats.gpuScaleUnmapSumUs += scaleReadbackTiming.unmapUs;
          stats.gpuScaleUnmapMaxUs = std::max(stats.gpuScaleUnmapMaxUs, scaleReadbackTiming.unmapUs);
        } else {
          ++stats.gpuScaleFail;
          capture.gpuScalerHealthy = false;
          std::cout << "[native-video-host] gpu scaler disabled after failure; fallback=cpu\n";
        }
      }
      if (!scaleOk) {
        ++stats.gpuScaleCpuFallback;
        if (!resize_bgra_bilinear(payload->data(), w, h, stride, encoder.activeEncodeW, encoder.activeEncodeH, &scaledBgra)) {
          return Flow::Continue;
        }
      }
      encodeSrc = scaledBgra.data();
      encodeSrcW = encoder.activeEncodeW;
      encodeSrcH = encoder.activeEncodeH;
      encodeSrcStride = encoder.activeEncodeW * 4;
      const uint64_t scaleDoneUs = qpc_now_us();
      scaleUs = (scaleDoneUs >= scaleStartUs) ? (scaleDoneUs - scaleStartUs) : 0;
    }

    const uint64_t prepDoneUs = qpc_now_us();
    preEncodePrepUs = (prepDoneUs >= preEncodeStartUs) ? (prepDoneUs - preEncodeStartUs) : 0;

    const uint64_t beforeEncodeUs = qpc_now_us();
    const uint64_t frameAgeBeforeEncodeUs =
        (callbackUs > 0 && beforeEncodeUs >= callbackUs) ? (beforeEncodeUs - callbackUs) : 0;
    uint64_t latestVersion = version;
    {
      std::lock_guard<std::mutex> lk(res.frame.mu);
      latestVersion = res.frame.version;
    }
    if (guardStalePreEncode &&
        frameAgeBeforeEncodeUs > kMaxPreEncodeFrameAgeUs && latestVersion != version) {
      ++stats.stalePreEncodeDropCount;
      return Flow::Continue;
    }

     if (clientMetrics.requestedKeyFrame.exchange(false)) {
      const uint16_t reason = clientMetrics.keyFrameReason.load();
      std::cout << "[native-video-host][control] keyframe-request-consumed reason=" << reason << "\n";
      encoder.forceKeyNext = true;
    }
    if (sender.requestKey.exchange(false, std::memory_order_acq_rel)) {
      // The sender dropped a backlog; the stream needs an IDR to resynchronize.
      encoder.forceKeyNext = true;
    }
     // The keyint schedule applies to REAL frames only. A kick/refresh-served synthetic frame
     // carries seq=0, and 0 % keyint == 0 made every one of them an IDR -- defeating the open-
     // barrier design of riding the held frame as a cheap P-frame (a 40-160KB IDR instead of a
     // few-KB P, once per kick/refresh). A closed barrier still gets its IDR via encoder.forceKeyNext.
     // A single submit latch (encoder.forceKeySubmittedAtUs) covers ALL key reasons -- request,
     // first-frame (encoder.encodedSeq==0), and the keyint schedule: one key input pending inside the
     // async MFT satisfies every one of them, so none may re-force while it is in flight. The
     // measured 4-5 consecutive-IDR trains came from forcing every input until the key finally
     // surfaced. The latch is stamped only after the encoder ACCEPTS the input (below), and
     // times out after 300ms so a lost key is retried.
      const uint64_t encodeStartUs = qpc_now_us();
     const bool forceKeyInFlight =
         encoder.forceKeySubmittedAtUs != 0 && encodeStartUs < encoder.forceKeySubmittedAtUs + 300'000;
     const bool scheduledKey =
         !servedBootstrap && (encoder.activeKeyint > 0) && ((seq % encoder.activeKeyint) == 0);
     const bool keyWanted = encoder.forceKeyNext || (encoder.encodedSeq == 0) || scheduledKey;
     const bool forceKeyFrame = keyWanted && !forceKeyInFlight;
      const uint64_t encodeInputUs = captureStampUs;
      if (capture.timelineOriginUs < 0) {
        capture.timelineOriginUs = static_cast<int64_t>(encodeInputUs);
      }
      const uint64_t queueToEncodeUs = (encodeStartUs >= queuePopUs) ? (encodeStartUs - queuePopUs) : 0;
     const uint64_t callbackToEncodeStartUs =
          (encodeStartUs >= callbackUs) ? (encodeStartUs - callbackUs) : 0;
      std::vector<H264AccessUnit> units;
      H264EncodeFrameStats encodeStats{};
      bool surfaceEncoded = false;
      // The MFT encode is the prime suspect for a driver/GPU wedge that stops the whole loop
      // without returning; mark the phase so the watchdog attributes a hang here correctly.
      watchdog.EnterMainPhase(MainLoopPhase::EncodeCall);
      if (wantSurfaceEncode) {
        auto nv12Tex = res.captureReadback.Nv12SlotTexture(nv12Slot, nv12Generation);
        if (nv12Tex &&
            encoder.codec.encode_frame_surface(nv12Tex.Get(), forceKeyFrame,
                                         static_cast<int64_t>(encodeInputUs) * 10, &units,
                                         &encodeStats)) {
          surfaceEncoded = true;
          ++encoder.nv12SurfaceEncodeCount;
          Nv12PendingRelease pending;
          pending.slot = nv12Slot;
          pending.generation = nv12Generation;
          pending.requiredOutputs = encoder.outputSamplesTotal + 1;
          encoder.nv12PendingReleases.push_back(pending);
          poppedNv12Slot = -1;  // ownership moved to the deferred-release queue
          // Accepting a DXGI sample is no proof the vendor path is fast: AMF accepts them
          // and then takes ~68ms a frame on internal synchronization (measured; the CPU
          // path runs 4.5ms). Probe the first frames and drop back for the session when
          // the surface path costs more than half the 33ms frame budget on average.
          encoder.surfaceEncodeProbeSumUs += encodeStats.encodeCallUs;
          if (++encoder.surfaceEncodeProbeCount == 30) {
            const uint64_t avgUs = encoder.surfaceEncodeProbeSumUs / encoder.surfaceEncodeProbeCount;
            if (avgUs > 16000) {
              encoder.surfaceEncodeHealthy = false;
              res.captureReadback.SetNv12Enabled(false);
              std::cout << "[native-video-host] nv12 surface encode too slow avgUs=" << avgUs
                        << " backend=" << encoder.codec.backend_name()
                        << "; reverting to cpu nv12\n";
            } else {
              std::cout << "[native-video-host] nv12 surface encode probe ok avgUs=" << avgUs
                        << " backend=" << encoder.codec.backend_name() << "\n";
            }
            encoder.surfaceEncodeProbeCount = 0;
            encoder.surfaceEncodeProbeSumUs = 0;
          }
        } else {
          // One rejection turns the path off for the session; this frame is dropped and
          // the next one takes the CPU route. Its slot is released at the next loop top.
          encoder.surfaceEncodeHealthy = false;
          res.captureReadback.SetNv12Enabled(false);
          std::cout << "[native-video-host] nv12 surface encode rejected backend="
                    << encoder.codec.backend_name() << "; falling back to cpu nv12\n";
          return Flow::Continue;
        }
      }
     if (!surfaceEncoded &&
         !encoder.codec.encode_frame_bgra(encodeSrc, encodeSrcW, encodeSrcH, encodeSrcStride,
                                    forceKeyFrame, static_cast<int64_t>(encodeInputUs) * 10,
                                    &units, &encodeStats)) {
      ++encoder.encodeFailCount;
      if ((encoder.encodeFailCount % 60) == 1) {
        std::cout << "[native-video-host] encode failed count=" << encoder.encodeFailCount << "\n";
      }
      return Flow::Continue;
    }
    // Encode returned; back to ordinary work for the watchdog's threshold.
    watchdog.EnterMainPhase(MainLoopPhase::Loop);
    if (forceKeyFrame) {
      // Latch/count only for inputs the encoder actually ACCEPTED: a failed encode never
      // reached the MFT, and arming the latch for it would suppress the retry for 300ms.
      ++encoder.forceKeyInputCount;
      encoder.forceKeySubmittedAtUs = encodeStartUs;
    }
    if (!surfaceEncoded) {
      nv12Us = encodeStats.colorConvertUs;
      preEncodePrepUs += nv12Us;
    }
    encoder.outputSamplesTotal += encodeStats.processOutputSamples;
    if (!servedBootstrap) {
      // A real frame was just handed to the async MFT; it becomes the encoder's held input until
      // the next frame arrives. Record its capture timestamp and (re)arm the trailing kick so the
      // deadline always trails the LAST real input -- continuous motion keeps pushing it out and
      // adds zero synthetic frames; only a genuine pause lets the kick fire to flush this frame.
      kick.lastRealInputCaptureUs = encodeInputUs;
      kick.Arm(qpc_now_us(), useH264);
    }
    while (!encoder.nv12PendingReleases.empty() &&
           encoder.nv12PendingReleases.front().requiredOutputs <= encoder.outputSamplesTotal) {
      res.captureReadback.ReleaseNv12Slot(encoder.nv12PendingReleases.front().slot,
                                      encoder.nv12PendingReleases.front().generation);
      encoder.nv12PendingReleases.pop_front();
    }
    const uint64_t encodeEndUs = qpc_now_us();

    // Encoder output-liveness heartbeat. Placed BEFORE the units.empty() early-out below so a
    // starved encoder -- which returns empty on every call -- is still observed here; the old
    // `continue` skipped the whole 1s stats / self-heal tail, so a wedge produced no telemetry at
    // all. A frame was just handed to the MFT this call, so input is advancing; only the OUTPUT is
    // in question. This block changes no control flow (diagnostic only).
    ++encoder.inputAcceptedTotal;
    if (servedBootstrap) {
      ++encoder.syntheticInputAccepted;
    } else {
      ++encoder.realInputAccepted;
    }
    if (encodeStats.processOutputSamples > 0) {
      encoder.outputAuTotal += encodeStats.processOutputSamples;
      encoder.lastOutputUs = encodeEndUs;
      encoder.noOutputSinceUs = 0;
      encoder.acceptedNoOutputStreak = 0;
      // Reset the episode so the next starvation logs its first line immediately, and clear the
      // per-streak async accumulators.
      encoder.lastStarvationLogUs = 0;
      encoder.starveNeedInputAccum = encoder.starveHaveOutputAccum = encoder.starveNoEventAccum = 0;
      encoder.starveNotAcceptingAccum = encoder.starveNeedMoreAccum = encoder.starveNeedInputOnlyCalls = 0;
      // Revive watchdog.mainLoopLastSeq (previously declared but never stored, so the watchdog record read
      // a constant 0): publish real encoder-output progress, not loop iterations. A follow-up can
      // make the watchdog fire on this age while input is still being accepted.
      watchdog.mainLoopLastSeq.store(encoder.outputSamplesTotal, std::memory_order_release);
    } else {
      ++encoder.acceptedNoOutputStreak;
      if (encoder.noOutputSinceUs == 0) encoder.noOutputSinceUs = encodeEndUs;
      encoder.starveNeedInputAccum += encodeStats.asyncPollNeedInputCount;
      encoder.starveHaveOutputAccum += encodeStats.asyncPollHaveOutputCount;
      encoder.starveNoEventAccum += encodeStats.asyncPollNoEventCount;
      encoder.starveNotAcceptingAccum += encodeStats.processInputNotAcceptingCount;
      encoder.starveNeedMoreAccum += encodeStats.processOutputNeedMoreInputCount;
      encoder.starveNeedInputOnlyCalls += encodeStats.asyncNeedInputOnlyCall;
      // Age is measured from when the streak began, NOT from encoder.lastOutputUs, so an encoder
      // that never emitted a single AU since startup (encoder.lastOutputUs==0) is still detected.
      const uint64_t noOutputAgeUs =
          (encoder.noOutputSinceUs > 0 && encodeEndUs > encoder.noOutputSinceUs)
              ? (encodeEndUs - encoder.noOutputSinceUs)
              : 0;
      // Stream active + encoder keeps accepting input but produces no output for a while = the
      // async-MFT output-starvation wedge (video frozen, main loop spinning, liveness watchdog
      // green). Emit one rate-limited anomaly line with the streak-accumulated async counters so a
      // field recurrence tells a host event-driving bug (NeedInput accrues, HaveOutput stays 0)
      // from a genuine vendor/hardware stall. Recovery is a separate follow-up; diagnostic only.
      if (clientSession.streamControlActive.load(std::memory_order_acquire) &&
          encoder.acceptedNoOutputStreak >= 8 && noOutputAgeUs >= 1000000ULL &&
          (encoder.lastStarvationLogUs == 0 ||
           encodeEndUs >= encoder.lastStarvationLogUs + 1000000ULL)) {
        encoder.lastStarvationLogUs = encodeEndUs;
        std::cout << "[native-video-host] encoder-output-starvation"
                  << " acceptedNoOutputStreak=" << encoder.acceptedNoOutputStreak
                  << " noOutputAgeUs=" << noOutputAgeUs
                  << " everOutput=" << (encoder.lastOutputUs > 0 ? 1 : 0)
                  << " realIn=" << encoder.realInputAccepted
                  << " synthIn=" << encoder.syntheticInputAccepted
                  << " outAu=" << encoder.outputAuTotal
                  << " asyncEnabled=" << static_cast<unsigned>(encodeStats.asyncEnabled)
                  << " streakNeedInput=" << encoder.starveNeedInputAccum
                  << " streakHaveOutput=" << encoder.starveHaveOutputAccum
                  << " streakNeedInputOnlyCalls=" << encoder.starveNeedInputOnlyCalls
                  << " streakNoEvent=" << encoder.starveNoEventAccum
                  << " streakNotAccepting=" << encoder.starveNotAcceptingAccum
                  << " streakNeedMore=" << encoder.starveNeedMoreAccum
                  << " pendingDepth=" << encodeStats.pendingInputDepth
                  << " pendingOverflow=" << encodeStats.pendingInputOverflowTotal
                  << "\n";
      }
    }

    if (units.empty()) return Flow::Continue;

    stats.captureAgeSumUs += captureAgeAtCallbackUs;
    stats.captureAgeMaxUs = std::max(stats.captureAgeMaxUs, captureAgeAtCallbackUs);
    stats.callbackToEncodeStartSumUs += callbackToEncodeStartUs;
    stats.callbackToEncodeStartMaxUs = std::max(stats.callbackToEncodeStartMaxUs, callbackToEncodeStartUs);

    bool encoderResetTriggered = false;
    bool sessionReconnectTriggered = false;
    bool countedRawForInput = false;
    if (sender.sendFailed.exchange(false, std::memory_order_acq_rel)) {
      // Same policy the inline path had: a UDP send failure on an endless session waits
      // for the peer to re-Hello rather than exiting.
      ++sender.udpTxFail;
      if (args.seconds == 0) {
        return Flow::Continue;
      }
    }
      // An async MFT can release several access units from one encode call. They are pushed
      // microseconds apart, so the sender thread has usually not been scheduled between them
      // and the queue depth reflects the burst rather than a backlogged wire. Counting that
      // as congestion discarded the whole GOP and forced an IDR on a perfectly healthy link.
      //
      // Judge congestion once, on the backlog that existed *before* this batch: that is the
      // only part of the queue the sender has genuinely failed to drain. Sizing the limit
      // from the batch instead would still overflow on the last unit whenever a frame was
      // already queued, and a large drain would authorise an equally large queue -- seconds
      // of latency -- so the absolute cap below bounds it regardless.
      constexpr size_t kSenderQueueMaxFrames = 6;
      size_t senderBacklogBeforeBatch = 0;
      {
        std::lock_guard<std::mutex> lk(sender.mu);
        senderBacklogBeforeBatch = sender.queue.size();
      }
      const bool senderBacklogged = senderBacklogBeforeBatch >= 2;
      for (const auto& au : units) {
        if (au.bytes.empty()) continue;
        const int64_t auCaptureUs = (au.sampleTimeHns > 0) ? (au.sampleTimeHns / 10) : static_cast<int64_t>(encodeInputUs);
        // This AU carries the capture timestamp of the input frame it was produced from (the async
        // MFT preserves input sample times FIFO). Observing it is the proof a given real input has
        // finally come OUT of the encoder -- the cancel signal for the trailing kick. Track the
        // newest we have seen so a pending kick disarms once the latest real input has emerged.
        if (auCaptureUs > 0 && static_cast<uint64_t>(auCaptureUs) > kick.lastEmittedAuCaptureUs) {
          kick.lastEmittedAuCaptureUs = static_cast<uint64_t>(auCaptureUs);
        }
        if (encoder.auTimelineOriginUs < 0 && capture.timelineOriginUs >= 0) {
          encoder.auTimelineOriginUs = static_cast<int64_t>(auCaptureUs) -
                               (static_cast<int64_t>(encodeInputUs) - capture.timelineOriginUs);
        }
        const int64_t captureTimelineRelativeUs = static_cast<int64_t>(encodeInputUs) - capture.timelineOriginUs;
        const int64_t auTimelineRelativeUs = static_cast<int64_t>(auCaptureUs) - encoder.auTimelineOriginUs;
        const int64_t captureToAuTimelineDeltaUs = captureTimelineRelativeUs - auTimelineRelativeUs;
        const uint64_t captureToAuTimelineSkewUs =
            (captureToAuTimelineDeltaUs >= 0)
                ? static_cast<uint64_t>(captureToAuTimelineDeltaUs)
                : static_cast<uint64_t>(-captureToAuTimelineDeltaUs);
        const int64_t captureToAuSignedDeltaUs = static_cast<int64_t>(auCaptureUs) - static_cast<int64_t>(encodeInputUs);
        const uint64_t captureToAuSkewUs =
            (captureToAuSignedDeltaUs >= 0)
                ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
        const uint64_t captureToAuUs = (captureToAuSignedDeltaUs >= 0)
                                           ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                           : 0;
        const uint64_t encodedAgeUs =
            (encodeEndUs >= static_cast<uint64_t>(auCaptureUs))
                ? (encodeEndUs - static_cast<uint64_t>(auCaptureUs))
                : 0;
      if (guardStaleEncoded && encodedAgeUs > kMaxEncodedFrameAgeUs) {
        ++stats.staleEncodedDropCount;
        ++encoder.consecutiveStaleFrames;
        if ((stats.staleEncodedDropCount % 60) == 1) {
          std::cout << "[native-video-host] stale encoded drop count=" << stats.staleEncodedDropCount
                    << " encodedAgeUs=" << encodedAgeUs
                    << " thresholdUs=" << kMaxEncodedFrameAgeUs
                    << " consecutive=" << encoder.consecutiveStaleFrames
                    << "\n";
        }
        if (encoder.consecutiveStaleFrames >= kMaxConsecutiveStaleEncodedFrames) {
          std::cout << "[native-video-host] encoder reset due to stale output age="
                    << encodedAgeUs << "us consecutive=" << encoder.consecutiveStaleFrames << "\n";
          encoder.codec.shutdown();
          if (!encoder.codec.initialize(encoder.activeEncodeW, encoder.activeEncodeH, encoder.activeFps, encoder.activeBitrate, encoder.activeKeyint)) {
          std::cerr << "[native-video-host] encoder reinitialize failed\n";
            sendFailed = true;
            break;
          }
          encoder.ResetTimelineAnchors(capture);
          encoder.ResetStarvationEpisode();
          // Same contract as the reinit sites above: the reset discarded any pending key input.
          encoder.forceKeySubmittedAtUs = 0;
          ++encoder.resetCount;
          encoder.consecutiveStaleFrames = 0;
          encoder.forceKeyNext = true;
          encoderResetTriggered = true;
          break;
        }
        continue;
      }
      encoder.consecutiveStaleFrames = 0;

      // The requested IDR can be delayed behind older async MFT output. Only the AU's
      // actual CleanPoint/IDR state is safe to advertise as a keyframe.
      const bool encodedKeyFrame = au.keyFrame;
      // A barrier-opening kick (fresh viewer, no reference frames) must deliver a real IDR: a
      // non-IDR AU would decode into garbage. Drop anything but an IDR in that case. An ordinary
      // trailing-edge kick on an OPEN stream, however, is flushing out the last real held frame,
      // whose P-frame references the decoder already has -- so let it through.
      if (servedBootstrap && kickForcedKey && !encodedKeyFrame) {
        continue;
      }
      if (kick.selectionFirstKeyframePendingGeneration != 0 &&
          streamGeneration == kick.selectionFirstKeyframePendingGeneration &&
          !encodedKeyFrame) {
        ++kick.selectionFirstKeyframeDropCount;
        if ((kick.selectionFirstKeyframeDropCount % 30ULL) == 1ULL) {
          std::cout << "[native-video-host] selection generation waiting keyframe streamGen="
                    << streamGeneration
                    << " droppedAu=" << kick.selectionFirstKeyframeDropCount
                    << " forceKeyNext=" << (encoder.forceKeyNext ? 1 : 0)
                    << "\n";
        }
        continue;
      }

      EncodedFrameHeader hdr{};
      hdr.header.magic = remote60::native_poc::kMagic;
      hdr.header.type = static_cast<uint16_t>(MessageType::EncodedFrameH264);
      hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
      hdr.seq = ++encoder.encodedSeq;
      hdr.width = encoder.activeEncodeW;
      hdr.height = encoder.activeEncodeH;
      hdr.payloadSize = static_cast<uint32_t>(au.bytes.size());
      hdr.flags = encodedKeyFrame ? 1u : 0u;
      hdr.streamGeneration = streamGeneration;
      hdr.captureQpcUs =
          static_cast<uint64_t>(std::max<int64_t>(0, auCaptureUs));
      hdr.encodeStartQpcUs = encodeStartUs;
      hdr.encodeEndQpcUs = encodeEndUs;
      SendPathStats sendPathStats{};
      const uint64_t sendStartUs = qpc_now_us();
      const uint64_t sendIntervalUs =
          (stats.lastSendStartUs > 0 && sendStartUs >= stats.lastSendStartUs) ? (sendStartUs - stats.lastSendStartUs) : 0;
      const uint64_t sendIntervalErrUs =
          (encoder.activeFrameIntervalUs > 0 && sendIntervalUs > 0)
              ? ((sendIntervalUs >= encoder.activeFrameIntervalUs) ? (sendIntervalUs - encoder.activeFrameIntervalUs)
                                                           : (encoder.activeFrameIntervalUs - sendIntervalUs))
              : 0;
      const uint64_t queueToSendUs = (sendStartUs >= queuePopUs) ? (sendStartUs - queuePopUs) : 0;
      const uint64_t sendToEncodeUs = (sendStartUs >= encodeEndUs) ? (sendStartUs - encodeEndUs) : 0;
      const uint64_t encodeSpanUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
      const uint64_t sendWaitUs =
          (queueToSendUs >= (queueToEncodeUs + encodeSpanUs))
              ? (queueToSendUs - queueToEncodeUs - encodeSpanUs)
              : 0;
      const uint64_t callbackToSendStartUs = (sendStartUs >= callbackUs) ? (sendStartUs - callbackUs) : 0;
      hdr.sendQpcUs = sendStartUs;

      bool sentOk = false;
      bool enqueuedForSend = false;
      if (transport == VideoTransport::Tcp) {
        enqueuedForSend = true;
        sentOk = send_all_timed(clientSession.clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                                &sendPathStats.headerCallCount) &&
                 send_all_timed(clientSession.clientSock, au.bytes.data(), au.bytes.size(), &sendPathStats.payloadUs,
                               &sendPathStats.payloadCallCount);
      } else {
        if (!sender.udpPeerReady) {
          ++sender.udpTxNoPeer;
          sentOk = false;
        } else {
          EncodedSendItem item;
          item.keyFrame = (hdr.flags & 1u) != 0;
          item.frameIntervalUs = encoder.activeFrameIntervalUs;
          item.udpHdr.magic = remote60::native_poc::kMagic;
          item.udpHdr.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
          item.udpHdr.size = static_cast<uint16_t>(sizeof(item.udpHdr));
          item.udpHdr.seq = hdr.seq;
          item.udpHdr.codec = static_cast<uint16_t>(UdpCodec::H264);
          item.udpHdr.flags = (hdr.flags & 1u) ? 0x1u : 0u;
          item.udpHdr.width = hdr.width;
          item.udpHdr.height = hdr.height;
          item.udpHdr.stride = 0;
          item.udpHdr.payloadSize = hdr.payloadSize;
          item.udpHdr.streamGeneration = hdr.streamGeneration;
          item.udpHdr.captureQpcUs = hdr.captureQpcUs;
          item.udpHdr.encodeStartQpcUs = hdr.encodeStartQpcUs;
          item.udpHdr.encodeEndQpcUs = hdr.encodeEndQpcUs;
          item.udpHdr.sendQpcUs = hdr.sendQpcUs;  // sender restamps at wire time
          item.bytes = std::move(au.bytes);
          {
            std::lock_guard<std::mutex> lk(sender.mu);
            // Stamp under the same lock the rollover bumps the epoch under, so the stamp is
            // consistent with the queue-clear: a delta stamped just after a rollover carries the
            // new epoch (and rides the fresh barrier); one stamped just before is dropped at
            // dequeue. This is also how the static bootstrap IDR gets tagged for the new epoch --
            // it flows through this same enqueue path and needs no special case.
            item.mediaEpoch = sender.mediaSessionEpoch.load(std::memory_order_acquire);
            item.enqueueUs = qpc_now_us();  // AU handed to sender; sender derives queueWaitUs
            if (item.keyFrame) {
              // A new IDR makes every queued frame irrelevant and re-anchors the stream. This is
              // also the barrier-open point: a real (or bootstrap) key AU for the current epoch
              // clears sender.waitingForKey so deltas may flow again.
              sender.dropCount.fetch_add(sender.queue.size(), std::memory_order_relaxed);
              sender.heldFrames += sender.queue.size();
              sender.sentFrames -= std::min<uint64_t>(sender.sentFrames, sender.queue.size());
              sender.queue.clear();
              sender.waitingForKey = false;
              if (sender.firstKeyEnqueuedUs == 0) sender.firstKeyEnqueuedUs = sendStartUs;
              sender.queue.push_back(std::move(item));
              enqueuedForSend = true;
            } else if (sender.waitingForKey) {
              // This delta references dropped frames; sending it would decode into
              // block garbage. Hold everything until the forced keyframe arrives.
              ++sender.nonKeyAuWhileWaiting;
              sender.dropCount.fetch_add(1, std::memory_order_relaxed);
              sender.requestKey.store(true, std::memory_order_release);
            } else if (senderBacklogged || sender.queue.size() >= kSenderQueueMaxFrames) {
              // Backlogged: drop the stale frames AND this delta -- it references what
              // was just dropped -- then resync with a fresh IDR.
              sender.dropCount.fetch_add(sender.queue.size() + 1, std::memory_order_relaxed);
              // Frames already counted as sent are being erased here; move them to the held
              // tally so the reported wire rate does not include what never left.
              sender.heldFrames += sender.queue.size();
              sender.sentFrames -= std::min<uint64_t>(sender.sentFrames, sender.queue.size());
              sender.queue.clear();
              sender.waitingForKey = true;
              sender.requestKey.store(true, std::memory_order_release);
            } else {
              sender.queue.push_back(std::move(item));
              enqueuedForSend = true;
            }
          }
          if (enqueuedForSend) sender.cv.notify_one();
          // Handing the frame off succeeded even when the queue policy discarded it; this
          // flag means "no transport failure", and clearing it here would tear the session
          // down. Whether the frame really went out is tracked by enqueuedForSend below.
          sentOk = true;
        }
      }
      const uint64_t sendDoneUs = qpc_now_us();
      const uint64_t sendDurUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
      const uint64_t sendCallCount = sendPathStats.headerCallCount + sendPathStats.payloadCallCount;
      if (sentOk) {
        stats.lastSendStartUs = sendStartUs;
        capture.LogFirstSentGeneration(res, stats, 
            transport == VideoTransport::Tcp ? "h264-tcp" : "h264-udp",
            streamGeneration, sendStartUs, hdr.captureQpcUs, hdr.width, hdr.height);
        if (kick.selectionFirstKeyframePendingGeneration != 0 &&
            streamGeneration == kick.selectionFirstKeyframePendingGeneration &&
            (hdr.flags & 1u) != 0) {
          std::cout << "[native-video-host] selection first keyframe sent streamGen="
                    << streamGeneration
                    << " captureQpcUs=" << hdr.captureQpcUs
                    << " sendQpcUs=" << hdr.sendQpcUs
                    << " key=1"
                    << "\n";
          kick.selectionFirstKeyframePendingGeneration = 0;
          kick.selectionFirstKeyframeDropCount = 0;
        }
        // UDP tx counters are owned by the sender thread now; nothing to count here.
        if (!servedBootstrap && frameGating.enabled && enqueuedForSend && payload &&
            !payload->empty()) {
          frameGating.lastSentUs = sendStartUs;
          frameGating.refPayload = payload;
          frameGating.refW = w;
          frameGating.refH = h;
          frameGating.refStride = stride;
        }
      }
      if (!sentOk) {
        if (transport == VideoTransport::Udp) {
          ++sender.udpTxFail;
          if (args.seconds == 0) {
            sessionReconnectTriggered = true;
            break;
          }
        } else if (reconnect_tcp_data_session(hx, "h264_send_fail")) {
          sessionReconnectTriggered = true;
          break;
        }
        sendFailed = true;
        break;
      }

      // A frame the sender queue discarded never reaches the wire. Counting it kept fps and
      // bitrate reporting a healthy stream straight through a cutout, which is precisely the
      // window that is visible to the user as a freeze -- so count only what was handed on.
      if (transport == VideoTransport::Udp && !enqueuedForSend) {
        ++sender.heldFrames;
        continue;
      }
      // A trailing-edge kick is a single sparse frame; keep it out of the fps/bitrate and ABR
      // evidence (it is counted separately as kick.count). It still consumes the forced
      // keyframe below so the normal path does not re-force one on the next real frame.
      if (!servedBootstrap) {
        ++sender.sentFrames;
        ++encoder.encodedFrames;
        sender.sentBytes += hdr.payloadSize;
        if (!countedRawForInput) {
          stats.rawEquivalentBytes +=
              static_cast<uint64_t>(encoder.activeEncodeW) * static_cast<uint64_t>(encoder.activeEncodeH) * 3 / 2;
          countedRawForInput = true;
        }
      }
      if ((hdr.flags & 1u) != 0) {
        encoder.forceKeyNext = false;
        encoder.forceKeySubmittedAtUs = 0;
      }

      if (args.traceEvery > 0 && (hdr.seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || stats.tracePrinted < args.traceMax)) {
        ++stats.tracePrinted;
        const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
        const uint64_t encQueueUs =
            (encodeStartUs >= static_cast<uint64_t>(auCaptureUs))
                ? (encodeStartUs - static_cast<uint64_t>(auCaptureUs))
                : 0;
        const uint64_t encQueueAlignedUs = (encodeStartUs >= encodeInputUs) ? (encodeStartUs - encodeInputUs) : 0;
        const uint64_t auTsFromOutput = au.sampleTimeFromOutput ? 1ull : 0ull;
        const uint64_t auTsSkewUs = (captureToAuSignedDeltaUs >= 0) ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                                                   : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
        const uint64_t encUs = (encodeSpanUs >= nv12Us) ? (encodeSpanUs - nv12Us) : 0;
        const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
        const char* encBackendName = encoder.codec.backend_name();
        const uint64_t encApiPathCode = encoder_api_path_code(encBackendName);
        const uint64_t encApiHw = encoder.codec.using_hardware() ? 1ull : 0ull;
        const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
            queueWaitUs, queueToEncodeUs, preEncodePrepUs, scaleUs, nv12Us, encUs, queueToSendUs,
            sendDurUs, sendIntervalErrUs);
        std::cout << "[native-video-host][trace] seq=" << hdr.seq
                  << " captureUs=" << hdr.captureQpcUs
                  << " encodeStartUs=" << hdr.encodeStartQpcUs
                  << " encodeEndUs=" << hdr.encodeEndQpcUs
                  << " sendUs=" << hdr.sendQpcUs
                  << " bottleneckStageCode=" << bottleneck.code
                  << " bottleneckStageUs=" << bottleneck.us
                  << " bottleneckStageName=" << bottleneck.name
                  << " c2eUs=" << c2eUs
                  << " captureToCallbackUs=" << captureToCallbackUs
                  << " callbackIntervalUs=" << callbackIntervalUs
                  << " captureIntervalUs=" << captureIntervalUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " captureD3DWaitUs=" << captureD3DWaitUs
                  << " captureCopyMapUs=" << captureCopyMapUs
                  << " captureMemcpyUs=" << captureMemcpyUs
                  << " captureUnmapWaitUs=" << captureUnmapWaitUs
                  << " captureUnmapUs=" << captureUnmapUs
                  << " selectWaitUs=" << frameAgeAtSelectUs
                   << " queueSelectWaitUs=" << queueSelectWaitUs
                   << " queueGapFrames=" << queueGapFrames
                   << " encQueueUs=" << encQueueUs
                   << " encQueueAlignedUs=" << encQueueAlignedUs
                   << " captureToAuSkewUs=" << captureToAuSkewUs
                   << " captureToAuTimelineDeltaUs="
                   << (captureToAuTimelineDeltaUs >= 0 ? captureToAuTimelineDeltaUs : 0 - captureToAuTimelineDeltaUs)
                    << " captureToAuTimelineSkewUs=" << captureToAuTimelineSkewUs
                    << " auTsFromOutput=" << auTsFromOutput
                    << " auTsSkewUs=" << auTsSkewUs
                    << " captureTimelineOriginUs=" << capture.timelineOriginUs
                   << " auTimelineOriginUs=" << encoder.auTimelineOriginUs
                   << " captureTimelineRelativeUs=" << captureTimelineRelativeUs
                   << " auTimelineRelativeUs=" << auTimelineRelativeUs
                    << " frameCaptureUs=" << captureStampUs
                    << " captureToAuUs=" << captureToAuUs
                    << " auCaptureUs=" << static_cast<uint64_t>(auCaptureUs)
                    << " encodeInputUs=" << encodeInputUs
                    << " captureToQueueUs=" << captureToQueueUs
                   << " queueWaitUs=" << queueWaitUs
                   << " queueWaitReason=" << queueWaitReason
                   << " queueToEncodeUs=" << queueToEncodeUs
                   << " queueToSendUs=" << queueToSendUs
                   << " sendIntervalUs=" << sendIntervalUs
                   << " sendIntervalErrUs=" << sendIntervalErrUs
                   << " preEncodePrepUs=" << preEncodePrepUs
                   << " scaleUs=" << scaleUs
                   << " scaleD3DWaitUs=" << scaleReadbackTiming.d3dWaitUs
                   << " scaleCopyMapUs=" << scaleReadbackTiming.copyMapUs
                   << " scaleMemcpyUs=" << scaleReadbackTiming.memcpyUs
                   << " scaleUnmapWaitUs=" << scaleReadbackTiming.unmapWaitUs
                   << " scaleUnmapUs=" << scaleReadbackTiming.unmapUs
                   << " nv12Us=" << nv12Us
                   << " sendWaitUs=" << sendWaitUs
                   << " sendToEncodeUs=" << sendToEncodeUs
                   << " tickWaitUs=" << tickWaitUs
                   << " queueDepth=" << queueDepthAtPop
                  << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
                  << " sendCallCount=" << sendCallCount
                  << " sendHeaderUs=" << sendPathStats.headerUs
                  << " sendPayloadUs=" << sendPathStats.payloadUs
                  << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                  << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                  << " sendChunkCount=" << sendPathStats.payloadChunkCount
                  << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                  << " sendStartUs=" << sendStartUs
                  << " sendDoneUs=" << sendDoneUs
                  << " sendDurUs=" << sendDurUs
                  << " cb2eUs=" << callbackToEncodeStartUs
                  << " capAgeUs=" << captureAgeAtCallbackUs
                  << " encUs=" << encUs
                  << " e2sUs=" << e2sUs
                  << " encApiPathCode=" << encApiPathCode
                  << " encApiHw=" << encApiHw
                  << " encApiInputUs=" << encodeStats.processInputUs
                  << " encApiDrainUs=" << encodeStats.processOutputDrainUs
                  << " encApiNotAcceptingCount=" << encodeStats.processInputNotAcceptingCount
                  << " encApiNeedMoreInputCount=" << encodeStats.processOutputNeedMoreInputCount
                  << " encApiStreamChangeCount=" << encodeStats.processOutputStreamChangeCount
                  << " encApiOutputErrorCount=" << encodeStats.processOutputErrorCount
                  << " encApiAsyncEnabled=" << encodeStats.asyncEnabled
                  << " encApiAsyncPollCount=" << encodeStats.asyncPollCount
                  << " encApiAsyncNoEventCount=" << encodeStats.asyncPollNoEventCount
                  << " encApiAsyncNeedInputCount=" << encodeStats.asyncPollNeedInputCount
                  << " encApiAsyncHaveOutputCount=" << encodeStats.asyncPollHaveOutputCount
                  << " payloadBytes=" << hdr.payloadSize
                  << " key=" << ((hdr.flags & 1u) ? 1 : 0)
                  << "\n";
      }
      const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
      const uint64_t encQueueUs =
          (encodeStartUs >= static_cast<uint64_t>(auCaptureUs)) ? (encodeStartUs - static_cast<uint64_t>(auCaptureUs)) : 0;
      const uint64_t encQueueAlignedUs = (encodeStartUs >= encodeInputUs) ? (encodeStartUs - encodeInputUs) : 0;
      const uint64_t auTsFromOutput = au.sampleTimeFromOutput ? 1ull : 0ull;
      const uint64_t auTsSkewUs = (captureToAuSignedDeltaUs >= 0) ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                                                 : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
      const uint64_t encUs = (encodeSpanUs >= nv12Us) ? (encodeSpanUs - nv12Us) : 0;
      const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
      const uint64_t pipeUs = (hdr.sendQpcUs >= hdr.captureQpcUs) ? (hdr.sendQpcUs - hdr.captureQpcUs) : 0;
      const char* encBackendName = encoder.codec.backend_name();
      const uint64_t encApiPathCode = encoder_api_path_code(encBackendName);
      const uint64_t encApiHw = encoder.codec.using_hardware() ? 1ull : 0ull;
      const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
          queueWaitUs, queueToEncodeUs, preEncodePrepUs, scaleUs, nv12Us, encUs, queueToSendUs,
          sendDurUs, sendIntervalErrUs);
      if (pipeUs >= kHostUserFeedbackWarnUs &&
          (hdr.sendQpcUs >= lastUserFeedbackUs + kHostUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
      std::cout << "[native-video-host][user-feedback] seq=" << hdr.seq
                << " codec=" << "h264"
                << " pipeUs=" << pipeUs
                << " bottleneckStageCode=" << bottleneck.code
                << " bottleneckStageUs=" << bottleneck.us
                << " bottleneckStageName=" << bottleneck.name
                << " captureToCallbackUs=" << captureToCallbackUs
                  << " callbackIntervalUs=" << callbackIntervalUs
                  << " captureIntervalUs=" << captureIntervalUs
                  << " selectWaitUs=" << frameAgeAtSelectUs
                  << " queueSelectWaitUs=" << queueSelectWaitUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " captureD3DWaitUs=" << captureD3DWaitUs
                  << " captureCopyMapUs=" << captureCopyMapUs
                  << " captureMemcpyUs=" << captureMemcpyUs
                  << " captureUnmapWaitUs=" << captureUnmapWaitUs
                  << " captureUnmapUs=" << captureUnmapUs
                  << " captureToQueueUs=" << captureToQueueUs
                 << " queueWaitUs=" << queueWaitUs
                 << " queueWaitReason=" << queueWaitReason
                   << " queueGapFrames=" << queueGapFrames
                   << " queueDepth=" << queueDepthAtPop
                  << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
                  << " queueToEncodeUs=" << queueToEncodeUs
                  << " queueToSendUs=" << queueToSendUs
                  << " sendIntervalUs=" << sendIntervalUs
                  << " sendIntervalErrUs=" << sendIntervalErrUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " sendWaitUs=" << sendWaitUs
                  << " sendToEncodeUs=" << sendToEncodeUs
                   << " tickWaitUs=" << tickWaitUs
                   << " preEncodePrepUs=" << preEncodePrepUs
                   << " scaleUs=" << scaleUs
                   << " scaleD3DWaitUs=" << scaleReadbackTiming.d3dWaitUs
                   << " scaleCopyMapUs=" << scaleReadbackTiming.copyMapUs
                   << " scaleMemcpyUs=" << scaleReadbackTiming.memcpyUs
                   << " scaleUnmapWaitUs=" << scaleReadbackTiming.unmapWaitUs
                   << " scaleUnmapUs=" << scaleReadbackTiming.unmapUs
                   << " nv12Us=" << nv12Us
                   << " c2eUs=" << c2eUs
                    << " encQueueUs=" << encQueueUs
                   << " encQueueAlignedUs=" << encQueueAlignedUs
                    << " captureToAuSkewUs=" << captureToAuSkewUs
                    << " captureToAuTimelineSkewUs=" << captureToAuTimelineSkewUs
                    << " auTsFromOutput=" << auTsFromOutput
                    << " auTsSkewUs=" << auTsSkewUs
                    << " captureToAuTimelineDeltaUs="
                    << (captureToAuTimelineDeltaUs >= 0 ? captureToAuTimelineDeltaUs : 0 - captureToAuTimelineDeltaUs)
                    << " captureTimelineOriginUs=" << capture.timelineOriginUs
                    << " auTimelineOriginUs=" << encoder.auTimelineOriginUs
                    << " captureTimelineRelativeUs=" << captureTimelineRelativeUs
                    << " auTimelineRelativeUs=" << auTimelineRelativeUs
                    << " frameCaptureUs=" << captureStampUs
                    << " captureToAuUs=" << captureToAuUs
                   << " auCaptureUs=" << static_cast<uint64_t>(auCaptureUs)
                   << " encodeInputUs=" << encodeInputUs
                 << " cb2eUs=" << callbackToEncodeStartUs
                 << " cb2sUs=" << callbackToSendStartUs
                  << " sendCallCount=" << sendCallCount
                  << " sendHeaderUs=" << sendPathStats.headerUs
                  << " sendPayloadUs=" << sendPathStats.payloadUs
                  << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                  << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                  << " sendChunkCount=" << sendPathStats.payloadChunkCount
                  << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                  << " sendStartUs=" << sendStartUs
                  << " sendDoneUs=" << sendDoneUs
                  << " sendDurUs=" << sendDurUs
                  << " capAgeUs=" << captureAgeAtCallbackUs
                  << " encUs=" << encUs
                  << " e2sUs=" << e2sUs
                  << " encApiPathCode=" << encApiPathCode
                  << " encApiHw=" << encApiHw
                  << " encApiInputUs=" << encodeStats.processInputUs
                  << " encApiDrainUs=" << encodeStats.processOutputDrainUs
                  << " encApiNotAcceptingCount=" << encodeStats.processInputNotAcceptingCount
                  << " encApiNeedMoreInputCount=" << encodeStats.processOutputNeedMoreInputCount
                  << " encApiStreamChangeCount=" << encodeStats.processOutputStreamChangeCount
                  << " encApiOutputErrorCount=" << encodeStats.processOutputErrorCount
                  << " encApiAsyncEnabled=" << encodeStats.asyncEnabled
                  << " encApiAsyncPollCount=" << encodeStats.asyncPollCount
                  << " encApiAsyncNoEventCount=" << encodeStats.asyncPollNoEventCount
                  << " encApiAsyncNeedInputCount=" << encodeStats.asyncPollNeedInputCount
                  << " encApiAsyncHaveOutputCount=" << encodeStats.asyncPollHaveOutputCount
                  << " payloadBytes=" << hdr.payloadSize
                  << " key=" << ((hdr.flags & 1u) ? 1 : 0)
                  << "\n";
        lastUserFeedbackUs = hdr.sendQpcUs;
      }
    }

    if (encoderResetTriggered || sessionReconnectTriggered) {
      return Flow::Continue;
    }
    if (sendFailed) {
      std::cout << "[native-video-host] client disconnected\n";
      return Flow::Break;
    }
  }

  return Flow::Next;
}

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
