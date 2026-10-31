import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'

MAINT = '''    return true;
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
    if (holdEnabled && !gate.waitForKeyFrame && !assembler.AnyComplete()) {
      UdpH264FrameAssembler::IncompleteAuInfo info{};
      if (assembler.OldestIncomplete(nullptr, 0, &info)) {
        const bool chased = nackScheduler.current_seq() == info.seq &&
                            nackScheduler.current_generation() == info.generation;
        const bool roundsSpent = chased && nackScheduler.exhausted() &&
                                 nowUs >= nackScheduler.last_sent_us() + nackScheduler.config().roundUs;
        const bool holdPast = info.firstPacketUs != 0 && nowUs >= info.firstPacketUs + nack.holdUs &&
                              !nackScheduler.busy();
        if (roundsSpent || holdPast) {
          (void)assembler.DropIncomplete(info.seq, info.generation);
          nackScheduler.Reset();
          ++assemblyDropped;
          ++st.udpAssemblyDroppedCount;
          ++stuckHeadGiveUps;
          if (stuckHeadGiveUps <= 5 || (stuckHeadGiveUps % 100) == 1) {
            std::cout << "[native-video-client] stuck head given up seq=" << info.seq
                      << " gen=" << info.generation << " missing=" << info.missingTotal
                      << " of=" << info.chunkCount
                      << " reason=" << (roundsSpent ? "nack-exhausted" : "hold-expired")
                      << " ageUs=" << (info.firstPacketUs != 0 && nowUs >= info.firstPacketUs ? nowUs - info.firstPacketUs : 0)
                      << " total=" << stuckHeadGiveUps << "''' + NL + '''";
          }
          handle_udp_discontinuity();
        }
      }
    }
    fg.tick(nowUs, nackScheduler.busy());  // the keyframe recovery clock runs with or without frames
    return true;
  };

  ctx.recvLive.Enter(RecvStage::Recv, qpc_now_us());
  while (ctx.session.running.load()) {'''

edit(S + r'\viewer_video_receiver.cpp', [
("    return true;\n  };\n\n  ctx.recvLive.Enter(RecvStage::Recv, qpc_now_us());\n  while (ctx.session.running.load()) {", MAINT),
("""      ctx.recvLive.lastLoopUs.store(loopUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Recv, loopUs);
    }
    const int n = recv(""",
 """      ctx.recvLive.lastLoopUs.store(loopUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Recv, loopUs);
    }
    // Every path through the loop passes here first (see maintenance above).
    if (!maintenance(qpc_now_us(), false)) break;
    const int n = recv("""),
("""      if (remote60::native_poc::last_socket_error_is_retryable()) {
        if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
        if (!drain_deliveries()) break;
        maybe_send_nack(qpc_now_us());
        fg.tick(qpc_now_us(), nackScheduler.busy());  // the keyframe recovery clock runs with or without frames
        continue;
      }""",
 """      if (remote60::native_poc::last_socket_error_is_retryable()) {
        if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
        if (!maintenance(qpc_now_us(), true)) break;  // the clock work runs with or without frames
        continue;
      }"""),
("""    // In-order hold: Queued (and anything an expired hold releases) goes out here, in sequence.
    if (!drain_deliveries()) break;
    // On a busy link, also nudge the NACK for any still-stuck earlier AU (round-gated inside).
    maybe_send_nack(qpc_now_us());
    fg.tick(qpc_now_us(), nackScheduler.busy());
""",
 """    // In-order hold: Queued (and anything an expired hold releases) goes out here, in sequence, so
    // a completed AU is not held back by the maintenance cadence. The NACK poll and the recovery
    // timer follow within kMaintenanceMinIntervalUs at the top of the next iteration.
    if (!drain_deliveries()) break;
"""),
])
print('V14+A04 receiver edits ok')
