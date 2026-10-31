import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'

# Codex rejected twice: dropping the oldest record when the list is full is silent forgetting, and
# the same identity would be accepted again -- the defect this tombstone exists to prevent. The
# generation-based retirement is not confirmed either (a late packet's generation must not drive
# it, and the assembler is never Reset from the receive path today). Both come out; what stays is
# the TTL removal and the same-generation delivery watermark.
edit(S + r'\native_video_client_shared_core.hpp', [
("  // Retirement is by BOUNDARY, not by a clock: a host that keeps resending the abandoned AU for\n"
 "  // longer than any timeout would otherwise get it re-assembled and abandoned a second time. An\n"
 "  // entry is retired when\n"
 "  //   - delivery has moved past it in its own generation (`lastDeliveredSeq_` is at or beyond it):\n"
 "  //     the ordinary stale guard covers those chunks from then on, so the tombstone is redundant;\n"
 "  //   - a delivery of a DIFFERENT generation happens: the old seq space is gone and its entries\n"
 "  //     can never match a packet again;\n"
 "  //   - Reset() (a new session / decoder resync);\n"
 "  //   - or the capacity bound is reached: kAbandonedMax identities may be outstanding with no\n"
 "  //     delivery at all between them, and the oldest is dropped. That is a real (bounded) hole --\n"
 "  //     16 heads abandoned before a single AU gets through -- and it is preferred to unbounded\n"
 "  //     growth; the give-up rule itself is what keeps that number small.\n"
 "  struct AbandonedAu {\n"
 "    uint64_t generation = 0;\n"
 "    uint32_t seq = 0;\n"
 "    uint64_t atUs = 0;  // diagnostics only\n"
 "  };\n"
 "  static constexpr size_t kAbandonedMax = 16;\n"
 "  std::deque<AbandonedAu> abandoned_;\n",
 "  // Retirement is by BOUNDARY, not by a clock: a host that keeps resending the abandoned AU for\n"
 "  // longer than any timeout would otherwise get it re-assembled and abandoned a second time. An\n"
 "  // entry is retired when delivery has moved past it in its own generation (`lastDeliveredSeq_`\n"
 "  // is at or beyond it): from then on the ordinary stale guard covers those chunks, so the\n"
 "  // tombstone is redundant. Reset() clears everything with the rest of the assembler state.\n"
 "  //\n"
 "  // UNFINISHED, RELEASE-BLOCKING: what happens when many identities are abandoned with no\n"
 "  // delivery in between is not decided yet. Dropping the oldest record was rejected -- forgetting\n"
 "  // an identity means accepting it again, which is the very defect this exists to prevent -- and\n"
 "  // a time-to-live was rejected for the same reason. Today the list simply grows in that case\n"
 "  // (each entry is 24 bytes and the give-up rule makes many outstanding give-ups without a single\n"
 "  // delivery a pathological case), and the saturation design that closes it properly is being\n"
 "  // confirmed. Retirement on a generation change is likewise not settled: the assembler must not\n"
 "  // follow a late packet's generation, and today nothing calls Reset() from the receive path.\n"
 "  struct AbandonedAu {\n"
 "    uint64_t generation = 0;\n"
 "    uint32_t seq = 0;\n"
 "    uint64_t atUs = 0;  // diagnostics only\n"
 "  };\n"
 "  std::deque<AbandonedAu> abandoned_;\n"),
])
edit(S + r'\native_video_client_shared_core.cpp', [
("  // Remember it, so the chunks still on their way do not re-create it (see the header note).\n"
 "  while (abandoned_.size() >= kAbandonedMax) abandoned_.pop_front();\n"
 "  abandoned_.push_back(AbandonedAu{generation, seq, nowUs});",
 "  // Remember it, so the chunks still on their way do not re-create it (see the header note).\n"
 "  // Nothing is dropped to make room: forgetting an identity would let it be accepted again.\n"
 "  abandoned_.push_back(AbandonedAu{generation, seq, nowUs});"),
("  // Retire the tombstones this delivery makes redundant (see the header note): everything the\n"
 "  // stale guard now covers in this generation, and everything from any other generation.\n"
 "  {\n"
 "    const uint64_t deliveredGeneration = assembly.header.streamGeneration;\n"
 "    const uint32_t deliveredSeq = assembly.seq;\n"
 "    abandoned_.erase(std::remove_if(abandoned_.begin(), abandoned_.end(),\n"
 "                                    [&](const AbandonedAu& a) {\n"
 "                                      if (a.generation != deliveredGeneration) return true;\n"
 "                                      return !sequence_is_newer(a.seq, deliveredSeq);\n"
 "                                    }),\n"
 "                     abandoned_.end());\n"
 "  }",
 "  // Retire the tombstones this delivery makes redundant (see the header note): in this\n"
 "  // generation, everything the ordinary stale guard covers from now on. Entries of another\n"
 "  // generation are left alone -- whether a delivery may retire them is not settled.\n"
 "  {\n"
 "    const uint64_t deliveredGeneration = assembly.header.streamGeneration;\n"
 "    const uint32_t deliveredSeq = assembly.seq;\n"
 "    abandoned_.erase(std::remove_if(abandoned_.begin(), abandoned_.end(),\n"
 "                                    [&](const AbandonedAu& a) {\n"
 "                                      return a.generation == deliveredGeneration &&\n"
 "                                             !sequence_is_newer(a.seq, deliveredSeq);\n"
 "                                    }),\n"
 "                     abandoned_.end());\n"
 "  }"),
])

# the unit test's capacity and cross-generation retirement cases go with them
edit(S + r'\native_video_client_shared_core_test.cpp', [
("""  // A delivery in another generation retires entries of the old seq space as well.
  {
    UdpH264FrameAssembler c;
    c.ConfigureInOrderHold(120000, 8);
    (void)push_chunk(c, 50, 0, 12, 4, false, t0);
    if (!expect(c.GiveUpIncomplete(1, 50, t0), "tombstone: gen 1 seq 50 given up")) return false;
    const auto g2 = make_video_chunk(7, 0, 4, 4, true, 2);
    (void)c.PushDatagram(g2.data(), g2.size(), t0 + 1000);
    UdpH264AssemblyStepResult out2{};
    if (!expect(c.PopDelivery(t0 + 1000, true, &out2) && out2.frame.header.seq == 7,
                "tombstone: the new generation's AU delivers")) return false;
    if (!expect(c.AbandonedCount() == 0, "tombstone: the old generation's entries are retired")) return false;
  }
  // Capacity: at most kAbandonedMax outstanding identities with no delivery between them.
  {
    UdpH264FrameAssembler d;
    d.ConfigureInOrderHold(120000, 32);
    for (uint32_t i = 0; i < 20; ++i) {
      (void)push_chunk(d, 100 + i, 0, 12, 4, false, t0 + i * 1000);
      if (!expect(d.GiveUpIncomplete(1, 100 + i, t0 + i * 1000), "tombstone: capacity fill")) return false;
    }
    if (!expect(d.AbandonedCount() == 16, "tombstone: bounded at 16 (" + std::to_string(d.AbandonedCount()) + ")")) return false;
    if (!expect(d.IsAbandoned(1, 119) && !d.IsAbandoned(1, 100),
                "tombstone: the oldest identities are the ones dropped")) return false;
  }
  a.Reset();""",
 """  // Many identities abandoned with no delivery in between: nothing is forgotten (that is the
  // unfinished, release-blocking case -- see the header note; forgetting one would let it be
  // accepted again, which is the defect this exists to prevent).
  {
    UdpH264FrameAssembler d;
    d.ConfigureInOrderHold(120000, 32);
    for (uint32_t i = 0; i < 20; ++i) {
      (void)push_chunk(d, 100 + i, 0, 12, 4, false, t0 + i * 1000);
      if (!expect(d.GiveUpIncomplete(1, 100 + i, t0 + i * 1000), "tombstone: many give-ups")) return false;
    }
    if (!expect(d.AbandonedCount() == 20, "tombstone: every identity is still remembered (" + std::to_string(d.AbandonedCount()) + ")")) return false;
    if (!expect(d.IsAbandoned(1, 100) && d.IsAbandoned(1, 119),
                "tombstone: the first identity is remembered as well as the last")) return false;
  }
  a.Reset();"""),
])
print('tombstone scope trimmed to the confirmed part')
