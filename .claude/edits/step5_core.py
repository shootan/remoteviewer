import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'

# ---- A. assembler: byte cap + a complete keyframe releases the broken chain ahead of it ----
edit(SRC + r'\native_video_client_shared_core.hpp', [
("""  void ConfigureInOrderHold(uint64_t maxHoldUs, size_t maxConcurrent);
  bool InOrderHoldEnabled() const { return holdMaxUs_ > 0; }
""",
"""  // `maxHeldBytes` bounds the payload held across assemblies (0 = unbounded); past it the
  // oldest assembly is given up, exactly like the count cap. (Codex condition 1.)
  void ConfigureInOrderHold(uint64_t maxHoldUs, size_t maxConcurrent,
                            size_t maxHeldBytes = 8u * 1024u * 1024u);
  bool InOrderHoldEnabled() const { return holdMaxUs_ > 0; }
  size_t HeldBytes() const;
"""),
("""  uint64_t holdMaxUs_ = 0;      // 0 = legacy immediate delivery
  size_t maxConcurrent_ = 3;    // kMaxConcurrentVideoAssemblies unless ConfigureInOrderHold raised it
};
""",
"""  uint64_t holdMaxUs_ = 0;      // 0 = legacy immediate delivery
  size_t maxConcurrent_ = 3;    // kMaxConcurrentVideoAssemblies unless ConfigureInOrderHold raised it
  size_t maxHeldBytes_ = 0;     // hold mode only; 0 = unbounded
};
"""),
])

edit(SRC + r'\native_video_client_shared_core.cpp', [
("""void UdpH264FrameAssembler::ConfigureInOrderHold(uint64_t maxHoldUs, size_t maxConcurrent) {
  holdMaxUs_ = maxHoldUs;
  maxConcurrent_ = std::max<size_t>(1, maxConcurrent);
}
""",
"""void UdpH264FrameAssembler::ConfigureInOrderHold(uint64_t maxHoldUs, size_t maxConcurrent,
                                                 size_t maxHeldBytes) {
  holdMaxUs_ = maxHoldUs;
  maxConcurrent_ = std::max<size_t>(1, maxConcurrent);
  maxHeldBytes_ = maxHeldBytes;
}

size_t UdpH264FrameAssembler::HeldBytes() const {
  size_t total = 0;
  for (const Assembly& a : assemblies_) total += a.payloadSize;
  return total;
}
"""),
("""    if (oldest->complete) {
      *out = DeliverAssembly(*oldest);
      return true;
    }
    const bool anyComplete = std::any_of(assemblies_.begin(), assemblies_.end(),
                                         [](const Assembly& a) { return a.complete; });
    if (!anyComplete) return false;  // nothing behind it to release anyway
    const bool keyFrame = (oldest->header.flags & kEncodedFrameFlagKeyFrame) != 0;
""",
"""    if (oldest->complete) {
      *out = DeliverAssembly(*oldest);
      return true;
    }
    const bool anyComplete = std::any_of(assemblies_.begin(), assemblies_.end(),
                                         [](const Assembly& a) { return a.complete; });
    if (!anyComplete) return false;  // nothing behind it to release anyway
    // A complete keyframe behind the broken chain is the recovery point: nothing older than it
    // can be decoded without the missing reference anyway, and the IDR does not need it. Give up
    // everything ahead of the newest complete IDR at once, so the IDR goes out now and the gap it
    // carries is closed by the IDR itself (no request). (Codex condition 1.)
    auto newestKey = assemblies_.end();
    for (auto it = assemblies_.begin(); it != assemblies_.end(); ++it) {
      if (!it->complete || (it->header.flags & kEncodedFrameFlagKeyFrame) == 0) continue;
      if (newestKey == assemblies_.end() || sequence_is_newer(it->seq, newestKey->seq)) newestKey = it;
    }
    if (newestKey != assemblies_.end()) {
      const uint32_t keySeq = newestKey->seq;
      assemblies_.erase(std::remove_if(assemblies_.begin(), assemblies_.end(),
                                       [&](const Assembly& a) { return sequence_is_newer(keySeq, a.seq); }),
                        assemblies_.end());
      continue;
    }
    const bool keyFrame = (oldest->header.flags & kEncodedFrameFlagKeyFrame) != 0;
"""),
("""  if (assemblyIt == assemblies_.end()) {
    if (assemblies_.size() >= maxConcurrent_) {
      if (holdMaxUs_ > 0) {
        // The oldest by sequence is the stuck head (complete ones were already popped).
        assemblies_.erase(std::min_element(assemblies_.begin(), assemblies_.end(),
                                           [](const Assembly& a, const Assembly& b) {
                                             return sequence_is_newer(b.seq, a.seq);
                                           }));
      } else {
        assemblies_.pop_front();
      }
      result.droppedPreviousIncomplete = true;
    }
""",
"""  if (assemblyIt == assemblies_.end()) {
    const auto evict_oldest = [&]() {
      if (holdMaxUs_ > 0) {
        // The oldest by sequence is the stuck head (complete ones were already popped).
        assemblies_.erase(std::min_element(assemblies_.begin(), assemblies_.end(),
                                           [](const Assembly& a, const Assembly& b) {
                                             return sequence_is_newer(b.seq, a.seq);
                                           }));
      } else {
        assemblies_.pop_front();
      }
      result.droppedPreviousIncomplete = true;
    };
    if (assemblies_.size() >= maxConcurrent_) evict_oldest();
    // Hold mode: the payload held across assemblies is bounded too (a burst of large IDRs must
    // not pile up behind one stuck AU). (Codex condition 1.)
    while (holdMaxUs_ > 0 && maxHeldBytes_ > 0 && !assemblies_.empty() &&
           HeldBytes() + packet.payloadSize > maxHeldBytes_) {
      evict_oldest();
    }
"""),
])

# ---- B. NACK scheduler: the tail phase gets its own round budget ----
edit(SRC + r'\udp_video_nack.hpp', [
("""  void Reset() {
    seq_ = 0;
    generation_ = 0;
    firstUs_ = 0;
    lastUs_ = 0;
    rounds_ = 0;
    exhaustedCounted_ = false;
  }
""",
"""  void Reset() {
    seq_ = 0;
    generation_ = 0;
    firstUs_ = 0;
    lastUs_ = 0;
    rounds_ = 0;
    exhaustedCounted_ = false;
    tailPhase_ = false;
  }
  // An AU is being chased and still has rounds left: the keyframe recovery timer defers to it so
  // a retransmit and a fresh IDR do not race for the same loss. (Codex condition 2.)
  bool busy() const { return seq_ != 0 && rounds_ < cfg_.maxRounds; }
"""),
("""      seq_ = info.seq;
      generation_ = info.generation;
      firstUs_ = (info.firstPacketUs != 0 && info.firstPacketUs <= nowUs) ? info.firstPacketUs : nowUs;
      lastUs_ = 0;
      rounds_ = 0;
      exhaustedCounted_ = false;
    }
    if (rounds_ >= cfg_.maxRounds) {
      if (!exhaustedCounted_) {
        ++stats_.roundsExhausted;
        exhaustedCounted_ = true;
      }
      return false;
    }
    if (lastUs_ != 0 && nowUs - lastUs_ < cfg_.roundUs) return false;
    const uint64_t age = (nowUs >= firstUs_) ? (nowUs - firstUs_) : 0;
    const bool gapEligible = age >= cfg_.gapGraceUs;
    const bool tailEligible = age >= cfg_.tailGraceUs;
    if (!gapEligible && !tailEligible) return false;
    const uint16_t have = std::min<uint16_t>(info.missingTotal, kMax);
""",
"""      seq_ = info.seq;
      generation_ = info.generation;
      firstUs_ = (info.firstPacketUs != 0 && info.firstPacketUs <= nowUs) ? info.firstPacketUs : nowUs;
      lastUs_ = 0;
      rounds_ = 0;
      exhaustedCounted_ = false;
      tailPhase_ = false;
    }
    const uint64_t age = (nowUs >= firstUs_) ? (nowUs - firstUs_) : 0;
    const bool gapEligible = age >= cfg_.gapGraceUs;
    const bool tailEligible = age >= cfg_.tailGraceUs;
    const uint16_t have = std::min<uint16_t>(info.missingTotal, kMax);
    // Holes and the tail are two phases with a round budget each: rounds spent chasing a hole
    // while a large frame's tail was still (legitimately) in flight must not leave the tail with
    // none once its long grace passes. (Codex condition 2.)
    const bool hasTail = have > 0 && missing[have - 1] >= info.highWater;
    if (tailEligible && hasTail && !tailPhase_) {
      tailPhase_ = true;
      rounds_ = 0;
      lastUs_ = 0;
      exhaustedCounted_ = false;
    }
    if (rounds_ >= cfg_.maxRounds) {
      if (!exhaustedCounted_) {
        ++stats_.roundsExhausted;
        exhaustedCounted_ = true;
      }
      return false;
    }
    if (lastUs_ != 0 && nowUs - lastUs_ < cfg_.roundUs) return false;
    if (!gapEligible && !tailEligible) return false;
"""),
("""  uint32_t rounds_ = 0;
  bool exhaustedCounted_ = false;
};
""",
"""  uint32_t rounds_ = 0;
  bool exhaustedCounted_ = false;
  bool tailPhase_ = false;
};
"""),
])

# ---- C. the recovery timer defers to a NACK repair in progress ----
edit(SRC + r'\viewer_frame_gate_state.hpp', [
("""  uint64_t recoveryRetryCount = 0;      // telemetry: timer-driven requests (reason 7)
""",
"""  uint64_t recoveryRetryCount = 0;      // telemetry: timer-driven requests (reason 7)
  uint64_t recoveryRetryDeferred = 0;   // telemetry: ticks that held the re-ask for a NACK repair
"""),
])
edit(SRC + r'\viewer_frame_gate.hpp', [
("""  void tick(uint64_t nowUs);
""",
"""  // `repairInProgress`: a NACK retransmit is still being chased for the AU that would otherwise
  // need this IDR; the re-ask waits so the two recoveries do not race (Codex condition 2).
  void tick(uint64_t nowUs, bool repairInProgress = false);
"""),
])
edit(SRC + r'\viewer_frame_gate.cpp', [
("""void FrameGate::tick(uint64_t nowUs) {
""",
"""void FrameGate::tick(uint64_t nowUs, bool repairInProgress) {
"""),
("""  if (nowUs < gate.recoveryNextRetryUs) return;
  const bool firstRetry = (gate.recoveryRetryCurrentUs == gate.recoveryRetryIntervalUs);
""",
"""  if (nowUs < gate.recoveryNextRetryUs) return;
  if (repairInProgress) {
    // A retransmit may still complete the missing AU (or the missing IDR itself): asking for a
    // fresh IDR now would have both answers on the wire. Checked again on the next tick.
    ++gate.recoveryRetryDeferred;
    return;
  }
  const bool firstRetry = (gate.recoveryRetryCurrentUs == gate.recoveryRetryIntervalUs);
"""),
("""       << " keyRetries=" << gate.recoveryRetryCount
       << " keyRetryEpisodes=" << gate.recoveryRetryEpisodes
""",
"""       << " keyRetries=" << gate.recoveryRetryCount
       << " keyRetryDeferred=" << gate.recoveryRetryDeferred
       << " keyRetryEpisodes=" << gate.recoveryRetryEpisodes
"""),
])
edit(SRC + r'\viewer_video_receiver.cpp', [
("""        maybe_send_nack(qpc_now_us());
        fg.tick(qpc_now_us());  // the keyframe recovery clock runs with or without frames
        continue;
""",
"""        maybe_send_nack(qpc_now_us());
        fg.tick(qpc_now_us(), nackScheduler.busy());  // the keyframe recovery clock runs with or without frames
        continue;
"""),
("""    maybe_send_nack(qpc_now_us());
    fg.tick(qpc_now_us());
""",
"""    maybe_send_nack(qpc_now_us());
    fg.tick(qpc_now_us(), nackScheduler.busy());
"""),
])

# ---- E. shutdown: bounded join, then end the process rather than hang ----
edit(SRC + r'\viewer_shutdown.cpp', [
("""#include "viewer_shutdown.hpp"

#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {
""",
"""#include "viewer_shutdown.hpp"

#include <iostream>

#include "viewer_state.hpp"
#include "viewer_thread_join.hpp"

namespace remote60::native_poc::viewer {

namespace {
// The recv thread leaves within one receive timeout (25 ms on UDP) once the socket is closed and
// `running` is down; a wedged decoder / D3D call is the only thing that keeps it. Long enough to
// never cut a healthy shutdown short, short enough that a dead session does not become a hung
// process.
constexpr uint32_t kRecvThreadJoinTimeoutMs = 3000;
}  // namespace
"""),
("""  if (ctx.controlThread.joinable()) ctx.controlThread.join();
  if (ctx.recvThread.joinable()) ctx.recvThread.join();
""",
"""  if (ctx.controlThread.joinable()) ctx.controlThread.join();
  if (!join_with_timeout(ctx.recvThread, kRecvThreadJoinTimeoutMs)) {
    // The recv thread is wedged (the liveness watchdog will have logged the stage). Nothing
    // below -- decoder shutdown, MFShutdown -- can be trusted to return either, and the shell
    // can only restart a viewer that has actually gone. End the process: a last resort that
    // reports itself, not a recovery. (Codex condition 4 on history #390 item 5.)
    const uint64_t nowUs = qpc_now_us();
    std::cout << "[native-video-client][liveness] recv thread did not exit within "
              << kRecvThreadJoinTimeoutMs << " ms at shutdown stage="
              << recv_stage_name(ctx.recvLive.current_stage())
              << " stageAgeUs=" << liveness_age_us(nowUs, ctx.recvLive.stageEnterUs.load(std::memory_order_relaxed))
              << " loops=" << ctx.recvLive.loopIterations.load(std::memory_order_relaxed)
              << "; terminating the process\\n";
    std::cout.flush();
    ctx.recvThread.detach();
    TerminateProcess(GetCurrentProcess(), 44);
  }
"""),
])

# ---- F. the product's Hello / ack / timeout through the shared helpers ----
edit(SRC + r'\viewer_startup.cpp', [
("""    remote60::native_poc::UdpHelloOptions hello;
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
""",
"""    // The Hello (with the NACK request), the ack bits kept, the receive timeout armed: the three
    // pieces the receive-path integration test runs through the same functions
    // (viewer_udp_session.hpp), so a regression here fails that test.
    const remote60::native_poc::UdpHelloOptions hello =
        viewer_udp_hello_options(ctx.directoryPunchToken, ctx.videoNackEnabled);
    uint32_t ackFeatures = 0;
    std::string helloError;
    const bool handshakeOk = remote60::native_poc::udp_hello_handshake(
        ctx.session.sock, hello, nullptr, &helloError, &ackFeatures);
    // A short receive timeout from here on, on BOTH the direct and the tunnelled path. It is the
    // clock of everything the recv thread does on a quiet link -- NACK rounds, the in-order hold,
    // the keyframe recovery deadline, the control tunnel's retransmits -- and the direct path used
    // to block forever, which left a lost chunk on a static screen unrepaired until the next frame.
    (void)viewer_arm_udp_recv_timeout(ctx.session.sock, ctx.udpRecvTimeoutMs);
    if (!handshakeOk) {
"""),
("""    ctx.session.udpHelloAckFeatures = ackFeatures;
    ctx.session.hostSupportsNack =
        ctx.videoNackEnabled &&
        (ackFeatures & remote60::native_poc::kUdpFeatureVideoNack) != 0;
    std::cout << "[native-video-client] udp hello ack features=0x" << std::hex << ackFeatures
""",
"""    viewer_apply_udp_hello_ack(ackFeatures, ctx.videoNackEnabled, ctx.session);
    std::cout << "[native-video-client] udp hello ack features=0x" << std::hex << ackFeatures
"""),
])
import re
p = SRC + r'\viewer_startup.cpp'
t = open(p, 'rb').read().decode('utf-8')
if '#include "viewer_udp_session.hpp"' not in t:
    anchor = '#include "viewer_startup.hpp"\n'
    i = t.find(anchor)
    assert i >= 0, 'viewer_startup.cpp: no self include'
    i += len(anchor)
    t = t[:i] + '#include "viewer_udp_session.hpp"\n' + t[i:]
    open(p, 'wb').write(t.encode('utf-8'))
    print('ok include added to viewer_startup.cpp')
