#include "viewer_common.hpp"
#include "viewer_globals.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_args.hpp"
#include "viewer_decoder_backend.hpp"
#include "viewer_gdi_util.hpp"
#include "viewer_nv12_renderer.hpp"
#include "viewer_layout.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_picker.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_cursor_overlay.hpp"
#include "viewer_window_proc.hpp"
#include "viewer_recv_stats.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_decoder_state.hpp"
#include "viewer_video_receiver.hpp"

namespace remote60::native_poc::viewer {












































































































}  // namespace remote60::native_poc::viewer

using namespace remote60::native_poc::viewer;

int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);

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

  // Without this the OS bitmap-stretches the whole window on a scaled display, which blurs
  // both the panel text and the decoded video. Must run before any window is created.
  if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
  }

  const Args args = parse_args(argc, argv);
  DecoderState dec;
  gPresent.traceEvery = args.traceEvery;
  gPresent.traceMax = args.traceMax;
  gPresent.presentFrameIntervalUs = static_cast<uint32_t>(std::max<uint64_t>(
      1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, args.fpsHint))));
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
  FrameGateState gate;
  gate.catchupReenterMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_CATCHUP_REENTER_MIN_INTERVAL_US",
      static_cast<uint32_t>(kCatchupReenterMinIntervalUsDefault), 100000, 3000000);
  gate.staleCaptureDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_STALE_CAPTURE_DROP_US",
      static_cast<uint32_t>(kStaleCaptureDropUs), 1000, 2000000);
  gate.congestionRecoverMinUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVER_MIN_US",
      static_cast<uint32_t>(kCongestionRecoverMinUsDefault), 50000, 5000000);
  gate.congestionRecoveryTimeoutUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVERY_TIMEOUT_US",
      static_cast<uint32_t>(kCongestionRecoveryTimeoutUsDefault), 100000, 10000000);
  const uint32_t udpSimDropPm = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_PM", 0, 0, 1000);
  const uint32_t udpSimDropSeed = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_SEED", 0, 0, 0x7fffffffu);
  gControl.keyframeRequests.Reset();

  dec.useRaw = (args.codec == "raw");
  dec.useH264 = (args.codec == "h264");
  const bool encodedExperimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  if (!dec.useRaw && !dec.useH264) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec << " (supported: raw,h264)\n";
    return 10;
  }
  if (dec.useH264 && !encodedExperimentEnabled) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 10;
  }
  std::string effectiveTransport = args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = dec.useH264 ? "udp" : "tcp";
  }
  if (!parse_video_transport(effectiveTransport, &dec.transport)) {
    std::cerr << "[native-video-client] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 12;
  }
  if (dec.transport == VideoTransport::Udp && dec.useRaw) {
    std::cerr << "[native-video-client] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 13;
  }

  gSession.overlayConfig.host = args.host;
  gSession.overlayConfig.port = args.port;
  gSession.overlayConfig.controlPort = args.controlPort;
  gSession.overlayConfig.transport = video_transport_name(dec.transport);
  gSession.overlayConfig.codec = args.codec;
  gSession.overlayConfig.fpsHint = args.fpsHint;
  gSession.overlayConfig.controlIntervalMs = args.controlIntervalMs;
  gSession.overlayConfig.tcpRecvBufKb = args.tcpRecvBufKb;
  gSession.overlayConfig.tcpSendBufKb = args.tcpSendBufKb;
  gSession.overlayConfig.udpMtu = args.udpMtu;
  gSession.overlayConfig.keyReqMinIntervalUs = gControl.keyframeRequests.min_interval_us();
  gSession.overlayConfig.keyReqTokenRefillUs = gControl.keyframeRequests.token_refill_us();
  gSession.overlayConfig.keyReqTokenCapacity = gControl.keyframeRequests.token_capacity();
  gSession.overlayConfig.udpSimDropPm = udpSimDropPm;
  gControl.runtimeTune.Reset(args.runtimeBitrate, args.runtimeKeyint, args.runtimeFps);
  gSession.requestedMonitorId = args.monitorId;
  gControl.connected.store(false, std::memory_order_relaxed);
  // How the session opens. The explicit flag wins; with no flag we fall back to the legacy env
  // var so the automation probes (which all set REMOTE60_NATIVE_START_STREAM_VIEW=1) are
  // unaffected. "targets" is the product flow: open on the picker and stream only after a pick.
  bool startInStreamView;
  if (args.initialView == "targets" || args.initialView == "picker") {
    startInStreamView = false;
  } else if (args.initialView == "stream") {
    startInStreamView = true;
  } else {
    startInStreamView = env_truthy("REMOTE60_NATIVE_START_STREAM_VIEW");
  }
  const bool startInPicker = !startInStreamView;
  gControl.captureOverviewMode.store(startInPicker, std::memory_order_relaxed);
  gPicker.visible.store(startInPicker, std::memory_order_relaxed);
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
  if (startInPicker) {
    gControl.streamState.Request(false);
  }
  gControl.captureModeRequests.Reset();
  gPicker.windowPanel.Reset();
  gInput.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  gInput.activeTouchPointerId.store(0, std::memory_order_relaxed);
  gInput.activeTouchDown.store(false, std::memory_order_relaxed);

  remote60::native_poc::WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-client] WSAStartup failed\n";
    return 1;
  }

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
    remote60::native_poc::session_toolbar_set_visible(startInStreamView);
    push_session_toolbar_state();
  }

  dec.waitForKeyFrame = dec.useH264;
  if (dec.useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-client] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      return 11;
    }
    dec.mfStarted = true;
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
        dec.d3dDevice = gUi.nv12Renderer.device;
        dec.d3dContext = gUi.nv12Renderer.context;
        (void)dec.decoder.set_d3d11_device(dec.d3dDevice.Get());
      } else {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        const HRESULT d3dHr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                D3D11_SDK_VERSION, &dec.d3dDevice, &fl,
                                                &dec.d3dContext);
        if (SUCCEEDED(d3dHr) && dec.d3dDevice) {
          (void)dec.decoder.set_d3d11_device(dec.d3dDevice.Get());
        }
      }
    }
  }

  // Reaching the host through the directory replaces the address entirely: the socket comes back
  // already prepared (it is the one the directory observed, so the host is punching towards it)
  // and the capability that follows is what the host authorises the session against.
  std::string directoryPunchToken;
  Args resolvedArgs = args;
  if (!args.directoryUrl.empty()) {
    if (dec.transport != VideoTransport::Udp) {
      std::cerr << "[native-video-client] the directory path is udp only\n";
      if (dec.mfStarted) MFShutdown();
      return 3;
    }
    std::string directoryError;
    std::string sessionToken = args.directorySession;
    if (sessionToken.empty() &&
        !remote60::native_poc::directory_login(args.directoryUrl, args.directoryAccount,
                                               args.directoryPassword, &sessionToken,
                                               &directoryError)) {
      std::cerr << "[native-video-client] directory login failed: " << directoryError << "\n";
      if (dec.mfStarted) MFShutdown();
      return 3;
    }

    std::string hostId = args.directoryHostId;
    if (hostId.empty()) {
      std::vector<remote60::native_poc::DirectoryHostEntry> hosts;
      if (!remote60::native_poc::directory_list_hosts(args.directoryUrl, sessionToken, &hosts,
                                                      &directoryError)) {
        std::cerr << "[native-video-client] directory hosts failed: " << directoryError << "\n";
        if (dec.mfStarted) MFShutdown();
        return 3;
      }
      for (const auto& entry : hosts) {
        if (!args.directoryHostName.empty() && entry.hostName != args.directoryHostName) continue;
        // An offline host has no mapping to punch towards, so preferring an online one avoids a
        // four-second wait that was never going to succeed.
        if (hostId.empty() || entry.online) hostId = entry.hostId;
        if (entry.online) break;
      }
      if (hostId.empty()) {
        std::cerr << "[native-video-client] no host on this account"
                  << (args.directoryHostName.empty() ? "" : " named " + args.directoryHostName)
                  << "\n";
        if (dec.mfStarted) MFShutdown();
        return 3;
      }
    }

    remote60::native_poc::DirectorySessionRequest request{};
    request.url = args.directoryUrl;
    request.sessionToken = sessionToken;
    request.hostId = hostId;
    remote60::native_poc::DirectorySessionResult session{};
    if (!remote60::native_poc::directory_session_open(request, &session, &directoryError)) {
      std::cerr << "[native-video-client] directory connect failed: " << directoryError << "\n";
      if (dec.mfStarted) MFShutdown();
      return 3;
    }
    gSession.sock = session.socket;
    resolvedArgs.host = session.chosen.ip;
    resolvedArgs.port = session.chosen.port;
    // Control travels over the media socket on this path; a separate TCP port cannot survive
    // hole punching.
    resolvedArgs.controlPort = 0;
    directoryPunchToken = session.punchToken;
    gSession.relayPath.store(session.relay, std::memory_order_relaxed);
    push_session_toolbar_state();
    std::cout << "[native-video-client] directory chose " << session.chosen.ip << ":"
              << session.chosen.port << " ("
              << remote60::native_poc::candidate_kind_name(session.chosen.kind) << ")"
              << (session.answered ? "" : " [no answer, trying anyway]") << "\n";
  } else {
    gSession.sock = socket(AF_INET,
                   (dec.transport == VideoTransport::Udp) ? SOCK_DGRAM : SOCK_STREAM,
                   (dec.transport == VideoTransport::Udp) ? IPPROTO_UDP : IPPROTO_TCP);
  }
  if (gSession.sock == INVALID_SOCKET) {
    std::cerr << "[native-video-client] socket create failed\n";
    if (dec.mfStarted) MFShutdown();
    return 3;
  }

  if (dec.transport == VideoTransport::Tcp) {
    int noDelay = 1;
    setsockopt(gSession.sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  }
  if (dec.transport == VideoTransport::Udp) {
    if (args.tcpRecvBufKb == 0) {
      const int recvBuf = 1024 * 1024;
      (void)setsockopt(gSession.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    }
    if (args.tcpSendBufKb == 0) {
      const int sendBuf = 256 * 1024;
      (void)setsockopt(gSession.sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
    }
  }
  if (args.tcpRecvBufKb > 0) {
    const int recvBuf = static_cast<int>(args.tcpRecvBufKb * 1024u);
    setsockopt(gSession.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
  }
  if (args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
    setsockopt(gSession.sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(resolvedArgs.port);
  if (inet_pton(AF_INET, resolvedArgs.host.c_str(), &addr.sin_addr) != 1) {
    std::cerr << "[native-video-client] invalid host " << resolvedArgs.host << "\n";
    closesocket(gSession.sock);
    gSession.sock = INVALID_SOCKET;
    if (dec.mfStarted) MFShutdown();
    return 4;
  }
  if (connect(gSession.sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "[native-video-client] connect failed " << resolvedArgs.host << ":" << resolvedArgs.port << "\n";
    closesocket(gSession.sock);
    gSession.sock = INVALID_SOCKET;
    if (dec.mfStarted) MFShutdown();
    return 5;
  }
  if (dec.transport == VideoTransport::Udp) {
    int timeoutMs = 200;
    (void)setsockopt(gSession.sock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    bool handshakeOk = false;
    for (int attempt = 0; attempt < 40 && !handshakeOk; ++attempt) {
      UdpHelloPacket hello{};
      hello.kind = static_cast<uint16_t>(UdpPacketKind::Hello);
      // The capability from /api/connect. Without it the host treats this as a plain LAN client
      // and refuses anything that needs authorisation -- secure-desktop input in particular.
      if (!directoryPunchToken.empty()) {
        std::snprintf(hello.authToken, sizeof(hello.authToken), "%s", directoryPunchToken.c_str());
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
      std::cerr << "[native-video-client] udp handshake failed " << args.host << ":" << args.port << "\n";
      closesocket(gSession.sock);
      gSession.sock = INVALID_SOCKET;
      if (dec.mfStarted) MFShutdown();
      return 6;
    }
  }

  // No second port to dial means the directory path: control tunnels through the socket the
  // punch just opened. The send is bare because the socket is connected -- the same socket the
  // receive loop below reads, which is what makes the two directions one NAT mapping.
  if (args.controlPort == 0 && dec.transport == VideoTransport::Udp && gSession.sock != INVALID_SOCKET) {
    gControl.udpControl.Configure(
        [](const void* data, size_t len) -> bool {
          return send(gSession.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        remote60::native_poc::kUdpControlStreamClientToHost,
        remote60::native_poc::kUdpControlStreamHostToClient, args.udpMtu);
    gControl.overUdp.store(true, std::memory_order_release);
    // Without this the receive blocks forever on a link that has gone quiet, and the tick above
    // never runs -- which is the one moment recovery is needed.
    DWORD recvTimeoutMs = 200;
    setsockopt(gSession.sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs),
               sizeof(recvTimeoutMs));
    std::cout << "[native-video-client] control tunnelled over the media socket\n";
  }

  std::cout << "[native-video-client] connected host=" << args.host
            << " port=" << args.port
            << " transport=" << video_transport_name(dec.transport)
            << " codec=" << args.codec
            << " seconds=" << args.seconds << "\n";
  std::cout << "[native-video-client] keyframe-request-limiter minIntervalUs="
            << gControl.keyframeRequests.min_interval_us()
            << " tokenRefillUs=" << gControl.keyframeRequests.token_refill_us()
            << " tokenCapacity=" << gControl.keyframeRequests.token_capacity()
            << " catchupReenterMinUs=" << gate.catchupReenterMinIntervalUs
            << " staleCaptureDropUs=" << gate.staleCaptureDropUs
            << " congestionRecoverMinUs=" << gate.congestionRecoverMinUs
            << " congestionRecoveryTimeoutUs=" << gate.congestionRecoveryTimeoutUs
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

  SOCKET controlSock = INVALID_SOCKET;
  std::thread controlThread;
  // Two ways to reach the host's control protocol, and the session only ever has one of them.
  // A direct host answers on its own TCP port; a host behind NAT is reachable solely through
  // the punched media socket, and dialling a second port there connects to nothing.
  bool controlReady = gControl.overUdp.load(std::memory_order_acquire);
  if (args.controlPort > 0) {
    controlSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (controlSock != INVALID_SOCKET) {
      int ctlNoDelay = 1;
      setsockopt(controlSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
      sockaddr_in ctlAddr{};
      ctlAddr.sin_family = AF_INET;
      ctlAddr.sin_port = htons(args.controlPort);
      if (inet_pton(AF_INET, args.host.c_str(), &ctlAddr.sin_addr) == 1 &&
          connect(controlSock, reinterpret_cast<const sockaddr*>(&ctlAddr), sizeof(ctlAddr)) == 0) {
        controlReady = true;
      }
    }
  }
  {
    const bool inputChannelEnabled =
        controlReady && args.enableInputChannel && !kInputPolicyForceBlock;
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
    gControl.scheduler.Reset(args.controlIntervalMs, qpc_now_us());
    if (controlReady) {
      controlThread = std::thread([&]() {
        // Fetch one queued preview over the control socket. Runs between scheduler
        // actions on the same strict request/response pipeline, one card per call so a
        // large backlog cannot starve input events. Only invoked when the host advertised
        // the capability, because an older host would drain the request and never reply.
        // Returns: 1 fetched, 0 nothing to do, -1 socket failure (stream desynced).
        auto fetch_one_thumbnail = [&](remote60::native_poc::ControlLink& link) -> int {
          // Routed through the ControlLink, not the raw socket, so a directory session (control
          // tunnelled over the punched UDP socket) fetches previews too -- modelled on the
          // Android ClientSessionController::FetchOneThumbnailLocked. One card per idle action
          // keeps the strict request/response loop from being starved. Only invoked when the
          // host advertised the capability, because an older host would drain the request and
          // never reply. Returns: 1 fetched, 0 nothing to do, -1 link failure (stream desynced).
          if (!gPicker.hostSupportsThumbnails.load(std::memory_order_relaxed)) return 0;
          uint64_t id = 0;
          {
            std::lock_guard<std::mutex> lk(gPicker.thumbMu);
            if (gPicker.thumbFetchQueue.empty()) return 0;
            id = gPicker.thumbFetchQueue.front();
            gPicker.thumbFetchQueue.pop_front();
          }
          remote60::native_poc::ControlWindowThumbnailRequestMessage req{};
          req.header.magic = remote60::native_poc::kMagic;
          req.header.type =
              static_cast<uint16_t>(MessageType::ControlWindowThumbnailRequest);
          req.header.size = static_cast<uint16_t>(sizeof(req));
          req.seq = 0;
          req.windowId = id;
          req.maxWidth = 256;
          req.maxHeight = 160;
          req.clientSendQpcUs = qpc_now_us();
          // One request is one message; EndMessage() draws the boundary UDP needs and TCP ignores.
          if (!link.Write(&req, sizeof(req)) || !link.EndMessage()) return -1;
          remote60::native_poc::ControlWindowThumbnailHeader rsp{};
          if (!link.Read(&rsp, sizeof(rsp))) return -1;
          if (rsp.header.magic != remote60::native_poc::kMagic ||
              rsp.header.type != static_cast<uint16_t>(MessageType::ControlWindowThumbnail) ||
              rsp.payloadSize > remote60::native_poc::kWindowThumbnailMaxPayloadBytes) {
            return -1;
          }
          std::vector<uint8_t> payload(rsp.payloadSize);
          if (rsp.payloadSize > 0 && !link.Read(payload.data(), payload.size())) {
            return -1;
          }
          if ((rsp.flags & 0x1u) != 0 && rsp.width > 0 && rsp.height > 0 &&
              payload.size() == static_cast<size_t>(rsp.width) * rsp.height * 4u) {
            auto thumb = std::make_shared<WindowThumb>();
            thumb->width = rsp.width;
            thumb->height = rsp.height;
            thumb->bgra = std::move(payload);
            thumb->fetchedUs = qpc_now_us();
            {
              std::lock_guard<std::mutex> lk(gPicker.thumbMu);
              gPicker.thumbs[id] = std::move(thumb);
            }
            // Outside the lock: the paint handler takes gPicker.thumbMu, and invalidating while
            // holding it invited a stall on every received preview.
            InvalidateRect(gSession.hwnd, nullptr, FALSE);
          }
          return 1;
        };
          // Built once, not per action: the tunnelled link carries the partially-read inbound
          // message between calls, and a fresh one each time would drop whatever it held.
          std::unique_ptr<remote60::native_poc::ControlLink> controlLink;
          if (gControl.overUdp.load(std::memory_order_acquire)) {
            controlLink = std::make_unique<remote60::native_poc::UdpControlLink>(
                &gControl.udpControl, kUdpControlReadTimeoutMs);
          } else {
            controlLink = std::make_unique<remote60::native_poc::TcpControlLink>(controlSock);
          }

          while (gSession.running.load()) {
            // Drives retransmission and gap recovery; cheap when there is nothing outstanding.
            if (gControl.overUdp.load(std::memory_order_acquire)) gControl.udpControl.Tick();
            bool didWork = false;
            const uint64_t nowUs = qpc_now_us();
            ControlOutboundAction action{};
            if (gControl.scheduler.NextAction(
                    nowUs, capture_client_control_metrics_snapshot(), &gPicker.windowPanel,
                    &gControl.streamState, &gControl.captureModeRequests, &gControl.keyframeRequests, &gControl.runtimeTune,
                    &gControl.inputQueue, &action)) {
              TcpControlResponse response{};
              const uint64_t actionStartUs = qpc_now_us();
              const bool actionOk = execute_control_action(*controlLink, action, &response);
              // One exchange that never gets its reply stalls every later one behind it,
              // including input. Naming the slow action is the only way to see which.
              const uint64_t actionUs = qpc_now_us() - actionStartUs;
              if (actionUs > 1000000ULL) {
                std::cout << "[native-video-client][control] slow action kind="
                          << static_cast<int>(action.kind) << " tookUs=" << actionUs
                          << " ok=" << (actionOk ? 1 : 0) << "\n";
              }
              if (!actionOk) {
                // A failed exchange ends the session's control, so it has to say which one and
                // on what transport. This used to break out silently, which made a control
                // channel that died on one bad message look identical to one that never
                // connected.
                std::cout << "[native-video-client][control] action failed kind="
                          << static_cast<int>(action.kind) << " transport="
                          << (gControl.overUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp");
                if (gControl.overUdp.load(std::memory_order_acquire)) {
                  // Closed means the channel gave up on the peer; open means the exchange came
                  // back as something other than the reply this action was waiting for.
                  const auto stats = gControl.udpControl.GetStats();
                  std::cout << " closed=" << (gControl.udpControl.IsClosed() ? 1 : 0)
                            << " reason=" << to_string(gControl.udpControl.CloseReason())
                            << " sent=" << stats.messagesSent
                            << " received=" << stats.messagesReceived
                            << " retx=" << stats.fragmentRetransmits
                            << " nacks=" << stats.nacksSent;
                }
                std::cout << "\n";
                break;
              }
              if (action.kind == ControlOutboundActionKind::InputEvent) {
                const uint64_t sent = ++gSession.inputEventsSent;
                if (args.inputLogEvery > 0 && (sent % args.inputLogEvery) == 0) {
                  std::cout << "[native-video-client][input] sent=" << sent
                            << " kind=" << action.inputEvent.kind
                            << " seq=" << action.inputEvent.seq << "\n";
                }
              }
              didWork = true;

              if (action.kind == ControlOutboundActionKind::CaptureMode) {
                gControl.captureOverviewMode.store(action.captureMode.mode == 1, std::memory_order_relaxed);
                std::cout << "[native-video-client][control] capture-mode-request seq=" << action.captureMode.seq
                          << " mode=" << action.captureMode.mode
                          << " xPermille=" << action.captureMode.xPermille
                          << " yPermille=" << action.captureMode.yPermille
                          << "\n";
              } else if (action.kind == ControlOutboundActionKind::KeyframeRequest) {
                std::cout << "[native-video-client][control] keyframe-request seq=" << action.keyframe.seq
                          << " reason=" << action.keyframe.reason << "\n";
              } else if (action.kind == ControlOutboundActionKind::StreamState) {
                std::cout << "[native-video-client][control] stream-state seq="
                          << action.streamState.seq
                          << " active=" << ((action.streamState.flags & 0x1u) ? 1 : 0) << "\n";
              } else if (action.kind == ControlOutboundActionKind::RuntimeTune) {
                std::cout << "[native-video-client][control] runtime-config seq=" << action.runtimeTune.seq
                          << " bitrate=" << action.runtimeTune.bitrate
                          << " keyint=" << action.runtimeTune.keyint
                          << " flags=" << action.runtimeTune.flags
                          << "\n";
              }

              switch (response.kind) {
                case TcpControlResponseKind::Pong: {
                  const auto& pong = response.pong;
                  const uint64_t doneUs = qpc_now_us();
                  gControl.scheduler.OnPingCompleted(doneUs);
                  gControl.hostCaptureTargetPid.store(pong.captureTargetPid, std::memory_order_relaxed);
                  gControl.hostCaptureTargetFlags.store(pong.captureTargetFlags, std::memory_order_relaxed);
                  gControl.hostCaptureRebindCount.store(pong.captureRebindCount, std::memory_order_relaxed);
                  gControl.hostCaptureTargetHwnd.store(pong.captureTargetHwnd, std::memory_order_relaxed);
                  gControl.hostCaptureMetaUpdatedUs.store(doneUs, std::memory_order_relaxed);
                  gControl.captureOverviewMode.store(
                      (pong.captureTargetFlags &
                       remote60::native_poc::kCaptureFlagWindowTargetEnabled) == 0,
                      std::memory_order_relaxed);
                  {
                    // Say it once per transition rather than every ping. A frozen picture with no
                    // explanation is the worst version of this; a line saying a Windows security
                    // prompt is on screen turns it into something the operator can act on.
                    const bool secure =
                        (pong.captureTargetFlags &
                         remote60::native_poc::kCaptureFlagSecureDesktopActive) != 0;
                    if (secure != gControl.reportedSecure) {
                      gControl.reportedSecure = secure;
                      std::cout << "[native-video-client] secure-desktop-active="
                                << (secure ? 1 : 0)
                                << (secure ? "  (a Windows security prompt is on screen; it "
                                             "cannot be captured, so the picture is paused)"
                                           : "  (picture resumes)")
                                << std::endl;
                    }
                  }
                  {
                    std::lock_guard<std::mutex> lk(gControl.hostCaptureMetaMu);
                    gControl.hostCaptureTargetProcess =
                        fixed_cstr_to_string(pong.captureTargetProcess, sizeof(pong.captureTargetProcess));
                    gControl.hostCaptureTargetTitle =
                        fixed_cstr_to_string(pong.captureTargetTitle, sizeof(pong.captureTargetTitle));
                  }
                  const uint64_t rttUs =
                      (doneUs >= action.ping.clientSendQpcUs) ? (doneUs - action.ping.clientSendQpcUs) : 0;
                  std::cout << "[native-video-client][control] seq=" << pong.seq
                            << " rttUs=" << rttUs
                            << " hostQueueUs=" << ((pong.hostSendQpcUs >= pong.hostRecvQpcUs)
                                                        ? (pong.hostSendQpcUs - pong.hostRecvQpcUs)
                                                        : 0)
                            << " hostCapPid=" << pong.captureTargetPid
                            << " hostCapProc=" << fixed_cstr_to_string(
                                   pong.captureTargetProcess, sizeof(pong.captureTargetProcess))
                            << " hostCapRebind=" << pong.captureRebindCount
                            << "\n";
                  // GNLink stream telemetry (diagnostics only): a periodic NTP-style clock offset
                  // (host QPC minus client QPC) plus RTT, so the seq-joined host/client logs can
                  // also be roughly aligned on an absolute timeline. Runs once per pong (~control
                  // interval); no new control traffic is introduced.
                  {
                    const int64_t t1 = static_cast<int64_t>(action.ping.clientSendQpcUs);
                    const int64_t t2 = static_cast<int64_t>(pong.hostRecvQpcUs);
                    const int64_t t3 = static_cast<int64_t>(pong.hostSendQpcUs);
                    const int64_t t4 = static_cast<int64_t>(doneUs);
                    const int64_t clockOffsetUs = ((t2 - t1) + (t3 - t4)) / 2;
                    std::ostringstream telem;
                    telem << "[native-video-client][telemetry] stage=clock"
                          << " pingSeq=" << pong.seq
                          << " rttUs=" << rttUs
                          << " clockOffsetUs=" << clockOffsetUs
                          << " clientSendUs=" << action.ping.clientSendQpcUs
                          << " hostRecvUs=" << pong.hostRecvQpcUs
                          << " hostSendUs=" << pong.hostSendQpcUs
                          << " clientRecvUs=" << doneUs;
                    log_client_line(telem.str());
                  }
                  break;
                }
                case TcpControlResponseKind::WindowList: {
                  apply_window_list_snapshot(response.windowList);
                  // The window list is where the host says whether it knows the monitor
                  // messages; asking one that does not would stall this loop waiting for a
                  // reply that never comes.
                  const bool supportsMonitors =
                      (response.windowList.flags &
                       remote60::native_poc::kControlWindowListFlagMonitors) != 0;
                  const bool monitorsNewlySupported =
                      gPicker.windowPanel.SetHostSupportsMonitors(supportsMonitors);
                  // The stored --monitor is auto-applied only when the session opens straight
                  // into the stream. In picker mode the user has not chosen a target yet, so
                  // selecting a monitor here would restart the host capture before any pick and
                  // fight the first-frame gate; a monitor pick is a follow-up (toolbar) action.
                  if (!startInPicker && monitorsNewlySupported && gSession.requestedMonitorId > 0) {
                    // Only when a screen other than the primary was asked for: selecting monitor
                    // zero would restart the capture for no change.
                    gPicker.windowPanel.RequestMonitorSelect(gSession.requestedMonitorId);
                  }
                  InvalidateRect(gSession.hwnd, nullptr, FALSE);
                  break;
                }
                case TcpControlResponseKind::MonitorList:
                  gPicker.windowPanel.ApplyMonitorList(response.monitorList);
                  break;
                case TcpControlResponseKind::WindowSelected:
                  apply_window_selected_result(response.windowSelected);
                  queue_window_list_request("window_list_request pending");
                  InvalidateRect(gSession.hwnd, nullptr, FALSE);
                  break;
                case TcpControlResponseKind::InputAck: {
                  const uint64_t ackCount = gControl.scheduler.RecordInputAck(args.inputLogEvery);
                  if (ackCount > 0) {
                    std::cout << "[native-video-client][input] ackSeq=" << response.inputAck.seq
                              << " sent=" << ackCount
                              << " dropped=" << gControl.inputQueue.dropped_count()
                              << "\n";
                  }
                  break;
                }
                case TcpControlResponseKind::None:
                default:
                  break;
              }
            }

            if (!didWork && gPicker.visible.load(std::memory_order_relaxed)) {
              const int fetched = fetch_one_thumbnail(*controlLink);
              if (fetched < 0) break;
              didWork = (fetched > 0);
            }
            if (!didWork) Sleep(2);
          }
          gControl.connected.store(false, std::memory_order_relaxed);
          gControl.runtimeTune.SetEnabled(false);
          // A selection cannot complete once control is gone: drop the pending state so the picker
          // re-enables instead of staying locked on "waiting for first frame". The viewer exits
          // shortly after (the video socket dies too), which returns the shell to the host list.
          clear_pc_target_selection();
          // Drop the persistent generation filter too: a reconnect renegotiates generations from
          // scratch, so an old value must not silently filter the new stream to nothing.
          gSel.activeStreamGeneration.store(0, std::memory_order_release);
          set_window_panel_status("control_disconnected");
          InvalidateRect(gSession.hwnd, nullptr, FALSE);
      });
    }
    if (controlReady) {
      gControl.connected.store(true, std::memory_order_relaxed);
      gControl.runtimeTune.SetEnabled(dec.useH264);
      queue_window_list_request("window_list_request pending");
      if (dec.useH264 && (args.runtimeBitrate > 0 || args.runtimeKeyint > 0)) {
        gControl.runtimeTune.MarkDirty();
      }
      std::cout << "[native-video-client] control connected transport="
                << (gControl.overUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp")
                << " port=" << args.controlPort
                << " inputChannel=" << (inputChannelEnabled ? 1 : 0) << "\n";
    } else {
      if (controlSock != INVALID_SOCKET) {
        closesocket(controlSock);
        controlSock = INVALID_SOCKET;
      }
      gControl.connected.store(false, std::memory_order_relaxed);
      gControl.runtimeTune.SetEnabled(false);
      set_window_panel_status("control_connect_failed");
      std::cout << "[native-video-client] control unavailable port=" << args.controlPort << "\n";
    }
  }

  const uint64_t startUs = qpc_now_us();
  VideoReceiver receiver(args, dec, gate, startUs, udpSimDropPm, udpSimDropSeed);
  std::thread recvThread([&]() { receiver.Run(); });

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

    if (args.seconds > 0) {
      const uint64_t nowUs = qpc_now_us();
      if (nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
        gSession.running = false;
        break;
      }
    }

    if (!hadMessage) {
      Sleep(5);
    }
  }

  gSession.running = false;
  gSession.inputEnabled = false;
  // Before anything is joined: the control thread can be parked in a blocking receive for the
  // read timeout, and closing the channel is what wakes it. Otherwise shutdown waits it out.
  gControl.udpControl.Close(remote60::native_poc::ControlCloseReason::Shutdown);
  gInput.macro.StopPlayback();
  gInput.macro.StopRecording();
  remote60::native_poc::macro_window_destroy();
  if (gSession.sock != INVALID_SOCKET) {
    shutdown(gSession.sock, SD_BOTH);
    closesocket(gSession.sock);
    gSession.sock = INVALID_SOCKET;
  }
  if (controlSock != INVALID_SOCKET) {
    shutdown(controlSock, SD_BOTH);
    closesocket(controlSock);
    controlSock = INVALID_SOCKET;
  }
  if (controlThread.joinable()) controlThread.join();
  if (recvThread.joinable()) recvThread.join();

  if (dec.useH264) {
    {
      std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
      gFrameBuf.frame.surfaceSample.Reset();
      gFrameBuf.frame.surfaceTexture.Reset();
      gFrameBuf.frame.bytes.reset();
    }
    dec.decoder.shutdown();
    if (dec.mfStarted) MFShutdown();
  }

  std::cout << "[native-video-client] done\n";
  return 0;
}
