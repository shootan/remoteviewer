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
  gKeyframeRequests.Configure(keyframeReqMinIntervalUs, keyframeReqTokenRefillUs, keyframeReqTokenCapacity);
  const uint64_t catchupReenterMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_CATCHUP_REENTER_MIN_INTERVAL_US",
      static_cast<uint32_t>(kCatchupReenterMinIntervalUsDefault), 100000, 3000000);
  const uint64_t staleCaptureDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_STALE_CAPTURE_DROP_US",
      static_cast<uint32_t>(kStaleCaptureDropUs), 1000, 2000000);
  const uint64_t congestionRecoverMinUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVER_MIN_US",
      static_cast<uint32_t>(kCongestionRecoverMinUsDefault), 50000, 5000000);
  const uint64_t congestionRecoveryTimeoutUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVERY_TIMEOUT_US",
      static_cast<uint32_t>(kCongestionRecoveryTimeoutUsDefault), 100000, 10000000);
  const uint32_t udpSimDropPm = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_PM", 0, 0, 1000);
  const uint32_t udpSimDropSeed = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_SEED", 0, 0, 0x7fffffffu);
  gKeyframeRequests.Reset();

  const bool useRaw = (args.codec == "raw");
  const bool useH264 = (args.codec == "h264");
  const bool encodedExperimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  if (!useRaw && !useH264) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec << " (supported: raw,h264)\n";
    return 10;
  }
  if (useH264 && !encodedExperimentEnabled) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 10;
  }
  std::string effectiveTransport = args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = useH264 ? "udp" : "tcp";
  }
  VideoTransport transport = VideoTransport::Tcp;
  if (!parse_video_transport(effectiveTransport, &transport)) {
    std::cerr << "[native-video-client] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 12;
  }
  if (transport == VideoTransport::Udp && useRaw) {
    std::cerr << "[native-video-client] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 13;
  }

  gSession.overlayConfig.host = args.host;
  gSession.overlayConfig.port = args.port;
  gSession.overlayConfig.controlPort = args.controlPort;
  gSession.overlayConfig.transport = video_transport_name(transport);
  gSession.overlayConfig.codec = args.codec;
  gSession.overlayConfig.fpsHint = args.fpsHint;
  gSession.overlayConfig.controlIntervalMs = args.controlIntervalMs;
  gSession.overlayConfig.tcpRecvBufKb = args.tcpRecvBufKb;
  gSession.overlayConfig.tcpSendBufKb = args.tcpSendBufKb;
  gSession.overlayConfig.udpMtu = args.udpMtu;
  gSession.overlayConfig.keyReqMinIntervalUs = gKeyframeRequests.min_interval_us();
  gSession.overlayConfig.keyReqTokenRefillUs = gKeyframeRequests.token_refill_us();
  gSession.overlayConfig.keyReqTokenCapacity = gKeyframeRequests.token_capacity();
  gSession.overlayConfig.udpSimDropPm = udpSimDropPm;
  gRuntimeTuneState.Reset(args.runtimeBitrate, args.runtimeKeyint, args.runtimeFps);
  gSession.requestedMonitorId = args.monitorId;
  gControlConnected.store(false, std::memory_order_relaxed);
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
  gCaptureOverviewMode.store(startInPicker, std::memory_order_relaxed);
  gWindowPickerVisible.store(startInPicker, std::memory_order_relaxed);
  clear_pc_target_selection();
  // No target has taken effect yet. 0 disables the persistent generation filter, so the legacy
  // stream-view start and the pre-first-pick window accept whatever the host sends, as before.
  gActiveStreamGeneration.store(0, std::memory_order_release);
  gSelectionRevealPosted.store(false, std::memory_order_release);
  // Picker-first sessions must not keep the host's default stream running under the picker: the
  // request rides the scheduler (StreamState before WindowList/Select) and is queued before the
  // control link exists, so it goes out first thing once connected. An initial default-desktop
  // frame that slips through before the stream stops is dropped by the receive-path gate rather
  // than painted, and no flip swap chain is created until the user's pick produces a real frame.
  if (startInPicker) {
    gStreamStateControl.Request(false);
  }
  gCaptureModeRequests.Reset();
  gWindowPanelState.Reset();
  gSuppressMouseUntilUs.store(0, std::memory_order_relaxed);
  gActiveTouchPointerId.store(0, std::memory_order_relaxed);
  gActiveTouchDown.store(false, std::memory_order_relaxed);

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
      gWindowPanelState.RequestMonitorSelect(monitorId);
    };
    remote60::native_poc::session_toolbar_create(gSession.hwnd, std::move(toolbarCallbacks));
    remote60::native_poc::session_toolbar_set_visible(startInStreamView);
    push_session_toolbar_state();
  }

  bool mfStarted = false;
  H264Decoder decoder;
  bool decoderReady = false;
  bool waitForKeyFrame = useH264;
  uint32_t decoderW = 0;
  uint32_t decoderH = 0;
  Microsoft::WRL::ComPtr<ID3D11Device> decD3dDevice;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> decD3dContext;
  if (useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-client] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      return 11;
    }
    mfStarted = true;
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
      if (!gNv12Renderer.ready) (void)gNv12Renderer.init(gSession.hwnd);
      if (gNv12Renderer.ready) {
        decD3dDevice = gNv12Renderer.device;
        decD3dContext = gNv12Renderer.context;
        (void)decoder.set_d3d11_device(decD3dDevice.Get());
      } else {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        const HRESULT d3dHr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                D3D11_SDK_VERSION, &decD3dDevice, &fl,
                                                &decD3dContext);
        if (SUCCEEDED(d3dHr) && decD3dDevice) {
          (void)decoder.set_d3d11_device(decD3dDevice.Get());
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
    if (transport != VideoTransport::Udp) {
      std::cerr << "[native-video-client] the directory path is udp only\n";
      if (mfStarted) MFShutdown();
      return 3;
    }
    std::string directoryError;
    std::string sessionToken = args.directorySession;
    if (sessionToken.empty() &&
        !remote60::native_poc::directory_login(args.directoryUrl, args.directoryAccount,
                                               args.directoryPassword, &sessionToken,
                                               &directoryError)) {
      std::cerr << "[native-video-client] directory login failed: " << directoryError << "\n";
      if (mfStarted) MFShutdown();
      return 3;
    }

    std::string hostId = args.directoryHostId;
    if (hostId.empty()) {
      std::vector<remote60::native_poc::DirectoryHostEntry> hosts;
      if (!remote60::native_poc::directory_list_hosts(args.directoryUrl, sessionToken, &hosts,
                                                      &directoryError)) {
        std::cerr << "[native-video-client] directory hosts failed: " << directoryError << "\n";
        if (mfStarted) MFShutdown();
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
        if (mfStarted) MFShutdown();
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
      if (mfStarted) MFShutdown();
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
                   (transport == VideoTransport::Udp) ? SOCK_DGRAM : SOCK_STREAM,
                   (transport == VideoTransport::Udp) ? IPPROTO_UDP : IPPROTO_TCP);
  }
  if (gSession.sock == INVALID_SOCKET) {
    std::cerr << "[native-video-client] socket create failed\n";
    if (mfStarted) MFShutdown();
    return 3;
  }

  if (transport == VideoTransport::Tcp) {
    int noDelay = 1;
    setsockopt(gSession.sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  }
  if (transport == VideoTransport::Udp) {
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
    if (mfStarted) MFShutdown();
    return 4;
  }
  if (connect(gSession.sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "[native-video-client] connect failed " << resolvedArgs.host << ":" << resolvedArgs.port << "\n";
    closesocket(gSession.sock);
    gSession.sock = INVALID_SOCKET;
    if (mfStarted) MFShutdown();
    return 5;
  }
  if (transport == VideoTransport::Udp) {
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
      if (mfStarted) MFShutdown();
      return 6;
    }
  }

  // No second port to dial means the directory path: control tunnels through the socket the
  // punch just opened. The send is bare because the socket is connected -- the same socket the
  // receive loop below reads, which is what makes the two directions one NAT mapping.
  if (args.controlPort == 0 && transport == VideoTransport::Udp && gSession.sock != INVALID_SOCKET) {
    gUdpControl.Configure(
        [](const void* data, size_t len) -> bool {
          return send(gSession.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        remote60::native_poc::kUdpControlStreamClientToHost,
        remote60::native_poc::kUdpControlStreamHostToClient, args.udpMtu);
    gControlOverUdp.store(true, std::memory_order_release);
    // Without this the receive blocks forever on a link that has gone quiet, and the tick above
    // never runs -- which is the one moment recovery is needed.
    DWORD recvTimeoutMs = 200;
    setsockopt(gSession.sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs),
               sizeof(recvTimeoutMs));
    std::cout << "[native-video-client] control tunnelled over the media socket\n";
  }

  std::cout << "[native-video-client] connected host=" << args.host
            << " port=" << args.port
            << " transport=" << video_transport_name(transport)
            << " codec=" << args.codec
            << " seconds=" << args.seconds << "\n";
  std::cout << "[native-video-client] keyframe-request-limiter minIntervalUs="
            << gKeyframeRequests.min_interval_us()
            << " tokenRefillUs=" << gKeyframeRequests.token_refill_us()
            << " tokenCapacity=" << gKeyframeRequests.token_capacity()
            << " catchupReenterMinUs=" << catchupReenterMinIntervalUs
            << " staleCaptureDropUs=" << staleCaptureDropUs
            << " congestionRecoverMinUs=" << congestionRecoverMinUs
            << " congestionRecoveryTimeoutUs=" << congestionRecoveryTimeoutUs
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
  bool controlReady = gControlOverUdp.load(std::memory_order_acquire);
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
    gControlScheduler.Reset(args.controlIntervalMs, qpc_now_us());
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
          if (!gHostSupportsThumbnails.load(std::memory_order_relaxed)) return 0;
          uint64_t id = 0;
          {
            std::lock_guard<std::mutex> lk(gThumbMu);
            if (gThumbFetchQueue.empty()) return 0;
            id = gThumbFetchQueue.front();
            gThumbFetchQueue.pop_front();
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
              std::lock_guard<std::mutex> lk(gThumbMu);
              gThumbs[id] = std::move(thumb);
            }
            // Outside the lock: the paint handler takes gThumbMu, and invalidating while
            // holding it invited a stall on every received preview.
            InvalidateRect(gSession.hwnd, nullptr, FALSE);
          }
          return 1;
        };
          // Built once, not per action: the tunnelled link carries the partially-read inbound
          // message between calls, and a fresh one each time would drop whatever it held.
          std::unique_ptr<remote60::native_poc::ControlLink> controlLink;
          if (gControlOverUdp.load(std::memory_order_acquire)) {
            controlLink = std::make_unique<remote60::native_poc::UdpControlLink>(
                &gUdpControl, kUdpControlReadTimeoutMs);
          } else {
            controlLink = std::make_unique<remote60::native_poc::TcpControlLink>(controlSock);
          }

          while (gSession.running.load()) {
            // Drives retransmission and gap recovery; cheap when there is nothing outstanding.
            if (gControlOverUdp.load(std::memory_order_acquire)) gUdpControl.Tick();
            bool didWork = false;
            const uint64_t nowUs = qpc_now_us();
            ControlOutboundAction action{};
            if (gControlScheduler.NextAction(
                    nowUs, capture_client_control_metrics_snapshot(), &gWindowPanelState,
                    &gStreamStateControl, &gCaptureModeRequests, &gKeyframeRequests, &gRuntimeTuneState,
                    &gInputQueueState, &action)) {
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
                          << (gControlOverUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp");
                if (gControlOverUdp.load(std::memory_order_acquire)) {
                  // Closed means the channel gave up on the peer; open means the exchange came
                  // back as something other than the reply this action was waiting for.
                  const auto stats = gUdpControl.GetStats();
                  std::cout << " closed=" << (gUdpControl.IsClosed() ? 1 : 0)
                            << " reason=" << to_string(gUdpControl.CloseReason())
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
                gCaptureOverviewMode.store(action.captureMode.mode == 1, std::memory_order_relaxed);
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
                  gControlScheduler.OnPingCompleted(doneUs);
                  gHostCaptureTargetPid.store(pong.captureTargetPid, std::memory_order_relaxed);
                  gHostCaptureTargetFlags.store(pong.captureTargetFlags, std::memory_order_relaxed);
                  gHostCaptureRebindCount.store(pong.captureRebindCount, std::memory_order_relaxed);
                  gHostCaptureTargetHwnd.store(pong.captureTargetHwnd, std::memory_order_relaxed);
                  gHostCaptureMetaUpdatedUs.store(doneUs, std::memory_order_relaxed);
                  gCaptureOverviewMode.store(
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
                    static bool reportedSecure = false;
                    if (secure != reportedSecure) {
                      reportedSecure = secure;
                      std::cout << "[native-video-client] secure-desktop-active="
                                << (secure ? 1 : 0)
                                << (secure ? "  (a Windows security prompt is on screen; it "
                                             "cannot be captured, so the picture is paused)"
                                           : "  (picture resumes)")
                                << std::endl;
                    }
                  }
                  {
                    std::lock_guard<std::mutex> lk(gHostCaptureMetaMu);
                    gHostCaptureTargetProcess =
                        fixed_cstr_to_string(pong.captureTargetProcess, sizeof(pong.captureTargetProcess));
                    gHostCaptureTargetTitle =
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
                      gWindowPanelState.SetHostSupportsMonitors(supportsMonitors);
                  // The stored --monitor is auto-applied only when the session opens straight
                  // into the stream. In picker mode the user has not chosen a target yet, so
                  // selecting a monitor here would restart the host capture before any pick and
                  // fight the first-frame gate; a monitor pick is a follow-up (toolbar) action.
                  if (!startInPicker && monitorsNewlySupported && gSession.requestedMonitorId > 0) {
                    // Only when a screen other than the primary was asked for: selecting monitor
                    // zero would restart the capture for no change.
                    gWindowPanelState.RequestMonitorSelect(gSession.requestedMonitorId);
                  }
                  InvalidateRect(gSession.hwnd, nullptr, FALSE);
                  break;
                }
                case TcpControlResponseKind::MonitorList:
                  gWindowPanelState.ApplyMonitorList(response.monitorList);
                  break;
                case TcpControlResponseKind::WindowSelected:
                  apply_window_selected_result(response.windowSelected);
                  queue_window_list_request("window_list_request pending");
                  InvalidateRect(gSession.hwnd, nullptr, FALSE);
                  break;
                case TcpControlResponseKind::InputAck: {
                  const uint64_t ackCount = gControlScheduler.RecordInputAck(args.inputLogEvery);
                  if (ackCount > 0) {
                    std::cout << "[native-video-client][input] ackSeq=" << response.inputAck.seq
                              << " sent=" << ackCount
                              << " dropped=" << gInputQueueState.dropped_count()
                              << "\n";
                  }
                  break;
                }
                case TcpControlResponseKind::None:
                default:
                  break;
              }
            }

            if (!didWork && gWindowPickerVisible.load(std::memory_order_relaxed)) {
              const int fetched = fetch_one_thumbnail(*controlLink);
              if (fetched < 0) break;
              didWork = (fetched > 0);
            }
            if (!didWork) Sleep(2);
          }
          gControlConnected.store(false, std::memory_order_relaxed);
          gRuntimeTuneState.SetEnabled(false);
          // A selection cannot complete once control is gone: drop the pending state so the picker
          // re-enables instead of staying locked on "waiting for first frame". The viewer exits
          // shortly after (the video socket dies too), which returns the shell to the host list.
          clear_pc_target_selection();
          // Drop the persistent generation filter too: a reconnect renegotiates generations from
          // scratch, so an old value must not silently filter the new stream to nothing.
          gActiveStreamGeneration.store(0, std::memory_order_release);
          set_window_panel_status("control_disconnected");
          InvalidateRect(gSession.hwnd, nullptr, FALSE);
      });
    }
    if (controlReady) {
      gControlConnected.store(true, std::memory_order_relaxed);
      gRuntimeTuneState.SetEnabled(useH264);
      queue_window_list_request("window_list_request pending");
      if (useH264 && (args.runtimeBitrate > 0 || args.runtimeKeyint > 0)) {
        gRuntimeTuneState.MarkDirty();
      }
      std::cout << "[native-video-client] control connected transport="
                << (gControlOverUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp")
                << " port=" << args.controlPort
                << " inputChannel=" << (inputChannelEnabled ? 1 : 0) << "\n";
    } else {
      if (controlSock != INVALID_SOCKET) {
        closesocket(controlSock);
        controlSock = INVALID_SOCKET;
      }
      gControlConnected.store(false, std::memory_order_relaxed);
      gRuntimeTuneState.SetEnabled(false);
      set_window_panel_status("control_connect_failed");
      std::cout << "[native-video-client] control unavailable port=" << args.controlPort << "\n";
    }
  }

  const uint64_t startUs = qpc_now_us();
  std::thread recvThread([&]() {
    // Which selection generation this loop has already reset the decoder for. A bump by
    // begin_pc_target_selection() on the UI thread makes the next frame flush stale references.
    uint64_t recvSelectionEpoch = gSelectionEpoch.load(std::memory_order_acquire);
    uint64_t statAtUs = qpc_now_us() + 1000000ULL;
    uint64_t recvFrames = 0;
    uint64_t decodedFrames = 0;
    uint64_t skippedQueued = 0;
    uint64_t recvBytes = 0;
    uint64_t decodedBytes = 0;
    uint64_t sumLatencyUs = 0;
    uint64_t maxLatencyUs = 0;
    uint64_t sumDecodeTailUs = 0;
    uint64_t maxDecodeTailUs = 0;
    uint64_t decodeFailCount = 0;
    // Consecutive hard decode failures. A flush (decoder.reset) recovers a corrupt frame, but
    // not a wedged hardware MFT or a lost D3D device -- and the viewer's only recovery for a
    // same-resolution decode error was that flush, so once the decoder wedged (a YouTube scene
    // change on a busy GPU could do it) every following frame failed identically and the
    // picture froze until the app was restarted. Past a threshold, rebuild the decoder instead.
    uint32_t decodeConsecutiveFailCount = 0;
    constexpr uint32_t kDecodeRebuildThreshold = 8;
    uint64_t decodeTimestampOverflowCount = 0;
    uint64_t decodeEmptyCount = 0;
    uint64_t decodeEmptyStreak = 0;
    uint64_t decodeEmptyStreakStartUs = 0;
    uint64_t decodeEmptyRecoveryCount = 0;
    uint64_t waitingKeyDropCount = 0;
    uint64_t lagDropCount = 0;
    uint64_t udpChunkRecvCount = 0;
    uint64_t udpAssemblyCompletedCount = 0;
    uint64_t udpAssemblyDroppedCount = 0;
    uint64_t udpAssemblyMalformedCount = 0;
    uint64_t udpAssemblyReorderCount = 0;
    uint64_t udpAssemblyKeyReqCount = 0;
    uint64_t udpAssemblyFecRecoveredCount = 0;
    uint32_t udpAssemblyDropPmLast = 0;
    uint64_t lastPacketRecvUs = 0;
    uint32_t lagTriggerStreak = 0;
    uint64_t lastCatchupEnterUs = 0;
    uint64_t catchupEnterThrottledCount = 0;
    bool catchupMode = false;
    // lastPresentedCaptureUs is now gFrameBuf.lastPresentedCaptureUs (atomic, updated after actual present)
    bool captureTimelineReady = false;
    uint64_t captureRemoteBaseUs = 0;
    uint64_t captureLocalBaseUs = 0;
    bool sendTimelineReady = false;
    uint64_t sendRemoteBaseUs = 0;
    uint64_t sendLocalBaseUs = 0;
    const uint64_t frameIntervalUs = std::max<uint64_t>(
        1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, args.fpsHint)));
    ClientCongestionState congestionState = ClientCongestionState::Normal;
    uint64_t congestionStateEnterUs = 0;
    uint64_t congestionTransitionCount = 0;
    uint64_t congestionRecoveryCount = 0;
    uint64_t congestionRecoveryTotalUs = 0;
    uint64_t congestionRecoveryMaxUs = 0;
    uint64_t congestionRecoveryRequestCount = 0;
    uint64_t staleDropCount = 0;
    uint64_t holdLatestDropCount = 0;
    uint64_t burstDropCount = 0;
    uint64_t staleReferenceRecoveryCount = 0;
    // Capture timestamp of the newest keyframe the decoder has successfully consumed. A stale
    // frame OLDER than this anchor was already resynced past (safe to quiet-drop); one AT OR
    // AFTER it still sits in the live reference chain, so dropping it needs an IDR resync.
    uint64_t lastDecodedKeyCaptureUs = 0;
    uint64_t latestCaptureSeenUs = 0;
    uint64_t queueDepthSampleCount = 0;
    uint64_t queueDepthHist[5] = {0, 0, 0, 0, 0};
    uint32_t queueDepthFramesMax = 0;
    uint64_t recoveringSinceUs = 0;
    uint32_t recoveringHealthyStreak = 0;
    uint64_t lastRecoveryRequestUs = 0;
    auto queue_depth_frames = [&](uint64_t lagUs) -> uint32_t {
      if (lagUs == 0) return 0;
      const uint64_t depth64 = (lagUs + frameIntervalUs - 1) / frameIntervalUs;
      return static_cast<uint32_t>(std::min<uint64_t>(depth64, 1000ULL));
    };
    auto sample_queue_depth = [&](uint64_t lagUs) {
      const uint32_t depthFrames = queue_depth_frames(lagUs);
      ++queueDepthSampleCount;
      if (depthFrames > queueDepthFramesMax) queueDepthFramesMax = depthFrames;
      if (depthFrames == 0) {
        ++queueDepthHist[0];
      } else if (depthFrames == 1) {
        ++queueDepthHist[1];
      } else if (depthFrames == 2) {
        ++queueDepthHist[2];
      } else if (depthFrames == 3) {
        ++queueDepthHist[3];
      } else {
        ++queueDepthHist[4];
      }
    };
    auto transition_congestion_state = [&](ClientCongestionState nextState, uint64_t nowUs, const char* reason,
                                           uint64_t streamLagUs, uint64_t decodeQueueLagEstimateUs, uint32_t seq) {
      if (nextState == congestionState) return;
      const ClientCongestionState prev = congestionState;
      if (prev != ClientCongestionState::Normal &&
          nextState == ClientCongestionState::Normal &&
          congestionStateEnterUs > 0 &&
          nowUs >= congestionStateEnterUs) {
        const uint64_t recoverUs = nowUs - congestionStateEnterUs;
        ++congestionRecoveryCount;
        congestionRecoveryTotalUs += recoverUs;
        if (recoverUs > congestionRecoveryMaxUs) congestionRecoveryMaxUs = recoverUs;
      }
      congestionState = nextState;
      congestionStateEnterUs = (nextState == ClientCongestionState::Normal) ? 0 : nowUs;
      if (nextState == ClientCongestionState::Recovering) {
        recoveringSinceUs = nowUs;
        recoveringHealthyStreak = 0;
      } else if (nextState != ClientCongestionState::Recovering) {
        recoveringSinceUs = 0;
        recoveringHealthyStreak = 0;
      }
      ++congestionTransitionCount;
      std::cout << "[native-video-client][congestion] state=" << congestion_state_name(nextState)
                << " prev=" << congestion_state_name(prev)
                << " reason=" << reason
                << " streamLagUs=" << streamLagUs
                << " decodeQueueLagUs=" << decodeQueueLagEstimateUs
                << " seq=" << seq
                << "\n";
    };
    struct PresentCounterSnapshot {
      uint64_t d3dPresentSuccess = 0;
      uint64_t d3dPresentFail = 0;
      uint64_t gdiFallbackPresented = 0;
      uint64_t fallbackInitFail = 0;
      uint64_t fallbackRenderFail = 0;
      uint64_t fallbackNv12ConvertFail = 0;
      uint64_t paintCoalesced = 0;
      uint64_t overwriteBeforePresent = 0;
    };
    auto load_present_counters = [&]() -> PresentCounterSnapshot {
      PresentCounterSnapshot s{};
      s.d3dPresentSuccess = gPresent.d3dPresentSuccessCount.load(std::memory_order_relaxed);
      s.d3dPresentFail = gPresent.d3dPresentFailCount.load(std::memory_order_relaxed);
      s.gdiFallbackPresented = gPresent.gdiFallbackPresentedCount.load(std::memory_order_relaxed);
      s.fallbackInitFail = gPresent.fallbackInitFailCount.load(std::memory_order_relaxed);
      s.fallbackRenderFail = gPresent.fallbackRenderFailCount.load(std::memory_order_relaxed);
      s.fallbackNv12ConvertFail = gPresent.fallbackNv12ConvertFailCount.load(std::memory_order_relaxed);
      s.paintCoalesced = gFrameBuf.paintCoalescedCount.load(std::memory_order_relaxed);
      s.overwriteBeforePresent = gFrameBuf.overwriteBeforePresentCount.load(std::memory_order_relaxed);
      return s;
    };
    PresentCounterSnapshot lastPresentCounters = load_present_counters();
    auto append_present_counter_fields = [&](std::ostream& os) {
      const PresentCounterSnapshot nowCounters = load_present_counters();
      const uint64_t d3dPresentSuccess = nowCounters.d3dPresentSuccess - lastPresentCounters.d3dPresentSuccess;
      const uint64_t d3dPresentFail = nowCounters.d3dPresentFail - lastPresentCounters.d3dPresentFail;
      const uint64_t gdiFallbackPresented =
          nowCounters.gdiFallbackPresented - lastPresentCounters.gdiFallbackPresented;
      const uint64_t fallbackInitFail = nowCounters.fallbackInitFail - lastPresentCounters.fallbackInitFail;
      const uint64_t fallbackRenderFail = nowCounters.fallbackRenderFail - lastPresentCounters.fallbackRenderFail;
      const uint64_t fallbackNv12ConvertFail =
          nowCounters.fallbackNv12ConvertFail - lastPresentCounters.fallbackNv12ConvertFail;
      const uint64_t paintCoalesced = nowCounters.paintCoalesced - lastPresentCounters.paintCoalesced;
      const uint64_t overwriteBeforePresent =
          nowCounters.overwriteBeforePresent - lastPresentCounters.overwriteBeforePresent;
      const uint64_t d3dAttempts = d3dPresentSuccess + d3dPresentFail;
      const uint64_t gdiFallbackRateX1000 = (d3dAttempts > 0)
          ? ((gdiFallbackPresented * 1000ULL) / d3dAttempts)
          : 0;
      os << " d3dPresentSuccess=" << d3dPresentSuccess
         << " d3dPresentFail=" << d3dPresentFail
         << " gdiFallbackPresented=" << gdiFallbackPresented
         << " gdiFallbackRateX1000=" << gdiFallbackRateX1000
         << " fallbackInitFail=" << fallbackInitFail
         << " fallbackRenderFail=" << fallbackRenderFail
         << " fallbackNv12ConvertFail=" << fallbackNv12ConvertFail
         << " paintCoalesced=" << paintCoalesced
         << " overwriteBeforePresent=" << overwriteBeforePresent;
      lastPresentCounters = nowCounters;
    };
    auto append_congestion_fields = [&](std::ostream& os) {
      const uint64_t recoveryAvgUs =
          (congestionRecoveryCount > 0) ? (congestionRecoveryTotalUs / congestionRecoveryCount) : 0;
      os << " congestionState=" << congestion_state_name(congestionState)
         << " congestionTransitions=" << congestionTransitionCount
         << " congestionRecoveryCount=" << congestionRecoveryCount
         << " congestionRecoveryAvgUs=" << recoveryAvgUs
         << " congestionRecoveryMaxUs=" << congestionRecoveryMaxUs
         << " congestionRecoveryReq=" << congestionRecoveryRequestCount
         << " staleDrops=" << staleDropCount
         << " holdLatestDrops=" << holdLatestDropCount
         << " burstDrops=" << burstDropCount
         << " staleRefRecoveries=" << staleReferenceRecoveryCount
         << " queueDepthSamples=" << queueDepthSampleCount
         << " queueDepthMax=" << queueDepthFramesMax
         << " queueDepthH0=" << queueDepthHist[0]
         << " queueDepthH1=" << queueDepthHist[1]
         << " queueDepthH2=" << queueDepthHist[2]
         << " queueDepthH3=" << queueDepthHist[3]
         << " queueDepthH4p=" << queueDepthHist[4];
    };
    auto aligned_lag_us = [&](uint64_t remoteTsUs, uint64_t localNowUs,
                              bool& timelineReady, uint64_t& remoteBaseUs, uint64_t& localBaseUs) -> uint64_t {
      if (!timelineReady || remoteTsUs < remoteBaseUs) {
        timelineReady = true;
        remoteBaseUs = remoteTsUs;
        localBaseUs = localNowUs;
        return 0;
      }
      const uint64_t remoteDeltaUs = remoteTsUs - remoteBaseUs;
      uint64_t expectedLocalUs = localBaseUs;
      if (std::numeric_limits<uint64_t>::max() - expectedLocalUs < remoteDeltaUs) {
        expectedLocalUs = std::numeric_limits<uint64_t>::max();
      } else {
        expectedLocalUs += remoteDeltaUs;
      }
      return (localNowUs >= expectedLocalUs) ? (localNowUs - expectedLocalUs) : 0;
    };
    auto publish_metrics = [&](uint32_t metricW, uint32_t metricH, uint64_t nowUs,
                               uint64_t avgLatencyUs, uint64_t maxLatencyUsLocal,
                               uint64_t avgDecodeTailUs, uint64_t maxDecodeTailUsLocal,
                               double mbpsLocal) {
      const uint64_t cappedRecvFpsX100 = std::min<uint64_t>(recvFrames * 100ULL, 0xFFFFFFFFULL);
      const uint64_t cappedDecodedFpsX100 = std::min<uint64_t>(decodedFrames * 100ULL, 0xFFFFFFFFULL);
      const double mbpsX1000 = mbpsLocal * 1000.0;
      uint32_t recvMbpsX1000 = 0;
      if (mbpsX1000 > 0.0) {
        recvMbpsX1000 = static_cast<uint32_t>(
            std::min<double>(mbpsX1000, static_cast<double>(0xFFFFFFFFu)));
      }
      gMetrics.client.width = metricW;
      gMetrics.client.height = metricH;
      gMetrics.client.recvFpsX100 = static_cast<uint32_t>(cappedRecvFpsX100);
      gMetrics.client.decodedFpsX100 = static_cast<uint32_t>(cappedDecodedFpsX100);
      gMetrics.client.recvMbpsX1000 = recvMbpsX1000;
      gMetrics.client.skippedFrames = static_cast<uint32_t>(std::min<uint64_t>(skippedQueued, 0xFFFFFFFFULL));
      gMetrics.client.avgLatencyUs = avgLatencyUs;
      gMetrics.client.maxLatencyUs = maxLatencyUsLocal;
      gMetrics.client.avgDecodeTailUs = avgDecodeTailUs;
      gMetrics.client.maxDecodeTailUs = maxDecodeTailUsLocal;
      gMetrics.client.congestionState = static_cast<uint32_t>(congestionState);
      gMetrics.client.congestionTransitions =
          static_cast<uint32_t>(std::min<uint64_t>(congestionTransitionCount, 0xFFFFFFFFULL));
      gMetrics.client.congestionRecoveryCount =
          static_cast<uint32_t>(std::min<uint64_t>(congestionRecoveryCount, 0xFFFFFFFFULL));
      gMetrics.client.congestionRecoveryReq =
          static_cast<uint32_t>(std::min<uint64_t>(congestionRecoveryRequestCount, 0xFFFFFFFFULL));
      gMetrics.client.congestionRecoveryMaxUs =
          static_cast<uint32_t>(std::min<uint64_t>(congestionRecoveryMaxUs, 0xFFFFFFFFULL));
      gMetrics.client.queueDepthMax = queueDepthFramesMax;
      gMetrics.client.queueDepthH4p =
          static_cast<uint32_t>(std::min<uint64_t>(queueDepthHist[4], 0xFFFFFFFFULL));
      gMetrics.client.udpAssemblyDropPm = udpAssemblyDropPmLast;
      gMetrics.client.seq.fetch_add(1);
      gMetrics.client.updatedQpcUs = nowUs;
      push_overlay_metric_sample(gMetrics.client.recvFpsX100.load(std::memory_order_relaxed),
                                 gMetrics.client.decodedFpsX100.load(std::memory_order_relaxed),
                                 gMetrics.client.recvMbpsX1000.load(std::memory_order_relaxed),
                                 gMetrics.client.avgLatencyUs.load(std::memory_order_relaxed),
                                 nowUs);
      if (gSession.hwnd && !gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (!gFrameBuf.paintQueued.exchange(true)) {
          InvalidateRect(gSession.hwnd, nullptr, FALSE);
        } else {
          ++gFrameBuf.paintCoalescedCount;
        }
      }
    };
    auto process_h264_frame = [&](const EncodedFrameHeader& h, std::vector<uint8_t>* payloadPtr,
                                  uint64_t packetNowUs) -> bool {
      if (!payloadPtr) return true;
      ++recvFrames;
      recvBytes += h.payloadSize;
      const uint64_t recvGapUs =
          (lastPacketRecvUs > 0 && packetNowUs >= lastPacketRecvUs) ? (packetNowUs - lastPacketRecvUs) : 0;
      lastPacketRecvUs = packetNowUs;
      if (recvGapUs > 250000) {
        // Sparse arrival usually means source/capture stall, not decoder backlog.
        lagTriggerStreak = 0;
      }

      if (!useH264) {
        ++skippedQueued;
        return true;
      }

      // Target-selection gate (mobile parity, Android commit 4892dea). While the user's pick is
      // resolving, keep the picker up and present nothing until the acknowledged generation's
      // first frame decodes.
      if (gSelectionEpoch.load(std::memory_order_acquire) != recvSelectionEpoch) {
        // A fresh pick: drop stale reference frames and hold for the new generation's keyframe.
        recvSelectionEpoch = gSelectionEpoch.load(std::memory_order_acquire);
        decoder.reset();
        waitForKeyFrame = true;
      }
      if (gSelectionPending.load(std::memory_order_acquire)) {
        if (gSelectionAwaitingAck.load(std::memory_order_acquire)) {
          // No ack yet: every frame here is either the old target or an unconfirmed guess.
          ++skippedQueued;
          return true;
        }
        const uint64_t expectedGen = gSelectionExpectedGeneration.load(std::memory_order_acquire);
        if (expectedGen != 0 && h.streamGeneration != expectedGen) {
          // The previous target's stream still draining after the ack; not what we selected.
          ++skippedQueued;
          return true;
        }
      } else {
        // No selection in flight. After a reveal, only the active target's generation is welcome:
        // a late straggler from the previously selected target, still in flight on the wire, would
        // otherwise flash on screen. gActiveStreamGeneration==0 means no PC-side selection has
        // taken effect (legacy stream-view start, or before the first pick), so accept anything as
        // before. Host auto-resolution changes keep the same generation, so this does not fight
        // them -- only a host-side target selection bumps the generation.
        const uint64_t activeGen = gActiveStreamGeneration.load(std::memory_order_acquire);
        if (activeGen != 0 && h.streamGeneration != activeGen) {
          ++skippedQueued;
          return true;
        }
      }

      if (!decoderReady || decoderW != h.width || decoderH != h.height) {
        if (!decoder.initialize(h.width, h.height, args.fpsHint)) {
          std::cerr << "[native-video-client] H264 decoder initialize failed size=" << h.width << "x" << h.height
                    << "\n";
          return false;
        }
    const std::string requestedDecoderBackend = env_string_or_empty("REMOTE60_NATIVE_DECODER_BACKEND");
    const std::string requestedDecoderBackendPrint =
        requestedDecoderBackend.empty() ? "default(mft_auto)" : requestedDecoderBackend;
        const std::string backendFallbackReason =
            backend_fallback_reason(requestedDecoderBackend, decoder.backend_name());
        std::cout << "[native-video-client] H264 decoder backend=" << decoder.backend_name()
                  << " backendRequested=" << requestedDecoderBackendPrint
                  << " backendResolved=" << decoder.backend_name()
                  << " backendFallbackReason=" << backendFallbackReason
                  << " hw=" << (decoder.using_hardware() ? 1 : 0)
                  << " size=" << h.width << "x" << h.height << "\n";
        decoderReady = true;
        decoderW = h.width;
        decoderH = h.height;
        waitForKeyFrame = true;
      }

      const bool keyFrame = ((h.flags & 1u) != 0);
      if (h.captureQpcUs > latestCaptureSeenUs) {
        latestCaptureSeenUs = h.captureQpcUs;
      }
      const uint64_t streamLagUs = aligned_lag_us(
          h.captureQpcUs, packetNowUs, captureTimelineReady, captureRemoteBaseUs, captureLocalBaseUs);
      const uint64_t presentedCapUs = gFrameBuf.lastPresentedCaptureUs.load(std::memory_order_relaxed);
      const uint64_t decodeQueueLagEstimateUs =
          (presentedCapUs > 0 && h.captureQpcUs >= presentedCapUs)
              ? (h.captureQpcUs - presentedCapUs)
              : 0;
      sample_queue_depth(decodeQueueLagEstimateUs);
      const uint64_t staleBehindPresentedUs =
          (presentedCapUs > 0 && presentedCapUs > h.captureQpcUs)
              ? (presentedCapUs - h.captureQpcUs)
              : 0;
      const uint64_t staleBehindLatestUs =
          (latestCaptureSeenUs > h.captureQpcUs)
              ? (latestCaptureSeenUs - h.captureQpcUs)
              : 0;
      if (staleBehindPresentedUs > staleCaptureDropUs || staleBehindLatestUs > staleCaptureDropUs) {
        ++skippedQueued;
        ++lagDropCount;
        ++staleDropCount;
        if (staleBehindLatestUs > staleCaptureDropUs) {
          ++holdLatestDropCount;
        }
        // Dropping a frame that is NOT older than the last decoded keyframe breaks the still-live
        // reference chain. This is a B=0 low-latency IPPP stream and the wire header carries no
        // ref flag, so every such P must be treated as a reference: decoding later P-frames that
        // referenced the dropped one produces garbage (the corrupted text/scroll seen in the
        // field). Resync on the next IDR instead -- freeze on the last good frame until it lands.
        // A frame older than the anchor is a late/reordered straggler the decoder already resynced
        // past, so quiet-drop stays safe there. Recover once per gap; the wait gate below then
        // drops non-key frames until the IDR and request_keyframe's limiter throttles the ask.
        const bool inLiveReferenceChain = (h.captureQpcUs >= lastDecodedKeyCaptureUs);
        if (inLiveReferenceChain && !waitForKeyFrame) {
          waitForKeyFrame = true;
          decoder.reset();
          request_keyframe(6);  // stale_reference_gap
          ++congestionRecoveryRequestCount;
          ++staleReferenceRecoveryCount;
          std::cout << "[native-video-client] stale-reference recovery seq=" << h.seq
                    << " count=" << staleReferenceRecoveryCount
                    << " staleBehindLatestUs=" << staleBehindLatestUs << "\n";
        }
        if ((lagDropCount % 120) == 1) {
          std::cout << "[native-video-client] stale frame drop count=" << lagDropCount
                    << " staleBehindPresentedUs=" << staleBehindPresentedUs
                    << " staleBehindLatestUs=" << staleBehindLatestUs
                    << " inRefChain=" << (inLiveReferenceChain ? 1 : 0)
                    << " seq=" << h.seq << "\n";
        }
        return true;
      }

      const bool lagTrigger =
          (decodeQueueLagEstimateUs > kDecodeQueueLagDropUs) ||
          (presentedCapUs > 0 && streamLagUs > kCatchupLagDropUs);
      const bool denseArrival = (recvGapUs == 0 || recvGapUs <= 150000);
      // The picker overlay pauses presents on purpose; lag measured against a frozen present
      // anchor is not congestion. Same for the short post-close grace until the anchor is fresh.
      const bool catchupSuppressed =
          gWindowPickerVisible.load(std::memory_order_relaxed) ||
          packetNowUs < gFrameBuf.catchupSuppressUntilUs.load(std::memory_order_relaxed);
      if (lagTrigger && denseArrival && !catchupSuppressed) {
        if (lagTriggerStreak < std::numeric_limits<uint32_t>::max()) {
          ++lagTriggerStreak;
        }
      } else {
        lagTriggerStreak = 0;
      }
      if (congestionState != ClientCongestionState::Congested && lagTriggerStreak >= 3) {
        lagTriggerStreak = 0;
        const bool catchupEnterAllowed =
            (lastCatchupEnterUs == 0) || (packetNowUs >= (lastCatchupEnterUs + catchupReenterMinIntervalUs));
        if (!catchupEnterAllowed) {
          ++catchupEnterThrottledCount;
          if ((catchupEnterThrottledCount % 120) == 1) {
            std::cout << "[native-video-client] catchup-enter-throttled count="
                      << catchupEnterThrottledCount
                      << " streamLagUs=" << streamLagUs
                      << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                      << " minIntervalUs=" << catchupReenterMinIntervalUs
                      << "\n";
          }
        } else {
          transition_congestion_state(ClientCongestionState::Congested, packetNowUs,
                                      (decodeQueueLagEstimateUs > kDecodeQueueLagDropUs)
                                          ? "decode_queue"
                                          : "stream_lag_emergency",
                                      streamLagUs, decodeQueueLagEstimateUs, h.seq);
          catchupMode = true;
          lastCatchupEnterUs = packetNowUs;
          waitForKeyFrame = true;
          decoder.reset();
          request_keyframe(1);
          ++congestionRecoveryRequestCount;
          std::cout << "[native-video-client] catchup enter streamLagUs=" << streamLagUs
                    << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                    << " recvGapUs=" << recvGapUs
                    << " reason="
                    << ((decodeQueueLagEstimateUs > kDecodeQueueLagDropUs) ? "decode_queue" : "stream_lag_emergency")
                    << " seq=" << h.seq << "\n";
        }
      }
      if (congestionState == ClientCongestionState::Congested && !keyFrame) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++lagDropCount;
        ++burstDropCount;
        if ((lagDropCount % 120) == 1) {
          std::cout << "[native-video-client] catchup drops=" << lagDropCount
                    << " streamLagUs=" << streamLagUs
                    << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                    << "\n";
        }
        return true;
      }
      if (congestionState == ClientCongestionState::Congested && keyFrame) {
        catchupMode = false;
        transition_congestion_state(ClientCongestionState::Recovering, packetNowUs, "keyframe",
                                    streamLagUs, decodeQueueLagEstimateUs, h.seq);
        std::cout << "[native-video-client] catchup exit streamLagUs=" << streamLagUs
                  << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                  << " seq=" << h.seq << "\n";
      }
      if (congestionState == ClientCongestionState::Recovering) {
        const bool lagHealthy =
            decodeQueueLagEstimateUs <= kDecodeQueueLagResumeUs &&
            streamLagUs <= kCatchupResumeKeyLagUs;
        if (lagHealthy) {
          if (recoveringHealthyStreak < std::numeric_limits<uint32_t>::max()) {
            ++recoveringHealthyStreak;
          }
        } else {
          recoveringHealthyStreak = 0;
        }
        const bool recoverMinElapsed =
            recoveringSinceUs > 0 && packetNowUs >= (recoveringSinceUs + congestionRecoverMinUs);
        if (lagHealthy && recoverMinElapsed && recoveringHealthyStreak >= 3) {
          transition_congestion_state(ClientCongestionState::Normal, packetNowUs, "recover_stable",
                                      streamLagUs, decodeQueueLagEstimateUs, h.seq);
        } else if (!lagHealthy && !catchupSuppressed &&
                   recoveringSinceUs > 0 &&
                   packetNowUs >= (recoveringSinceUs + congestionRecoveryTimeoutUs)) {
          const bool requestAllowed =
              (lastRecoveryRequestUs == 0) || (packetNowUs >= (lastRecoveryRequestUs + 300000));
          if (requestAllowed) {
            request_keyframe(1);
            ++congestionRecoveryRequestCount;
            lastRecoveryRequestUs = packetNowUs;
          }
          catchupMode = true;
          waitForKeyFrame = true;
          decoder.reset();
          lastCatchupEnterUs = packetNowUs;
          transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "recover_timeout",
                                      streamLagUs, decodeQueueLagEstimateUs, h.seq);
        }
      }

      if (waitForKeyFrame && !keyFrame) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++waitingKeyDropCount;
        ++burstDropCount;
        if ((waitingKeyDropCount % 30) == 1) {
          request_keyframe(3);
        }
        if ((waitingKeyDropCount % 120) == 1) {
          std::cout << "[native-video-client] waiting keyframe drops=" << waitingKeyDropCount << "\n";
        }
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, packetNowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }

      const uint64_t decodeStartUs = qpc_now_us();
      std::vector<DecodedFrameNv12> outFrames;
      const int64_t inputSampleTimeHns = static_cast<int64_t>(h.captureQpcUs) * 10;
      bool pendingTimestampOverflow = false;
      if (!decoder.decode_access_unit(*payloadPtr, keyFrame, inputSampleTimeHns, &outFrames,
                                      &pendingTimestampOverflow)) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++decodeFailCount;
        request_keyframe(4);
        ++congestionRecoveryRequestCount;
        if ((decodeFailCount % 60) == 1) {
          std::cout << "[native-video-client] decode failed count=" << decodeFailCount << "\n";
        }
        catchupMode = true;
        lastCatchupEnterUs = packetNowUs;
        waitForKeyFrame = true;
        if (++decodeConsecutiveFailCount >= kDecodeRebuildThreshold) {
          // Flush did not clear it: the transform or device is wedged. A full rebuild is the
          // only recovery, and it is what the resolution-change path already does -- reached
          // here without a resolution change so the wedge is not caught otherwise.
          std::cout << "[native-video-client] decoder wedged (consecutive fails="
                    << decodeConsecutiveFailCount << "); rebuilding\n";
          if (decoder.initialize(decoderW, decoderH, args.fpsHint)) {
            decodeConsecutiveFailCount = 0;
          }
          // On rebuild failure, keep the streak so the next frame retries the rebuild.
        } else {
          decoder.reset();
        }
        transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "decode_fail",
                                    streamLagUs, decodeQueueLagEstimateUs, h.seq);
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, packetNowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }
      // decode_access_unit succeeded: the transform is healthy, so the wedge streak is clear.
      decodeConsecutiveFailCount = 0;
      if (pendingTimestampOverflow) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++decodeTimestampOverflowCount;
        request_keyframe(4);
        ++congestionRecoveryRequestCount;
        if ((decodeTimestampOverflowCount % 10ULL) == 1ULL) {
          std::cout << "[native-video-client] decoder timestamp queue overflow count="
                    << decodeTimestampOverflowCount << "\n";
        }
        catchupMode = true;
        lastCatchupEnterUs = packetNowUs;
        waitForKeyFrame = true;
        decoder.reset();
        transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "decode_ts_overflow",
                                    streamLagUs, decodeQueueLagEstimateUs, h.seq);
        return true;
      }
      waitForKeyFrame = false;
      if (keyFrame) {
        // Advance the reference-chain anchor: a successfully decoded IDR resyncs the decoder, so
        // any later stale frame older than this is safe to quiet-drop.
        lastDecodedKeyCaptureUs = h.captureQpcUs;
      }
      if (outFrames.empty()) {
        ++decodeEmptyCount;
        ++decodeEmptyStreak;
        if (decodeEmptyStreak == 1) {
          decodeEmptyStreakStartUs = packetNowUs;
        }
        const uint64_t emptyStreakUs =
            (decodeEmptyStreakStartUs > 0 && packetNowUs >= decodeEmptyStreakStartUs)
                ? (packetNowUs - decodeEmptyStreakStartUs)
                : 0;
        if (decodeEmptyStreak >= 12 || emptyStreakUs >= 300000) {
          const bool catchupEnterAllowed =
              (lastCatchupEnterUs == 0) || (packetNowUs >= (lastCatchupEnterUs + catchupReenterMinIntervalUs));
          if (catchupEnterAllowed) {
            ++decodeEmptyRecoveryCount;
            waitForKeyFrame = true;
            catchupMode = true;
            lastCatchupEnterUs = packetNowUs;
            request_keyframe(5);
            ++congestionRecoveryRequestCount;
            decoder.reset();
            transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "decode_empty",
                                        streamLagUs, decodeQueueLagEstimateUs, h.seq);
            if ((decodeEmptyRecoveryCount % 10) == 1) {
              std::cout << "[native-video-client] decode empty recovery count=" << decodeEmptyRecoveryCount
                        << " streak=" << decodeEmptyStreak
                        << " emptyUs=" << emptyStreakUs
                        << "\n";
            }
          } else {
            ++catchupEnterThrottledCount;
            if ((catchupEnterThrottledCount % 120) == 1) {
              std::cout << "[native-video-client] decode-empty-recovery-throttled count="
                        << catchupEnterThrottledCount
                        << " streak=" << decodeEmptyStreak
                        << " emptyUs=" << emptyStreakUs
                        << " minIntervalUs=" << catchupReenterMinIntervalUs
                        << "\n";
            }
          }
          decodeEmptyStreak = 0;
          decodeEmptyStreakStartUs = 0;
        }
        if ((decodeEmptyCount % 120) == 1) {
          std::cout << "[native-video-client] decode output empty count=" << decodeEmptyCount
                    << " streak=" << decodeEmptyStreak
                    << " emptyUs=" << emptyStreakUs
                    << "\n";
        }
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, packetNowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }
      decodeEmptyStreak = 0;
      decodeEmptyStreakStartUs = 0;

      auto& decoded = outFrames.back();
      const bool tsFromMft = decoded.sampleTimeFromOutput && (decoded.sampleTimeHns > 0);
      const bool tsFromInputFallback = (!decoded.sampleTimeFromOutput) && (decoded.sampleTimeHns > 0);
      const bool tsFromHeaderFallback = (decoded.sampleTimeHns <= 0);
      const uint64_t decodedCaptureUs =
          tsFromHeaderFallback ? h.captureQpcUs : static_cast<uint64_t>(decoded.sampleTimeHns / 10);
      const char* tsSource = tsFromMft ? "mft" : (tsFromInputFallback ? "input_fallback" : "header_fallback");
      if (decoded.bytes.empty() && !decoded.surfaceTexture) {
        ++skippedQueued;
        waitForKeyFrame = true;
        return true;
      }
      const uint64_t decodedPayloadBytes = decoded.bytes.empty()
          ? (static_cast<uint64_t>(decoded.width) * decoded.height * 3 / 2)
          : static_cast<uint64_t>(decoded.bytes.size());
      const uint64_t decodeEndUs = qpc_now_us();
      std::shared_ptr<std::vector<uint8_t>> frameNv12;
      if (!decoded.bytes.empty()) {
        frameNv12 = std::make_shared<std::vector<uint8_t>>(std::move(decoded.bytes));
        if (!frameNv12 || frameNv12->empty()) {
          ++skippedQueued;
          waitForKeyFrame = true;
          return true;
        }
      }

      const uint64_t nowUs = qpc_now_us();
      const uint64_t queueSetUs = nowUs;
      const uint64_t decodeToQueueUs = (queueSetUs >= decodeEndUs) ? (queueSetUs - decodeEndUs) : 0;
      {
        std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
        const uint64_t prevVersion = gFrameBuf.frame.version;
        const uint64_t lastPresentedVersion = gFrameBuf.lastPresentedVersion.load(std::memory_order_relaxed);
        // Overwrites while the picker covers the stream are the intended latest-wins behavior of a
        // deliberately paused present, not a symptom -- keep them out of the telemetry.
        if (prevVersion > lastPresentedVersion &&
            !gWindowPickerVisible.load(std::memory_order_relaxed)) {
          ++gFrameBuf.overwriteBeforePresentCount;
        }
        gFrameBuf.frame.format = SharedFrame::PixelFormat::Nv12;
        gFrameBuf.frame.width = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;
        gFrameBuf.frame.height = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;
        gFrameBuf.frame.codedWidth = decoded.width;
        gFrameBuf.frame.codedHeight = decoded.height;
        gFrameBuf.frame.visibleLeft = decoded.visibleLeft;
        gFrameBuf.frame.visibleTop = decoded.visibleTop;
        gFrameBuf.frame.stride = decoded.width;
        gFrameBuf.frame.seq = h.seq;
        gFrameBuf.frame.captureUs = decodedCaptureUs;
        gFrameBuf.frame.encodeStartUs = h.encodeStartQpcUs;
        gFrameBuf.frame.encodeEndUs = h.encodeEndQpcUs;
        gFrameBuf.frame.sendUs = h.sendQpcUs;
        gFrameBuf.frame.recvUs = packetNowUs;
        gFrameBuf.frame.decodeStartUs = decodeStartUs;
        gFrameBuf.frame.decodeEndUs = decodeEndUs;
        gFrameBuf.frame.queueSetUs = queueSetUs;
        gFrameBuf.frame.decodeToQueueUs = decodeToQueueUs;
        gFrameBuf.frame.streamGeneration = h.streamGeneration;
        gFrameBuf.frame.key = keyFrame;
        gFrameBuf.frame.version = prevVersion + 1;
        gFrameBuf.frame.bytes = std::move(frameNv12);
        gFrameBuf.frame.surfaceSample = std::move(decoded.surfaceSample);
        gFrameBuf.frame.surfaceTexture = std::move(decoded.surfaceTexture);
        gFrameBuf.frame.surfaceSubresource = decoded.surfaceSubresource;
      }
      // First real frame of the acknowledged selection just landed. The gate above guarantees it
      // belongs to the selected generation; record the candidate and post the reveal once. The
      // picker flip, input guard and toolbar are committed on the UI thread (after revalidation),
      // not here, so a racing cancel/new-selection/disconnect cannot wrongly close the picker.
      if (gSelectionPending.load(std::memory_order_acquire) &&
          !gSelectionAwaitingAck.load(std::memory_order_acquire)) {
        post_pc_selection_reveal(h.streamGeneration,
                                 gSelectionEpoch.load(std::memory_order_acquire));
      }
      // While the picker overlays a live stream, WM_PAINT redraws the picker (not the video), so
      // a per-frame invalidate would repaint the whole card grid at video cadence for nothing.
      // The reveal above and the picker-close handler invalidate on their own, so the newest
      // decoded frame still shows the moment the picker leaves.
      if (gSession.hwnd && !gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (!gFrameBuf.paintQueued.exchange(true)) {
          InvalidateRect(gSession.hwnd, nullptr, FALSE);
        } else {
          ++gFrameBuf.paintCoalescedCount;
        }
      }

      if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || gPresent.traceRecvPrinted.load() < args.traceMax)) {
        const auto nowPrinted = gPresent.traceRecvPrinted.fetch_add(1) + 1;
        if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
          std::ostringstream oss;
          oss << "[native-video-client][trace_recv] seq=" << h.seq
              << " captureUs=" << decodedCaptureUs
              << " hdrCaptureUs=" << h.captureQpcUs
              << " encodeStartUs=" << h.encodeStartQpcUs
              << " encodeEndUs=" << h.encodeEndQpcUs
              << " sendUs=" << h.sendQpcUs
              << " recvUs=" << packetNowUs
              << " decodeStartUs=" << decodeStartUs
              << " decodeEndUs=" << decodeEndUs
              << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
              << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
              << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
              << " netUs=" << ((packetNowUs >= h.sendQpcUs) ? (packetNowUs - h.sendQpcUs) : 0)
              << " r2dUs=" << ((decodeStartUs >= packetNowUs) ? (decodeStartUs - packetNowUs) : 0)
              << " decUs=" << ((decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0)
              << " decodeQueueLagUs=" << ((h.captureQpcUs >= decodedCaptureUs) ? (h.captureQpcUs - decodedCaptureUs) : 0)
              << " tsSource=" << tsSource
              << " bytes=" << h.payloadSize
              << " key=" << (keyFrame ? 1 : 0);
          log_client_line(oss.str());
        }
      }

      // GNLink stream telemetry (diagnostics only): one line per decoded keyframe, plus any
      // non-key frame whose decode cost ran past 1.5x the expected frame interval. Joins the host
      // 'wire seq=' log by seq+gen. decodeQueueLagUs is the capture-lag estimate already computed
      // for scheduling (not a literal decoder input-queue count -- the MFT does not expose one).
      {
        const uint64_t decodeUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
        const uint64_t decodeAnomalyUs = (frameIntervalUs * 3ULL) / 2ULL;
        if (keyFrame || decodeUs > decodeAnomalyUs) {
          const uint64_t r2dUs = (decodeStartUs >= packetNowUs) ? (decodeStartUs - packetNowUs) : 0;
          std::ostringstream telem;
          telem << "[native-video-client][telemetry] stage=decode"
                << " seq=" << h.seq
                << " gen=" << h.streamGeneration
                << " key=" << (keyFrame ? 1 : 0)
                << " recvUs=" << packetNowUs
                << " decodeStartUs=" << decodeStartUs
                << " decodeEndUs=" << decodeEndUs
                << " decodeUs=" << decodeUs
                << " r2dUs=" << r2dUs
                << " decodeQueueLagUs=" << decodeQueueLagEstimateUs
                << " pts=" << decodedCaptureUs;
          log_client_line(telem.str());
        }
      }

      ++decodedFrames;
      decodedBytes += decodedPayloadBytes;
      // lastPresentedCaptureUs is now updated by render thread via gFrameBuf.lastPresentedCaptureUs
      const uint64_t latencyUs = aligned_lag_us(
          decodedCaptureUs, nowUs, captureTimelineReady, captureRemoteBaseUs, captureLocalBaseUs);
      const uint64_t decodeTailUs = aligned_lag_us(
          h.sendQpcUs, nowUs, sendTimelineReady, sendRemoteBaseUs, sendLocalBaseUs);
      sumLatencyUs += latencyUs;
      sumDecodeTailUs += decodeTailUs;
      maxLatencyUs = std::max(maxLatencyUs, latencyUs);
      maxDecodeTailUs = std::max(maxDecodeTailUs, decodeTailUs);

      if (nowUs >= statAtUs) {
        const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
        const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
        const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
        const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
        const uint64_t decodeRatioX100 =
            (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
        const uint32_t visibleW = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;
        const uint32_t visibleH = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;
        publish_metrics(visibleW, visibleH, nowUs,
                        avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
        std::ostringstream oss;
        oss << "[native-video-client] recvFrames=" << recvFrames
            << " decodedFrames=" << decodedFrames
            << " skippedQueued=" << skippedQueued
            << " avgLatencyUs=" << avgLatencyUs
            << " maxLatencyUs=" << maxLatencyUs
            << " avgDecodeTailUs=" << avgDecodeTailUs
            << " maxDecodeTailUs=" << maxDecodeTailUs
            << " mbps=" << mbps
            << " decodedRawMbps=" << decodedRawMbps
            << " decodeRatioX100=" << decodeRatioX100
            << " size=" << visibleW << "x" << visibleH
            << " codedSize=" << decoded.width << "x" << decoded.height;
        append_congestion_fields(oss);
        append_present_counter_fields(oss);
        log_client_line(oss.str());
        recvFrames = 0;
        decodedFrames = 0;
        skippedQueued = 0;
        recvBytes = 0;
        decodedBytes = 0;
        sumLatencyUs = 0;
        maxLatencyUs = 0;
        sumDecodeTailUs = 0;
        maxDecodeTailUs = 0;
        statAtUs += 1000000ULL;
      }
      return true;
    };

    if (transport == VideoTransport::Udp) {
      std::array<uint8_t, 1600> datagram{};
      const uint32_t effectiveUdpSimDropSeed = (udpSimDropSeed > 0)
                                                   ? udpSimDropSeed
                                                   : static_cast<uint32_t>(qpc_now_us() & 0x7fffffffu);
      std::minstd_rand udpSimRng(effectiveUdpSimDropSeed);
      std::uniform_int_distribution<uint32_t> udpSimDropDist(0, 999);
      UdpH264FrameAssembler assembler;
      uint64_t assemblyDropped = 0;
      uint64_t oversizePayloadDropCount = 0;
      uint64_t udpSimDroppedCount = 0;
      uint64_t udpSimAcceptedCount = 0;
      uint64_t udpAssemblyStatAtUs = qpc_now_us() + 1000000ULL;
      uint64_t lastUdpChunkRecvCount = 0;
      uint64_t lastUdpAssemblyCompletedCount = 0;
      uint64_t lastUdpAssemblyDroppedCount = 0;
      uint64_t lastUdpAssemblyMalformedCount = 0;
      uint64_t lastUdpAssemblyReorderCount = 0;
      uint64_t lastUdpAssemblyKeyReqCount = 0;
      uint64_t lastUdpAssemblyFecRecoveredCount = 0;
      uint64_t lastUdpSimDroppedCount = 0;
      uint64_t lastUdpSimAcceptedCount = 0;

      while (gSession.running.load()) {
        const int n = recv(gSession.sock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
        if (n <= 0) {
          // A read timeout is not a dead socket. It is also the tunnel's heartbeat: the control
          // thread spends most of its time blocked waiting for a reply, so if retransmission
          // were driven from there it would stop exactly when a reply goes missing -- and the
          // host, hearing nothing, declares the client lost. This thread always runs.
          if (remote60::native_poc::last_socket_error_is_retryable()) {
            if (gControlOverUdp.load(std::memory_order_acquire)) gUdpControl.Tick();
            continue;
          }
          break;
        }
        if (gControlOverUdp.load(std::memory_order_acquire)) gUdpControl.Tick();
        // Control is offered the datagram BEFORE the video length guard, and the order is the
        // whole point. A control message is not bounded below by the video header: a
        // single-fragment input ack is 32 + 28 = 60 bytes against an 88-byte video header, so
        // checking the video size first silently ate every small reply -- input acks and window
        // selections -- while the larger ones (pong, window lists) came through and made the
        // channel look healthy. OnPacket claims only its own kinds, so video cannot be stolen.
        if (gControlOverUdp.load(std::memory_order_acquire) &&
            gUdpControl.OnPacket(datagram.data(), static_cast<size_t>(n))) {
          continue;
        }
        // Remote hardware-cursor sample: smaller than the video header, so it must be claimed
        // before the size guard below silently eats it. Latest-wins into atomics; the UI timer
        // does the mapping and drawing.
        if (n == static_cast<int>(sizeof(remote60::native_poc::UdpCursorPosPacket))) {
          remote60::native_poc::UdpCursorPosPacket cp{};
          std::memcpy(&cp, datagram.data(), sizeof(cp));
          if (cp.magic == remote60::native_poc::kMagic &&
              cp.kind == static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::CursorPos) &&
              cp.size == sizeof(cp)) {
            // Bounds sanity before the values reach mapping math: a malformed peer packet must
            // not be able to feed the clamp arithmetic absurd dimensions. Claimed either way.
            if (cp.captureW >= 2 && cp.captureW <= 16384 && cp.captureH >= 2 &&
                cp.captureH <= 16384) {
              gRemoteCursorX.store(cp.x, std::memory_order_relaxed);
              gRemoteCursorY.store(cp.y, std::memory_order_relaxed);
              gRemoteCursorCapW.store(cp.captureW, std::memory_order_relaxed);
              gRemoteCursorCapH.store(cp.captureH, std::memory_order_relaxed);
              gRemoteCursorGeneration.store(cp.streamGeneration, std::memory_order_relaxed);
              gRemoteCursorVisible.store((cp.flags & 0x1u) != 0, std::memory_order_relaxed);
              gRemoteCursorUpdateUs.store(qpc_now_us(), std::memory_order_release);
            }
            continue;
          }
        }
        if (n < static_cast<int>(sizeof(UdpVideoChunkHeader))) continue;

        UdpVideoChunkHeader u{};
        std::memcpy(&u, datagram.data(), sizeof(u));
        if (u.magic != remote60::native_poc::kMagic ||
            u.kind != static_cast<uint16_t>(UdpPacketKind::VideoChunk) ||
            u.size != sizeof(UdpVideoChunkHeader)) {
          continue;
        }
        if (u.codec != static_cast<uint16_t>(UdpCodec::H264)) {
          ++skippedQueued;
          continue;
        }
        if (udpSimDropPm > 0) {
          const uint32_t samplePm = udpSimDropDist(udpSimRng);
          if (samplePm < udpSimDropPm) {
            ++udpSimDroppedCount;
            ++skippedQueued;
            continue;
          }
        }
        ++udpSimAcceptedCount;
        ++udpChunkRecvCount;

        const auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n));
        if (assembleResult.fecRecovered) {
          udpAssemblyFecRecoveredCount += assembleResult.fecRecoveredChunks;
        }
        bool discontinuityHandled = false;
        auto handle_udp_discontinuity = [&]() {
          if (discontinuityHandled) return;
          discontinuityHandled = true;
          waitForKeyFrame = true;
          decoder.reset();
          request_keyframe(2);
          ++udpAssemblyKeyReqCount;
        };
        if (assembleResult.droppedPreviousIncomplete) {
          ++assemblyDropped;
          ++udpAssemblyDroppedCount;
          handle_udp_discontinuity();
        }

        if (assembleResult.disposition == UdpH264AssemblyDisposition::Malformed) {
          ++skippedQueued;
          ++udpAssemblyMalformedCount;
          handle_udp_discontinuity();
          if (assembleResult.oversizePayload && ((++oversizePayloadDropCount % 30ULL) == 1ULL)) {
            std::cout << "[native-video-client] dropped oversized udp payload bytes="
                      << assembleResult.rejectedPayloadSize
                      << " count=" << oversizePayloadDropCount << "\n";
          }
          continue;
        }

        if (assembleResult.disposition == UdpH264AssemblyDisposition::Dropped) {
          ++skippedQueued;
          ++assemblyDropped;
          ++udpAssemblyDroppedCount;
          if (assembleResult.reorderDetected) ++udpAssemblyReorderCount;
          handle_udp_discontinuity();
          if ((assemblyDropped % 120) == 1) {
            std::cout << "[native-video-client] udp assembly drop count=" << assemblyDropped
                      << " seq=" << u.seq
                      << " expectedSeq=" << assembleResult.expectedSeq
                      << " chunkOffset=" << u.chunkOffset
                      << " nextOffset=" << assembleResult.expectedNextOffset
                      << "\n";
          }
          continue;
        }

        if (assembleResult.disposition == UdpH264AssemblyDisposition::Completed) {
          ++udpAssemblyCompletedCount;
          const uint64_t packetNowUs = qpc_now_us();
          // GNLink stream telemetry (diagnostics only): one line per assembled keyframe, plus any
          // non-key frame that needed FEC repair or showed loss/reorder, so a periodic-stutter
          // session joins the host 'wire seq=' log by seq+gen while steady play stays quiet.
          {
            const auto& fh = assembleResult.frame.header;
            const bool key = ((fh.flags & 1u) != 0);
            if (key || assembleResult.fecRecovered || assembleResult.reorderDetected ||
                assembleResult.droppedPreviousIncomplete) {
              std::ostringstream telem;
              telem << "[native-video-client][telemetry] stage=assembly"
                    << " seq=" << fh.seq
                    << " gen=" << fh.streamGeneration
                    << " key=" << (key ? 1 : 0)
                    << " lastChunkRecvUs=" << packetNowUs
                    << " bytes=" << fh.payloadSize
                    << " fecRecovered=" << (assembleResult.fecRecovered ? 1 : 0)
                    << " fecRecoveredChunks=" << assembleResult.fecRecoveredChunks
                    << " reorder=" << (assembleResult.reorderDetected ? 1 : 0)
                    << " droppedPrev=" << (assembleResult.droppedPreviousIncomplete ? 1 : 0);
              log_client_line(telem.str());
            }
          }
          auto payload = std::move(assembleResult.frame.payload);
          if (!process_h264_frame(assembleResult.frame.header, &payload, packetNowUs)) break;
        }

        const uint64_t nowUs = qpc_now_us();
        if (nowUs >= udpAssemblyStatAtUs) {
          const uint64_t chunksDelta = udpChunkRecvCount - lastUdpChunkRecvCount;
          const uint64_t completedDelta = udpAssemblyCompletedCount - lastUdpAssemblyCompletedCount;
          const uint64_t droppedDelta = udpAssemblyDroppedCount - lastUdpAssemblyDroppedCount;
          const uint64_t malformedDelta = udpAssemblyMalformedCount - lastUdpAssemblyMalformedCount;
          const uint64_t reorderDelta = udpAssemblyReorderCount - lastUdpAssemblyReorderCount;
          const uint64_t keyReqDelta = udpAssemblyKeyReqCount - lastUdpAssemblyKeyReqCount;
          const uint64_t fecRecoveredDelta =
              udpAssemblyFecRecoveredCount - lastUdpAssemblyFecRecoveredCount;
          const uint64_t simDroppedDelta = udpSimDroppedCount - lastUdpSimDroppedCount;
          const uint64_t simAcceptedDelta = udpSimAcceptedCount - lastUdpSimAcceptedCount;
          const uint64_t simTotalDelta = simDroppedDelta + simAcceptedDelta;
          const uint64_t simDropPermille = (simTotalDelta > 0)
              ? ((simDroppedDelta * 1000ULL) / simTotalDelta)
              : 0;
          const uint64_t totalFramesDelta = completedDelta + droppedDelta;
          const uint64_t dropPermille = (totalFramesDelta > 0)
              ? ((droppedDelta * 1000ULL) / totalFramesDelta)
              : 0;
          udpAssemblyDropPmLast = static_cast<uint32_t>(std::min<uint64_t>(dropPermille, 1000ULL));
          std::cout << "[native-video-client] udp-assembly chunks=" << chunksDelta
                    << " completed=" << completedDelta
                    << " dropped=" << droppedDelta
                    << " dropPm=" << dropPermille
                    << " malformed=" << malformedDelta
                    << " reorder=" << reorderDelta
                    << " keyReq=" << keyReqDelta
                    << " fecRecovered=" << fecRecoveredDelta
                    << " simDropPm=" << simDropPermille
                    << " simDropTotal=" << simDroppedDelta
                    << " waitForKey=" << (waitForKeyFrame ? 1 : 0)
                    << " catchup=" << (catchupMode ? 1 : 0)
                    << "\n";
          lastUdpChunkRecvCount = udpChunkRecvCount;
          lastUdpAssemblyCompletedCount = udpAssemblyCompletedCount;
          lastUdpAssemblyDroppedCount = udpAssemblyDroppedCount;
          lastUdpAssemblyMalformedCount = udpAssemblyMalformedCount;
          lastUdpAssemblyReorderCount = udpAssemblyReorderCount;
          lastUdpAssemblyKeyReqCount = udpAssemblyKeyReqCount;
          lastUdpAssemblyFecRecoveredCount = udpAssemblyFecRecoveredCount;
          lastUdpSimDroppedCount = udpSimDroppedCount;
          lastUdpSimAcceptedCount = udpSimAcceptedCount;
          udpAssemblyStatAtUs += 1000000ULL;
        }
        if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
          break;
        }
      }

      gSession.running = false;
      if (gSession.hwnd) PostMessageW(gSession.hwnd, WM_CLOSE, 0, 0);
      return;
    }

    while (gSession.running.load()) {
      MessageHeader header{};
      if (!remote60::native_poc::recv_all(gSession.sock, &header, sizeof(header))) break;
      if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
      const auto msgType = static_cast<MessageType>(header.type);

      if (msgType == MessageType::RawFrameBgra && header.size == sizeof(RawFrameHeader)) {
        RawFrameHeader h{};
        h.header = header;
        if (!remote60::native_poc::recv_all(gSession.sock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
        std::vector<uint8_t> payload(h.payloadSize);
        if (!remote60::native_poc::recv_all(gSession.sock, payload.data(), payload.size())) break;

        if (!useRaw) {
          ++skippedQueued;
          continue;
        }

        const uint64_t nowUs = qpc_now_us();
        const uint64_t queueSetUs = nowUs;
        auto frameBgra = std::make_shared<std::vector<uint8_t>>(std::move(payload));
        if (!frameBgra || frameBgra->empty()) {
          ++skippedQueued;
          continue;
        }
        {
          std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
          const uint64_t prevVersion = gFrameBuf.frame.version;
          const uint64_t lastPresentedVersion = gFrameBuf.lastPresentedVersion.load(std::memory_order_relaxed);
          if (prevVersion > lastPresentedVersion) {
            ++gFrameBuf.overwriteBeforePresentCount;
          }
          gFrameBuf.frame.format = SharedFrame::PixelFormat::Bgra32;
          gFrameBuf.frame.width = h.width;
          gFrameBuf.frame.height = h.height;
          gFrameBuf.frame.codedWidth = h.width;
          gFrameBuf.frame.codedHeight = h.height;
          gFrameBuf.frame.visibleLeft = 0;
          gFrameBuf.frame.visibleTop = 0;
          gFrameBuf.frame.stride = h.stride;
          gFrameBuf.frame.seq = h.seq;
          gFrameBuf.frame.captureUs = h.captureQpcUs;
          gFrameBuf.frame.encodeStartUs = h.encodeStartQpcUs;
          gFrameBuf.frame.encodeEndUs = h.encodeEndQpcUs;
          gFrameBuf.frame.sendUs = h.sendQpcUs;
          gFrameBuf.frame.recvUs = nowUs;
          gFrameBuf.frame.decodeStartUs = nowUs;
          gFrameBuf.frame.decodeEndUs = nowUs;
          gFrameBuf.frame.queueSetUs = queueSetUs;
          gFrameBuf.frame.decodeToQueueUs = 0;
          gFrameBuf.frame.streamGeneration = h.streamGeneration;
          gFrameBuf.frame.key = false;  // raw BGRA has no keyframe concept; keeps present telemetry quiet.
          gFrameBuf.frame.version = prevVersion + 1;
          gFrameBuf.frame.bytes = std::move(frameBgra);
          gFrameBuf.frame.surfaceSample.Reset();
          gFrameBuf.frame.surfaceTexture.Reset();
          gFrameBuf.frame.surfaceSubresource = 0;
        }
        if (gSession.hwnd) {
          if (!gFrameBuf.paintQueued.exchange(true)) {
            InvalidateRect(gSession.hwnd, nullptr, FALSE);
          } else {
            ++gFrameBuf.paintCoalescedCount;
          }
        }

        if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
            (args.traceMax == 0 || gPresent.traceRecvPrinted.load() < args.traceMax)) {
          const auto nowPrinted = gPresent.traceRecvPrinted.fetch_add(1) + 1;
          if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
            std::ostringstream oss;
            oss << "[native-video-client][trace_recv] seq=" << h.seq
                << " captureUs=" << h.captureQpcUs
                << " encodeStartUs=" << h.encodeStartQpcUs
                << " encodeEndUs=" << h.encodeEndQpcUs
                << " sendUs=" << h.sendQpcUs
                << " recvUs=" << nowUs
                << " decodeStartUs=" << nowUs
                << " decodeEndUs=" << nowUs
                << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
                << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
                << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
                << " netUs=" << ((nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0)
                << " r2dUs=0"
                << " decUs=0"
                << " bytes=" << h.payloadSize;
            log_client_line(oss.str());
          }
        }

        ++recvFrames;
        ++decodedFrames;
        recvBytes += h.payloadSize;
        decodedBytes += static_cast<uint64_t>(h.payloadSize);
        const uint64_t latencyUs = (nowUs >= h.captureQpcUs) ? (nowUs - h.captureQpcUs) : 0;
        const uint64_t decodeTailUs = (nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0;
        sumLatencyUs += latencyUs;
        sumDecodeTailUs += decodeTailUs;
        maxLatencyUs = std::max(maxLatencyUs, latencyUs);
        maxDecodeTailUs = std::max(maxDecodeTailUs, decodeTailUs);

        if (nowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (recvFrames > 0) ? (sumLatencyUs / recvFrames) : 0;
          const uint64_t avgDecodeTailUs = (recvFrames > 0) ? (sumDecodeTailUs / recvFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, nowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
      } else if (msgType == MessageType::EncodedFrameH264 && header.size == sizeof(EncodedFrameHeader)) {
        EncodedFrameHeader h{};
        h.header = header;
        if (!remote60::native_poc::recv_all(gSession.sock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
        std::vector<uint8_t> payload(h.payloadSize);
        if (!remote60::native_poc::recv_all(gSession.sock, payload.data(), payload.size())) break;
        const uint64_t packetNowUs = qpc_now_us();
        if (!process_h264_frame(h, &payload, packetNowUs)) break;
      } else {
        const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
        if (bodySize > 0 && !remote60::native_poc::recv_discard(gSession.sock, bodySize)) break;
        ++skippedQueued;
      }

      const uint64_t nowUs = qpc_now_us();
      if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
        break;
      }
    }
    gSession.running = false;
    if (gSession.hwnd) PostMessageW(gSession.hwnd, WM_CLOSE, 0, 0);
  });

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
  gUdpControl.Close(remote60::native_poc::ControlCloseReason::Shutdown);
  gInputMacro.StopPlayback();
  gInputMacro.StopRecording();
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

  if (useH264) {
    {
      std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
      gFrameBuf.frame.surfaceSample.Reset();
      gFrameBuf.frame.surfaceTexture.Reset();
      gFrameBuf.frame.bytes.reset();
    }
    decoder.shutdown();
    if (mfStarted) MFShutdown();
  }

  std::cout << "[native-video-client] done\n";
  return 0;
}
