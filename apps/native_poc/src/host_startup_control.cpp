// Host startup 3/5: the control threads -- TCP control accept loop, UDP control channel + reader
// thread (Hello / session epoch) + dispatcher thread.
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

void startup_start_control_threads(HostContext& hx, ControlSessionServer& controlServer) {
  auto& args = hx.args;
  auto& transport = hx.transport;
  auto& stop = hx.stop;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  if (args.controlPort > 0) {
    clientSession.controlListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSession.controlListenSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] control listen socket create failed port=" << args.controlPort << "\n";
    } else {
      sockaddr_in ctlLocal{};
      ctlLocal.sin_family = AF_INET;
      ctlLocal.sin_port = htons(args.controlPort);
      ctlLocal.sin_addr.s_addr = resolve_bind_address(args.bindAddress);
      if (bind(clientSession.controlListenSock, reinterpret_cast<const sockaddr*>(&ctlLocal), sizeof(ctlLocal)) != 0 ||
          listen(clientSession.controlListenSock, 1) != 0) {
        std::cerr << "[native-video-host] control bind/listen failed port=" << args.controlPort << "\n";
        closesocket(clientSession.controlListenSock);
        clientSession.controlListenSock = INVALID_SOCKET;
      } else {
        std::cout << "[native-video-host] control waiting port=" << args.controlPort << "\n";
        clientSession.controlThread = std::thread([&]() {
          while (!stop.load()) {
            sockaddr_in cpeer{};
            int cpeerLen = sizeof(cpeer);
            SOCKET acceptedSock = accept(clientSession.controlListenSock, reinterpret_cast<sockaddr*>(&cpeer), &cpeerLen);
            if (acceptedSock == INVALID_SOCKET) {
              if (stop.load()) break;
              Sleep(50);
              continue;
            }
            clientSession.controlClientSock = acceptedSock;
            int ctlNoDelay = 1;
            setsockopt(acceptedSock, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
            std::cout << "[native-video-host][control] client connected\n";
            {
              TcpControlLink link(acceptedSock);
              controlServer.Serve(link);
            }
            // This thread is the ONLY closer of an accepted control socket. Taking the handle
            // out of controlClientSock with an exchange is what makes that true: shutdown_host
            // only shutdown()s whatever it finds there (to break the blocking read) and never
            // closes, so the descriptor cannot be closed twice and a recycled value cannot be
            // closed out from under a later socket. (Ledger H-02.)
            const SOCKET mine = clientSession.controlClientSock.exchange(INVALID_SOCKET);
            if (mine != INVALID_SOCKET) {
              shutdown(mine, SD_BOTH);
              closesocket(mine);
            }
            std::cout << "[native-video-host][control] tcp client disconnected\n";
          }
        });
      }
    }
  }

  // Control over the media socket. A client that arrived through the directory service has no
  // way to open a TCP connection back to us, so the same dispatch is also served here; a LAN
  // client that prefers TCP simply never sends control datagrams and this stays idle.

  // ---------------------------------------------------------------- session epoch
  //
  // A session begins when a Hello presents a capability we have not seen before, and that is the
  // only reliable signal there is. The endpoint is not one: through a relay every client reaches
  // us from the same address and port, so "the peer changed" stays false forever and the second
  // client inherits the first one's control channel -- where its messages are acknowledged and
  // then dropped, because their sequence numbers look like ones already delivered.
  //
  // The epoch serialises the handover. The reader raises it and waits; the dispatcher resets the
  // channel, re-enters its session loop (which is also what turns the stream back on) and
  // publishes that it is ready; only then does the reader answer the Hello. Since the client
  // repeats its Hello until it sees an Ack, nothing it sends can arrive before the reset.
  // Starts at one, not zero: control is only wired up after the handshake loop above has already
  // accepted a Hello, so by the time the dispatcher starts there is a session waiting for it.

  if (transport == VideoTransport::Udp) {
    clientSession.udpControlChannel.Configure(
        [&](const void* data, size_t len) -> bool {
          const uint32_t ip = sender.udpPeerIpNet.load(std::memory_order_acquire);
          const uint16_t port = sender.udpPeerPortNet.load(std::memory_order_acquire);
          if (ip == 0 || port == 0) return false;
          sockaddr_in to{};
          to.sin_family = AF_INET;
          to.sin_addr.s_addr = ip;
          to.sin_port = port;
          return sendto(clientSession.clientSock, static_cast<const char*>(data), static_cast<int>(len), 0,
                        reinterpret_cast<const sockaddr*>(&to), sizeof(to)) > 0;
        },
        remote60::native_poc::kUdpControlStreamHostToClient,
        remote60::native_poc::kUdpControlStreamClientToHost, args.udpMtu);

    clientSession.udpReaderThread = std::thread([&]() {
      // Startup barrier. The dispatcher's first Reset races this thread: if the client's first
      // ControlData lands here first, OnPacket ACKs it into rxReady_, then the dispatcher's
      // Reset wipes rxReady_ -- and the client, holding an ACK, never retransmits. The serve
      // loop then starves for its full 10s read timeout ("ended reason=none") with a 40-70%
      // field hit rate. Hold this thread off the socket until the dispatcher has published
      // clientSession.controlReadyEpoch for the current epoch; datagrams meanwhile wait, unharmed, in the
      // kernel socket buffer. wait_for (not wait) so shutdown cannot strand us if no one
      // signals the cv after stop.
      {
        std::unique_lock<std::mutex> lock(clientSession.epochMu);
        while (!stop.load() &&
               clientSession.controlReadyEpoch.load(std::memory_order_acquire) <
                   clientSession.epoch.load(std::memory_order_acquire)) {
          clientSession.epochCv.wait_for(lock, std::chrono::milliseconds(50));
        }
      }
      int lastLoggedRecvError = 0;
      while (!stop.load()) {
        uint8_t rx[kUdpReceiveBufferBytes];
        sockaddr_in peer{};
        int peerLen = sizeof(peer);
        const int n = recvfrom(clientSession.clientSock, reinterpret_cast<char*>(rx), sizeof(rx), 0,
                               reinterpret_cast<sockaddr*>(&peer), &peerLen);
        // A zero-length datagram is legal and arrives from NAT keepalives and port scanners.
        // It used to fall into the error path below and end this thread, after which no Hello
        // was ever read again: video kept streaming to the previous peer while every new
        // client connected its control channel and then watched nothing arrive.
        if (n == 0) continue;
        if (n < 0) {
          const int err = WSAGetLastError();
          if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK || err == WSAEMSGSIZE ||
              err == WSAECONNRESET) {
            // Nothing arrived, or one datagram was malformed. Keep the retransmit timers moving
            // so a stalled transfer still recovers while the link is quiet.
            clientSession.udpControlChannel.Tick();
            continue;
          }
          // This thread is the only reader of hellos; while the process lives it must too.
          // Whatever went wrong with one receive, the socket itself outlives it.
          if (err != lastLoggedRecvError) {
            lastLoggedRecvError = err;
            std::cout << "[native-video-host] udp reader recv error err=" << err
                      << " (continuing)\n";
          }
          clientSession.udpControlChannel.Tick();
          Sleep(50);
          continue;
        }
        const size_t len = static_cast<size_t>(n);

        UdpHelloPacket hello{};
        if (len >= sizeof(UdpHelloPacket)) {
          std::memcpy(&hello, rx, sizeof(hello));
          if (hello.magic == remote60::native_poc::kMagic &&
              hello.kind == static_cast<uint16_t>(UdpPacketKind::Hello) &&
              hello.version == remote60::native_poc::kUdpProtocolVersion &&
              (hello.features & remote60::native_poc::kUdpFeatureVideoFec) != 0) {
            sender.fecInterleaved.store(
                (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved) != 0,
                std::memory_order_relaxed);

            UdpHelloPacket ack{};
            ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
            ack.features =
                remote60::native_poc::kUdpFeatureVideoFec |
                (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved);
            size_t tokenLen = 0;
            while (tokenLen < sizeof(hello.authToken) && hello.authToken[tokenLen] != '\0') {
              ++tokenLen;
            }
            bool directoryAuthenticated = false;
            bool newSession = false;
            if (tokenLen > 0) {
              const std::string authToken(hello.authToken, hello.authToken + tokenLen);
              const auto kind = clientSession.ClassifyDirectoryHello(authToken, peer);
              if (kind == DirectoryHello::Rejected) {
                std::cerr << "[native-video-host] rejected reconnect hello with invalid directory capability\n";
                continue;
              }
              newSession = (kind == DirectoryHello::NewSession);
              directoryAuthenticated = true;
              ack.features |= remote60::native_poc::kUdpFeatureDirectoryAuth;
              std::string secureInputStatus;
              (void)inputRouter.broker.EnsureInstalledAndConnected(
                  remote60::native_poc::sibling_executable_path(
                      L"GNLinkInputService.exe"),
                  &secureInputStatus);
            } else if (clientSession.directoryAuthenticated.load(std::memory_order_acquire)) {
              // Do not let an unauthenticated LAN Hello take over or de-authorize an active
              // directory session. Direct-LAN mode remains available before authentication.
              std::cerr << "[native-video-host] rejected unauthenticated reconnect during directory session\n";
              continue;
            }
            clientSession.directoryAuthenticated.store(directoryAuthenticated,
                                                std::memory_order_release);
            const bool changed =
                sender.udpPeerIpNet.load(std::memory_order_acquire) != peer.sin_addr.s_addr ||
                sender.udpPeerPortNet.load(std::memory_order_acquire) != peer.sin_port;
            // An unauthenticated LAN client has no capability to compare, so the endpoint is all
            // there is to go on. It is a weaker signal -- an app restart that lands on the same
            // port is invisible -- but the relay, which is what makes endpoints ambiguous, only
            // ever carries authenticated sessions.
            const bool startsSession = directoryAuthenticated ? newSession : changed;
            if (changed) {
              sender.udpPeerIpNet.store(peer.sin_addr.s_addr, std::memory_order_release);
              sender.udpPeerPortNet.store(peer.sin_port, std::memory_order_release);
            }
            if (startsSession) {
              // Even when the endpoint is unchanged: a new client has a new decoder, and sending
              // it deltas against frames it never saw leaves it grey until the next keyframe.
              sender.udpPeerChanged.store(true, std::memory_order_release);
              const uint64_t epoch = clientSession.BeginEpoch();
              std::cout << "[native-video-host][control] session epoch=" << epoch
                        << (changed ? " peer=new" : " peer=same") << "\n";
              clientSession.AwaitControlReady(epoch);
            }
            // Answered last, so that by the time the client believes it is connected the control
            // channel behind this endpoint is already the new session's.
            (void)sendto(clientSession.clientSock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                         reinterpret_cast<const sockaddr*>(&peer), peerLen);
            continue;
          }
        }

        if (clientSession.udpControlChannel.OnPacket(rx, len)) continue;
        (void)clientSession.directoryAgent.ConsumeUdpPacket(rx, len, peer);
      }
      clientSession.udpControlChannel.Close(remote60::native_poc::ControlCloseReason::Shutdown);
      clientSession.epochCv.notify_all();
    });

    // One dispatcher for the life of the process, serving one session after another. It used to
    // serve exactly one: any failed read returned from serve_control_session and the thread
    // exited for good, taking the stream with it (the session teardown clears
    // clientSession.streamControlActive, and only re-entry restores it). A client that merely walked out of
    // Wi-Fi range was enough to leave the host answering handshakes and nothing else.
    clientSession.udpControlThread = std::thread([&]() {
      uint64_t servedEpoch = 0;
      for (;;) {
        {
          std::unique_lock<std::mutex> lock(clientSession.epochMu);
          clientSession.epochCv.wait(lock, [&] {
            return stop.load() || clientSession.epoch.load(std::memory_order_acquire) > servedEpoch;
          });
        }
        if (stop.load()) break;
        servedEpoch = clientSession.epoch.load(std::memory_order_acquire);
        // Reset belongs here rather than in the reader: this is the thread that owns the
        // channel's read side, so nothing is being consumed while the queues are cleared.
        clientSession.udpControlChannel.Reset();
        {
          std::lock_guard<std::mutex> lock(clientSession.epochMu);
          clientSession.controlReadyEpoch.store(servedEpoch, std::memory_order_release);
        }
        clientSession.epochCv.notify_all();

        // The read timeout is what lets the host notice a client that simply vanished. The
        // channel only declares peer-lost while it has something to retransmit; a client that
        // dies between requests leaves nothing outstanding, and a blocking read sat here for
        // the rest of the process with the stream still marked active -- capturing, encoding,
        // and sending to nobody. The client pings about once a second, so ten silent seconds
        // is a client that is gone, not one that is slow.
        UdpControlLink link(&clientSession.udpControlChannel, 10000);
        controlServer.Serve(link);
        // Closed is not finished. Retransmits running out means this client is gone, which is
        // the ordinary end of a session and the reason to wait for the next one.
        std::cout << "[native-video-host][control] udp control session ended epoch=" << servedEpoch
                  << " reason=" << remote60::native_poc::to_string(clientSession.udpControlChannel.CloseReason())
                  << "\n";
      }
    });
  }
}

}  // namespace remote60::native_poc
