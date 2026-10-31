import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
T = S + r'\native_video_client_shared_core_test.cpp'

UNIT = '''
// A04 saturation episode: the abandoned list's cap starts a recovery episode instead of evicting
// records. Covers what Codex asked for: no release on a wrong generation (and no state moved by
// one), a refused generation changing nothing, the K=2 candidate slots and the floor, an old seq
// against a newer delivered one causing no reset, reordering, a live candidate surviving late
// duplicates, and P frames resuming after the accepted key.
bool test_udp_assembler_saturation_episode() {
  const uint64_t t0 = 20000000;
  auto fill_to_saturation = [&](UdpH264FrameAssembler& a, uint32_t firstSeq, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      const uint32_t seq = firstSeq + static_cast<uint32_t>(i);
      (void)push_chunk(a, seq, 0, 12, 4, false, t0 + i * 1000);
      (void)a.GiveUpIncomplete(1, seq, t0 + i * 1000);
    }
  };

  // The cap starts the episode; nothing is forgotten and the non-key assemblies go with it.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 32);
    fill_to_saturation(a, 200, 15);
    (void)push_chunk(a, 300, 0, 12, 4, false, t0 + 90000);  // a non-key assembly still in flight
    if (!expect(!a.saturated() && a.PendingCount() == 1, "saturation: not yet at the cap")) return false;
    (void)push_chunk(a, 215, 0, 12, 4, false, t0 + 91000);
    (void)a.GiveUpIncomplete(1, 215, t0 + 91000);
    if (!expect(a.saturated(), "saturation: the cap started the episode")) return false;
    if (!expect(a.AbandonedCount() == 16, "saturation: every record is kept")) return false;
    if (!expect(a.PendingCount() == 0, "saturation: the non-key assemblies were dropped")) return false;
    // A non-key AU may not start an assembly now; a keyframe may.
    auto r = push_chunk(a, 400, 0, 12, 4, false, t0 + 92000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && a.PendingCount() == 0,
                "saturation: a new P frame is refused")) return false;
    r = push_chunk(a, 401, 0, 12, 4, true, t0 + 93000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial && a.saturation_candidates() == 1,
                "saturation: a keyframe becomes a candidate")) return false;
    // K = 2: a third key replaces the oldest candidate and the floor rises to it.
    r = push_chunk(a, 402, 0, 12, 4, true, t0 + 94000);
    if (!expect(a.saturation_candidates() == 2, "saturation: two candidate slots")) return false;
    r = push_chunk(a, 403, 0, 12, 4, true, t0 + 95000);
    if (!expect(a.saturation_candidates() == 2 && a.saturation_floor_set() && a.saturation_floor_seq() == 401,
                "saturation: the oldest candidate gave way and the floor rose to it")) return false;
    // The replaced candidate's late chunks cannot re-create it; the live ones keep repairing.
    r = push_chunk(a, 401, 1, 12, 4, true, t0 + 96000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && a.saturation_candidates() == 2,
                "saturation: the replaced candidate stays gone")) return false;
    r = push_chunk(a, 402, 1, 12, 4, true, t0 + 97000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial,
                "saturation: a live candidate keeps being repaired")) return false;
    r = push_chunk(a, 402, 1, 12, 4, true, t0 + 98000);  // a duplicate of a live candidate
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial && a.saturation_candidates() == 2,
                "saturation: a late duplicate does not disturb the live candidate")) return false;
    // Delivery alone does not end the episode: only a key the caller ACCEPTS does, and only one
    // of this episode's candidates.
    r = push_chunk(a, 402, 2, 12, 4, true, t0 + 99000);
    UdpH264AssemblyStepResult out{};
    if (!expect(a.PopDelivery(t0 + 99000, true, &out) && out.frame.header.seq == 402,
                "saturation: the completed candidate is delivered")) return false;
    if (!expect(a.saturated(), "saturation: delivery alone does not end it")) return false;
    a.NoteKeyAccepted(1, 999);   // never a candidate
    if (!expect(a.saturated(), "saturation: an unknown key does not end it")) return false;
    a.NoteKeyAccepted(2, 402);   // right seq, wrong generation
    if (!expect(a.saturated() && a.AbandonedCount() == 16 && a.saturation_candidates() == 1 &&
                    a.saturation_floor_set() && a.saturation_floor_seq() == 401,
                "saturation: a wrong generation releases nothing and moves nothing")) return false;
    a.NoteKeyAccepted(1, 402);   // the accepted recovery key
    if (!expect(!a.saturated() && !a.saturation_floor_set(),
                "saturation: the accepted key ended the episode")) return false;
    if (!expect(a.AbandonedCount() == 0, "saturation: the records it covers were retired")) return false;
    // P frames flow again.
    r = push_chunk(a, 404, 0, 4, 4, false, t0 + 100000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Queued ||
                    r.disposition == UdpH264AssemblyDisposition::Partial,
                "saturation: P frames resume after the accepted key")) return false;
  }

  // A generation the caller refuses may not evict a candidate or raise the floor.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 32);
    a.SetSaturationAdmitFilter([](uint64_t generation) { return generation == 1; });
    fill_to_saturation(a, 500, 16);
    if (!expect(a.saturated(), "saturation(admit): episode started")) return false;
    (void)push_chunk(a, 600, 0, 12, 4, true, t0 + 200000);
    (void)push_chunk(a, 601, 0, 12, 4, true, t0 + 201000);
    if (!expect(a.saturation_candidates() == 2, "saturation(admit): two accepted candidates")) return false;
    const auto refused = make_video_chunk(700, 0, 12, 4, true, 6);  // generation 6: refused
    const auto r = a.PushDatagram(refused.data(), refused.size(), t0 + 202000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored, "saturation(admit): refused")) return false;
    if (!expect(a.saturation_candidates() == 2 && !a.saturation_floor_set() && a.AbandonedCount() == 16,
                "saturation(admit): candidates, floor and records untouched")) return false;
    // The same seq from the admitted generation is accepted and does replace one.
    (void)push_chunk(a, 700, 0, 12, 4, true, t0 + 203000);
    if (!expect(a.saturation_candidates() == 2 && a.saturation_floor_set() && a.saturation_floor_seq() == 600,
                "saturation(admit): an admitted generation replaces the oldest")) return false;
  }

  // An older seq than the last delivered one is stale traffic, never a reason to reset anything;
  // reordering is unaffected (a lower seq arriving later still assembles when it is not retired).
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 32);
    (void)push_chunk(a, 100, 0, 4, 4, true, t0 + 300000);
    UdpH264AssemblyStepResult out{};
    if (!expect(a.PopDelivery(t0 + 300000, true, &out) && out.frame.header.seq == 100,
                "saturation(stale): seq 100 delivered")) return false;
    const auto old99 = make_video_chunk(99, 0, 12, 4, false, 1);
    const auto r = a.PushDatagram(old99.data(), old99.size(), t0 + 301000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && !a.saturated() &&
                    a.PendingCount() == 0,
                "saturation(stale): an old seq is ignored, nothing is reset")) return false;
    // Reordering: 31 arrives before 30 -- but here nothing is retired, so 30 still assembles.
    UdpH264FrameAssembler b;
    b.ConfigureInOrderHold(120000, 32);
    (void)push_chunk(b, 31, 0, 12, 4, false, t0 + 400000);
    const auto r30 = push_chunk(b, 30, 0, 12, 4, false, t0 + 401000);
    if (!expect(r30.disposition == UdpH264AssemblyDisposition::Partial && b.PendingCount() == 2,
                "saturation(reorder): a later-arriving lower seq still assembles")) return false;
    // Giving up 31 (the arrival-order head) must not block 30 either.
    if (!expect(b.GiveUpIncomplete(1, 31, t0 + 402000), "saturation(reorder): 31 given up")) return false;
    const auto r30b = push_chunk(b, 30, 1, 12, 4, false, t0 + 403000);
    if (!expect(r30b.disposition == UdpH264AssemblyDisposition::Partial,
                "saturation(reorder): 30 is not blocked by the give-up of 31")) return false;
  }
  return true;
}
'''

edit(T, [
("bool test_udp_assembler_in_order_hold() {", UNIT.lstrip('\n') + "\nbool test_udp_assembler_in_order_hold() {"),
("  if (!test_udp_assembler_abandoned_tombstone()) return 1;\n",
 "  if (!test_udp_assembler_abandoned_tombstone()) return 1;\n  if (!test_udp_assembler_saturation_episode()) return 1;\n"),
])
print('saturation unit tests ok')
