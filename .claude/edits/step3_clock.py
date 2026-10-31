import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'

edit(SRC + r'\viewer_frame_gate_state.hpp', [
("""  uint64_t waitingKeyDropCount = 0;
  uint64_t lagDropCount = 0;
  uint64_t lastPacketRecvUs = 0;
""",
"""  uint64_t waitingKeyDropCount = 0;
  uint64_t lagDropCount = 0;
  // Two arrival clocks (history #390 item 4). lastPacketRecvUs is the heartbeat: every completed
  // frame, host kicks and static refreshes included. lastRealPacketRecvUs is the real-content
  // clock. The idle re-anchor and the dense-arrival test judge a REAL frame by the gap since the
  // last REAL frame: a kick 150 ms after a sparse capture used to make the next real burst look
  // dense (gap < 250 ms -> no re-anchor) while its capture sat 350-570 ms past the last presented
  // real frame -> "decode backlog" -> Congested + IDR, with streamLag ~0 and presentBacklog 0.
  uint64_t lastPacketRecvUs = 0;
  uint64_t lastRealPacketRecvUs = 0;
"""),
])

edit(SRC + r'\viewer_frame_gate.hpp', [
("""  // Packet arrival bookkeeping: returns recvGapUs and resets the lag-trigger streak on sparse arrival.
  uint64_t note_packet(uint64_t packetNowUs);
""",
"""  // Packet arrival bookkeeping: returns the frame's recvGapUs and resets the lag-trigger streak on
  // sparse arrival. A real frame's gap is measured since the last REAL frame (kicks and static
  // refreshes do not shorten it); a synthetic frame's gap is since any frame.
  uint64_t note_packet(uint64_t packetNowUs, bool synthetic = false);
"""),
])

edit(SRC + r'\viewer_frame_gate.cpp', [
("""uint64_t FrameGate::note_packet(uint64_t packetNowUs) {
  const uint64_t recvGapUs =
      (gate.lastPacketRecvUs > 0 && packetNowUs >= gate.lastPacketRecvUs) ? (packetNowUs - gate.lastPacketRecvUs) : 0;
  gate.lastPacketRecvUs = packetNowUs;
  if (recvGapUs > 250000) {
    // Sparse arrival usually means source/capture stall, not decoder backlog.
    gate.lagTriggerStreak = 0;
  }
  return recvGapUs;
}
""",
"""uint64_t FrameGate::note_packet(uint64_t packetNowUs, bool synthetic) {
  const uint64_t anyGapUs =
      (gate.lastPacketRecvUs > 0 && packetNowUs >= gate.lastPacketRecvUs) ? (packetNowUs - gate.lastPacketRecvUs) : 0;
  const uint64_t realGapUs =
      (gate.lastRealPacketRecvUs > 0 && packetNowUs >= gate.lastRealPacketRecvUs)
          ? (packetNowUs - gate.lastRealPacketRecvUs)
          : 0;
  gate.lastPacketRecvUs = packetNowUs;
  if (!synthetic) gate.lastRealPacketRecvUs = packetNowUs;
  // A real frame is judged on the real-content clock: the seconds a still screen spent sending
  // kicks / refreshes are idle time for the decode pipeline, and the resume frame must re-anchor
  // the lag floor (admit) exactly as it does after a silent idle. A synthetic frame keeps reporting
  // the heartbeat gap; it never feeds the congestion trigger anyway.
  const uint64_t recvGapUs = synthetic ? anyGapUs : realGapUs;
  if (recvGapUs > 250000) {
    // Sparse arrival usually means source/capture stall, not decoder backlog.
    gate.lagTriggerStreak = 0;
  }
  return recvGapUs;
}
"""),
])

edit(SRC + r'\viewer_video_receiver_frame.cpp', [
("""    if (!payloadPtr) return true;
    ++st.recvFrames;
    st.recvBytes += h.payloadSize;
    const uint64_t recvGapUs = fg.note_packet(packetNowUs);
""",
"""    if (!payloadPtr) return true;
    ++st.recvFrames;
    st.recvBytes += h.payloadSize;
    // The gate keeps a real-content clock next to the heartbeat: a kick / static refresh must not
    // make the next real frame look like a dense continuation (history #390 item 4).
    const uint64_t recvGapUs =
        fg.note_packet(packetNowUs, (h.flags & kEncodedFrameFlagSynthetic) != 0);
"""),
])

edit(SRC + r'\viewer_frame_gate_test.cpp', [
("""    in.packetNowUs = t;
    in.recvGapUs = fg.note_packet(t);
""",
"""    in.packetNowUs = t;
    in.recvGapUs = fg.note_packet(t, synthetic);
"""),
("""int main() {
  test_recovery_timer_retries_while_congested_without_idr();
""",
"""// history #390 item 4 (the probe in .claude/freeze-diagnosis-20260905, expectation inverted): a still
// screen fed 2 s of synthetic frames (kicks / refreshes every 100 ms), then three real frames landed
// within 6 ms, before the renderer's next vsync moved the present anchor. The synthetic arrivals kept
// recvGap under the 250 ms idle threshold, so the resume frame never re-anchored the lag floor and
// its 2 s of idle read as a decode backlog -> Congested + reason-1 IDR. The real-content clock fixes
// that; a genuine dense backlog still trips.
void test_synthetic_gap_then_real_burst_no_false_congestion() {
  std::printf("[T1] synthetic idle then real burst: no false congestion; a real dense backlog still trips\\n");
  Rig r;
  FrameGateInputs in{};
  FrameGateLag lag{};
  const uint64_t start = 1000 * kMs;
  CHECK(r.feed(start, start, true, 0, false, &lag, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  // Still screen: synthetics every 100 ms are rendered, but the last REAL presented stamp stays `start`.
  for (int n = 1; n <= 20; ++n) {
    const uint64_t now = start + n * 100 * kMs;
    CHECK(r.feed(now, now, false, start, false, &lag, &in, true) == FrameGateVerdict::Decode);
    r.decoded(in);
  }
  // Fresh real frames after the idle, 2 ms apart, with only 6 ms before a 60 Hz renderer can paint.
  for (int n = 1; n <= 3; ++n) {
    const uint64_t now = start + 2000 * kMs + n * 2 * kMs;
    CHECK(r.feed(now, now, false, start, false, &lag, &in, false) == FrameGateVerdict::Decode);
    r.decoded(in);
    CHECK(lag.decodeQueueLagEstimateUs < kDecodeQueueLagDropUs);  // the resume frame re-anchored the floor
  }
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  CHECK(r.gate.lagTriggerStreak == 0);
  CHECK(r.sink.requests(1) == 0);
  CHECK(r.sink.resets == 0);

  // Not weakened: the same synthetic run, then a REAL backlog -- 25 real frames 16.7 ms apart while
  // the present anchor never moves -- still enters Congested once the lag passes 300 ms.
  Rig r2;
  CHECK(r2.feed(start, start, true, 0, false, &lag, &in) == FrameGateVerdict::Decode);
  r2.decoded(in);
  for (int n = 1; n <= 20; ++n) {
    const uint64_t now = start + n * 100 * kMs;
    CHECK(r2.feed(now, now, false, start, false, &lag, &in, true) == FrameGateVerdict::Decode);
    r2.decoded(in);
  }
  bool congested = false;
  for (int n = 1; n <= 25 && !congested; ++n) {
    const uint64_t now = start + 2000 * kMs + n * kFrame;
    const uint64_t presented = start + 2000 * kMs + kFrame;  // the first real resume frame was presented, then nothing
    const auto v = r2.feed(now, now, false, presented, false, &lag, &in, false);
    if (v == FrameGateVerdict::Decode) r2.decoded(in);
    congested = (r2.gate.congestionState == ClientCongestionState::Congested);
  }
  CHECK(congested);
  CHECK(r2.sink.requests(1) == 1);
}

int main() {
  test_synthetic_gap_then_real_burst_no_false_congestion();
  test_recovery_timer_retries_while_congested_without_idr();
"""),
])

edit(SRC + r'\viewer_udp_recovery_test.cpp', [
("""}  // namespace

int main() {
  std::cout.setf(std::ios::unitbuf);
""",
"""// S10: a still screen -- the host re-encodes its cached picture as synthetic frames every 150 ms
// (a trailing kick per input, the 1 Hz refresh) -- then real content resumes as a burst of three
// frames within a few ms, before the renderer has presented any of them. The burst must not be read
// as a decode backlog (Congested + IDR); the field showed exactly this with streamLag ~0.
void scenario_synthetic_idle_then_real_burst_no_false_congestion() {
  std::printf("[S10] synthetic idle (kicks) then real burst: no false congestion\\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint64_t transitionsBefore = rig.gate.congestionTransitionCount;
  // The renderer showed the last real frame; from here it does not advance the anchor (synthetic
  // frames never do in viewer_present.cpp, and the burst lands before the next vsync).
  sleep_ms(40);
  rig.pinPresentAnchor = true;
  for (int i = 0; i < 12; ++i) {
    (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    (void)host.SendFrame(false, true, qpc_now_us());
    sleep_ms(150);
  }
  const uint64_t burstStartUs = qpc_now_us();
  for (int i = 0; i < 3; ++i) {
    (void)host.SendFrame(false, false, burstStartUs + static_cast<uint64_t>(i) * kFrameIntervalUs);
  }
  sleep_ms(120);
  CHECK(rig.gate.congestionTransitionCount == transitionsBefore,
        "no congestion transition (state " + state_name(rig) + ", transitions +" +
            std::to_string(rig.gate.congestionTransitionCount - transitionsBefore) + ")");
  CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request");
  rig.pinPresentAnchor = false;
  pump(host, rig, 300);
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
}

}  // namespace

int main() {
  std::cout.setf(std::ios::unitbuf);
"""),
("""  scenario_congested_first_idr_lost_recovers_by_timer();
  MFShutdown();
""",
"""  scenario_congested_first_idr_lost_recovers_by_timer();
  scenario_synthetic_idle_then_real_burst_no_false_congestion();
  MFShutdown();
"""),
])
