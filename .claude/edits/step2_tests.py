import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'

edit(SRC + r'\viewer_frame_gate_test.cpp', [
("""    gate.lagTriggerStreakMin = kLagTriggerStreakMinDefault;                   // 3
    gate.frameIntervalUs = 16667;
""",
"""    gate.lagTriggerStreakMin = kLagTriggerStreakMinDefault;                   // 3
    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;                // 500 ms
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;          // 2 s
    gate.frameIntervalUs = 16667;
"""),
("""int main() {
  test_congestion_entry_tunables();
""",
"""// The recovery timer (history #390 item 2). Before it, the probe in .claude/freeze-diagnosis-20260905
// fed 60 s of P frames into a Congested gate and counted 0 re-requests: the Congested early return
// bypassed the reason-3 re-ask and the Recovering timeout, and nothing ran when no frame arrived.
void test_recovery_timer_retries_while_congested_without_idr() {
  std::printf("[T1] recovery timer: Congested + 60 s of P frames -> re-asks on the clock (0.5 s, x2 up to 2 s), never per frame\n");
  Rig r;
  FrameGateInputs in{};
  FrameGateLag lag{};
  const uint64_t start = 1000 * kMs;
  CHECK(r.feed(start, start, true, 0, false, &lag, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  // A real decoder failure enters Congested and asks for one replacement IDR (reason 4).
  r.fg.note_decode_failure(in, lag);
  CHECK(r.gate.congestionState == ClientCongestionState::Congested);
  const size_t before = r.sink.keyframeReasons.size();
  int dropped = 0;
  for (int n = 1; n <= 3600; ++n) {
    const uint64_t now = start + n * kFrame;
    if (r.feed(now, now, false, start, false) == FrameGateVerdict::DropCongested) ++dropped;
    r.fg.tick(now);  // what run_udp does after every datagram
  }
  CHECK(dropped == 3600);
  const int retries = r.sink.requests(7);
  // +0.5 s, +1.5 s, +3.5 s, then every 2 s: 3 + (60 - 3.5) / 2 = 31
  CHECK(retries >= 28 && retries <= 33);
  CHECK(r.sink.keyframeReasons.size() - before == static_cast<size_t>(retries));  // nothing per frame
  CHECK(r.sink.requests(3) == 0);  // the Congested drop never reached the reason-3 path, as before
  CHECK(r.gate.recoveryRetryCount == static_cast<uint64_t>(retries));
  CHECK(r.gate.recoveryRetryEpisodes == 1);
  CHECK(r.gate.recoveryRetryCurrentUs == kKeyRecoveryRetryMaxUsDefault);
  // The IDR lands: Recovering, the timer stops, the wait bookkeeping clears.
  const uint64_t tIdr = start + 3601 * kFrame;
  CHECK(r.feed(tIdr, tIdr, true, tIdr - 100 * kMs, false, &lag, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  r.fg.tick(tIdr);
  CHECK(r.gate.congestionState == ClientCongestionState::Recovering);
  CHECK(r.gate.keyWaitSinceUs == 0);
  CHECK(r.gate.keyWaitMaxUs >= 60 * 1000 * kMs);
  for (int n = 1; n <= 200; ++n) r.fg.tick(tIdr + n * 25 * kMs);
  CHECK(r.sink.requests(7) == retries);
}

void test_recovery_timer_without_frames_and_backoff_per_wait() {
  std::printf("[T1] recovery timer: no frames at all -> 0.5 / 1.5 / 3.5 s re-asks; stops on the IDR; a new wait starts over\n");
  Rig r;
  FrameGateInputs in{};
  const uint64_t start = 2000 * kMs;
  CHECK(r.feed(start, start, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  r.fg.tick(start);
  CHECK(r.gate.keyWaitSinceUs == 0);
  // A lost frame: the receiver sets the wait (and asks once itself, reason 2 -- not through the gate).
  r.gate.waitForKeyFrame = true;
  uint64_t t = start;
  for (; t <= start + 4000 * kMs; t += 25 * kMs) r.fg.tick(t);
  CHECK(r.sink.requests(7) == 3);
  CHECK(r.gate.recoveryRetryEpisodes == 1);
  // The IDR arrives and decodes: the wait ends, no further asks.
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  CHECK(!r.gate.waitForKeyFrame);
  r.fg.tick(t);
  CHECK(r.gate.keyWaitSinceUs == 0);
  for (uint64_t t2 = t; t2 <= t + 4000 * kMs; t2 += 25 * kMs) r.fg.tick(t2);
  CHECK(r.sink.requests(7) == 3);
  // A second wait backs off from 0.5 s again, not from where the last one left off.
  const uint64_t t3 = t + 5000 * kMs;
  r.gate.waitForKeyFrame = true;
  r.fg.tick(t3);
  r.fg.tick(t3 + 499 * kMs);
  CHECK(r.sink.requests(7) == 3);
  r.fg.tick(t3 + 500 * kMs);
  CHECK(r.sink.requests(7) == 4);
  CHECK(r.gate.recoveryRetryEpisodes == 2);
  // Off switch: interval 0 never asks.
  Rig off;
  off.gate.recoveryRetryIntervalUs = 0;
  off.gate.waitForKeyFrame = true;
  for (uint64_t t4 = start; t4 <= start + 5000 * kMs; t4 += 25 * kMs) off.fg.tick(t4);
  CHECK(off.sink.requests(7) == 0);
}

int main() {
  test_recovery_timer_retries_while_congested_without_idr();
  test_recovery_timer_without_frames_and_backoff_per_wait();
  test_congestion_entry_tunables();
"""),
])

edit(SRC + r'\viewer_udp_recovery_test.cpp', [
("""    gate.lagTriggerStreakMin = kLagTriggerStreakMinDefault;
    gate.waitForKeyFrame = true;  // init_decoder: an H.264 session starts waiting for its first IDR
""",
"""    gate.lagTriggerStreakMin = kLagTriggerStreakMinDefault;
    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;
    gate.waitForKeyFrame = true;  // init_decoder: an H.264 session starts waiting for its first IDR
"""),
("""}  // namespace

int main() {
  std::cout.setf(std::ios::unitbuf);
""",
"""// S7: the host consumed seqs it never sent (its sender queue was cleared by an IDR) and the next AU
// on the wire is that complete IDR. The gap is not loss and the IDR is the resync: no decoder reset
// + IDR request may follow, or every host-answered IDR breeds the next request (the 90/90 pattern).
void scenario_completed_idr_after_seq_gap_needs_no_request() {
  std::printf("[S7] complete IDR behind a seq gap: no extra IDR request\\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint64_t keyframesBefore = host.keyframes_sent();
  const uint32_t before = host.last_seq();
  for (int i = 0; i < 3; ++i) {
    pump(host, rig, 300);
    host.SkipSeq();
    (void)host.SendFrame(true, false, qpc_now_us());
  }
  pump(host, rig, 500);
  CHECK(host.keyframes_sent() >= keyframesBefore + 3, "three unsolicited IDRs went out");
  CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request (got " + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.maxPublishedSeq.load() > before + 60, "stream flowed (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
}

// S8: a whole P frame is lost, the host never answers the IDR request, and the source stops. The
// viewer must keep asking on the clock -- not per frame, there are none -- and resume once an IDR
// finally comes.
void scenario_keyframe_wait_retries_on_timer_when_source_stops() {
  std::printf("[S8] keyframe wait + source stop: timer-driven re-requests, then recovery\\n");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> lostP{0};
  LossPlan plan;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t, bool key, uint16_t) {
    if (!key && seq >= 40 && lostP.load() == 0) lostP = seq;
    return seq == lostP.load();  // the whole frame
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  host.SetHonorKeyframeRequests(false);
  CHECK(pump_until(host, rig, 3000, [&]() { return rig.gate.waitForKeyFrame; }), "the lost P frame opened a keyframe wait");
  // Let the first (loss-driven, reason 2) request be consumed, then stop the source.
  idle(host, rig, 50);
  const uint64_t requestsAtStop = host.keyframe_requests();
  const uint64_t stopUs = qpc_now_us();
  idle(host, rig, 2600);
  const uint64_t retries = host.keyframe_requests() - requestsAtStop;
  CHECK(retries >= 2 && retries <= 4, "timer re-asks while nothing arrives: " + std::to_string(retries));
  CHECK(rig.gate.waitForKeyFrame, "still waiting: nothing was sent");
  CHECK(rig.gate.recoveryRetryCount >= 2, "gate counted the retries (" + std::to_string(rig.gate.recoveryRetryCount) + ")");
  // The host is back: the next request is honoured and the stream resumes.
  host.SetHonorKeyframeRequests(true);
  const bool resumed = pump_until(host, rig, 5000, [&]() {
    return !rig.gate.waitForKeyFrame && rig.maxPublishedSeq.load() > lostP.load() + 5;
  });
  CHECK(resumed, "resumed after the IDR (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(host.keyframe_requests() <= requestsAtStop + 6, "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsAtStop) + " since the stop)");
  (void)stopUs;
}

// S9: a frozen present anchor makes dense frames read as a decode backlog -> Congested + IDR
// request; the first recovery IDR is lost entirely. The viewer must re-ask on the clock and come
// back through the next IDR, instead of dropping every P frame until the host's next spontaneous
// one (the probe's 60 s / 0 re-requests).
void scenario_congested_first_idr_lost_recovers_by_timer() {
  std::printf("[S9] Congested, first recovery IDR lost: timer re-request, recovery\\n");
  FakeHost host;
  ViewerRig rig;
  std::atomic<bool> armed{false};
  std::atomic<uint32_t> lostIdr{0};
  LossPlan plan;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t, bool key, uint16_t) {
    if (armed.load() && key && lostIdr.load() == 0) lostIdr = seq;
    return seq == lostIdr.load();  // the whole IDR
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  rig.pinPresentAnchor = true;  // the renderer stops advancing the anchor: lag climbs 16.7 ms per frame
  armed = true;
  CHECK(pump_until(host, rig, 3000, [&]() { return rig.gate.congestionState == ClientCongestionState::Congested; }),
        "entered Congested (state " + state_name(rig) + ")");
  CHECK(pump_until(host, rig, 3000, [&]() { return lostIdr.load() != 0 && host.keyframe_requests() >= requestsBefore + 2; }),
        "the lost IDR was followed by a second request on the clock (requests " + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  rig.pinPresentAnchor = false;
  CHECK(pump_until(host, rig, 5000, [&]() {
          return rig.gate.congestionState == ClientCongestionState::Normal && !rig.gate.waitForKeyFrame;
        }),
        "back to Normal (state " + state_name(rig) + ")");
  CHECK(host.keyframe_requests() <= requestsBefore + 6, "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.gate.recoveryRetryCount >= 1, "the gate's timer drove the re-ask");
}

}  // namespace

int main() {
  std::cout.setf(std::ios::unitbuf);
"""),
("""  scenario_nack_unanswered_falls_back_to_idr();
  MFShutdown();
""",
"""  scenario_nack_unanswered_falls_back_to_idr();
  scenario_completed_idr_after_seq_gap_needs_no_request();
  scenario_keyframe_wait_retries_on_timer_when_source_stops();
  scenario_congested_first_idr_lost_recovers_by_timer();
  MFShutdown();
"""),
])
