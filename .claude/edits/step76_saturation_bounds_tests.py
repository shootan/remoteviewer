import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
T = S + r'\native_video_client_shared_core_test.cpp'

TESTS = '''
// A04 saturation, boundary behaviour (the six cases the verifying Claude found missing). Each one
// asserts product state directly, because a passing scenario run does not prove any of them.
bool test_udp_assembler_saturation_bounds() {
  const uint64_t t0 = 30000000;
  auto saturate = [&](UdpH264FrameAssembler& a, uint32_t firstSeq) {
    for (uint32_t i = 0; i < 16; ++i) {
      const uint32_t seq = firstSeq + i;
      (void)push_chunk(a, seq, 0, 12, 4, false, t0 + i * 1000);
      (void)a.GiveUpIncomplete(1, seq, t0 + i * 1000);
    }
  };

  // (1) A candidate that fails during the episode is another attempt at the same recovery, not a
  //     new retirement: twenty of them must not grow the record list by one.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 100);
    if (!expect(a.saturated() && a.AbandonedCount() == 16, "bounds(1): episode started")) return false;
    const size_t before = a.AbandonedCount();
    for (uint32_t i = 0; i < 20; ++i) {
      const uint32_t seq = 1000 + i;
      (void)push_chunk(a, seq, 0, 12, 4, true, t0 + 100000 + i * 1000);  // a key candidate
      (void)a.GiveUpIncomplete(1, seq, t0 + 100000 + i * 1000);          // ... which then fails
    }
    if (!expect(a.AbandonedCount() == before,
                "bounds(1): candidate failures add no records (" + std::to_string(a.AbandonedCount()) + ")")) return false;
    if (!expect(a.saturated(), "bounds(1): still one episode")) return false;
  }

  // (2) A key that is newer than the floor but OLDER than both candidates must not evict one --
  //     and the packet that is refused must not disturb the live assemblies (the erase used to
  //     invalidate the iterator the caller then compared).
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 100);
    (void)push_chunk(a, 200, 0, 12, 4, true, t0 + 200000);
    (void)push_chunk(a, 202, 0, 12, 4, true, t0 + 201000);
    if (!expect(a.saturation_candidates() == 2, "bounds(2): two candidates")) return false;
    const auto r = push_chunk(a, 201, 0, 12, 4, true, t0 + 202000);  // between them, not newer
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored,
                "bounds(2): a key older than the candidates is refused")) return false;
    if (!expect(a.saturation_candidates() == 2 && !a.saturation_floor_set(),
                "bounds(2): candidates and floor untouched")) return false;
    // The live candidates still work, and a chunk for one of them is handled normally.
    const auto live = push_chunk(a, 202, 1, 12, 4, true, t0 + 203000);
    if (!expect(live.disposition == UdpH264AssemblyDisposition::Partial,
                "bounds(2): the live candidate still takes chunks")) return false;
    // A genuinely newer key does replace the oldest, and the floor rises to it.
    const auto newer = push_chunk(a, 203, 0, 12, 4, true, t0 + 204000);
    if (!expect(newer.disposition == UdpH264AssemblyDisposition::Partial &&
                    a.saturation_candidates() == 2 && a.saturation_floor_set() &&
                    a.saturation_floor_seq() == 200,
                "bounds(2): a newer key replaces the oldest and raises the floor")) return false;
  }

  // (3) The floor is a session-wide sequence, not a per-generation one: a late key of another
  //     generation at or below it is refused just the same.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 100);
    (void)push_chunk(a, 300, 0, 12, 4, true, t0 + 300000);
    (void)push_chunk(a, 302, 0, 12, 4, true, t0 + 301000);
    (void)push_chunk(a, 304, 0, 12, 4, true, t0 + 302000);  // replaces 300, floor = 300
    if (!expect(a.saturation_floor_set() && a.saturation_floor_seq() == 300, "bounds(3): floor at 300")) return false;
    const auto other = make_video_chunk(300, 0, 12, 4, true, 9);  // generation 9, same seq
    const auto r = a.PushDatagram(other.data(), other.size(), t0 + 303000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored,
                "bounds(3): another generation at the floor is refused too")) return false;
  }

  // (4) Retirement follows the session-wide sequence as well: records made under an older
  //     generation are retired by a newer generation's accepted key, so P frames resume.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 400);  // records 400..415 under generation 1
    const auto key = make_video_chunk(500, 0, 4, 4, true, 2);  // generation 2 recovery key
    const auto r = a.PushDatagram(key.data(), key.size(), t0 + 400000);
    if (!expect(r.disposition == UdpH264AssemblyDisposition::Queued, "bounds(4): the new generation's key completed")) return false;
    UdpH264AssemblyStepResult out{};
    if (!expect(a.PopDelivery(t0 + 400000, true, &out) && out.frame.header.seq == 500,
                "bounds(4): it is delivered")) return false;
    a.NoteKeyAccepted(2, 500);
    if (!expect(a.AbandonedCount() == 0, "bounds(4): the older generation's records are retired (" + std::to_string(a.AbandonedCount()) + ")")) return false;
    if (!expect(!a.saturated(), "bounds(4): the episode ended")) return false;
    const auto p = make_video_chunk(501, 0, 4, 4, false, 2);
    const auto rp = a.PushDatagram(p.data(), p.size(), t0 + 401000);
    if (!expect(rp.disposition == UdpH264AssemblyDisposition::Queued, "bounds(4): P frames resume")) return false;
  }

  // (5) The delivered-candidate list is bounded: fifty completed keys the caller never accepts
  //     must not make it grow without end.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 600);
    UdpH264AssemblyStepResult out{};
    for (uint32_t i = 0; i < 50; ++i) {
      const uint32_t seq = 700 + i;
      (void)push_chunk(a, seq, 0, 4, 4, true, t0 + 500000 + i * 1000);
      (void)a.PopDelivery(t0 + 500000 + i * 1000, true, &out);  // delivered, never accepted
    }
    if (!expect(a.delivered_key_candidates() <= 8,
                "bounds(5): the delivered-candidate list is bounded (" + std::to_string(a.delivered_key_candidates()) + ")")) return false;
  }

  // (6) Entering the episode trims the key assemblies to the two slots (the newest), raising the
  //     floor to what it drops; and a release that keeps the latch keeps the floor with it.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    for (uint32_t i = 0; i < 4; ++i) {
      (void)push_chunk(a, 800 + i, 0, 12, 4, true, t0 + 600000 + i * 1000);  // four key assemblies
    }
    if (!expect(a.saturation_candidates() == 4, "bounds(6): four key assemblies before the episode")) return false;
    for (uint32_t i = 0; i < 16; ++i) {
      const uint32_t seq = 900 + i;
      (void)push_chunk(a, seq, 0, 12, 4, false, t0 + 700000 + i * 1000);
      (void)a.GiveUpIncomplete(1, seq, t0 + 700000 + i * 1000);
    }
    if (!expect(a.saturated(), "bounds(6): episode started")) return false;
    if (!expect(a.saturation_candidates() == 2,
                "bounds(6): trimmed to two slots (" + std::to_string(a.saturation_candidates()) + ")")) return false;
    if (!expect(a.saturation_floor_set() && a.saturation_floor_seq() == 801,
                "bounds(6): the floor rose to the newest one dropped")) return false;
  }

  // (6b) A release that leaves the list at the cap keeps both the latch and the floor.
  {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    saturate(a, 500);                     // records 500..515, episode starts
    (void)push_chunk(a, 400, 0, 12, 4, true, t0 + 800000);   // an older key candidate
    (void)push_chunk(a, 402, 0, 12, 4, true, t0 + 801000);
    (void)push_chunk(a, 404, 0, 12, 4, true, t0 + 802000);   // replaces 400: floor = 400
    if (!expect(a.saturation_floor_set() && a.saturation_floor_seq() == 400, "bounds(6b): floor at 400")) return false;
    (void)push_chunk(a, 402, 1, 12, 4, true, t0 + 803000);
    (void)push_chunk(a, 402, 2, 12, 4, true, t0 + 804000);
    UdpH264AssemblyStepResult out{};
    if (!expect(a.PopDelivery(t0 + 805000, true, &out) && out.frame.header.seq == 402,
                "bounds(6b): the candidate is delivered")) return false;
    a.NoteKeyAccepted(1, 402);
    if (!expect(a.saturated(), "bounds(6b): records above it keep the episode")) return false;
    if (!expect(a.saturation_floor_set() && a.saturation_floor_seq() == 400,
                "bounds(6b): the floor is kept while the episode lasts")) return false;
  }
  return true;
}
'''

edit(T, [
("bool test_udp_assembler_in_order_hold() {", TESTS.lstrip('\n') + "\nbool test_udp_assembler_in_order_hold() {"),
("  if (!test_udp_assembler_saturation_episode()) return 1;\n",
 "  if (!test_udp_assembler_saturation_episode()) return 1;\n  if (!test_udp_assembler_saturation_bounds()) return 1;\n"),
])
print('saturation bounds tests ok')
