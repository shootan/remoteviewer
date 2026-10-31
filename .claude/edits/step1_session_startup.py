import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SESSION = r'D:\remote\remote\apps\native_poc\src\native_video_client_session.cpp'
SESSION_STATE = r'D:\remote\remote\apps\native_poc\src\viewer_session_state.hpp'
CONTEXT = r'D:\remote\remote\apps\native_poc\src\viewer_context.hpp'
STARTUP = r'D:\remote\remote\apps\native_poc\src\viewer_startup.cpp'

edit(SESSION, [
(
"""#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
""",
"""#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "udp_video_nack.hpp"
"""),
(
"""  // Video NACK: ask the host to replay the missing chunks of the oldest stuck AU, instead of
  // waiting for the next (on a static screen, far-off) frame to reveal the loss and then eating a
  // full IDR. Only when the host advertised support. Bounded by a reorder grace and a few rounds;
  // if it does not recover, the existing keyframe path takes over. (video NACK.)
  uint32_t nackSeq = 0;
  uint64_t nackFirstUs = 0;
  uint64_t nackLastUs = 0;
  uint32_t nackRounds = 0;
  auto maybe_send_nack = [&](SocketHandle sock) {
    if (sock == kInvalidSocket || !hostSupportsNack_.load(std::memory_order_relaxed)) return;
    constexpr uint16_t kMax = remote60::native_poc::kUdpVideoNackMaxMissing;
    uint16_t missing[kMax];
    UdpH264FrameAssembler::IncompleteAuInfo info{};
    if (!assembler.OldestIncomplete(missing, kMax, &info)) {
      nackSeq = 0;
      nackRounds = 0;
      return;
    }
    // While waiting for an IDR, only repairing THAT keyframe helps -- a non-key incomplete AU will
    // be resynced by the coming IDR. But the keyframe itself MUST be repairable here, or a lossy
    // 200KB IDR never completes and the picture is stuck (the 60s freeze). (Codex.)
    if (waitForKeyframe && !info.keyFrame) {
      nackSeq = 0;
      nackRounds = 0;
      return;
    }
    const uint64_t now = now_us();
    if (info.seq != nackSeq) {  // a different AU is now the blocker -> restart the round schedule
      nackSeq = info.seq;
      nackFirstUs = now;
      nackLastUs = 0;
      nackRounds = 0;
    }
    // A missing index below highWater is a confirmed hole (a later chunk already arrived); ask for
    // it after a short reorder grace. A missing index at/above highWater is the still-in-flight tail
    // of a large frame -- NACKing it early is exactly what ignites a retransmit flood, so wait a much
    // longer grace (the whole frame should have arrived) before touching it. (Codex: frame-end aware.)
    constexpr uint64_t kGapGraceUs = 25000;    // reorder settle for a confirmed hole
    constexpr uint64_t kTailGraceUs = 120000;  // large frame fully sent by now
    constexpr uint64_t kRoundUs = 25000;       // spacing between rounds (>= a few RTTs)
    constexpr uint32_t kMaxRounds = 3;         // then give up -> the IDR path recovers
    if (nackRounds >= kMaxRounds) return;
    if (nackLastUs != 0 && now - nackLastUs < kRoundUs) return;
    const uint64_t age = now - nackFirstUs;
    const bool gapEligible = age >= kGapGraceUs;
    const bool tailEligible = age >= kTailGraceUs;
    if (!gapEligible && !tailEligible) return;
    const uint16_t have = std::min<uint16_t>(info.missingTotal, kMax);
    uint16_t out[kMax];
    uint16_t outCount = 0;
    for (uint16_t i = 0; i < have; ++i) {
      const bool isTail = missing[i] >= info.highWater;
      if (isTail ? !tailEligible : !gapEligible) continue;
      out[outCount++] = missing[i];
    }
    if (outCount == 0) return;  // only the tail remains and its long grace has not elapsed yet
    UdpVideoNackPacket nack{};
    nack.streamGeneration = info.generation;
    nack.seq = info.seq;
    nack.chunkCount = info.chunkCount;
    nack.missingCount = outCount;
    nack.round = static_cast<uint16_t>(nackRounds);
    std::memcpy(nack.missing, out, static_cast<size_t>(outCount) * sizeof(uint16_t));
    (void)send(sock, reinterpret_cast<const char*>(&nack), sizeof(nack), 0);
    ++nackRounds;
    nackLastUs = now;
  };
""",
"""  // Video NACK: ask the host to replay the missing chunks of the oldest stuck AU, instead of
  // waiting for the next (on a static screen, far-off) frame to reveal the loss and then eating a
  // full IDR. Only when the host advertised support. The grace / round policy lives in the shared
  // VideoNackScheduler (udp_video_nack.hpp) so the Windows viewer drives the same rules; if it does
  // not recover, the existing keyframe path takes over. (video NACK; Windows NACK wiring.)
  VideoNackScheduler nackScheduler;
  auto maybe_send_nack = [&](SocketHandle sock) {
    if (sock == kInvalidSocket || !hostSupportsNack_.load(std::memory_order_relaxed)) return;
    // While waiting for an IDR, only repairing THAT keyframe helps -- a non-key incomplete AU will
    // be resynced by the coming IDR. But the keyframe itself MUST be repairable here, or a lossy
    // 200KB IDR never completes and the picture is stuck (the 60s freeze). (Codex.)
    UdpVideoNackPacket nack{};
    if (nackScheduler.Poll(assembler, !waitForKeyframe, now_us(), &nack)) {
      (void)send(sock, reinterpret_cast<const char*>(&nack), sizeof(nack), 0);
    }
  };
"""),
])

edit(SESSION_STATE, [
(
"""  std::atomic<bool> unlockRequested{false};  // set by the unlock trigger; the control thread runs it
  std::atomic<bool> unlockSupported{false};  // host advertised kCaptureFlagUnlockSealedV1 (Pong)
""",
"""  std::atomic<bool> unlockRequested{false};  // set by the unlock trigger; the control thread runs it
  std::atomic<bool> unlockSupported{false};  // host advertised kCaptureFlagUnlockSealedV1 (Pong)
  // UDP video handshake result: the HelloAck feature bits, and whether the host acknowledged
  // kUdpFeatureVideoNack so the recv thread may ask for selective retransmits. main sets both at
  // connect, before the threads start; the recv thread reads. (Windows NACK wiring: the viewer
  // used to discard the ack bits, so the 0.2.95/96 NACK path never ran in GNLinkViewer.)
  uint32_t udpHelloAckFeatures = 0;
  bool hostSupportsNack = false;
"""),
])

edit(CONTEXT, [
(
"""  uint32_t udpSimDropPm = 0;        // REMOTE60_NATIVE_UDP_SIM_DROP_PM
  uint32_t udpSimDropSeed = 0;      // REMOTE60_NATIVE_UDP_SIM_DROP_SEED
""",
"""  uint32_t udpSimDropPm = 0;        // REMOTE60_NATIVE_UDP_SIM_DROP_PM
  uint32_t udpSimDropSeed = 0;      // REMOTE60_NATIVE_UDP_SIM_DROP_SEED
  // Video NACK policy (REMOTE60_NATIVE_VIDEO_NACK / _NACK_HOLD_US), effective only once the host
  // acknowledged kUdpFeatureVideoNack; and the media socket's receive timeout for UDP
  // (REMOTE60_NATIVE_UDP_RECV_TIMEOUT_MS), which is the clock of every timer the recv thread runs
  // on a quiet link (NACK rounds, the in-order hold, the keyframe recovery deadline).
  bool videoNackEnabled = true;
  uint64_t videoNackHoldUs = 120000;
  uint32_t udpRecvTimeoutMs = 25;
"""),
])

edit(STARTUP, [
(
"""  ctx.udpSimDropSeed = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_SEED", 0, 0, 0x7fffffffu);
  ctx.control.keyframeRequests.Reset();
}
""",
"""  ctx.udpSimDropSeed = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_SEED", 0, 0, 0x7fffffffu);
  ctx.videoNackEnabled = env_u32_clamped("REMOTE60_NATIVE_VIDEO_NACK", 1, 0, 1) != 0;
  ctx.videoNackHoldUs = env_u32_clamped("REMOTE60_NATIVE_VIDEO_NACK_HOLD_US", 120000, 0, 2000000);
  ctx.udpRecvTimeoutMs = env_u32_clamped("REMOTE60_NATIVE_UDP_RECV_TIMEOUT_MS", 25, 1, 1000);
  ctx.control.keyframeRequests.Reset();
}
"""),
(
"""    remote60::native_poc::UdpHelloOptions hello;
    hello.authToken = ctx.directoryPunchToken;
    hello.budgetMs = 10000;
    hello.sliceMaxMs = 200;
    hello.retrySleepMs = 50;
    std::string helloError;
    const bool handshakeOk =
        remote60::native_poc::udp_hello_handshake(ctx.session.sock, hello, nullptr, &helloError);
    // Back to a blocking read, as before; the control tunnel (if any) sets its own timeout next.
    int timeoutMs = 0;
    (void)setsockopt(ctx.session.sock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    if (!handshakeOk) {
      std::cerr << "[native-video-client] udp handshake failed " << ctx.resolvedArgs.host << ":"
                << ctx.resolvedArgs.port << " (" << helloError << ")\\n";
      closesocket(ctx.session.sock);
      ctx.session.sock = INVALID_SOCKET;
      if (ctx.dec.mfStarted) MFShutdown();
      return 6;
    }
  }
  return 0;
}
""",
"""    remote60::native_poc::UdpHelloOptions hello;
    hello.authToken = ctx.directoryPunchToken;
    hello.budgetMs = 10000;
    hello.sliceMaxMs = 200;
    hello.retrySleepMs = 50;
    // Ask for selective retransmit (kUdpFeatureVideoNack) and keep the HelloAck bits: the recv
    // thread only NACKs against a host that acknowledged it, and an old host that never sets the
    // bit is never sent one. (Windows NACK wiring -- the ack used to be discarded here.)
    hello.requestNack = ctx.videoNackEnabled;
    uint32_t ackFeatures = 0;
    std::string helloError;
    const bool handshakeOk = remote60::native_poc::udp_hello_handshake(
        ctx.session.sock, hello, nullptr, &helloError, &ackFeatures);
    // A short receive timeout from here on, on BOTH the direct and the tunnelled path. It is the
    // clock of everything the recv thread does on a quiet link -- NACK rounds, the in-order hold,
    // the keyframe recovery deadline, the control tunnel's retransmits -- and the direct path used
    // to block forever, which left a lost chunk on a static screen unrepaired until the next frame.
    const DWORD recvTimeoutMs = ctx.udpRecvTimeoutMs;
    (void)setsockopt(ctx.session.sock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&recvTimeoutMs), sizeof(recvTimeoutMs));
    if (!handshakeOk) {
      std::cerr << "[native-video-client] udp handshake failed " << ctx.resolvedArgs.host << ":"
                << ctx.resolvedArgs.port << " (" << helloError << ")\\n";
      closesocket(ctx.session.sock);
      ctx.session.sock = INVALID_SOCKET;
      if (ctx.dec.mfStarted) MFShutdown();
      return 6;
    }
    ctx.session.udpHelloAckFeatures = ackFeatures;
    ctx.session.hostSupportsNack =
        ctx.videoNackEnabled &&
        (ackFeatures & remote60::native_poc::kUdpFeatureVideoNack) != 0;
    std::cout << "[native-video-client] udp hello ack features=0x" << std::hex << ackFeatures
              << std::dec << " nackRequested=" << (ctx.videoNackEnabled ? 1 : 0)
              << " nackNegotiated=" << (ctx.session.hostSupportsNack ? 1 : 0)
              << " nackHoldUs=" << ctx.videoNackHoldUs
              << " recvTimeoutMs=" << ctx.udpRecvTimeoutMs << "\\n";
  }
  return 0;
}
"""),
(
"""    ctx.control.overUdp.store(true, std::memory_order_release);
    // Without this the receive blocks forever on a link that has gone quiet, and the tick above
    // never runs -- which is the one moment recovery is needed.
    DWORD recvTimeoutMs = 200;
    setsockopt(ctx.session.sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs),
               sizeof(recvTimeoutMs));
    std::cout << "[native-video-client] control tunnelled over the media socket\\n";
""",
"""    ctx.control.overUdp.store(true, std::memory_order_release);
    // The receive timeout that lets the tick above run on a quiet link is set once for every UDP
    // session in connect_media_socket (it used to be set here, for the tunnel only).
    std::cout << "[native-video-client] control tunnelled over the media socket\\n";
"""),
(
"""void start_receiver(ViewerContext& ctx) {
  ctx.startUs = qpc_now_us();
  ctx.receiver.emplace(ctx, ctx.args, ctx.dec, ctx.gate, ctx.startUs, ctx.udpSimDropPm, ctx.udpSimDropSeed);
""",
"""void start_receiver(ViewerContext& ctx) {
  ctx.startUs = qpc_now_us();
  VideoReceiver::NackOptions nack;
  nack.enabled = ctx.session.hostSupportsNack;
  nack.holdUs = ctx.videoNackHoldUs;
  ctx.receiver.emplace(ctx, ctx.args, ctx.dec, ctx.gate, ctx.startUs, ctx.udpSimDropPm, ctx.udpSimDropSeed,
                       nack);
"""),
])
