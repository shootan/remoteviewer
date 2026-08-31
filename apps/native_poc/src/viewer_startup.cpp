// The viewer's startup sequence, one function per step, in the order main() calls them; each step
// is the corresponding block of the former main() verbatim, its locals now ViewerContext members.
// A step that can fail returns the exit code main() used to return there (0 = go on).
// (viewer split refactor Phase 2-10 / 3)

#include "viewer_startup.hpp"

#include <iostream>
#include <vector>

#include "viewer_env_util.hpp"
#include "viewer_globals.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_picker.hpp"
#include "viewer_window_proc.hpp"

namespace remote60::native_poc::viewer {

void apply_latency_priority() {
  // Decoder/present deadlines should not lose their timeslice to ordinary background work.
  // Keep this reversible for diagnostics and battery-sensitive deployments.
  if (!env_truthy("REMOTE60_NATIVE_NORMAL_PRIORITY")) {
    const BOOL processPriorityOk =
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    const BOOL threadPriorityOk =
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    std::cout << "[native-video-client] latency-priority processAboveNormal="
              << (processPriorityOk ? 1 : 0)
              << " mainThreadAboveNormal=" << (threadPriorityOk ? 1 : 0) << "\n";
  }
}

void apply_dpi_awareness() {
  // Without this the OS bitmap-stretches the whole window on a scaled display, which blurs
  // both the panel text and the decoded video. Must run before any window is created.
  if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
  }
}

void load_config(ViewerContext& ctx, int argc, char** argv) {
  ctx.args = parse_args(argc, argv);
  gPresent.traceEvery = ctx.args.traceEvery;
  gPresent.traceMax = ctx.args.traceMax;
  gPresent.presentFrameIntervalUs = static_cast<uint32_t>(std::max<uint64_t>(
      1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, ctx.args.fpsHint))));
  // Paced playout (F-11 / P3), opt-in. The clock is seeded from the fps hint and re-measures the
  // sender's real cadence from capture timestamps as frames arrive.
  gFrameBuf.pacedPlayout = env_truthy("REMOTE60_NATIVE_PACED_PLAYOUT");
  gFrameBuf.playout.SetTargetFrameIntervalUs(gPresent.presentFrameIntervalUs);
  if (gFrameBuf.pacedPlayout) {
    std::cout << "[native-video-client] paced playout enabled targetUs=" << gPresent.presentFrameIntervalUs
              << " leadUs=" << VideoPlayoutClock::LeadForStepUs(gPresent.presentFrameIntervalUs) << "\n";
  }
  const uint64_t keyframeReqMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_MIN_INTERVAL_US",
      static_cast<uint32_t>(kKeyframeRequestMinIntervalUsDefault), 10000, 1000000);
  const uint64_t keyframeReqTokenRefillUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_REFILL_US",
      static_cast<uint32_t>(kKeyframeRequestTokenRefillUsDefault), 10000, 2000000);
  const uint32_t keyframeReqTokenCapacity = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_CAPACITY",
      kKeyframeRequestTokenCapacityDefault, 1, 16);
  gControl.keyframeRequests.Configure(keyframeReqMinIntervalUs, keyframeReqTokenRefillUs, keyframeReqTokenCapacity);
  ctx.gate.catchupReenterMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_CATCHUP_REENTER_MIN_INTERVAL_US",
      static_cast<uint32_t>(kCatchupReenterMinIntervalUsDefault), 100000, 3000000);
  ctx.gate.staleCaptureDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_STALE_CAPTURE_DROP_US",
      static_cast<uint32_t>(kStaleCaptureDropUs), 1000, 2000000);
  ctx.gate.congestionRecoverMinUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVER_MIN_US",
      static_cast<uint32_t>(kCongestionRecoverMinUsDefault), 50000, 5000000);
  ctx.gate.congestionRecoveryTimeoutUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVERY_TIMEOUT_US",
      static_cast<uint32_t>(kCongestionRecoveryTimeoutUsDefault), 100000, 10000000);
  // Congestion-entry thresholds (F-18). Tunable so a machine where local CPU contention alone
  // trips the trigger can be measured and adjusted without a rebuild.
  ctx.gate.decodeQueueLagDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_DECODE_QUEUE_LAG_US",
      static_cast<uint32_t>(kDecodeQueueLagDropUs), 50000, 5000000);
  ctx.gate.catchupLagDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_STREAM_LAG_US",
      static_cast<uint32_t>(kCatchupLagDropUs), 50000, 5000000);
  ctx.gate.denseArrivalMaxGapUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_DENSE_ARRIVAL_US",
      static_cast<uint32_t>(kDenseArrivalMaxGapUsDefault), 10000, 2000000);
  ctx.gate.lagTriggerStreakMin = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_TRIGGER_STREAK", kLagTriggerStreakMinDefault, 1, 60);
  ctx.udpSimDropPm = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_PM", 0, 0, 1000);
  ctx.udpSimDropSeed = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_SEED", 0, 0, 0x7fffffffu);
  gControl.keyframeRequests.Reset();
}

int validate_codec_transport(ViewerContext& ctx) {
  ctx.dec.useRaw = (ctx.args.codec == "raw");
  ctx.dec.useH264 = (ctx.args.codec == "h264");
  const bool encodedExperimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  if (!ctx.dec.useRaw && !ctx.dec.useH264) {
    std::cerr << "[native-video-client] unsupported codec: " << ctx.args.codec << " (supported: raw,h264)\n";
    return 10;
  }
  if (ctx.dec.useH264 && !encodedExperimentEnabled) {
    std::cerr << "[native-video-client] unsupported codec: " << ctx.args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 10;
  }
  std::string effectiveTransport = ctx.args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = ctx.dec.useH264 ? "udp" : "tcp";
  }
  if (!parse_video_transport(effectiveTransport, &ctx.dec.transport)) {
    std::cerr << "[native-video-client] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 12;
  }
  if (ctx.dec.transport == VideoTransport::Udp && ctx.dec.useRaw) {
    std::cerr << "[native-video-client] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 13;
  }
  return 0;
}

void apply_initial_state(ViewerContext& ctx) {
  gControl.runtimeTune.Reset(ctx.args.runtimeBitrate, ctx.args.runtimeKeyint, ctx.args.runtimeFps);
  gSession.requestedMonitorId = ctx.args.monitorId;
  gControl.connected.store(false, std::memory_order_relaxed);
  // How the session opens. The explicit flag wins; with no flag we fall back to the legacy env
  // var so the automation probes (which all set REMOTE60_NATIVE_START_STREAM_VIEW=1) are
  // unaffected. "targets" is the product flow: open on the picker and stream only after a pick.
  if (ctx.args.initialView == "targets" || ctx.args.initialView == "picker") {
    ctx.startInStreamView = false;
  } else if (ctx.args.initialView == "stream") {
    ctx.startInStreamView = true;
  } else {
    ctx.startInStreamView = env_truthy("REMOTE60_NATIVE_START_STREAM_VIEW");
  }
  ctx.startInPicker = !ctx.startInStreamView;
  gPicker.visible.store(ctx.startInPicker, std::memory_order_relaxed);
  clear_pc_target_selection();
  // No target has taken effect yet. 0 disables the persistent generation filter, so the legacy
  // stream-view start and the pre-first-pick window accept whatever the host sends, as before.
  gSel.activeStreamGeneration.store(0, std::memory_order_release);
  gSel.revealPosted.store(false, std::memory_order_release);
  // Picker-first sessions must not keep the host's default stream running under the picker: the
  // request rides the scheduler (StreamState before WindowList/Select) and is queued before the
  // control link exists, so it goes out first thing once connected. An initial default-desktop
  // frame that slips through before the stream stops is dropped by the receive-path gate rather
  // than painted, and no flip swap chain is created until the user's pick produces a real frame.
  if (ctx.startInPicker) {
    gControl.streamState.Request(false);
  }
  gControl.captureModeRequests.Reset();
  gPicker.windowPanel.Reset();
  gInput.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  gInput.activeTouchPointerId.store(0, std::memory_order_relaxed);
  gInput.activeTouchDown.store(false, std::memory_order_relaxed);
}

int create_window_and_toolbar(ViewerContext& ctx) {
  if (!create_window()) {
    std::cerr << "[native-video-client] window create failed\n";
    return 2;
  }

  {
    remote60::native_poc::SessionToolbarCallbacks toolbarCallbacks;
    // Re-enabled: the reason this was unset -- entering the picker mid-session looked like a
    // freeze -- is fixed (the picker no longer stops the stream, and its repaint is composited).
    // With the invisible legacy top-left buttons removed, this is the ONLY road back to target
    // selection during a session, so it must exist.
    toolbarCallbacks.onTargets = [] {
      set_picker_visible_and_sync_stream(true);
      push_session_toolbar_state();
      if (gSession.hwnd) InvalidateRect(gSession.hwnd, nullptr, FALSE);
    };
    toolbarCallbacks.onMacro = [] {
      toggle_macro_window(gSession.hwnd);
      push_session_toolbar_state();
    };
    toolbarCallbacks.onMonitor = [](uint32_t monitorId) {
      gPicker.windowPanel.RequestMonitorSelect(monitorId);
    };
    remote60::native_poc::session_toolbar_create(gSession.hwnd, std::move(toolbarCallbacks));
    remote60::native_poc::session_toolbar_set_visible(ctx.startInStreamView);
    push_session_toolbar_state();
  }
  return 0;
}

int init_decoder(ViewerContext& ctx) {
  ctx.gate.waitForKeyFrame = ctx.dec.useH264;
  if (ctx.dec.useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-client] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      return 11;
    }
    ctx.dec.mfStarted = true;
    // Supplying AMD's decoder with an external DXGI device manager can enter atidxx64's
    // direct-surface path even when the caller later reads a CPU buffer. Keep the proven
    // system-memory decoder path as the safe default; the zero-copy experiment is an
    // explicit opt-in because affected drivers can TDR or access-violate in that path.
    const bool enableDxgiDecodeSurface =
        env_truthy("REMOTE60_NATIVE_DXGI_DECODE_SURFACE") &&
        !env_truthy("REMOTE60_NATIVE_DISABLE_DXGI_DECODE_SURFACE");
    if (enableDxgiDecodeSurface) {
      // Decode and paint share one D3D11 device so an opt-in hardware-decoder NV12 surface
      // can be sampled directly without a GPU->CPU copy and CPU->GPU upload.
      if (!gUi.nv12Renderer.ready) (void)gUi.nv12Renderer.init(gSession.hwnd);
      if (gUi.nv12Renderer.ready) {
        ctx.dec.d3dDevice = gUi.nv12Renderer.device;
        ctx.dec.d3dContext = gUi.nv12Renderer.context;
        (void)ctx.dec.decoder.set_d3d11_device(ctx.dec.d3dDevice.Get());
      } else {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        const HRESULT d3dHr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                D3D11_SDK_VERSION, &ctx.dec.d3dDevice, &fl,
                                                &ctx.dec.d3dContext);
        if (SUCCEEDED(d3dHr) && ctx.dec.d3dDevice) {
          (void)ctx.dec.decoder.set_d3d11_device(ctx.dec.d3dDevice.Get());
        }
      }
    }
  }
  return 0;
}

int open_media_socket(ViewerContext& ctx) {
  // Reaching the host through the directory replaces the address entirely: the socket comes back
  // already prepared (it is the one the directory observed, so the host is punching towards it)
  // and the capability that follows is what the host authorises the session against.
  ctx.resolvedArgs = ctx.args;
  if (!ctx.args.directoryUrl.empty()) {
    if (ctx.dec.transport != VideoTransport::Udp) {
      std::cerr << "[native-video-client] the directory path is udp only\n";
      if (ctx.dec.mfStarted) MFShutdown();
      return 3;
    }
    std::string directoryError;
    std::string sessionToken = ctx.args.directorySession;
    if (sessionToken.empty() &&
        !remote60::native_poc::directory_login(ctx.args.directoryUrl, ctx.args.directoryAccount,
                                               ctx.args.directoryPassword, &sessionToken,
                                               &directoryError)) {
      std::cerr << "[native-video-client] directory login failed: " << directoryError << "\n";
      if (ctx.dec.mfStarted) MFShutdown();
      return 3;
    }

    std::string hostId = ctx.args.directoryHostId;
    if (hostId.empty()) {
      std::vector<remote60::native_poc::DirectoryHostEntry> hosts;
      if (!remote60::native_poc::directory_list_hosts(ctx.args.directoryUrl, sessionToken, &hosts,
                                                      &directoryError)) {
        std::cerr << "[native-video-client] directory hosts failed: " << directoryError << "\n";
        if (ctx.dec.mfStarted) MFShutdown();
        return 3;
      }
      for (const auto& entry : hosts) {
        if (!ctx.args.directoryHostName.empty() && entry.hostName != ctx.args.directoryHostName) continue;
        // An offline host has no mapping to punch towards, so preferring an online one avoids a
        // four-second wait that was never going to succeed.
        if (hostId.empty() || entry.online) hostId = entry.hostId;
        if (entry.online) break;
      }
      if (hostId.empty()) {
        std::cerr << "[native-video-client] no host on this account"
                  << (ctx.args.directoryHostName.empty() ? "" : " named " + ctx.args.directoryHostName)
                  << "\n";
        if (ctx.dec.mfStarted) MFShutdown();
        return 3;
      }
    }

    remote60::native_poc::DirectorySessionRequest request{};
    request.url = ctx.args.directoryUrl;
    request.sessionToken = sessionToken;
    request.hostId = hostId;
    remote60::native_poc::DirectorySessionResult session{};
    if (!remote60::native_poc::directory_session_open(request, &session, &directoryError)) {
      std::cerr << "[native-video-client] directory connect failed: " << directoryError << "\n";
      if (ctx.dec.mfStarted) MFShutdown();
      return 3;
    }
    gSession.sock = session.socket;
    ctx.resolvedArgs.host = session.chosen.ip;
    ctx.resolvedArgs.port = session.chosen.port;
    // Control travels over the media socket on this path; a separate TCP port cannot survive
    // hole punching. Everything downstream reads resolvedArgs.controlPort, so this is what
    // actually routes control -- it used to be a write nobody read while the tunnel / TCP
    // branches consulted args.controlPort instead (harmless only because the shell never passed
    // --control-port on the directory path). (F-19.)
    ctx.resolvedArgs.controlPort = 0;
    ctx.directoryPunchToken = session.punchToken;
    gSession.relayPath.store(session.relay, std::memory_order_relaxed);
    push_session_toolbar_state();
    std::cout << "[native-video-client] directory chose " << session.chosen.ip << ":"
              << session.chosen.port << " ("
              << remote60::native_poc::candidate_kind_name(session.chosen.kind) << ")"
              << (session.answered ? "" : " [no answer, trying anyway]") << "\n";
  } else {
    gSession.sock = socket(AF_INET,
                   (ctx.dec.transport == VideoTransport::Udp) ? SOCK_DGRAM : SOCK_STREAM,
                   (ctx.dec.transport == VideoTransport::Udp) ? IPPROTO_UDP : IPPROTO_TCP);
  }
  if (gSession.sock == INVALID_SOCKET) {
    std::cerr << "[native-video-client] socket create failed\n";
    if (ctx.dec.mfStarted) MFShutdown();
    return 3;
  }
  return 0;
}

int connect_media_socket(ViewerContext& ctx) {
  if (ctx.dec.transport == VideoTransport::Tcp) {
    int noDelay = 1;
    setsockopt(gSession.sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  }
  if (ctx.dec.transport == VideoTransport::Udp) {
    if (ctx.args.tcpRecvBufKb == 0) {
      const int recvBuf = 1024 * 1024;
      (void)setsockopt(gSession.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    }
    if (ctx.args.tcpSendBufKb == 0) {
      const int sendBuf = 256 * 1024;
      (void)setsockopt(gSession.sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
    }
  }
  if (ctx.args.tcpRecvBufKb > 0) {
    const int recvBuf = static_cast<int>(ctx.args.tcpRecvBufKb * 1024u);
    setsockopt(gSession.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
  }
  if (ctx.args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(ctx.args.tcpSendBufKb * 1024u);
    setsockopt(gSession.sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ctx.resolvedArgs.port);
  if (inet_pton(AF_INET, ctx.resolvedArgs.host.c_str(), &addr.sin_addr) != 1) {
    std::cerr << "[native-video-client] invalid host " << ctx.resolvedArgs.host << "\n";
    closesocket(gSession.sock);
    gSession.sock = INVALID_SOCKET;
    if (ctx.dec.mfStarted) MFShutdown();
    return 4;
  }
  if (connect(gSession.sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "[native-video-client] connect failed " << ctx.resolvedArgs.host << ":" << ctx.resolvedArgs.port << "\n";
    closesocket(gSession.sock);
    gSession.sock = INVALID_SOCKET;
    if (ctx.dec.mfStarted) MFShutdown();
    return 5;
  }
  if (ctx.dec.transport == VideoTransport::Udp) {
    int timeoutMs = 200;
    (void)setsockopt(gSession.sock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    bool handshakeOk = false;
    for (int attempt = 0; attempt < 40 && !handshakeOk; ++attempt) {
      UdpHelloPacket hello{};
      hello.kind = static_cast<uint16_t>(UdpPacketKind::Hello);
      // The capability from /api/connect. Without it the host treats this as a plain LAN client
      // and refuses anything that needs authorisation -- secure-desktop input in particular.
      if (!ctx.directoryPunchToken.empty()) {
        std::snprintf(hello.authToken, sizeof(hello.authToken), "%s", ctx.directoryPunchToken.c_str());
      }
      const int sent = send(gSession.sock, reinterpret_cast<const char*>(&hello), sizeof(hello), 0);
      if (sent <= 0) {
        Sleep(50);
        continue;
      }
      UdpHelloPacket ack{};
      const int n = recv(gSession.sock, reinterpret_cast<char*>(&ack), sizeof(ack), 0);
      if (n >= static_cast<int>(sizeof(UdpHelloPacket)) &&
          ack.magic == remote60::native_poc::kMagic &&
          ack.kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) &&
          ack.version == remote60::native_poc::kUdpProtocolVersion &&
          (ack.features & remote60::native_poc::kUdpFeatureVideoFec) != 0) {
        handshakeOk = true;
        break;
      }
      Sleep(50);
    }
    timeoutMs = 0;
    (void)setsockopt(gSession.sock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    if (!handshakeOk) {
      std::cerr << "[native-video-client] udp handshake failed " << ctx.resolvedArgs.host << ":"
                << ctx.resolvedArgs.port << "\n";
      closesocket(gSession.sock);
      gSession.sock = INVALID_SOCKET;
      if (ctx.dec.mfStarted) MFShutdown();
      return 6;
    }
  }
  return 0;
}

void attach_control_tunnel_and_log(ViewerContext& ctx) {
  // No second port to dial means the directory path: control tunnels through the socket the
  // punch just opened. The send is bare because the socket is connected -- the same socket the
  // receive loop below reads, which is what makes the two directions one NAT mapping.
  if (ctx.resolvedArgs.controlPort == 0 && ctx.dec.transport == VideoTransport::Udp && gSession.sock != INVALID_SOCKET) {
    gControl.udpControl.Configure(
        [](const void* data, size_t len) -> bool {
          return send(gSession.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        remote60::native_poc::kUdpControlStreamClientToHost,
        remote60::native_poc::kUdpControlStreamHostToClient, ctx.args.udpMtu);
    gControl.overUdp.store(true, std::memory_order_release);
    // Without this the receive blocks forever on a link that has gone quiet, and the tick above
    // never runs -- which is the one moment recovery is needed.
    DWORD recvTimeoutMs = 200;
    setsockopt(gSession.sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs),
               sizeof(recvTimeoutMs));
    std::cout << "[native-video-client] control tunnelled over the media socket\n";
  }

  std::cout << "[native-video-client] connected host=" << ctx.resolvedArgs.host
            << " port=" << ctx.resolvedArgs.port
            << " transport=" << video_transport_name(ctx.dec.transport)
            << " codec=" << ctx.args.codec
            << " seconds=" << ctx.args.seconds << "\n";
  std::cout << "[native-video-client] keyframe-request-limiter minIntervalUs="
            << gControl.keyframeRequests.min_interval_us()
            << " tokenRefillUs=" << gControl.keyframeRequests.token_refill_us()
            << " tokenCapacity=" << gControl.keyframeRequests.token_capacity()
            << " catchupReenterMinUs=" << ctx.gate.catchupReenterMinIntervalUs
            << " staleCaptureDropUs=" << ctx.gate.staleCaptureDropUs
            << " congestionRecoverMinUs=" << ctx.gate.congestionRecoverMinUs
            << " congestionRecoveryTimeoutUs=" << ctx.gate.congestionRecoveryTimeoutUs
            << "\n";
  if (kInputPolicyForceBlock) {
    std::cout << "[native-video-client] input channel blocked by compile-time policy\n";
  }
  int effectiveRecvBuf = 0;
  int effectiveRecvBufLen = sizeof(effectiveRecvBuf);
  (void)getsockopt(gSession.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&effectiveRecvBuf), &effectiveRecvBufLen);
  int effectiveSendBuf = 0;
  int effectiveSendBufLen = sizeof(effectiveSendBuf);
  (void)getsockopt(gSession.sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
  std::cout << "[native-video-client] socket rcvbuf=" << effectiveRecvBuf
            << " sndbuf=" << effectiveSendBuf << " bytes\n";
}

void connect_control(ViewerContext& ctx) {
  ctx.control.emplace(ctx.args, ctx.startInPicker);
  // Two ways to reach the host's control protocol, and the session only ever has one of them.
  // A direct host answers on its own TCP port; a host behind NAT is reachable solely through
  // the punched media socket, and dialling a second port there connects to nothing.
  ctx.controlReady = gControl.overUdp.load(std::memory_order_acquire);
  if (ctx.resolvedArgs.controlPort > 0) {
    ctx.controlSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx.controlSock != INVALID_SOCKET) {
      int ctlNoDelay = 1;
      setsockopt(ctx.controlSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
      sockaddr_in ctlAddr{};
      ctlAddr.sin_family = AF_INET;
      ctlAddr.sin_port = htons(ctx.resolvedArgs.controlPort);
      if (inet_pton(AF_INET, ctx.args.host.c_str(), &ctlAddr.sin_addr) == 1 &&
          connect(ctx.controlSock, reinterpret_cast<const sockaddr*>(&ctlAddr), sizeof(ctlAddr)) == 0) {
        ctx.controlReady = true;
      }
    }
  }
  {
    const bool inputChannelEnabled =
        ctx.controlReady && ctx.args.enableInputChannel && !kInputPolicyForceBlock;
    gSession.inputEnabled = inputChannelEnabled;
    if (inputChannelEnabled) {
      // Clear any modifier the host is still holding from a previous session. A client that
      // lost focus while a modifier was down could not send its up, and that up-less state is
      // the host's real key state -- it survives the client closing and reopening, so
      // reconnecting is the only way to shake it loose, and only if the fresh client says so.
      for (const uint32_t vk : {static_cast<uint32_t>(VK_CONTROL), static_cast<uint32_t>(VK_LCONTROL),
                                static_cast<uint32_t>(VK_RCONTROL), static_cast<uint32_t>(VK_MENU),
                                static_cast<uint32_t>(VK_LMENU), static_cast<uint32_t>(VK_RMENU),
                                static_cast<uint32_t>(VK_SHIFT), static_cast<uint32_t>(VK_LSHIFT),
                                static_cast<uint32_t>(VK_RSHIFT), static_cast<uint32_t>(VK_LWIN),
                                static_cast<uint32_t>(VK_RWIN)}) {
        enqueue_input_event(6, 0, 0, 0, vk);
      }
    }
    gControl.scheduler.Reset(ctx.args.controlIntervalMs, qpc_now_us());
    if (ctx.controlReady) {
      ctx.control->controlSock = ctx.controlSock;
      // Published BEFORE the thread starts. Run() stores false on its first failed exchange, so
      // storing true after the spawn could land on top of that and leave "connected" stale for
      // the rest of the session. The window was milliseconds wide; it is now zero. (F-06.)
      gControl.connected.store(true, std::memory_order_relaxed);
      ctx.controlThread = std::thread([&ctx]() { ctx.control->Run(); });
    }
    if (ctx.controlReady) {
      gControl.runtimeTune.SetEnabled(ctx.dec.useH264);
      queue_window_list_request("window_list_request pending");
      if (ctx.dec.useH264 && (ctx.args.runtimeBitrate > 0 || ctx.args.runtimeKeyint > 0)) {
        gControl.runtimeTune.MarkDirty();
      }
      std::cout << "[native-video-client] control connected transport="
                << (gControl.overUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp")
                << " port=" << ctx.resolvedArgs.controlPort
                << " inputChannel=" << (inputChannelEnabled ? 1 : 0) << "\n";
    } else {
      if (ctx.controlSock != INVALID_SOCKET) {
        closesocket(ctx.controlSock);
        ctx.controlSock = INVALID_SOCKET;
      }
      gControl.connected.store(false, std::memory_order_relaxed);
      gControl.runtimeTune.SetEnabled(false);
      set_window_panel_status("control_connect_failed");
      std::cout << "[native-video-client] control unavailable port=" << ctx.resolvedArgs.controlPort << "\n";
    }
  }
}

void start_receiver(ViewerContext& ctx) {
  ctx.startUs = qpc_now_us();
  ctx.receiver.emplace(ctx.args, ctx.dec, ctx.gate, ctx.startUs, ctx.udpSimDropPm, ctx.udpSimDropSeed);
  ctx.recvThread = std::thread([&ctx]() { ctx.receiver->Run(); });
}

void run_message_pump(ViewerContext& ctx) {
  MSG msg{};
  while (gSession.running.load()) {
    bool hadMessage = false;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      hadMessage = true;
      if (msg.message == WM_QUIT) {
        gSession.running = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!gSession.running.load()) break;

    // The toolbar shows connection, input, path, frame rate and the monitor list, all of which
    // change on other threads. Refreshing it on a slow tick here beats a push at each of the
    // dozen places that move them, and it is a posted message either way.
    {
      const uint64_t nowUs = qpc_now_us();
      if (nowUs >= gSession.nextToolbarPushUs) {
        gSession.nextToolbarPushUs = nowUs + 500000ULL;
        push_session_toolbar_state();
      }
    }

    if (ctx.args.seconds > 0) {
      const uint64_t nowUs = qpc_now_us();
      if (nowUs >= ctx.startUs + static_cast<uint64_t>(ctx.args.seconds) * 1000000ULL) {
        gSession.running = false;
        break;
      }
    }

    if (!hadMessage) {
      Sleep(5);
    }
  }
}

}  // namespace remote60::native_poc::viewer
