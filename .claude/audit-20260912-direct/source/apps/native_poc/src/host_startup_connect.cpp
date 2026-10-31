// Host startup 2/5: the client connection -- TCP listen/accept, or UDP bind (port candidates, LAN
// direct-dial listener, directory agent) + the Hello handshake; secure-input broker; socket buffers.
//
// Host split refactor Phase 2-12: moved verbatim out of main() (native_video_host_main.cpp); see
// host_startup.hpp for the call order and HostContext (host_main_loop.hpp) for the shared state.

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
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "mf_h264_codec.hpp"
#include "bind_port_candidates.hpp"
#include "capture_cadence_gate.hpp"
#include "d3d_capture_readback.hpp"
#include "directory_client.hpp"
#include "encode_resolution_ladder.hpp"
#include "gdi_capture_process.hpp"
#include "json_profile.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "secure_input_broker.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "capture_backend_dxgi.hpp"
#include "host_string_util.hpp"
#include "host_log.hpp"
#include "host_args.hpp"
#include "host_bgra_scale.hpp"
#include "host_bottleneck.hpp"
#include "host_frame_state.hpp"
#include "host_gpu_scaler.hpp"
#include "host_window_enum.hpp"
#include "host_capture_device.hpp"
#include "host_net_io.hpp"
#include "host_input_inject.hpp"
#include "host_frame_gate.hpp"
#include "host_abr.hpp"
#include "host_kick.hpp"
#include "host_client_metrics.hpp"
#include "host_backend_policy.hpp"
#include "host_watchdog.hpp"
#include "host_input_router.hpp"
#include "host_encoded_sender.hpp"
#include "host_session.hpp"
#include "host_encoder_manager.hpp"
#include "host_stats.hpp"
#include "host_capture_session.hpp"
#include "host_control_session.hpp"
#include "host_main_loop.hpp"
#include "host_startup.hpp"

#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT
#define REMOTE60_NATIVE_ENCODED_EXPERIMENT 0
#endif

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::host::DesktopCaptureBackend;
using remote60::host::DxgiDesktopCaptureConfig;
using remote60::host::DxgiDesktopCaptureSession;

namespace remote60::native_poc {

int startup_connect_client(HostContext& hx) {
  auto& args = hx.args;
  auto& transport = hx.transport;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  // After a backlog drop every delta references frames that never went out; shipping them
  // paints macroblock corruption on the client until the next IDR. They are held back here
  // until the requested keyframe actually passes through.
  // Session media barrier. Bumped (under sender.mu) by the rollover transaction in pump_udp_hello;
  // read by the sender at dequeue to fence any item stamped for a previous session. Every item is
  // stamped with this value when enqueued. Starts at 1 to match clientSession.epoch.
  // Set by the sender thread when a same-epoch transport error re-armed the barrier. The main loop
  // consumes it at its top -> encoder.forceKeyNext + arm_trailing_kick, because sender.requestKey is only
  // consumed after a real frame is popped: on a static desktop no new frame arrives to carry it, so
  // the recovery IDR would never be produced. This is the only barrier-recovery signal that works
  // when the screen is not changing. encoder.forceKeyNext must never be written from the sender thread.
  // IDR telemetry written by the sender thread (per current media epoch): when the first key AU of
  // this session hit the wire, and the size/chunk count of the last key AU sent. Reset by the
  // rollover transaction so they describe the current session, not the previous one. Diagnostic
  // only -- never wired into ABR evidence.
  // The reader thread owns the peer address; the render loop picks up changes through these.
  if (transport == VideoTransport::Tcp) {
    clientSession.listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSession.listenSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] listen socket create failed\n";
      return 2;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(args.bindPort);
    local.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
    if (bind(clientSession.listenSock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
      std::cerr << "[native-video-host] bind failed port=" << args.bindPort << "\n";
      closesocket(clientSession.listenSock);
      return 3;
    }
    if (listen(clientSession.listenSock, 1) != 0) {
      std::cerr << "[native-video-host] listen failed\n";
      closesocket(clientSession.listenSock);
      return 4;
    }

    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    clientSession.clientSock = accept(clientSession.listenSock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    if (clientSession.clientSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] accept failed\n";
      closesocket(clientSession.listenSock);
      clientSession.listenSock = INVALID_SOCKET;
      return 5;
    }

    int noDelay = 1;
    setsockopt(clientSession.clientSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  } else {
    clientSession.clientSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSession.clientSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] udp socket create failed\n";
      return 2;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
    // Walk the candidates in order and keep the first that binds. A failed bind leaves the
    // socket unbound, so the next attempt can reuse it.
    std::vector<uint16_t> portCandidates = args.bindPortCandidates;
    if (portCandidates.empty()) portCandidates.push_back(args.bindPort);
    bool udpBound = false;
    for (const uint16_t candidate : portCandidates) {
      local.sin_port = htons(candidate);
      if (bind(clientSession.clientSock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0) {
        clientSession.mediaBindPort = candidate;
        udpBound = true;
        break;
      }
      std::cerr << "[native-video-host] udp bind failed port=" << candidate << "; trying next\n";
    }
    if (!udpBound) {
      std::cerr << "[native-video-host] udp bind failed on every candidate port\n";
      closesocket(clientSession.clientSock);
      return 3;
    }
    std::cout << "[native-video-host] udp bound port=" << clientSession.mediaBindPort << "\n";

    // Keep the last candidate listening as well, so dialling this PC by address still works.
    //
    // The candidate list exists to move the host onto a port restrictive networks allow, and
    // moving it is exactly what breaks the other way in: both clients default to the legacy port
    // when someone types an address by hand. The directory path is unaffected -- it dials
    // hostPublicUdpPort, which follows whatever the primary socket was given -- but a LAN user
    // has nothing telling them the port changed.
    //
    // Only the handshake watches both. Whichever socket the Hello arrives on becomes the media
    // socket and everything downstream is unchanged, so a session that never uses this listener
    // behaves exactly as it did before.
    const uint16_t lanPort =
        portCandidates.size() > 1 ? portCandidates.back() : 0;
    if (lanPort != 0 && lanPort != clientSession.mediaBindPort) {
      clientSession.lanSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (clientSession.lanSock != INVALID_SOCKET) {
        sockaddr_in lanAddr{};
        lanAddr.sin_family = AF_INET;
        lanAddr.sin_port = htons(lanPort);
        lanAddr.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
        if (bind(clientSession.lanSock, reinterpret_cast<const sockaddr*>(&lanAddr), sizeof(lanAddr)) == 0) {
          std::cout << "[native-video-host] lan direct-dial listener port=" << lanPort << "\n";
        } else {
          // Not fatal: the primary socket is the one that matters, and the usual reason this
          // fails is another GNLink host already holding the legacy port.
          std::cout << "[native-video-host] lan direct-dial listener unavailable port=" << lanPort
                    << "\n";
          closesocket(clientSession.lanSock);
          clientSession.lanSock = INVALID_SOCKET;
        }
      }
    }

    // The directory agent shares this socket on purpose: the public address it publishes has
    // to be the one NAT maps for the media stream, and that is a property of this socket.
    if (!clientSession.directoryUrl.empty()) {
      remote60::native_poc::directory::HostAgentConfig dirCfg;
      dirCfg.url = clientSession.directoryUrl;
      dirCfg.accountId = clientSession.directoryId;
      dirCfg.password = clientSession.directoryPw;
      dirCfg.hostName = args.directoryHostName;
      dirCfg.observeUdpPort = args.directoryObservePort;
      dirCfg.localUdpPort = clientSession.mediaBindPort;
      // The legacy/alternate listener from N6. Publishing it is what lets a client whose network
      // filters the primary port have something else to dial.
      dirCfg.alternateUdpPort = lanPort;
      dirCfg.heartbeatSeconds = env_u32_clamped("REMOTE60_DIRECTORY_HEARTBEAT_SEC", 25, 5, 300);
      std::string dirError;
      const bool started = clientSession.directoryAgent.Start(
          dirCfg,
          [clientSock = clientSession.clientSock](const void* data, size_t len, const sockaddr_in& to) {
            (void)sendto(clientSock, static_cast<const char*>(data), static_cast<int>(len), 0,
                         reinterpret_cast<const sockaddr*>(&to), sizeof(to));
          },
          &dirError);
      if (!started) {
        // Not fatal: direct LAN connections still work, so say why and carry on.
        std::cerr << "[native-video-host] directory disabled: " << dirError << "\n";
      } else {
        std::cout << "[native-video-host] directory agent started url=" << clientSession.directoryUrl << "\n";
      }
    }

    for (;;) {
      // Wait on the primary and, when present, the legacy direct-dial listener. Reading only the
      // primary would leave a LAN client's Hello sitting unanswered forever.
      SOCKET readySock = clientSession.clientSock;
      if (clientSession.lanSock != INVALID_SOCKET) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(clientSession.clientSock, &readSet);
        FD_SET(clientSession.lanSock, &readSet);
        timeval wait{};
        wait.tv_sec = 1;
        const int ready = select(0, &readSet, nullptr, nullptr, &wait);
        if (ready == 0) continue;
        if (ready == SOCKET_ERROR) {
          std::cerr << "[native-video-host] udp handshake select failed err=" << WSAGetLastError()
                    << "\n";
          closesocket(clientSession.clientSock);
          return 5;
        }
        // The primary wins a tie: it is the one the directory published.
        readySock = FD_ISSET(clientSession.clientSock, &readSet) ? clientSession.clientSock : clientSession.lanSock;
      }

      // Big enough for the directory's observation reply; a datagram larger than the buffer
      // would be dropped with WSAEMSGSIZE and taken for a handshake failure.
      uint8_t rx[kUdpReceiveBufferBytes];
      sockaddr_in peer{};
      int peerLen = sizeof(peer);
      const int n = recvfrom(readySock, reinterpret_cast<char*>(rx), sizeof(rx), 0,
                             reinterpret_cast<sockaddr*>(&peer), &peerLen);
      // Zero-length datagrams are legal (NAT keepalives, scanners) and must not end the
      // process while it waits for a real client.
      if (n == 0) continue;
      if (n < 0) {
        const int err = WSAGetLastError();
        if (err == WSAEMSGSIZE || err == WSAECONNRESET) continue;
        std::cerr << "[native-video-host] udp handshake recv failed err=" << err << "\n";
        closesocket(clientSession.clientSock);
        return 5;
      }
      UdpHelloPacket hello{};
      bool isHello = n >= static_cast<int>(sizeof(UdpHelloPacket));
      if (isHello) {
        std::memcpy(&hello, rx, sizeof(hello));
        isHello = hello.magic == remote60::native_poc::kMagic &&
                  hello.kind == static_cast<uint16_t>(UdpPacketKind::Hello) &&
                  hello.version == remote60::native_poc::kUdpProtocolVersion &&
                  (hello.features & remote60::native_poc::kUdpFeatureVideoFec) != 0;
      }
      if (!isHello) {
        // Only the primary socket carries directory traffic; the legacy listener never had a
        // punch or an observation sent to it, and feeding it in would let unrelated LAN noise
        // interrupt the heartbeat.
        if (readySock == clientSession.clientSock) {
          (void)clientSession.directoryAgent.ConsumeUdpPacket(rx, static_cast<size_t>(n), peer);
        }
        continue;
      }

      sender.fecInterleaved.store(
          (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved) != 0,
          std::memory_order_relaxed);

      UdpHelloPacket ack{};
      ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
      ack.features = remote60::native_poc::kUdpFeatureVideoFec |
                     (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved);
      // Video NACK: advertise host support; serve retransmits only if the client asked. (video NACK.)
      ack.features |= remote60::native_poc::kUdpFeatureVideoNack;
      sender.nackEnabled.store((hello.features & remote60::native_poc::kUdpFeatureVideoNack) != 0,
                               std::memory_order_relaxed);
      size_t tokenLen = 0;
      while (tokenLen < sizeof(hello.authToken) && hello.authToken[tokenLen] != '\0') ++tokenLen;
      if (tokenLen > 0) {
        const std::string authToken(hello.authToken, hello.authToken + tokenLen);
        if (!clientSession.AuthorizeDirectorySession(authToken, peer)) {
          std::cerr << "[native-video-host] rejected udp hello with invalid directory capability\n";
          continue;
        }
        clientSession.directoryAuthenticated.store(true, std::memory_order_release);
        ack.features |= remote60::native_poc::kUdpFeatureDirectoryAuth;
      }
      (void)sendto(readySock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                   reinterpret_cast<const sockaddr*>(&peer), peerLen);

      // The socket that answered becomes the media socket for the rest of the session, so
      // everything downstream keeps using clientSession.clientSock exactly as before.
      if (readySock != clientSession.clientSock) {
        std::cout << "[native-video-host] client arrived on the lan direct-dial listener; "
                     "media moves to port "
                  << lanPort << "\n";
        // The directory agent captured the primary socket and must keep heartbeating on it, so
        // it is retired rather than closed -- otherwise the host drops off the directory the
        // moment someone connects over the LAN.
        clientSession.retiredSock = clientSession.clientSock;
        clientSession.clientSock = clientSession.lanSock;
        clientSession.lanSock = INVALID_SOCKET;
      } else if (clientSession.lanSock != INVALID_SOCKET) {
        closesocket(clientSession.lanSock);
        clientSession.lanSock = INVALID_SOCKET;
      }

      sender.udpPeer = peer;
      sender.udpPeerReady = true;
      {
        std::lock_guard<std::mutex> lk(sender.mu);
        sender.peer = peer;
        sender.peerReady = true;
      }
      sender.udpPeerIpNet.store(peer.sin_addr.s_addr, std::memory_order_release);
      sender.udpPeerPortNet.store(peer.sin_port, std::memory_order_release);
      break;
    }
    // Stays blocking: a dedicated reader thread now owns receives, and control messages must
    // not wait for the next render-loop iteration. The timeout only exists so that thread can
    // notice shutdown.
    (void)remote60::native_poc::set_recv_timeout(clientSession.clientSock, 200);
  }

  if (clientSession.directoryAuthenticated.load(std::memory_order_acquire)) {
    std::string secureInputStatus;
    const std::wstring servicePath = remote60::native_poc::sibling_executable_path(
        L"GNLinkInputService.exe");
    const bool secureInputReady =
        inputRouter.broker.EnsureInstalledAndConnected(servicePath, &secureInputStatus);
    std::cout << "[native-video-host] secure-input ready=" << (secureInputReady ? 1 : 0)
              << " status=" << secureInputStatus << "\n";
  }

  if (transport == VideoTransport::Udp && args.tcpSendBufKb == 0) {
    const int sendBuf = 1024 * 1024;
    (void)setsockopt(clientSession.clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }
  if (args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
    setsockopt(clientSession.clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }
  int effectiveSendBuf = 0;
  int effectiveSendBufLen = sizeof(effectiveSendBuf);
  (void)getsockopt(clientSession.clientSock, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
  std::cout << "[native-video-host] client connected transport=" << video_transport_name(transport) << "\n";
  std::cout << "[native-video-host] socket sndbuf=" << effectiveSendBuf << " bytes\n";
  return 0;
}

}  // namespace remote60::native_poc
