import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'

edit(SRC + r'\viewer_constants.hpp', [
("""constexpr uint64_t kCongestionRecoverMinUsDefault = 250000;  // 250ms
constexpr uint64_t kCongestionRecoveryTimeoutUsDefault = 1500000;  // 1.5s
""",
"""constexpr uint64_t kCongestionRecoverMinUsDefault = 250000;  // 250ms
constexpr uint64_t kCongestionRecoveryTimeoutUsDefault = 1500000;  // 1.5s
// Time-based keyframe recovery (Windows recovery state machine, history #390 item 2): while the
// gate waits for an IDR (keyframe wait or Congested) it re-asks on the clock, first after this
// much, doubling per retry up to the max -- independent of frames arriving, so a lost recovery IDR
// or a stalled source cannot leave the picture frozen until the host's next spontaneous IDR. The
// KeyframeRequestState limiter still bounds the wire rate.
constexpr uint64_t kKeyRecoveryRetryUsDefault = 500000;      // 500ms
constexpr uint64_t kKeyRecoveryRetryMaxUsDefault = 2000000;  // 2s
"""),
])

edit(SRC + r'\viewer_frame_gate_state.hpp', [
("""  uint64_t recoveringSinceUs = 0;
  uint32_t recoveringHealthyStreak = 0;
  uint64_t lastRecoveryRequestUs = 0;
};
""",
"""  uint64_t recoveringSinceUs = 0;
  uint32_t recoveringHealthyStreak = 0;
  uint64_t lastRecoveryRequestUs = 0;
  // Time-based keyframe recovery (FrameGate::tick). The frame-driven re-asks (reason 3 every 30
  // dropped frames, the Recovering timeout) never ran while Congested and never ran at all when no
  // frame arrived, so a lost recovery IDR or a stalled source left the gate waiting for an IDR
  // nobody would send (history #390 item 2: 60 s of P frames, 0 re-requests). tick() re-asks on
  // the clock instead: recoveryRetryIntervalUs after the wait began, then doubling up to
  // recoveryRetryMaxIntervalUs, reset when an IDR decodes. 0 = off (config, REMOTE60_NATIVE_KEY_RECOVERY_RETRY_US).
  uint64_t recoveryRetryIntervalUs = 0;
  uint64_t recoveryRetryMaxIntervalUs = 0;
  uint64_t keyWaitSinceUs = 0;          // when the current wait began (0 = not waiting)
  uint64_t recoveryNextRetryUs = 0;     // the next scheduled re-ask
  uint64_t recoveryRetryCurrentUs = 0;  // the current backoff step
  uint64_t recoveryRetryCount = 0;      // telemetry: timer-driven requests (reason 7)
  uint64_t recoveryRetryEpisodes = 0;   // telemetry: waits that needed at least one retry
  uint64_t keyWaitMaxUs = 0;            // telemetry: the longest wait for an IDR so far
};
"""),
])

edit(SRC + r'\viewer_frame_gate.hpp', [
("""  // a frame came out: the empty-output streak ends.
  void clear_empty_streak();
""",
"""  // a frame came out: the empty-output streak ends.
  void clear_empty_streak();
  // The clock: called by the receive loop after every datagram AND on every receive timeout. While
  // the gate waits for an IDR (waitForKeyFrame or Congested) it re-asks on a backoff schedule
  // (reason 7, recovery_timer) so recovery does not depend on frames arriving; otherwise it clears
  // the wait bookkeeping. Cheap when idle.
  void tick(uint64_t nowUs);
"""),
])

edit(SRC + r'\viewer_frame_gate.cpp', [
("""void FrameGate::clear_empty_streak() {
  gate.decodeEmptyStreak = 0;
  gate.decodeEmptyStreakStartUs = 0;
}
""",
"""void FrameGate::clear_empty_streak() {
  gate.decodeEmptyStreak = 0;
  gate.decodeEmptyStreakStartUs = 0;
}

void FrameGate::tick(uint64_t nowUs) {
  const bool waiting =
      gate.waitForKeyFrame || gate.congestionState == ClientCongestionState::Congested;
  if (!waiting) {
    if (gate.keyWaitSinceUs != 0) {
      const uint64_t waitedUs = (nowUs >= gate.keyWaitSinceUs) ? (nowUs - gate.keyWaitSinceUs) : 0;
      if (waitedUs > gate.keyWaitMaxUs) gate.keyWaitMaxUs = waitedUs;
    }
    gate.keyWaitSinceUs = 0;
    gate.recoveryNextRetryUs = 0;
    gate.recoveryRetryCurrentUs = 0;
    return;
  }
  if (gate.recoveryRetryIntervalUs == 0) return;
  if (gate.keyWaitSinceUs == 0) {
    // The wait just began. Whoever opened it (a loss, a congestion entry, a decode failure)
    // asked once already; the first re-ask comes one interval later.
    gate.keyWaitSinceUs = nowUs;
    gate.recoveryRetryCurrentUs = gate.recoveryRetryIntervalUs;
    gate.recoveryNextRetryUs = nowUs + gate.recoveryRetryCurrentUs;
    return;
  }
  if (nowUs < gate.recoveryNextRetryUs) return;
  const bool firstRetry = (gate.recoveryRetryCurrentUs == gate.recoveryRetryIntervalUs);
  sink.request_keyframe(7);  // recovery_timer
  ++gate.congestionRecoveryRequestCount;
  ++gate.recoveryRetryCount;
  if (firstRetry) ++gate.recoveryRetryEpisodes;
  const uint64_t waitedUs = (nowUs >= gate.keyWaitSinceUs) ? (nowUs - gate.keyWaitSinceUs) : 0;
  const uint64_t maxUs = std::max<uint64_t>(gate.recoveryRetryIntervalUs, gate.recoveryRetryMaxIntervalUs);
  gate.recoveryRetryCurrentUs = std::min<uint64_t>(gate.recoveryRetryCurrentUs * 2, maxUs);
  gate.recoveryNextRetryUs = nowUs + gate.recoveryRetryCurrentUs;
  std::cout << "[native-video-client] keyframe recovery retry count=" << gate.recoveryRetryCount
            << " waitedUs=" << waitedUs
            << " state=" << congestion_state_name(gate.congestionState)
            << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0)
            << " nextRetryUs=" << gate.recoveryRetryCurrentUs << "\\n";
}
"""),
("""void FrameGate::append_congestion_fields(std::ostream& os) {
    const uint64_t recoveryAvgUs =
        (gate.congestionRecoveryCount > 0) ? (gate.congestionRecoveryTotalUs / gate.congestionRecoveryCount) : 0;
    os << " congestionState=" << congestion_state_name(gate.congestionState)
""",
"""void FrameGate::append_congestion_fields(std::ostream& os) {
    const uint64_t recoveryAvgUs =
        (gate.congestionRecoveryCount > 0) ? (gate.congestionRecoveryTotalUs / gate.congestionRecoveryCount) : 0;
    os << " congestionState=" << congestion_state_name(gate.congestionState)
       << " keyRetries=" << gate.recoveryRetryCount
       << " keyRetryEpisodes=" << gate.recoveryRetryEpisodes
       << " keyWaitMaxUs=" << gate.keyWaitMaxUs
"""),
])

edit(SRC + r'\viewer_startup.cpp', [
("""  ctx.gate.lagTriggerStreakMin = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_TRIGGER_STREAK", kLagTriggerStreakMinDefault, 1, 60);
""",
"""  ctx.gate.lagTriggerStreakMin = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_TRIGGER_STREAK", kLagTriggerStreakMinDefault, 1, 60);
  // Time-based keyframe recovery (FrameGate::tick): 0 turns the timer off.
  ctx.gate.recoveryRetryIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEY_RECOVERY_RETRY_US",
      static_cast<uint32_t>(kKeyRecoveryRetryUsDefault), 0, 10000000);
  ctx.gate.recoveryRetryMaxIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEY_RECOVERY_RETRY_MAX_US",
      static_cast<uint32_t>(kKeyRecoveryRetryMaxUsDefault), 100000, 30000000);
"""),
("""            << " congestionRecoverMinUs=" << ctx.gate.congestionRecoverMinUs
            << " congestionRecoveryTimeoutUs=" << ctx.gate.congestionRecoveryTimeoutUs
            << "\\n";
""",
"""            << " congestionRecoverMinUs=" << ctx.gate.congestionRecoverMinUs
            << " congestionRecoveryTimeoutUs=" << ctx.gate.congestionRecoveryTimeoutUs
            << " keyRecoveryRetryUs=" << ctx.gate.recoveryRetryIntervalUs
            << " keyRecoveryRetryMaxUs=" << ctx.gate.recoveryRetryMaxIntervalUs
            << "\\n";
"""),
])

edit(SRC + r'\viewer_recv_stats.hpp', [
("""  uint64_t udpAssemblyKeyReqCount = 0;
""",
"""  uint64_t udpAssemblyKeyReqCount = 0;
  uint64_t udpAssemblyKeyResyncCount = 0;  // seq gaps closed by the complete IDR that revealed them (no request)
"""),
])

edit(SRC + r'\viewer_video_receiver.cpp', [
("""  // The delivered AU's seq is not the previous one + 1: whatever sat between them is gone.
  auto note_sequence_gap = [&](const UdpH264AssemblyStepResult& r) {
    (void)r;
    ++assemblyDropped;
    ++st.udpAssemblyDroppedCount;
    handle_udp_discontinuity();
  };
""",
"""  // The delivered AU's seq is not the previous one + 1: whatever sat between them is gone.
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
"""),
("""        if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
        if (!drain_deliveries()) break;
        maybe_send_nack(qpc_now_us());
        continue;
""",
"""        if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
        if (!drain_deliveries()) break;
        maybe_send_nack(qpc_now_us());
        fg.tick(qpc_now_us());  // the keyframe recovery clock runs with or without frames
        continue;
"""),
("""    // In-order hold: Queued (and anything an expired hold releases) goes out here, in sequence.
    if (!drain_deliveries()) break;
    // On a busy link, also nudge the NACK for any still-stuck earlier AU (round-gated inside).
    maybe_send_nack(qpc_now_us());
""",
"""    // In-order hold: Queued (and anything an expired hold releases) goes out here, in sequence.
    if (!drain_deliveries()) break;
    // On a busy link, also nudge the NACK for any still-stuck earlier AU (round-gated inside).
    maybe_send_nack(qpc_now_us());
    fg.tick(qpc_now_us());
"""),
("""  uint64_t lastUdpAssemblyKeyReqCount = 0;
  uint64_t lastUdpAssemblyFecRecoveredCount = 0;
""",
"""  uint64_t lastUdpAssemblyKeyReqCount = 0;
  uint64_t lastUdpAssemblyKeyResyncCount = 0;
  uint64_t lastUdpAssemblyFecRecoveredCount = 0;
"""),
("""      const uint64_t keyReqDelta = st.udpAssemblyKeyReqCount - lastUdpAssemblyKeyReqCount;
""",
"""      const uint64_t keyReqDelta = st.udpAssemblyKeyReqCount - lastUdpAssemblyKeyReqCount;
      const uint64_t keyResyncDelta = st.udpAssemblyKeyResyncCount - lastUdpAssemblyKeyResyncCount;
"""),
("""                << " keyReq=" << keyReqDelta
                << " fecRecovered=" << fecRecoveredDelta
""",
"""                << " keyReq=" << keyReqDelta
                << " keyResync=" << keyResyncDelta
                << " fecRecovered=" << fecRecoveredDelta
"""),
("""      lastUdpAssemblyKeyReqCount = st.udpAssemblyKeyReqCount;
      lastUdpAssemblyFecRecoveredCount = st.udpAssemblyFecRecoveredCount;
""",
"""      lastUdpAssemblyKeyReqCount = st.udpAssemblyKeyReqCount;
      lastUdpAssemblyKeyResyncCount = st.udpAssemblyKeyResyncCount;
      lastUdpAssemblyFecRecoveredCount = st.udpAssemblyFecRecoveredCount;
"""),
])
