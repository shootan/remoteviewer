import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'
F = S + r'\viewer_udp_recovery_test.cpp'

# --- FakeHost: delayed NACK answers -----------------------------------------------------------
edit(F, [
("  bool answerNacks = true;            // false: NACKs are counted but never answered\n",
 "  bool answerNacks = true;            // false: NACKs are counted but never answered\n"
 "  uint32_t nackAnswerDelayMs = 0;     // answer NACKs this late (a slow link: S19)\n"),
("          if (!found) continue;\n          const uint16_t count = std::min<uint16_t>(nack.missingCount, kUdpVideoNackMaxMissing);\n          SendChunks(au, nack.missing, count);\n          retransmitChunks_ += count;\n          continue;",
 "          if (!found) continue;\n          const uint16_t count = std::min<uint16_t>(nack.missingCount, kUdpVideoNackMaxMissing);\n"
 "          if (plan_.nackAnswerDelayMs > 0) {\n"
 "            // A slow link: the answer leaves later, from a helper thread so the reader keeps reading.\n"
 "            std::vector<uint16_t> only(nack.missing, nack.missing + count);\n"
 "            const uint32_t delayMs = plan_.nackAnswerDelayMs;\n"
 "            std::thread([this, au, only, delayMs]() {\n"
 "              sleep_ms(delayMs);\n"
 "              if (stop_.load()) return;\n"
 "              SendChunks(au, only.data(), static_cast<uint16_t>(only.size()));\n"
 "            }).detach();\n"
 "            retransmitChunks_ += count;\n"
 "            continue;\n"
 "          }\n"
 "          SendChunks(au, nack.missing, count);\n          retransmitChunks_ += count;\n          continue;"),
])

# --- rig: give-up tunables + an injected control RTT ------------------------------------------
edit(F, [
("    VideoReceiver::NackOptions nack;\n    nack.enabled = ctx.session.hostSupportsNack;\n    nack.holdUs = holdUs;\n    receiver.emplace(ctx, args, dec, gate, qpc_now_us(), 0, 0, nack);",
 "    VideoReceiver::NackOptions nack;\n    nack.enabled = ctx.session.hostSupportsNack;\n    nack.holdUs = holdUs;\n"
 "    if (giveUpHardCapUs != 0) nack.giveUpHardCapUs = giveUpHardCapUs;\n"
 "    receiver.emplace(ctx, args, dec, gate, qpc_now_us(), 0, 0, nack);"),
("  std::atomic<uint64_t> publishedCount{0};\n  std::atomic<uint32_t> lastPublishedSeq{0};\n",
 "  // A04 give-up tunables for the scenarios (0 = product default), set before Connect().\n"
 "  uint64_t giveUpHardCapUs = 0;\n"
 "  // What the control thread would have measured: the give-up's reply allowance reads it.\n"
 "  void SetControlRtt(uint64_t rttUs) {\n"
 "    ctx.control.lastRttUs.store(rttUs, std::memory_order_relaxed);\n"
 "    ctx.control.lastRttAtUs.store(qpc_now_us(), std::memory_order_relaxed);\n"
 "  }\n"
 "  std::atomic<uint64_t> publishedCount{0};\n  std::atomic<uint32_t> lastPublishedSeq{0};\n"),
])

# --- scenarios S19..S23 -------------------------------------------------------------------------
SCEN = '''
// Shared shape of S19..S21: a static screen whose LAST AU lost its final chunk (a tail loss with no
// successor), under different NACK conditions. Returns the lost seq (0 = setup failure).
uint32_t arm_static_tail_loss(FakeHost& host, ViewerRig& rig, std::atomic<uint32_t>& armFrom,
                              std::atomic<uint32_t>& target, const char* label) {
  idle(host, rig, 200);
  armFrom = host.last_seq() + 1;
  for (int i = 0; i < 20 && target.load() == 0; ++i) {
    (void)host.SendFrame(false, false, qpc_now_us());
    sleep_ms(20);
  }
  const uint32_t t = target.load();
  CHECK(t != 0, std::string(label) + ": a multi-chunk P frame lost its last chunk");
  CHECK(host.last_seq() == t, std::string(label) + ": nothing was sent after it (last seq " + std::to_string(host.last_seq()) + ")");
  return t;
}

LossPlan static_tail_loss_plan(std::atomic<uint32_t>& armFrom, std::atomic<uint32_t>& target, uint16_t alsoDropIdx = 0xFFFF) {
  LossPlan plan;
  plan.stopAfterDrop = true;
  plan.dropFirstSend = [&armFrom, &target, alsoDropIdx](uint32_t seq, uint16_t idx, bool key, uint16_t count) {
    if (armFrom.load() == 0 || seq < armFrom.load() || key || count < 4) return false;
    if (target.load() == 0) target = seq;
    if (seq != target.load()) return false;
    return idx == count - 1 || idx == alsoDropIdx;
  };
  return plan;
}

// S19 (A04 rule, reply allowance from the RTT): the tail NACK is answered LATE (200 ms) on a link
// whose control RTT is 150 ms. The allowance is 2 x RTT = 300 ms, so the head waits for the late
// reply, completes, and no keyframe is asked for. With the 50 ms default the same reply would be
// too late -- which is what the RTT term is for.
void scenario_late_tail_reply_accepted_with_rtt_allowance() {
  std::printf("[S19] static tail loss, NACK answered 200 ms late, control RTT 150 ms: the reply is waited for, no IDR request''' + NL + '''");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> armFrom{0};
  std::atomic<uint32_t> target{0};
  LossPlan plan = static_tail_loss_plan(armFrom, target);
  plan.nackAnswerDelayMs = 200;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  rig.SetControlRtt(150000);
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint64_t publishedBefore = rig.publishedCount.load();
  const uint32_t t = arm_static_tail_loss(host, rig, armFrom, target, "S19");
  if (t == 0) return;
  const bool repaired = pump_until_idle(host, rig, 1200, [&]() { return rig.maxPublishedSeq.load() >= t; });
  std::printf("  S19: seq=%u nacks=%llu keyReq=+%llu published=%llu''' + NL + '''", t, static_cast<unsigned long long>(host.nacks_for(t)),
              static_cast<unsigned long long>(host.keyframe_requests() - requestsBefore),
              static_cast<unsigned long long>(rig.publishedCount.load() - publishedBefore));
  CHECK(repaired, "S19: the late retransmit completed the AU and it was shown (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(host.nacks_for(t) >= 1, "S19: a NACK went out");
  CHECK(host.keyframe_requests() == requestsBefore, "S19: no keyframe request (+" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(!rig.gate.waitForKeyFrame, "S19: not waiting for a keyframe");
}

// S20 (A04 rule (1)): the loss has a hole (index 1) AND a tail (the last chunk), unanswered. The
// hole rounds are spent by ~100 ms, but the tail phase is still to come at 120 ms: giving the AU
// up on the spent hole rounds would forfeit the tail's own rounds. It must wait for the tail
// phase to be spent too (6 NACKs), then fall back once.
void scenario_hole_spent_but_tail_phase_pending_is_not_given_up_early() {
  std::printf("[S20] hole + tail loss, unanswered: hole rounds spent early do not end it; the tail rounds run, then one fallback''' + NL + '''");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> armFrom{0};
  std::atomic<uint32_t> target{0};
  LossPlan plan = static_tail_loss_plan(armFrom, target, 1);
  plan.answerNacks = false;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint32_t t = arm_static_tail_loss(host, rig, armFrom, target, "S20");
  if (t == 0) return;
  idle(host, rig, 150);  // hole rounds (25/50/75 ms) are spent; the tail phase has just begun
  CHECK(host.keyframe_requests() == requestsBefore, "S20: not given up while the tail phase is pending (+" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(!rig.gate.waitForKeyFrame, "S20: no keyframe wait yet");
  idle(host, rig, 750);
  std::printf("  S20: seq=%u nacks=%llu keyReq=+%llu waitForKey=%d''' + NL + '''", t, static_cast<unsigned long long>(host.nacks_for(t)),
              static_cast<unsigned long long>(host.keyframe_requests() - requestsBefore), rig.gate.waitForKeyFrame ? 1 : 0);
  CHECK(host.nacks_for(t) >= 4 && host.nacks_for(t) <= 6, "S20: hole rounds and then tail rounds (" + std::to_string(host.nacks_for(t)) + ")");
  CHECK(host.keyframe_requests() >= requestsBefore + 1 && host.keyframe_requests() <= requestsBefore + 2, "S20: one fallback after the tail rounds (+" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.gate.waitForKeyFrame, "S20: waiting for the keyframe");
}

// S21 (A04 rule without NACK): an old host that declined NACK, a static tail loss. There is no
// scheduler to wait for: no progress for one allowance and terminalMin of age bound the wait,
// then the keyframe path recovers. (Legacy immediate delivery: the hold is off.)
void scenario_nack_off_static_tail_loss_falls_back() {
  std::printf("[S21] NACK not negotiated, static tail loss: no-progress + terminalMin fallback, then recovery''' + NL + '''");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> armFrom{0};
  std::atomic<uint32_t> target{0};
  LossPlan plan = static_tail_loss_plan(armFrom, target);
  plan.advertiseNack = false;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  CHECK(!rig.hostSupportsNack(), "S21: NACK not negotiated");
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint32_t t = arm_static_tail_loss(host, rig, armFrom, target, "S21");
  if (t == 0) return;
  idle(host, rig, 900);
  std::printf("  S21: seq=%u nacks=%llu keyReq=+%llu waitForKey=%d''' + NL + '''", t, static_cast<unsigned long long>(host.nacks_received()),
              static_cast<unsigned long long>(host.keyframe_requests() - requestsBefore), rig.gate.waitForKeyFrame ? 1 : 0);
  CHECK(host.nacks_received() == 0, "S21: no NACK against an old host");
  CHECK(host.keyframe_requests() >= requestsBefore + 1 && host.keyframe_requests() <= requestsBefore + 2, "S21: bounded fallback (+" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.gate.waitForKeyFrame, "S21: waiting for the keyframe");
  const uint64_t publishedBefore = rig.publishedCount.load();
  host.ResumeAfterDrop();
  const KeyOnWire idr = send_key_and_wait_on_wire(host, "S21");
  CHECK(idr.ok, "S21: the recovery IDR went out");
  const bool resumed = pump_until(host, rig, 3000, [&]() {
    return !rig.gate.waitForKeyFrame && rig.publishedCount.load() > publishedBefore && rig.published_key(idr.keySeq);
  });
  CHECK(resumed, "S21: resumed on the IDR seq " + std::to_string(idr.keySeq));
}

// S22 (A04 rule (3)/(5)): a big IDR whose chunks keep arriving slowly -- 40 chunks paced 30 ms
// apart, 1.2 s in all, NACKs unanswered. Every chunk is progress, so the head is preserved past
// terminalMin and the spent tail rounds, and completes; the hard cap (5 s) is never reached.
void scenario_slow_progressing_large_au_is_preserved() {
  std::printf("[S22] large IDR paced 30 ms per chunk (1.2 s), NACK unanswered: progress keeps it, no give-up, it completes''' + NL + '''");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  plan.answerNacks = false;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  idle(host, rig, 200);
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint64_t publishedBefore = rig.publishedCount.load();
  host.SetKeyInterChunkDelayUs(30000);
  const KeyOnWire idr = send_key_and_wait_on_wire(host, "S22", 30, 3000);
  CHECK(idr.ok, "S22: the paced IDR went out");
  host.SetKeyInterChunkDelayUs(0);
  const bool shown = pump_until_idle(host, rig, 3000, [&]() { return rig.published_key(idr.keySeq); });
  std::printf("  S22: idr=%u nacks=%llu keyReq=+%llu published=%llu''' + NL + '''", idr.keySeq, static_cast<unsigned long long>(host.nacks_for(idr.keySeq)),
              static_cast<unsigned long long>(host.keyframe_requests() - requestsBefore),
              static_cast<unsigned long long>(rig.publishedCount.load() - publishedBefore));
  CHECK(shown, "S22: the slow IDR completed and was shown (key seqs " + rig.published_key_seqs() + ")");
  CHECK(host.keyframe_requests() == requestsBefore, "S22: no keyframe request while it progressed (+" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(!rig.gate.waitForKeyFrame, "S22: not waiting");
}

// S23 (A04 rule (5)): the same slow IDR against a hard cap of 1 s (tunable, here lowered for the
// test): progress or not, at the cap the AU is given up ONCE -- one keyframe request episode, the
// existing backoff -- and the next IDR resumes the stream.
void scenario_hard_cap_gives_up_once_despite_progress() {
  std::printf("[S23] slow IDR (2 s of chunks) against a 1 s hard cap: one give-up episode, then recovery''' + NL + '''");
  FakeHost host;
  ViewerRig rig;
  rig.giveUpHardCapUs = 1000000;
  LossPlan plan;
  plan.answerNacks = false;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  idle(host, rig, 200);
  const uint64_t requestsBefore = host.keyframe_requests();
  host.SetKeyInterChunkDelayUs(50000);  // ~40 chunks -> ~2 s on the wire
  const KeyOnWire idr = send_key_and_wait_on_wire(host, "S23", 30, 4000);
  CHECK(idr.ok, "S23: the paced IDR went out");
  host.SetKeyInterChunkDelayUs(0);
  idle(host, rig, 400);
  std::printf("  S23: idr=%u keyReq=+%llu waitForKey=%d''' + NL + '''", idr.keySeq,
              static_cast<unsigned long long>(host.keyframe_requests() - requestsBefore), rig.gate.waitForKeyFrame ? 1 : 0);
  CHECK(host.keyframe_requests() >= requestsBefore + 1 && host.keyframe_requests() <= requestsBefore + 2, "S23: one give-up episode at the hard cap (+" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.gate.waitForKeyFrame, "S23: waiting for a keyframe after the cap");
  const uint64_t publishedBefore = rig.publishedCount.load();
  const KeyOnWire fresh = send_key_and_wait_on_wire(host, "S23 recovery");
  CHECK(fresh.ok, "S23: the recovery IDR went out");
  const bool resumed = pump_until(host, rig, 3000, [&]() {
    return !rig.gate.waitForKeyFrame && rig.publishedCount.load() > publishedBefore && rig.published_key(fresh.keySeq);
  });
  CHECK(resumed, "S23: resumed on the fresh IDR seq " + std::to_string(fresh.keySeq));
  CHECK(host.keyframe_requests() <= requestsBefore + 3, "S23: no request storm (+" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
}
'''

HELPERS = '''
// Wait for `pred` or timeout without sending frames (the screen is static); requests are drained.
bool pump_until_idle(FakeHost& host, ViewerRig& rig, uint32_t timeoutMs, const std::function<bool()>& pred) {
  const uint64_t endUs = qpc_now_us() + static_cast<uint64_t>(timeoutMs) * 1000ULL;
  while (qpc_now_us() < endUs) {
    if (pred()) return true;
    (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    sleep_ms(2);
  }
  return pred();
}
'''

edit(F, [
("  void SetHonorKeyframeRequests(bool honor) { plan_.honorKeyframeRequests = honor; }",
 "  void SetHonorKeyframeRequests(bool honor) { plan_.honorKeyframeRequests = honor; }\n  void SetKeyInterChunkDelayUs(uint32_t us) { plan_.keyInterChunkDelayUs = us; }"),
("// Idle wait (no frames) that also hands the viewer an ignorable datagram every `everyMs`: the",
 HELPERS.lstrip('\n') + "\n// Idle wait (no frames) that also hands the viewer an ignorable datagram every `everyMs`: the"),
("}  // namespace\n\nint main() {", SCEN + "\n}  // namespace\n\nint main() {"),
("  scenario_static_last_idr_tail_loss_unanswered_recovers();\n",
 "  scenario_static_last_idr_tail_loss_unanswered_recovers();\n  scenario_late_tail_reply_accepted_with_rtt_allowance();\n  scenario_hole_spent_but_tail_phase_pending_is_not_given_up_early();\n  scenario_nack_off_static_tail_loss_falls_back();\n  scenario_slow_progressing_large_au_is_preserved();\n  scenario_hard_cap_gives_up_once_despite_progress();\n"),
])

# --- shared_core unit test: progress semantics and give-up identity ---------------------------
UNIT = '''
// A04 give-up support in the assembler: lastProgressUs moves only on a NEW data chunk (a duplicate
// is not progress), and GiveUpIncomplete removes exactly the judged (generation, seq) -- not an
// "oldest" by arrival or by sequence -- and cancels itself if that AU completed meanwhile.
bool test_udp_assembler_progress_and_give_up() {
  UdpH264FrameAssembler a;
  a.ConfigureInOrderHold(120000, 8);
  UdpH264FrameAssembler::IncompleteAuInfo info{};
  const uint64_t t0 = 9000000;
  (void)push_chunk(a, 10, 0, 12, 4, false, t0);
  if (!expect(a.OldestIncomplete(nullptr, 0, &info) && info.seq == 10 && info.lastProgressUs == t0,
              "progress: the first chunk stamps lastProgressUs")) return false;
  (void)push_chunk(a, 10, 0, 12, 4, false, t0 + 10000);  // duplicate
  if (!expect(a.OldestIncomplete(nullptr, 0, &info) && info.lastProgressUs == t0,
              "progress: a duplicate chunk is not progress")) return false;
  (void)push_chunk(a, 10, 2, 12, 4, false, t0 + 20000);  // a new chunk
  if (!expect(a.OldestIncomplete(nullptr, 0, &info) && info.lastProgressUs == t0 + 20000,
              "progress: a new data chunk is progress")) return false;
  // A second incomplete AU behind it; give up 11 by identity: 10 stays, 11 goes.
  (void)push_chunk(a, 11, 0, 12, 4, false, t0 + 30000);
  if (!expect(a.PendingCount() == 2 && !a.AnyComplete(), "give-up: two incomplete AUs, none complete")) return false;
  if (!expect(!a.GiveUpIncomplete(1, 12), "give-up: an AU that is not held -> false")) return false;
  if (!expect(a.GiveUpIncomplete(1, 11) && a.PendingCount() == 1, "give-up: exactly seq 11 removed")) return false;
  if (!expect(a.OldestIncomplete(nullptr, 0, &info) && info.seq == 10, "give-up: seq 10 untouched")) return false;
  // 10 completes: a give-up judged earlier is cancelled (false, nothing removed) and it delivers.
  (void)push_chunk(a, 10, 1, 12, 4, false, t0 + 40000);
  if (!expect(a.AnyComplete(), "give-up: seq 10 completed")) return false;
  if (!expect(!a.GiveUpIncomplete(1, 10) && a.PendingCount() == 1, "give-up: a completed AU is not given up")) return false;
  UdpH264AssemblyStepResult out{};
  if (!expect(a.PopDelivery(t0 + 40000, true, &out) && out.frame.header.seq == 10 && out.frame.payload.size() == 12,
              "give-up: seq 10 delivered intact")) return false;
  return true;
}
'''
T = S + r'\native_video_client_shared_core_test.cpp'
edit(T, [
("bool test_udp_assembler_in_order_hold() {", UNIT.lstrip('\n') + "\nbool test_udp_assembler_in_order_hold() {"),
("  if (!test_udp_assembler_in_order_hold()) return 1;\n",
 "  if (!test_udp_assembler_in_order_hold()) return 1;\n  if (!test_udp_assembler_progress_and_give_up()) return 1;\n"),
])
print('N3 rule tests ok')
