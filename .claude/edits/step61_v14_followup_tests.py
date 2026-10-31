import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'
F = S + r'\viewer_udp_recovery_test.cpp'

# --- receiver: let a test read the give-up counter ----------------------------------------------
edit(S + r'\viewer_video_receiver.hpp', [
("  void run_udp();",
 "  void run_udp();\n"
 "  // Read-only view of the receive counters (the A04 give-up count among them) for tests.\n"
 "  const RecvStats& stats() const { return st; }"),
])

# --- fake host: replay specific chunks on demand (a late retransmit nobody asked for) ------------
edit(F, [
("  void SetKeyInterChunkDelayUs(uint32_t us) { plan_.keyInterChunkDelayUs = us; }",
 "  void SetKeyInterChunkDelayUs(uint32_t us) { plan_.keyInterChunkDelayUs = us; }\n"
 "  // Replays chunks of an AU the host still has cached -- a retransmit that arrives after the\n"
 "  // viewer has already given that AU up (S24). Returns false when the AU is no longer cached.\n"
 "  bool ResendChunks(uint32_t seq, const std::vector<uint16_t>& idx) {\n"
 "    CachedAu au;\n"
 "    {\n"
 "      std::lock_guard<std::mutex> lk(cacheMu_);\n"
 "      auto it = std::find_if(cache_.rbegin(), cache_.rend(), [&](const CachedAu& c) { return c.seq == seq; });\n"
 "      if (it == cache_.rend()) return false;\n"
 "      au = *it;\n"
 "    }\n"
 "    SendChunks(au, idx.data(), static_cast<uint16_t>(idx.size()));\n"
 "    return true;\n"
 "  }"),
])

# --- give-up count assertions + the late-chunk scenario -----------------------------------------
edit(F, [
("  const uint64_t requestsBefore = host.keyframe_requests();\n  const uint32_t t = arm_static_tail_loss(host, rig, armFrom, target, \"S21\");",
 "  const uint64_t requestsBefore = host.keyframe_requests();\n  const uint64_t giveUpsBefore = rig.give_ups();\n  const uint32_t t = arm_static_tail_loss(host, rig, armFrom, target, \"S21\");"),
("  CHECK(host.keyframe_requests() >= requestsBefore + 1 && host.keyframe_requests() <= requestsBefore + 2, \"S21: bounded fallback (+\" + std::to_string(host.keyframe_requests() - requestsBefore) + \")\");",
 "  CHECK(host.keyframe_requests() >= requestsBefore + 1 && host.keyframe_requests() <= requestsBefore + 2, \"S21: bounded fallback (+\" + std::to_string(host.keyframe_requests() - requestsBefore) + \")\");\n"
 "  CHECK(rig.give_ups() - giveUpsBefore == 1, \"S21: the head was given up exactly once (\" + std::to_string(rig.give_ups() - giveUpsBefore) + \")\");"),
])
edit(F, [
# S17/S18 shared body
("  const uint64_t requestsBefore = host.keyframe_requests();\n  armFrom = host.last_seq() + 1;\n  // One picture at a time until the drop happens, then nothing more.",
 "  const uint64_t requestsBefore = host.keyframe_requests();\n  const uint64_t giveUpsBefore = rig.give_ups();\n  armFrom = host.last_seq() + 1;\n  // One picture at a time until the drop happens, then nothing more."),
("  CHECK(requests >= 1 && requests <= 2, std::string(label) + \": the spent head was given up and a keyframe asked for (+\" + std::to_string(requests) + \", timer re-ask allowed)\");",
 "  CHECK(requests >= 1 && requests <= 2, std::string(label) + \": the spent head was given up and a keyframe asked for (+\" + std::to_string(requests) + \", timer re-ask allowed)\");\n"
 "  // The keyframe-request limiter can hide repeated give-ups (120 ms minimum interval, 3 tokens),\n"
 "  // so the give-up itself is counted: the same AU may be abandoned once, never twice.\n"
 "  CHECK(rig.give_ups() - giveUpsBefore == 1, std::string(label) + \": the head was given up exactly once (\" + std::to_string(rig.give_ups() - giveUpsBefore) + \")\");"),
# S23
("  const uint64_t requestsBefore = host.keyframe_requests();\n  host.SetKeyInterChunkDelayUs(50000);  // ~40 chunks -> ~2 s on the wire",
 "  const uint64_t requestsBefore = host.keyframe_requests();\n  const uint64_t giveUpsBefore = rig.give_ups();\n  host.SetKeyInterChunkDelayUs(50000);  // ~40 chunks -> ~2 s on the wire"),
("  CHECK(host.keyframe_requests() >= requestsBefore + 1 && host.keyframe_requests() <= requestsBefore + 2, \"S23: one give-up episode at the hard cap (+\" + std::to_string(host.keyframe_requests() - requestsBefore) + \")\");",
 "  CHECK(host.keyframe_requests() >= requestsBefore + 1 && host.keyframe_requests() <= requestsBefore + 2, \"S23: one give-up episode at the hard cap (+\" + std::to_string(host.keyframe_requests() - requestsBefore) + \")\");\n"
 "  // The episode is ONE give-up. Before the tombstone the chunks still arriving re-created the\n"
 "  // abandoned assembly and the same AU was given up two or three times (the request limiter hid\n"
 "  // it), and the good IDR behind it never got through.\n"
 "  CHECK(rig.give_ups() - giveUpsBefore == 1, \"S23: exactly one give-up episode (\" + std::to_string(rig.give_ups() - giveUpsBefore) + \")\");"),
])

SCEN = '''
// S24 (A04 follow-up): the chunks of an AU the viewer has already given up keep arriving -- the
// host was still sending them, or a retransmit crossed the decision. They must be ignored: before
// the tombstone they re-created the very assembly that was abandoned, blocked the head again, and
// the same AU was given up two and three times over while the good AU behind it waited.
void scenario_late_chunks_of_abandoned_au_do_not_reblock() {
  std::printf("[S24] late chunks of a given-up AU: ignored, no second give-up, the next IDR still arrives''' + NL + '''");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> armFrom{0};
  std::atomic<uint32_t> target{0};
  LossPlan plan = static_tail_loss_plan(armFrom, target);
  plan.answerNacks = false;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint64_t giveUpsBefore = rig.give_ups();
  const uint32_t t = arm_static_tail_loss(host, rig, armFrom, target, "S24");
  if (t == 0) return;
  CHECK(pump_until_idle(host, rig, 1500, [&]() { return rig.give_ups() > giveUpsBefore; }),
        "S24: the spent head was given up");
  CHECK(rig.gate.waitForKeyFrame, "S24: waiting for a keyframe after the give-up");
  // The withheld chunk finally shows up (and the whole AU once more, as a retransmit would).
  const uint16_t lastIdx = static_cast<uint16_t>(host.chunks_for(t) - 1);
  CHECK(host.ResendChunks(t, {lastIdx}), "S24: the host still had the AU cached");
  idle(host, rig, 300);
  CHECK(rig.give_ups() - giveUpsBefore == 1,
        "S24: the late chunk did not re-create the abandoned AU (give-ups " + std::to_string(rig.give_ups() - giveUpsBefore) + ")");
  // ... and the recovery IDR is delivered, which is what the re-block used to prevent.
  const uint64_t publishedBefore = rig.publishedCount.load();
  host.ResumeAfterDrop();
  const KeyOnWire idr = send_key_and_wait_on_wire(host, "S24");
  CHECK(idr.ok, "S24: the recovery IDR went out");
  const bool resumed = pump_until(host, rig, 4000, [&]() {
    return !rig.gate.waitForKeyFrame && rig.publishedCount.load() > publishedBefore && rig.published_key(idr.keySeq);
  });
  CHECK(resumed, "S24: resumed on the IDR seq " + std::to_string(idr.keySeq) + " (key seqs " + rig.published_key_seqs() + ")");
  CHECK(rig.give_ups() - giveUpsBefore == 1, "S24: still one give-up after the recovery");
  CHECK(host.keyframe_requests() <= requestsBefore + 3, "S24: no request storm (+" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
}
'''

edit(F, [
("  uint64_t corrupted_keys() const { return corruptedKeys_; }\n",
 "  uint64_t corrupted_keys() const { return corruptedKeys_; }\n"
 "  // Data-chunk count of a cached AU (0 = not cached), so a scenario can name its last chunk.\n"
 "  uint16_t chunks_for(uint32_t seq) {\n"
 "    std::lock_guard<std::mutex> lk(cacheMu_);\n"
 "    for (auto it = cache_.rbegin(); it != cache_.rend(); ++it) {\n"
 "      if (it->seq != seq) continue;\n"
 "      const uint32_t maxChunk = kMtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));\n"
 "      return static_cast<uint16_t>((it->payload.size() + maxChunk - 1) / maxChunk);\n"
 "    }\n"
 "    return 0;\n"
 "  }\n"),
("  uint64_t giveUpHardCapUs = 0;\n",
 "  uint64_t giveUpHardCapUs = 0;\n"
 "  // A04: how many incomplete heads the receiver has abandoned (RecvStats). The keyframe-request\n"
 "  // limiter can hide a repeated give-up, so scenarios assert on this rather than on requests.\n"
 "  uint64_t give_ups() const { return receiver ? receiver->stats().udpStuckHeadGiveUps : 0; }\n"),
("}  // namespace\n\nint main() {", SCEN + "\n}  // namespace\n\nint main() {"),
("  scenario_hard_cap_gives_up_once_despite_progress();\n",
 "  scenario_hard_cap_gives_up_once_despite_progress();\n  scenario_late_chunks_of_abandoned_au_do_not_reblock();\n"),
])

# --- shared_core unit: the tombstone's own rules -------------------------------------------------
UNIT = '''
// A04 follow-up: a given-up identity is tombstoned, so the chunks still arriving for it cannot
// re-create the assembly (which used to re-block the head and cause repeated give-ups). Only that
// exact (generation, seq): another generation restarts the seq space and must not be shadowed.
bool test_udp_assembler_abandoned_tombstone() {
  UdpH264FrameAssembler a;
  a.ConfigureInOrderHold(120000, 8);
  const uint64_t t0 = 12000000;
  (void)push_chunk(a, 30, 0, 12, 4, false, t0);
  if (!expect(a.GiveUpIncomplete(1, 30, t0 + 1000), "tombstone: seq 30 given up")) return false;
  if (!expect(a.IsAbandoned(1, 30, t0 + 2000), "tombstone: it is remembered")) return false;
  // A late chunk of it is stale traffic, not a new assembly.
  auto r = push_chunk(a, 30, 1, 12, 4, false, t0 + 3000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && a.PendingCount() == 0,
              "tombstone: a late chunk of the abandoned AU is ignored")) return false;
  // The next AU is unaffected and delivers.
  r = push_chunk(a, 31, 0, 4, 4, true, t0 + 4000);
  UdpH264AssemblyStepResult out{};
  if (!expect(a.PopDelivery(t0 + 4000, true, &out) && out.frame.header.seq == 31,
              "tombstone: the next AU still delivers")) return false;
  // Another generation with the same seq is a different AU: it must assemble normally.
  const auto d = make_video_chunk(30, 0, 12, 4, false, 2);
  r = a.PushDatagram(d.data(), d.size(), t0 + 5000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial,
              "tombstone: the same seq in another generation is not shadowed")) return false;
  if (!expect(!a.IsAbandoned(2, 30, t0 + 5000), "tombstone: it is keyed by generation too")) return false;
  // It ages out (5 s) and Reset() clears it.
  if (!expect(!a.IsAbandoned(1, 30, t0 + 6'000'000), "tombstone: expires with the ttl")) return false;
  a.Reset();
  if (!expect(!a.IsAbandoned(1, 30, t0 + 2000), "tombstone: Reset clears it")) return false;
  return true;
}
'''
T = S + r'\native_video_client_shared_core_test.cpp'
edit(T, [
("bool test_udp_assembler_in_order_hold() {", UNIT.lstrip('\n') + "\nbool test_udp_assembler_in_order_hold() {"),
("  if (!test_udp_assembler_progress_and_give_up()) return 1;\n",
 "  if (!test_udp_assembler_progress_and_give_up()) return 1;\n  if (!test_udp_assembler_abandoned_tombstone()) return 1;\n"),
])
print('V14 follow-up tests ok')
