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
#include "control_resume.hpp"
#include "host_diag_log.hpp"
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
          // This thread owns the listen socket for its whole life, including the close.
          //
          // Shutdown used to closesocket() it from another thread to break accept(), which is a
          // data race on a plain SOCKET and a concurrent close-with-accept that Winsock does not
          // allow -- and the value is reusable the instant it closes. Instead accept() is entered
          // only when select() says a connection is waiting, so a 200ms tick is enough to notice
          // `stop` and close it here. (Ledger H-24.)
          while (!stop.load()) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(clientSession.controlListenSock, &readSet);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            const int ready = select(0, &readSet, nullptr, nullptr, &tv);
            if (stop.load()) break;
            if (ready <= 0) continue;  // timeout, or the listener is going away
            sockaddr_in cpeer{};
            int cpeerLen = sizeof(cpeer);
            SOCKET acceptedSock = accept(clientSession.controlListenSock, reinterpret_cast<sockaddr*>(&cpeer), &cpeerLen);
            if (acceptedSock == INVALID_SOCKET) {
              if (stop.load()) break;
              Sleep(50);
              continue;
            }
            {
              std::lock_guard<std::mutex> lk(clientSession.controlClientSockMu);
              clientSession.controlClientSock = acceptedSock;
            }
            int ctlNoDelay = 1;
            setsockopt(acceptedSock, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
            std::cout << "[native-video-host][control] client connected\n";
            {
              TcpControlLink link(acceptedSock);
              link.SetWriteCounter(&sender.txControlBytes);
              controlServer.Serve(link);
            }
            // This thread is the ONLY closer of an accepted control socket, and the close
            // happens under controlClientSockMu. shutdown_host holds the same mutex for its
            // wake-up shutdown(), so the two can never overlap: either it shuts a socket this
            // thread has not closed yet, or it finds INVALID_SOCKET. Publishing the handle
            // atomically was not enough -- it left a window where shutdown_host had read a value
            // this thread then closed, and SOCKET values are reused immediately. (Ledger H-02.)
            SOCKET mine = INVALID_SOCKET;
            {
              std::lock_guard<std::mutex> lk(clientSession.controlClientSockMu);
              mine = clientSession.controlClientSock;
              clientSession.controlClientSock = INVALID_SOCKET;
              if (mine != INVALID_SOCKET) {
                shutdown(mine, SD_BOTH);
                closesocket(mine);
              }
            }
            std::cout << "[native-video-host][control] tcp client disconnected\n";
          }
          // Sole closer (H-24). shutdown_host only sets `stop` and joins.
          if (clientSession.controlListenSock != INVALID_SOCKET) {
            closesocket(clientSession.controlListenSock);
            clientSession.controlListenSock = INVALID_SOCKET;
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
          const int sent = sendto(clientSession.clientSock, static_cast<const char*>(data),
                                  static_cast<int>(len), 0, reinterpret_cast<const sockaddr*>(&to),
                                  sizeof(to));
          if (sent > 0) {
            sender.txControlBytes.fetch_add(static_cast<uint64_t>(sent), std::memory_order_relaxed);
            sender.txControlDatagrams.fetch_add(1, std::memory_order_relaxed);
          }
          return sent > 0;
        },
        remote60::native_poc::kUdpControlStreamHostToClient,
        remote60::native_poc::kUdpControlStreamClientToHost, args.udpMtu);

    clientSession.udpReaderThread = std::thread([&]() {
      // Control resume bookkeeping (item 8, C3). Reader-thread locals on purpose: this thread
      // is the only one that builds or sends a resume answer, so there is nothing to lock and
      // exactly one of each per session -- which is also what makes "bounded per session" true
      // by construction rather than by a sweep.
      remote60::native_poc::HostResumeAck resumeAck;
      remote60::native_poc::ResumeAckBudget resumeReplayBudget(
          remote60::native_poc::kResumeAckReplayPerSecond,
          remote60::native_poc::kResumeAckReplayBurst);
      remote60::native_poc::ResumeAckBudget resumeGlobalBudget(
          remote60::native_poc::kResumeAckGlobalPerSecond,
          remote60::native_poc::kResumeAckGlobalBurst);
      uint64_t resumeStrangerLogUs = 0;
      // Which ask the current probe result belongs to. A verdict about one attempt says
      // nothing about the next, and an Alive left over from a refused ask would refuse a
      // genuine break for the rest of the session. (item 8, C3 r2)
      uint32_t resumeProbeId = 0;
      uint64_t resumeProbeEpoch = 0;   // the session that id belonged to
      uint32_t resumeProbeCount = 0;   // probes spent on this recovery, as a hard ceiling
      // The attempt the dispatcher is currently being woken for, and the sequence number that
      // attempt is waiting on. A retry of the SAME ask joins that wake instead of starting a
      // second one -- the viewer repeats every 500 ms and the dispatcher may take longer than
      // one wait, so without this one logical recovery could re-key the channel twice.
      uint32_t resumeWakeId = 0;
      uint64_t resumeWakeWaitFor = 0;
      uint64_t resumeThrottleLogUs = 0;
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
          if (err == WSAENOTSOCK || err == WSAEINVAL || err == WSAESHUTDOWN) {
            // The descriptor cannot recover by reading it again. End this runtime so the
            // supervisor can construct new sockets, instead of advertising a dead listener.
            stop.store(true, std::memory_order_release);
            clientSession.udpControlChannel.Close(ControlCloseReason::Shutdown);
            clientSession.epochCv.notify_all();
            break;
          }
          clientSession.udpControlChannel.Tick();
          Sleep(50);
          continue;
        }
        // Every turn, not only on a timeout: the resume probe has a quarter-second budget and
        // a session carrying video never sits idle long enough for the timeout path to run.
        clientSession.udpControlChannel.Tick();
        const size_t len = static_cast<size_t>(n);

        UdpHelloPacket hello{};
        if (len >= sizeof(UdpHelloPacket)) {
          std::memcpy(&hello, rx, sizeof(hello));
          if (hello.magic == remote60::native_poc::kMagic &&
              hello.kind == static_cast<uint16_t>(UdpPacketKind::Hello) &&
              hello.version == remote60::native_poc::kUdpProtocolVersion &&
              (hello.features & remote60::native_poc::kUdpFeatureVideoFec) != 0) {
            UdpHelloPacket ack{};
            ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
            ack.features =
                remote60::native_poc::kUdpFeatureVideoFec |
                (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved);
            // Video NACK: the host supports selective retransmit; advertise it, and serve
            // retransmits only when this client asked for it. (video NACK.)
            ack.features |= remote60::native_poc::kUdpFeatureVideoNack;
            // Control resume (item 8). Advertised unconditionally, like video NACK above, and
            // acted on only for a client that asked -- a client that never asked does not know
            // what a resume answer is, so sending it one would be noise it has to ignore.
            ack.features |= remote60::native_poc::kUdpFeatureControlResume;
            // C0 stage 1: this host logs bandwidth observations. Advertised to everyone like the
            // two above; a viewer sends them only when it asked and sees this bit.
            ack.features |= remote60::native_poc::kUdpFeatureBandwidthObserve;
            // Whether THIS client asked is stored further down, once its Hello has been accepted.
            // Stored here it let a Hello that is then refused (a bad capability, or an
            // unauthenticated one during a directory session) switch resume off for the session
            // that is actually running. (RV-02 / C5)

            size_t tokenLen = 0;
            while (tokenLen < sizeof(hello.authToken) && hello.authToken[tokenLen] != '\0') {
              ++tokenLen;
            }
            bool directoryAuthenticated = false;
            bool newSession = false;
            if (tokenLen > 0) {
              const std::string authToken(hello.authToken, hello.authToken + tokenLen);
              remote60::native_poc::directory::HostAgent::PeerAuthDiag authDiag;
              const auto kind = clientSession.ClassifyDirectoryHello(authToken, peer, &authDiag);
              if (kind == DirectoryHello::Rejected) {
                std::cerr << "[native-video-host] rejected reconnect hello with invalid directory capability\n";
                // pc2-connect-diag: which of the several refusals this was.
                //
                // The line above says the same thing whether this host holds no capability at
                // all, holds ones that expired, or holds one that does not match -- and those
                // have different causes. On 2026-09-21 it was always the first: the capability
                // was correct and had not been collected yet. Nothing said so.
                //
                // Bounded: a client repeats its hello several times a second, so a run of the
                // same reason prints once and then every 32nd, carrying the running total.
                {
                  remote60::native_poc::RejectFacts facts;
                  facts.matched = authDiag.matched;
                  facts.endpointMoved = authDiag.endpointMoved;
                  facts.held = authDiag.held;
                  facts.expiredNow = authDiag.expiredNow;
                  const char* why = remote60::native_poc::reject_reason(facts);
                  static std::string lastWhy;
                  static uint64_t sameRun = 0;
                  static uint64_t rejectTotal = 0;
                  ++rejectTotal;
                  if (lastWhy == why) ++sameRun; else { lastWhy = why; sameRun = 1; }
                  if (remote60::native_poc::reject_should_emit(sameRun)) {
                    std::cerr << "[native-video-host][dir-reject] reason=" << why
                              << " held=" << authDiag.held
                              << " expiredNow=" << authDiag.expiredNow
                              << " run=" << sameRun << " total=" << rejectTotal << "\n";
                  }
                }
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
            // Accepted: from here this Hello is the session's. (C5) Every per-client option it
            // carries is stored only now -- the FEC layout and NACK used to be stored before the
            // refusals above too, so a refused Hello could change them under the running session.
            clientSession.controlResumeNegotiated.store(
                remote60::native_poc::host_resume_negotiated(hello.features),
                std::memory_order_release);
            sender.fecInterleaved.store(
                (hello.features & remote60::native_poc::kUdpFeatureVideoFecInterleaved) != 0,
                std::memory_order_relaxed);
            sender.nackEnabled.store(
                (hello.features & remote60::native_poc::kUdpFeatureVideoNack) != 0,
                std::memory_order_relaxed);
            clientSession.bandwidthObserveNegotiated.store(
                (hello.features & remote60::native_poc::kUdpFeatureBandwidthObserve) != 0,
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

        // Video NACK: replay just the missing chunks of an AU from the sender's recent-AU cache.
        // A no-op unless the client negotiated it (sender.nackEnabled). (video NACK.)
        if (len >= sizeof(UdpVideoNackPacket)) {
          UdpVideoNackPacket nack{};
          std::memcpy(&nack, rx, sizeof(nack));
          if (nack.magic == remote60::native_poc::kMagic &&
              nack.kind == static_cast<uint16_t>(UdpPacketKind::VideoNack) &&
              nack.size == sizeof(nack)) {
            uint16_t missingCount = nack.missingCount;
            if (missingCount > remote60::native_poc::kUdpVideoNackMaxMissing)
              missingCount = remote60::native_poc::kUdpVideoNackMaxMissing;
            if (missingCount > 0) {
              sender.RetransmitAu(clientSession.clientSock, peer, nack.streamGeneration, nack.seq,
                                  nack.missing, missingCount);
            }
            continue;
          }
        }
        // Control resume (item 8): rebuild the control stream on this session.
        //
        // Handled here, before OnPacket, because the channel it is about to re-key is the thing
        // OnPacket feeds. The answer is sent from this thread on the raw socket for the same
        // reason: the channel is the broken part, so the repair cannot be carried by it.
        if (len >= sizeof(remote60::native_poc::UdpControlResumePacket)) {
          remote60::native_poc::UdpControlResumePacket resume{};
          std::memcpy(&resume, rx, sizeof(resume));
          if (resume.magic == remote60::native_poc::kMagic &&
              resume.kind == static_cast<uint16_t>(UdpPacketKind::ControlResume) &&
              resume.size == sizeof(resume)) {
            // The endpoint the session is bound to. A client whose mapping moved is not
            // reachable by the video the host is still sending, so it could not be in the state
            // this feature is for -- and treating a stranger's packet as this client would let
            // it reset the stream.
            const bool fromCurrentPeer =
                sender.udpPeerIpNet.load(std::memory_order_acquire) == peer.sin_addr.s_addr &&
                sender.udpPeerPortNet.load(std::memory_order_acquire) == peer.sin_port;
            const uint64_t readyEpoch =
                clientSession.controlReadyEpoch.load(std::memory_order_acquire);
            remote60::native_poc::HostResumeInputs decide;
            decide.negotiated =
                clientSession.controlResumeNegotiated.load(std::memory_order_acquire);
            decide.sessionActive = fromCurrentPeer && readyEpoch > 0;
            decide.servingControl = clientSession.controlServing.load(std::memory_order_acquire);

            // A source with no session here gets nothing: no reply, and -- more to the point --
            // no state. Everything below this line either reads or writes something kept per
            // session, and the way an unauthenticated flood turns into a resource is by being
            // allowed past a check like this one first. It used to be answered with accepted=0,
            // which told a stranger the host was listening and cost a send per packet; a client
            // whose mapping really had moved could not have been helped by that answer anyway,
            // because the video it is still being sent goes to the endpoint it left. (C3)
            if (!decide.negotiated || !decide.sessionActive) {
              const uint64_t nowUs = remote60::native_poc::qpc_now_us();
              if (nowUs - resumeStrangerLogUs >= 5000000ull) {
                resumeStrangerLogUs = nowUs;
                std::cout << "[native-video-host][control] resume ignored id=" << resume.resumeId
                          << " negotiated=" << (decide.negotiated ? 1 : 0)
                          << " peer=" << (fromCurrentPeer ? "same" : "other")
                          << " session=" << (readyEpoch > 0 ? 1 : 0)
                          << " (no reply, no state)\n";
              }
              continue;
            }

            const uint64_t nowUs = remote60::native_poc::qpc_now_us();
            const auto channelIds = clientSession.udpControlChannel.StreamIds();
            remote60::native_poc::HostResumeReplayInputs replay;
            replay.negotiated = decide.negotiated;
            replay.sessionActive = decide.sessionActive;
            replay.epoch = readyEpoch;
            replay.peerIpNet = peer.sin_addr.s_addr;
            replay.peerPortNet = peer.sin_port;
            replay.resumeId = resume.resumeId;
            replay.requestStreamId = resume.streamId;
            replay.channelTxStreamId = channelIds.tx;
            replay.channelRxStreamId = channelIds.rx;

            // The same ask, answered the same way -- even while serving, because repeating an
            // answer changes nothing. This is the whole of the Ack-loss fix: without it the
            // host serves the resume, its answer is lost, and every retry is then refused
            // because the dispatcher went straight back into Serve(). (C3, Codex seq 2796.)
            if (remote60::native_poc::host_should_replay_resume_ack(resumeAck, replay)) {
              if (!resumeReplayBudget.Take(nowUs) || !resumeGlobalBudget.Take(nowUs)) {
                if (nowUs - resumeThrottleLogUs >= 1000000ull) {
                  resumeThrottleLogUs = nowUs;
                  std::cout << "[native-video-host][control] resume replay throttled id="
                            << resume.resumeId << "\n";
                }
                continue;
              }
              remote60::native_poc::UdpControlResumePacket again{};
              again.kind = static_cast<uint16_t>(UdpPacketKind::ControlResumeAck);
              again.streamId = resumeAck.ackStreamId;
              again.resumeId = resumeAck.resumeId;
              again.accepted = 1u;
              (void)sendto(clientSession.clientSock, reinterpret_cast<const char*>(&again),
                           sizeof(again), 0, reinterpret_cast<const sockaddr*>(&peer), peerLen);
              std::cout << "[native-video-host][control] resume replay id=" << resume.resumeId
                        << " streamId=" << again.streamId
                        << " serving=" << (decide.servingControl ? 1 : 0)
                        << " rekeyed=0\n";
              continue;
            }

            // A different ask ends the previous one: the viewer only changes the id when it has
            // started a new recovery, and the answer kept for the old one describes a repair it
            // has stopped waiting for. Its budget goes with it.
            if (!resumeAck.valid || resumeAck.resumeId != resume.resumeId) {
              resumeAck = remote60::native_poc::HostResumeAck{};
              resumeReplayBudget.Restart(nowUs);
            }

            // Taken before anything is woken. A budget checked after the dispatcher has been
            // signalled and waited on would bound the sends and not the work, which is the
            // half that matters when the asks are invented rather than real.
            if (!resumeGlobalBudget.Take(nowUs)) {
              if (nowUs - resumeThrottleLogUs >= 1000000ull) {
                resumeThrottleLogUs = nowUs;
                std::cout << "[native-video-host][control] resume throttled id=" << resume.resumeId
                          << " (host budget)\n";
              }
              continue;
            }
            // Whether this ask may interrupt a serve, and on what evidence. (item 8, C3 r2/r3)
            //
            // The bookkeeping is keyed to (session, resumeId): a verdict reached for one
            // recovery says nothing about the next, and a session rollover ends both.
            if (resumeProbeId != resume.resumeId || resumeProbeEpoch != readyEpoch) {
              clientSession.udpControlChannel.ClearProbe();
              resumeProbeId = resume.resumeId;
              resumeProbeEpoch = readyEpoch;
              resumeProbeCount = 0;
            }
            remote60::native_poc::HostResumeEligibility elig;
            elig.negotiated = decide.negotiated;
            elig.sessionActive = decide.sessionActive;
            elig.servingControl = decide.servingControl;
            elig.cacheMatches = false;  // the replay path above already took those
            elig.sameIdAsCache = resumeAck.valid && resumeAck.resumeId == resume.resumeId;
            elig.channelPeerLost =
                clientSession.udpControlChannel.IsClosed() &&
                clientSession.udpControlChannel.CloseReason() ==
                    remote60::native_poc::ControlCloseReason::PeerLost;
            // A verdict is about the instant it was reached. Re-reading one Alive for every
            // retry of the same recovery is what r2 did, and it refused a genuine break for the
            // whole ceiling whenever the channel happened to answer the first ask. The viewer
            // cannot route around it -- one break is one resumeId, deliberately -- so the
            // staleness has to be handled here. (item 8, C3 r3)
            remote60::native_poc::ResumeProbeFreshness fresh;
            fresh.state = clientSession.udpControlChannel.ProbeStatus();
            fresh.ageUs = clientSession.udpControlChannel.ProbeAgeUs();
            fresh.probesThisRecovery = resumeProbeCount;
            if (remote60::native_poc::resume_probe_action(fresh) ==
                remote60::native_poc::ResumeProbeAction::Restart) {
              clientSession.udpControlChannel.ClearProbe();
              fresh.state = remote60::native_poc::ControlProbeState::Idle;
            }
            elig.probe = fresh.state;
            const remote60::native_poc::HostResumeVerdict verdict =
                remote60::native_poc::host_resume_verdict(elig);
            if (verdict == remote60::native_poc::HostResumeVerdict::Probe &&
                fresh.state == remote60::native_poc::ControlProbeState::Idle) {
              // Ask the channel whether it is still there. Answering served=0 meanwhile is what
              // keeps the viewer asking, which is how the answer gets collected.
              ++resumeProbeCount;
              clientSession.udpControlChannel.StartProbe(
                  remote60::native_poc::kResumeProbeMaxAttempts,
                  remote60::native_poc::kResumeProbeIntervalUs);
            }
            const bool accept = verdict == remote60::native_poc::HostResumeVerdict::Serve;
            // Woken only when there is something to wake. A resume that arrives between
            // sessions is served by the dispatcher's ordinary wait.
            const bool wake = accept && decide.servingControl;

            // A retry of the ask already being served joins it rather than raising a second
            // one. Both would be honoured, and the second would re-key a channel the first had
            // just repaired.
            const bool wakePending =
                clientSession.controlResumePending.load(std::memory_order_acquire);
            const bool alreadyPending = wakePending && resumeWakeId == resume.resumeId;
            const bool raiseWake = remote60::native_poc::host_resume_should_raise_wake(
                accept, wakePending, resumeWakeId == resume.resumeId);
            uint64_t waitFor = alreadyPending ? resumeWakeWaitFor : 0;
            if (raiseWake) {
              {
                std::lock_guard<std::mutex> lock(clientSession.epochMu);
                clientSession.controlResumeId.store(resume.resumeId, std::memory_order_release);
                waitFor =
                    clientSession.controlResumeSeq.fetch_add(1, std::memory_order_acq_rel) + 1;
              }
              resumeWakeId = resume.resumeId;
              resumeWakeWaitFor = waitFor;
              if (wake) {
                // Published BEFORE the wake, because the thing being woken reads it on its way
                // out: this is a repair, not the end of a session, and the video stream stays.
                clientSession.controlResumePending.store(true, std::memory_order_release);
                // The dispatcher is inside Serve(), blocked on a read that will not return for
                // its full timeout. Closing the channel is how a rollover already wakes it
                // (SessionState::BeginEpoch); ResumeWith re-opens it a moment later, on the
                // re-keyed stream ids. No new thread and no new lock -- the dispatcher is the
                // only thing that touches the channel's read side, and by the time it reaches
                // the resume branch its Serve() has already returned.
                clientSession.udpControlChannel.Close(
                    remote60::native_poc::ControlCloseReason::PeerLost);
              }
              clientSession.epochCv.notify_all();
            }
            if (accept) {
              // Bounded, like AwaitControlReady: if the dispatcher cannot come back we simply
              // do not answer, and the client keeps asking or gives up at its own ceiling.
              std::unique_lock<std::mutex> lock(clientSession.epochMu);
              clientSession.epochCv.wait_for(lock, std::chrono::milliseconds(1500), [&] {
                return stop.load() ||
                       clientSession.controlResumeServedSeq.load(std::memory_order_acquire) >=
                           waitFor;
              });
            }
            const bool served =
                accept && clientSession.controlResumeServedSeq.load(std::memory_order_acquire) >=
                              waitFor;
            // Cleared only once the repair is done. Leaving it set while the dispatcher is
            // still working is what lets the next retry recognise its own wake and join it,
            // and it is also what keeps the video stream on across the hand-over.
            if (served) {
              clientSession.controlResumePending.store(false, std::memory_order_release);
              resumeWakeId = 0;
              resumeWakeWaitFor = 0;
            }

            remote60::native_poc::UdpControlResumePacket answer{};
            answer.kind = static_cast<uint16_t>(UdpPacketKind::ControlResumeAck);
            answer.streamId = served ? remote60::native_poc::control_resume_stream_id(
                                           remote60::native_poc::kUdpControlStreamHostToClient,
                                           resume.resumeId)
                                     : remote60::native_poc::kUdpControlStreamHostToClient;
            answer.resumeId = resume.resumeId;
            answer.accepted = served ? 1u : 0u;
            // Kept only when it is true. A refusal is not an answer worth repeating -- it is a
            // state, and the state changes -- so served=0 leaves the cache empty and the next
            // ask goes through the ordinary path again.
            if (served) {
              const auto servedIds = clientSession.udpControlChannel.StreamIds();
              resumeAck.valid = true;
              resumeAck.epoch = readyEpoch;
              resumeAck.peerIpNet = peer.sin_addr.s_addr;
              resumeAck.peerPortNet = peer.sin_port;
              resumeAck.resumeId = resume.resumeId;
              resumeAck.requestStreamId = resume.streamId;
              resumeAck.txStreamId = servedIds.tx;
              resumeAck.rxStreamId = servedIds.rx;
              resumeAck.ackStreamId = answer.streamId;
            }
            (void)sendto(clientSession.clientSock, reinterpret_cast<const char*>(&answer),
                         sizeof(answer), 0, reinterpret_cast<const sockaddr*>(&peer), peerLen);
            std::cout << "[native-video-host][control] resume id=" << resume.resumeId
                      << " accepted=" << (served ? 1 : 0)
                      << " verdict=" << remote60::native_poc::to_string(verdict)
                      << " evidence="
                      << (elig.channelPeerLost
                              ? "peer-lost"
                              : (elig.probe == remote60::native_poc::ControlProbeState::Dead
                                     ? "probe-failed"
                                     : remote60::native_poc::to_string(elig.probe)))
                      << " probeAttempts=" << clientSession.udpControlChannel.ProbeAttempts()
                      << " probes=" << resumeProbeCount
                      << " wake=" << (wake ? 1 : 0)
                      << " negotiated=" << (decide.negotiated ? 1 : 0)
                      << " peer=" << (fromCurrentPeer ? "same" : "other")
                      << " serving=" << (decide.servingControl ? 1 : 0) << "\n";
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
      uint64_t servedResumeSeq = 0;
      for (;;) {
        bool resuming = false;
        {
          const auto look = [&]() {
            remote60::native_poc::ControlDispatchInputs in;
            in.stop = stop.load();
            in.epoch = clientSession.epoch.load(std::memory_order_acquire);
            in.servedEpoch = servedEpoch;
            in.resumeSeq = clientSession.controlResumeSeq.load(std::memory_order_acquire);
            in.servedResumeSeq = servedResumeSeq;
            return remote60::native_poc::control_dispatch_decide(in);
          };
          std::unique_lock<std::mutex> lock(clientSession.epochMu);
          clientSession.epochCv.wait(lock, [&] { return look().wake; });
          resuming = look().resuming;
        }
        if (stop.load()) break;
        servedEpoch = clientSession.epoch.load(std::memory_order_acquire);
        // Reset belongs here rather than in the reader: this is the thread that owns the
        // channel's read side, so nothing is being consumed while the queues are cleared.
        //
        // A resume clears the same state but onto re-keyed stream ids (item 8). Clearing alone
        // would restart the sequence numbers while the channel still answered on the old ids,
        // and one datagram delayed across the break would then be delivered as new traffic and
        // swallow every real message below it. The ids are derived from the resumeId the client
        // sent, which it derives the same way, so each side stops recognising the old stream at
        // the moment it starts listening for the new one.
        if (resuming) {
          const uint32_t resumeId = clientSession.controlResumeId.load(std::memory_order_acquire);
          if (!clientSession.udpControlChannel.ResumeWith(
                  remote60::native_poc::control_resume_stream_id(
                      remote60::native_poc::kUdpControlStreamHostToClient, resumeId),
                  remote60::native_poc::control_resume_stream_id(
                      remote60::native_poc::kUdpControlStreamClientToHost, resumeId))) {
            // Shut down between the wake and here: left closed, and the Serve() below returns at
            // once. (C5)
            std::cout << "[native-video-host][control] resume id=" << resumeId
                      << " not re-keyed: the channel is shut down\n";
          }
        } else {
          clientSession.udpControlChannel.Reset();
        }
        // Outstanding main-loop requests belong to the client that made them. This is the only
        // point where that is provably true for a UDP rollover: the previous Serve() has already
        // returned, so the old client can no longer post, and the new one cannot post until
        // controlReadyEpoch is published below. (Only the TCP reconnect path cleared before --
        // a UDP handover carried the old viewer's monitor / capture-mode / tune / backend /
        // keyframe requests into the new session. Ledger H-26c.)
        // Deliberately NOT done on a resume: the outstanding requests belong to the client that
        // made them, and on a resume that is the same client. Clearing them here would drop the
        // monitor list or capture-mode request a viewer had in flight when its uplink went, which
        // is the opposite of continuing the session. The rollover case still clears, for exactly
        // the reason the original comment gives.
        if (!resuming) hx.mailbox.Clear();
        {
          std::lock_guard<std::mutex> lock(clientSession.epochMu);
          if (resuming) {
            // Releases the reader, which is holding the client's request unanswered until the
            // re-key above is done. controlReadyEpoch is left alone: the epoch did not move, and
            // republishing it would restart the reader's startup barrier for no reason.
            servedResumeSeq = clientSession.controlResumeSeq.load(std::memory_order_acquire);
            clientSession.controlResumeServedSeq.store(servedResumeSeq, std::memory_order_release);
            // The repair is done, so the flag that held the video stream through it comes down
            // here rather than only on the reader's path. The reader's wait is bounded: if it
            // times out it never clears this, and a flag left set would keep a departed client's
            // stream alive through the next genuine session end.
            clientSession.controlResumePending.store(false, std::memory_order_release);
          } else {
            clientSession.controlReadyEpoch.store(servedEpoch, std::memory_order_release);
            // A rollover supersedes any resume that was pending for the client that just left.
            servedResumeSeq = clientSession.controlResumeSeq.load(std::memory_order_acquire);
            clientSession.controlResumePending.store(false, std::memory_order_release);
          }
        }
        clientSession.epochCv.notify_all();

        // The read timeout is what lets the host notice a client that simply vanished. The
        // channel only declares peer-lost while it has something to retransmit; a client that
        // dies between requests leaves nothing outstanding, and a blocking read sat here for
        // the rest of the process with the stream still marked active -- capturing, encoding,
        // and sending to nobody. The client pings about once a second, so ten silent seconds
        // is a client that is gone, not one that is slow.
        UdpControlLink link(&clientSession.udpControlChannel, 10000);
        // Published around Serve() so a resume arriving while this session is healthy is
        // refused. Inside here the channel is in use, and re-keying it would break a link that
        // is working -- the reader reads this flag to decide (item 8).
        clientSession.controlServing.store(true, std::memory_order_release);
        controlServer.Serve(link);
        clientSession.controlServing.store(false, std::memory_order_release);
        // Closed is not finished. Retransmits running out means this client is gone, which is
        // the ordinary end of a session and the reason to wait for the next one.
        std::cout << "[native-video-host][control] udp control session ended epoch=" << servedEpoch
                  << " resumed=" << (resuming ? 1 : 0)
                  << " reason=" << remote60::native_poc::to_string(clientSession.udpControlChannel.CloseReason())
                  << "\n";
      }
    });
  }
}

}  // namespace remote60::native_poc
