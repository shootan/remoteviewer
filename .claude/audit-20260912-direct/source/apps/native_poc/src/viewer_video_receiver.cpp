// See viewer_video_receiver.hpp. Bodies are the recvThread lambda of native_video_client_main.cpp,
// verbatim (viewer split refactor Phase 2-1).

#include "viewer_video_receiver.hpp"

#include "viewer_frame_gate.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>

#include "udp_video_nack.hpp"
#include "viewer_decoder_backend.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"
#include "viewer_present.hpp"

namespace remote60::native_poc::viewer {

void VideoReceiver::DecoderSink::reset_decoder() { dec.decoder.reset(); }

bool VideoReceiver::DecoderSink::rebuild_decoder() {
  return dec.decoder.initialize(dec.decoderW, dec.decoderH, args.fpsHint);
}

void VideoReceiver::DecoderSink::request_keyframe(uint16_t reason) { viewer::request_keyframe(ctx, reason); }

namespace {

// Wait until the socket is readable or the timeout passes. Lets a loop that would otherwise sit in
// a blocking recv come up for air on a quiet link -- which is exactly when a --seconds limit or a
// running=false has to be noticed. True on readable OR error (the recv reports the error).
bool wait_readable(SOCKET s, int timeoutMs) {
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(s, &readSet);
  timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  const int ready = select(0, &readSet, nullptr, nullptr, &tv);
  return ready != 0;  // 0 = timeout; >0 readable; <0 error -> let recv see it
}

}  // namespace

void VideoReceiver::run_udp() {
  std::array<uint8_t, 1600> datagram{};
  const uint32_t effectiveUdpSimDropSeed = (udpSimDropSeed > 0)
                                               ? udpSimDropSeed
                                               : static_cast<uint32_t>(qpc_now_us() & 0x7fffffffu);
  std::minstd_rand udpSimRng(effectiveUdpSimDropSeed);
  std::uniform_int_distribution<uint32_t> udpSimDropDist(0, 999);
  UdpH264FrameAssembler assembler;
  // Video NACK (Windows wiring). Against a host that acknowledged kUdpFeatureVideoNack the
  // assembler delivers in sequence order and holds a completed AU for up to nack.holdUs while an
  // older one may still be repaired; the shared scheduler (udp_video_nack.hpp) decides when to
  // ask. Both stay off otherwise, which is the pre-NACK behaviour exactly (old host, env override).
  const bool nackEnabled = nack.enabled;
  const bool holdEnabled = nackEnabled && nack.holdUs > 0;
  constexpr size_t kHoldMaxConcurrentAssemblies = 8;  // ~120 ms of 60 fps AUs in flight
  if (holdEnabled) assembler.ConfigureInOrderHold(nack.holdUs, kHoldMaxConcurrentAssemblies);
  // A04 saturation: the assembler asks the viewer's own generation gate before it starts or
  // replaces a key candidate, rather than deciding admissibility itself (shared_core must not
  // depend on viewer state). The final gate in the decode path is unchanged and still decides.
  assembler.SetSaturationAdmitFilter([this](uint64_t generation) {
    return ctx.sel.AdmitGeneration(generation) == SelectionAdmit::Accept;
  });
  // The frame path (process_h264_frame) reports the accepted recovery key back to the assembler.
  // Valid only for this call's lifetime; cleared on the way out.
  activeAssembler_ = &assembler;
  struct AssemblerScope {
    UdpH264FrameAssembler** slot;
    ~AssemblerScope() { *slot = nullptr; }
  } assemblerScope{&activeAssembler_};
  VideoNackScheduler nackScheduler;
  uint64_t lastUdpNackSentCount = 0;
  uint64_t lastUdpNackChunkCount = 0;
  auto maybe_send_nack = [&](uint64_t nowUs) {
    if (!nackEnabled) return;
    UdpVideoNackPacket pkt{};
    if (!nackScheduler.Poll(assembler, !gate.waitForKeyFrame, nowUs, &pkt)) return;
    (void)send(ctx.session.sock, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0);
    ++st.udpNackSentCount;
    st.udpNackChunkCount += pkt.missingCount;
    if (st.udpNackSentCount <= 5 || (st.udpNackSentCount % 200) == 1) {
      std::cout << "[native-video-client] video-nack seq=" << pkt.seq
                << " gen=" << pkt.streamGeneration << " missing=" << pkt.missingCount
                << " of=" << pkt.chunkCount << " round=" << pkt.round
                << " total=" << st.udpNackSentCount
                << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0) << "\n";
    }
  };
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
  uint64_t lastUdpAssemblyKeyResyncCount = 0;
  uint64_t lastUdpAssemblyFecRecoveredCount = 0;
  uint64_t lastUdpSimDroppedCount = 0;
  uint64_t lastUdpSimAcceptedCount = 0;

  // One decoder reset + one keyframe request per loss EPISODE, not per lost frame (P5). Under
  // sustained loss every frame is a discontinuity; resetting the decoder and asking for an IDR
  // each time kept the reference chain permanently broken and turned each answer into a large
  // IDR spike that tipped a marginal link further over -- a death spiral that ended in
  // peer-lost. While already waiting for a keyframe the frame gate keeps re-asking on its own
  // timeout, so suppressing the repeat only removes churn, never the eventual recovery.
  // (history #337, item P5.) The latch is per receive-loop iteration.
  bool discontinuityHandled = false;
  auto handle_udp_discontinuity = [&]() {
    if (discontinuityHandled) return;
    discontinuityHandled = true;
    const bool alreadyWaiting = gate.waitForKeyFrame;
    gate.waitForKeyFrame = true;
    ++st.udpAssemblyKeyReqCount;
    if (alreadyWaiting) return;
    dec.decoder.reset();
    request_keyframe(ctx, 2);
  };
  // The delivered AU's seq is not the previous one + 1: whatever sat between them is gone.
  auto note_sequence_gap = [&](const UdpH264AssemblyStepResult& r) {
    ++assemblyDropped;
    ++st.udpAssemblyDroppedCount;
    const bool completedKey =
        r.disposition == UdpH264AssemblyDisposition::Completed &&
        (r.frame.header.flags & kEncodedFrameFlagKeyFrame) != 0;
    if (completedKey) {
      // The AU that revealed the gap is itself a complete IDR: it resyncs the decoder on its own
      // (an IDR clears the reference set), so asking the host for ANOTHER one only buys a second
      // ~300KB IDR -- and because the host clears its send queue when it enqueues that IDR, the
      // answer arrives behind a fresh gap and asks again (09-05 log: 90 of 90 reason-2 requests
      // came right after a complete keyframe). A gap here is normal host behaviour (EnqueueKey /
      // HoldForKey consume seqs they never send), not loss. The wait flag still closes the gate
      // until this IDR decodes; if it fails, note_decode_failure asks (reason 4). (history #390
      // item 3.)
      ++st.udpAssemblyKeyResyncCount;
      gate.waitForKeyFrame = true;
      return;
    }
    handle_udp_discontinuity();
  };
  // Everything that used to follow a Completed disposition inline: the telemetry line, then the
  // frame. False when the receive loop must end (process_h264_frame said so).
  auto deliver_completed = [&](UdpH264AssemblyStepResult& r) -> bool {
    ++st.udpAssemblyCompletedCount;
    const uint64_t packetNowUs = qpc_now_us();
    ctx.recvLive.lastAssembledUs.store(packetNowUs, std::memory_order_relaxed);
    // GNLink stream telemetry (diagnostics only): one line per assembled keyframe, plus any
    // non-key frame that needed FEC repair or showed loss/reorder, so a periodic-stutter
    // session joins the host 'wire seq=' log by seq+gen while steady play stays quiet.
    {
      const auto& fh = r.frame.header;
      const bool key = ((fh.flags & 1u) != 0);
      if (key || r.fecRecovered || r.reorderDetected || r.droppedPreviousIncomplete) {
        std::ostringstream telem;
        telem << "[native-video-client][telemetry] stage=assembly"
              << " seq=" << fh.seq
              << " gen=" << fh.streamGeneration
              << " key=" << (key ? 1 : 0)
              << " lastChunkRecvUs=" << packetNowUs
              << " bytes=" << fh.payloadSize
              << " fecRecovered=" << (r.fecRecovered ? 1 : 0)
              << " fecRecoveredChunks=" << r.fecRecoveredChunks
              << " reorder=" << (r.reorderDetected ? 1 : 0)
              << " droppedPrev=" << (r.droppedPreviousIncomplete ? 1 : 0)
              << " pending=" << assembler.PendingCount();
        log_client_line(ctx, telem.str());
      }
    }
    auto payload = std::move(r.frame.payload);
    return process_h264_frame(r.frame.header, &payload, packetNowUs);
  };
  // In-order hold: hand out everything that is ready in sequence, and whatever an expired hold
  // releases. Runs after every datagram AND on every receive timeout, because the hold expires on
  // the clock, not on arrival. False when the receive loop must end.
  auto drain_deliveries = [&]() -> bool {
    if (!holdEnabled) return true;
    UdpH264AssemblyStepResult ready{};
    while (assembler.PopDelivery(qpc_now_us(), !gate.waitForKeyFrame, &ready)) {
      if (ready.droppedPreviousIncomplete) note_sequence_gap(ready);
      if (!deliver_completed(ready)) return false;
    }
    return true;
  };

  // Clock-driven maintenance, one place for every path through the loop (V14 / A04). The in-order
  // hold expires on the clock, the NACK rounds are spaced on the clock and the keyframe recovery
  // timer is a clock -- yet all three used to run only after a video chunk or a receive timeout.
  // Control replies over the tunnel, cursor samples, malformed or ignored datagrams took a
  // `continue` before them, so a link that carried nothing but control traffic every few ms
  // (recv never timing out) starved them: a lost frame's keyframe wait was never re-asked and a
  // held AU was never released. Runs at the top of every iteration, rate-limited by elapsed time
  // (>= kMaintenanceMinIntervalUs) so a 5 ms control cadence still gets it every 5 ms; forced on
  // a receive timeout (the old behaviour there).
  //
  // N3 (A04): an incomplete head with NOTHING complete behind it. PopDelivery only gives a head up
  // when a complete AU is waiting behind it (there is nothing else to release), and the NACK
  // scheduler, once its rounds are spent, simply stops -- so on a static screen whose last AU
  // lost a chunk the viewer sat on the old picture with a silent, exhausted NACK and no
  // discontinuity ever raised (the host's next AU could be far away, or never, on a secure
  // desktop). Here the head is given up when its rounds are spent (one round after the last
  // request, so the final retransmit gets its chance) or its hold is past with no repair in
  // progress, and the usual discontinuity path follows: keyframe wait + request (reason 2, the
  // existing limiter and 500 ms -> 2 s re-ask backoff). A complete IDR behind a gap is still the
  // resync without a request (note_sequence_gap), and Android (hold 0) never enters this.
  uint64_t lastMaintenanceUs = 0;
  uint64_t stuckHeadGiveUps = 0;
  constexpr uint64_t kMaintenanceMinIntervalUs = 5000;
  auto maintenance = [&](uint64_t nowUs, bool force) -> bool {
    if (!force && lastMaintenanceUs != 0 && nowUs >= lastMaintenanceUs &&
        nowUs - lastMaintenanceUs < kMaintenanceMinIntervalUs) {
      return true;
    }
    lastMaintenanceUs = nowUs;
    if (!drain_deliveries()) return false;
    maybe_send_nack(nowUs);
    // N3 (A04), the confirmed give-up rule, for an incomplete head with NOTHING complete behind it
    // (a static screen: PopDelivery has nothing to release; A03's hold policy for a head with a
    // complete successor is untouched). The head is given up only when every repair avenue had
    // its chance: (1) the scheduler spent its LAST applicable phase for this AU (tail rounds when
    // the loss includes a tail, else hole rounds), (2) one reply allowance passed since the last
    // NACK actually sent, (3) one reply allowance passed since the AU last made progress (a new
    // data chunk or an FEC-recovered chunk; duplicates, control and other AUs are not progress),
    // (4) the AU is at least terminalMin old. Without NACK (not negotiated, or the scheduler is
    // not chasing it) (3)+(4) alone bound the wait. (5) Past hardCap it is given up whatever its
    // progress -- a failure budget, not a delivery guarantee: an AU that keeps completing chunks
    // for over a second is preserved until then. replyAllowance = clamp(max(50 ms, 2 x the control
    // thread's recent RTT), replyAllowanceMax), 50 ms when the RTT is unknown or stale;
    // terminalMin = tailGrace + maxRounds x round + replyAllowance (245 ms with the defaults and
    // no RTT). While a keyframe is awaited a non-key head goes on (3) alone -- no new request or
    // reset per head (handle_udp_discontinuity's alreadyWaiting guard) -- so a following incomplete
    // IDR keeps its repair chance; a key head takes the full rule. Only the judged (gen, seq) is
    // removed, and only if it is still incomplete (GiveUpIncomplete cancels otherwise).
    if (!assembler.AnyComplete()) {
      uint16_t missing[kUdpVideoNackMaxMissing];
      UdpH264FrameAssembler::IncompleteAuInfo info{};
      if (assembler.OldestIncomplete(missing, kUdpVideoNackMaxMissing, &info) && info.firstPacketUs != 0) {
        const auto& ncfg = nackScheduler.config();
        const uint64_t rttUs = ctx.control.lastRttUs.load(std::memory_order_relaxed);
        const uint64_t rttAtUs = ctx.control.lastRttAtUs.load(std::memory_order_relaxed);
        const bool rttFresh = rttAtUs != 0 && nowUs >= rttAtUs && nowUs - rttAtUs <= nack.rttStaleUs;
        uint64_t allowanceUs = nack.replyAllowanceMinUs;
        if (rttFresh) allowanceUs = std::max<uint64_t>(allowanceUs, 2 * rttUs);
        allowanceUs = std::min<uint64_t>(allowanceUs, nack.replyAllowanceMaxUs);
        const uint64_t terminalMinUs =
            ncfg.tailGraceUs + static_cast<uint64_t>(ncfg.maxRounds) * ncfg.roundUs + allowanceUs;
        const uint64_t hardCapUs = std::max<uint64_t>(nack.giveUpHardCapUs, terminalMinUs);
        const uint64_t ageUs = nowUs >= info.firstPacketUs ? nowUs - info.firstPacketUs : 0;
        const uint64_t sinceProgressUs =
            (info.lastProgressUs != 0 && nowUs >= info.lastProgressUs) ? nowUs - info.lastProgressUs : ageUs;
        const uint16_t have = std::min<uint16_t>(info.missingTotal, kUdpVideoNackMaxMissing);
        const bool hasTail = have > 0 && missing[have - 1] >= info.highWater;
        const bool chased = nackEnabled && nackScheduler.current_seq() == info.seq &&
                            nackScheduler.current_generation() == info.generation;
        const bool noProgress = sinceProgressUs >= allowanceUs;  // (3)
        const bool oldEnough = ageUs >= terminalMinUs;          // (4)
        const char* why = nullptr;
        if (ageUs >= hardCapUs) {
          why = "hard-cap";  // (5)
        } else if (gate.waitForKeyFrame && !info.keyFrame) {
          if (noProgress) why = "no-progress-during-key-wait";
        } else if (chased) {
          const bool spent = nackScheduler.spent_for(hasTail);  // (1)
          const bool replyOver = nackScheduler.last_sent_us() != 0 &&
                                 nowUs >= nackScheduler.last_sent_us() + allowanceUs;  // (2)
          if (spent && replyOver && noProgress && oldEnough) why = "nack-spent";
        } else if (noProgress && oldEnough) {
          why = nackEnabled ? "no-progress" : "no-progress-nack-off";
        }
        if (why && assembler.GiveUpIncomplete(info.generation, info.seq, nowUs)) {
          if (chased) nackScheduler.Reset();
          ++assemblyDropped;
          ++st.udpAssemblyDroppedCount;
          ++stuckHeadGiveUps;
          ++st.udpStuckHeadGiveUps;
          if (stuckHeadGiveUps <= 5 || (stuckHeadGiveUps % 100) == 1) {
            std::cout << "[native-video-client] stuck head given up seq=" << info.seq
                      << " gen=" << info.generation << " missing=" << info.missingTotal
                      << " of=" << info.chunkCount << " reason=" << why
                      << " ageUs=" << ageUs << " sinceProgressUs=" << sinceProgressUs
                      << " hasTail=" << (hasTail ? 1 : 0) << " chased=" << (chased ? 1 : 0)
                      << " rttUs=" << rttUs << (rttFresh ? "" : "(stale)")
                      << " replyAllowanceUs=" << allowanceUs << " terminalMinUs=" << terminalMinUs
                      << " hardCapUs=" << hardCapUs << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0)
                      << " total=" << stuckHeadGiveUps << "\n";
          }
          handle_udp_discontinuity();
        }
      }
    }
    fg.tick(nowUs, nackScheduler.busy());  // the keyframe recovery clock runs with or without frames
    return true;
  };

  ctx.recvLive.Enter(RecvStage::Recv, qpc_now_us());
  while (ctx.session.running.load()) {
    // At the top of the loop, so it also runs on the receive timeouts a quiet link produces --
    // previously it sat after the packet processing and a socket that stayed silent never
    // reached it, so a harness --seconds limit could only end through the UI's own timer and
    // the socket close. (F-13.)
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
    discontinuityHandled = false;
    {
      // Liveness heartbeat for the UI watchdog: one pass per loop, timeouts included.
      const uint64_t loopUs = qpc_now_us();
      ctx.recvLive.loopIterations.fetch_add(1, std::memory_order_relaxed);
      ctx.recvLive.lastLoopUs.store(loopUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Recv, loopUs);
    }
    // Every path through the loop passes here first (see maintenance above).
    if (!maintenance(qpc_now_us(), false)) break;
    const int n = recv(ctx.session.sock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
    if (n <= 0) {
      // A read timeout is not a dead socket. It is also the tunnel's heartbeat: the control
      // thread spends most of its time blocked waiting for a reply, so if retransmission
      // were driven from there it would stop exactly when a reply goes missing -- and the
      // host, hearing nothing, declares the client lost. This thread always runs. It is also
      // the clock of the in-order hold and of the NACK rounds on a quiet link: a lost chunk on
      // a static screen is noticed here, not by a next frame that may be seconds away.
      if (remote60::native_poc::last_socket_error_is_retryable()) {
        if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
        if (!maintenance(qpc_now_us(), true)) break;  // the clock work runs with or without frames
        continue;
      }
      break;
    }
    {
      const uint64_t gotUs = qpc_now_us();
      ctx.recvLive.lastDatagramUs.store(gotUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Control, gotUs);
    }
    if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
    // Control is offered the datagram BEFORE the video length guard, and the order is the
    // whole point. A control message is not bounded below by the video header: a
    // single-fragment input ack is 32 + 28 = 60 bytes against an 88-byte video header, so
    // checking the video size first silently ate every small reply -- input acks and window
    // selections -- while the larger ones (pong, window lists) came through and made the
    // channel look healthy. OnPacket claims only its own kinds, so video cannot be stolen.
    if (ctx.control.overUdp.load(std::memory_order_acquire) &&
        ctx.control.udpControl.OnPacket(datagram.data(), static_cast<size_t>(n))) {
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
          RemoteCursorSample sample;
          sample.x = cp.x;
          sample.y = cp.y;
          sample.capW = cp.captureW;
          sample.capH = cp.captureH;
          sample.generation = cp.streamGeneration;
          sample.visible = (cp.flags & 0x1u) != 0;
          sample.updateUs = qpc_now_us();
          ctx.cursor.Publish(sample);  // whole sample at once (F-15)
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
      ++st.skippedQueued;
      continue;
    }
    if (udpSimDropPm > 0) {
      const uint32_t samplePm = udpSimDropDist(udpSimRng);
      if (samplePm < udpSimDropPm) {
        ++udpSimDroppedCount;
        ++st.skippedQueued;
        continue;
      }
    }
    ++udpSimAcceptedCount;
    ++st.udpChunkRecvCount;

    {
      const uint64_t chunkUs = qpc_now_us();
      ctx.recvLive.lastVideoChunkUs.store(chunkUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Assembly, chunkUs);
    }
    auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n), qpc_now_us());
    if (assembleResult.fecRecovered) {
      st.udpAssemblyFecRecoveredCount += assembleResult.fecRecoveredChunks;
    }
    // Legacy delivery reports a seq gap on the datagram that revealed it (this may be a Partial:
    // an evicted older assembly). With the in-order hold the gap belongs to the delivery that
    // carries it (drain_deliveries), so a hold that is still running is not read as loss.
    if (assembleResult.droppedPreviousIncomplete && !holdEnabled) note_sequence_gap(assembleResult);

    if (assembleResult.disposition == UdpH264AssemblyDisposition::Malformed) {
      ++st.skippedQueued;
      ++st.udpAssemblyMalformedCount;
      handle_udp_discontinuity();
      if (assembleResult.oversizePayload && ((++oversizePayloadDropCount % 30ULL) == 1ULL)) {
        std::cout << "[native-video-client] dropped oversized udp payload bytes="
                  << assembleResult.rejectedPayloadSize
                  << " count=" << oversizePayloadDropCount << "\n";
      }
      continue;
    }

    if (assembleResult.disposition == UdpH264AssemblyDisposition::Dropped) {
      ++st.skippedQueued;
      ++assemblyDropped;
      ++st.udpAssemblyDroppedCount;
      if (assembleResult.reorderDetected) ++st.udpAssemblyReorderCount;
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
      // Legacy immediate delivery (no hold): the AU that just completed.
      if (!deliver_completed(assembleResult)) break;
    }
    // In-order hold: Queued (and anything an expired hold releases) goes out here, in sequence, so
    // a completed AU is not held back by the maintenance cadence. The NACK poll and the recovery
    // timer follow within kMaintenanceMinIntervalUs at the top of the next iteration.
    if (!drain_deliveries()) break;

    const uint64_t nowUs = qpc_now_us();
    if (nowUs >= udpAssemblyStatAtUs) {
      const uint64_t chunksDelta = st.udpChunkRecvCount - lastUdpChunkRecvCount;
      const uint64_t completedDelta = st.udpAssemblyCompletedCount - lastUdpAssemblyCompletedCount;
      const uint64_t droppedDelta = st.udpAssemblyDroppedCount - lastUdpAssemblyDroppedCount;
      const uint64_t malformedDelta = st.udpAssemblyMalformedCount - lastUdpAssemblyMalformedCount;
      const uint64_t reorderDelta = st.udpAssemblyReorderCount - lastUdpAssemblyReorderCount;
      const uint64_t keyReqDelta = st.udpAssemblyKeyReqCount - lastUdpAssemblyKeyReqCount;
      const uint64_t keyResyncDelta = st.udpAssemblyKeyResyncCount - lastUdpAssemblyKeyResyncCount;
      const uint64_t fecRecoveredDelta =
          st.udpAssemblyFecRecoveredCount - lastUdpAssemblyFecRecoveredCount;
      const uint64_t nackSentDelta = st.udpNackSentCount - lastUdpNackSentCount;
      const uint64_t nackChunkDelta = st.udpNackChunkCount - lastUdpNackChunkCount;
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
      st.udpAssemblyDropPmLast = static_cast<uint32_t>(std::min<uint64_t>(dropPermille, 1000ULL));
      std::cout << "[native-video-client] udp-assembly chunks=" << chunksDelta
                << " completed=" << completedDelta
                << " dropped=" << droppedDelta
                << " dropPm=" << dropPermille
                << " malformed=" << malformedDelta
                << " reorder=" << reorderDelta
                << " keyReq=" << keyReqDelta
                << " keyResync=" << keyResyncDelta
                << " fecRecovered=" << fecRecoveredDelta
                << " simDropPm=" << simDropPermille
                << " simDropTotal=" << simDroppedDelta
                << " nackOn=" << (nackEnabled ? 1 : 0)
                << " nackSent=" << nackSentDelta
                << " nackChunks=" << nackChunkDelta
                << " nackExhausted=" << nackScheduler.stats().roundsExhausted
                << " pending=" << assembler.PendingCount()
                << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0)
                << " catchup=" << (gate.catchupMode ? 1 : 0)
                << "\n";
      lastUdpChunkRecvCount = st.udpChunkRecvCount;
      lastUdpAssemblyCompletedCount = st.udpAssemblyCompletedCount;
      lastUdpAssemblyDroppedCount = st.udpAssemblyDroppedCount;
      lastUdpAssemblyMalformedCount = st.udpAssemblyMalformedCount;
      lastUdpAssemblyReorderCount = st.udpAssemblyReorderCount;
      lastUdpAssemblyKeyReqCount = st.udpAssemblyKeyReqCount;
      lastUdpAssemblyKeyResyncCount = st.udpAssemblyKeyResyncCount;
      lastUdpAssemblyFecRecoveredCount = st.udpAssemblyFecRecoveredCount;
      lastUdpNackSentCount = st.udpNackSentCount;
      lastUdpNackChunkCount = st.udpNackChunkCount;
      lastUdpSimDroppedCount = udpSimDroppedCount;
      lastUdpSimAcceptedCount = udpSimAcceptedCount;
      udpAssemblyStatAtUs += 1000000ULL;
    }
  }

  ctx.recvLive.Enter(RecvStage::Exited, qpc_now_us());
  ctx.session.running = false;
  if (ctx.session.hwnd) PostMessageW(ctx.session.hwnd, WM_CLOSE, 0, 0);
  return;
}

void VideoReceiver::run_tcp() {
  ctx.recvLive.Enter(RecvStage::Recv, qpc_now_us());
  while (ctx.session.running.load()) {
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
    {
      const uint64_t loopUs = qpc_now_us();
      ctx.recvLive.loopIterations.fetch_add(1, std::memory_order_relaxed);
      ctx.recvLive.lastLoopUs.store(loopUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Recv, loopUs);
    }
    // The TCP socket has no receive timeout, so recv_all would block for as long as the host
    // stays quiet; a bounded select in front of it keeps the checks above alive. (F-13.)
    if (!wait_readable(ctx.session.sock, 200)) continue;
    MessageHeader header{};
    if (!remote60::native_poc::recv_all(ctx.session.sock, &header, sizeof(header))) break;
    if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
    ctx.recvLive.lastDatagramUs.store(qpc_now_us(), std::memory_order_relaxed);
    const auto msgType = static_cast<MessageType>(header.type);

    if (msgType == MessageType::RawFrameBgra && header.size == sizeof(RawFrameHeader)) {
      RawFrameHeader h{};
      h.header = header;
      if (!remote60::native_poc::recv_all(ctx.session.sock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
      std::vector<uint8_t> payload(h.payloadSize);
      if (!remote60::native_poc::recv_all(ctx.session.sock, payload.data(), payload.size())) break;

      if (!dec.useRaw) {
        ++st.skippedQueued;
        continue;
      }

      const uint64_t nowUs = qpc_now_us();
      const uint64_t queueSetUs = nowUs;
      auto frameBgra = std::make_shared<std::vector<uint8_t>>(std::move(payload));
      if (!frameBgra || frameBgra->empty()) {
        ++st.skippedQueued;
        continue;
      }
      {
        std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
        const uint64_t prevVersion = ctx.frameBuf.frame.version;
        const uint64_t lastPresentedVersion = ctx.frameBuf.lastPresentedVersion.load(std::memory_order_relaxed);
        if (prevVersion > lastPresentedVersion) {
          ++ctx.frameBuf.overwriteBeforePresentCount;
        }
        ctx.frameBuf.frame.format = SharedFrame::PixelFormat::Bgra32;
        ctx.frameBuf.frame.width = h.width;
        ctx.frameBuf.frame.height = h.height;
        ctx.frameBuf.frame.codedWidth = h.width;
        ctx.frameBuf.frame.codedHeight = h.height;
        ctx.frameBuf.frame.visibleLeft = 0;
        ctx.frameBuf.frame.visibleTop = 0;
        ctx.frameBuf.frame.stride = h.stride;
        ctx.frameBuf.frame.seq = h.seq;
        ctx.frameBuf.frame.captureUs = h.captureQpcUs;
        ctx.frameBuf.frame.encodeStartUs = h.encodeStartQpcUs;
        ctx.frameBuf.frame.encodeEndUs = h.encodeEndQpcUs;
        ctx.frameBuf.frame.sendUs = h.sendQpcUs;
        ctx.frameBuf.frame.recvUs = nowUs;
        ctx.frameBuf.frame.decodeStartUs = nowUs;
        ctx.frameBuf.frame.decodeEndUs = nowUs;
        ctx.frameBuf.frame.queueSetUs = queueSetUs;
        ctx.frameBuf.frame.decodeToQueueUs = 0;
        ctx.frameBuf.frame.streamGeneration = h.streamGeneration;
        ctx.frameBuf.frame.key = false;  // raw BGRA has no keyframe concept; keeps present telemetry quiet.
        ctx.frameBuf.frame.version = prevVersion + 1;
        ctx.frameBuf.frame.bytes = std::move(frameBgra);
        ctx.frameBuf.frame.surfaceSample.Reset();
        ctx.frameBuf.frame.surfaceTexture.Reset();
        ctx.frameBuf.frame.surfaceSubresource = 0;
      }
      request_video_paint(ctx, ctx.session.hwnd);

      if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || ctx.present.traceRecvPrinted.load() < args.traceMax)) {
        const auto nowPrinted = ctx.present.traceRecvPrinted.fetch_add(1) + 1;
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
          log_client_line(ctx, oss.str());
        }
      }

      ++st.recvFrames;
      ++st.decodedFrames;
      st.recvBytes += h.payloadSize;
      st.decodedBytes += static_cast<uint64_t>(h.payloadSize);
      const uint64_t latencyUs = (nowUs >= h.captureQpcUs) ? (nowUs - h.captureQpcUs) : 0;
      const uint64_t decodeTailUs = (nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0;
      st.sumLatencyUs += latencyUs;
      st.sumDecodeTailUs += decodeTailUs;
      st.maxLatencyUs = std::max(st.maxLatencyUs, latencyUs);
      st.maxDecodeTailUs = std::max(st.maxDecodeTailUs, decodeTailUs);

      flush_stats_if_due(nowUs, h.width, h.height, false, 0, 0);
    } else if (msgType == MessageType::EncodedFrameH264 && header.size == sizeof(EncodedFrameHeader)) {
      EncodedFrameHeader h{};
      h.header = header;
      if (!remote60::native_poc::recv_all(ctx.session.sock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
      std::vector<uint8_t> payload(h.payloadSize);
      if (!remote60::native_poc::recv_all(ctx.session.sock, payload.data(), payload.size())) break;
      const uint64_t packetNowUs = qpc_now_us();
      if (!process_h264_frame(h, &payload, packetNowUs)) break;
    } else {
      const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
      if (bodySize > 0 && !remote60::native_poc::recv_discard(ctx.session.sock, bodySize)) break;
      ++st.skippedQueued;
    }

  }
  ctx.recvLive.Enter(RecvStage::Exited, qpc_now_us());
  ctx.session.running = false;
  if (ctx.session.hwnd) PostMessageW(ctx.session.hwnd, WM_CLOSE, 0, 0);
}

void VideoReceiver::Run() {
  dec.recvSelectionEpoch = ctx.sel.epoch.load(std::memory_order_acquire);
  st.statAtUs = qpc_now_us() + 1000000ULL;
  gate.frameIntervalUs = std::max<uint64_t>(
      1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, args.fpsHint)));
  st.lastPresentCounters = load_present_counters();
  if (dec.transport == VideoTransport::Udp) {
    run_udp();
    return;
  }
  run_tcp();
}

}  // namespace remote60::native_poc::viewer
