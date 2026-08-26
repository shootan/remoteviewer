// Main-loop helpers: capture restart, cursor forwarder, TCP data reconnect, window selection.
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

}  // namespace remote60::native_poc
