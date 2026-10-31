import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

TEST = r'D:\remote\remote\apps\native_poc\src\native_video_client_shared_core_test.cpp'

edit(TEST, [
(
"""#include "native_socket.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_session.hpp"
""",
"""#include "native_socket.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_session.hpp"
#include "udp_video_nack.hpp"
"""),
(
"""bool test_session_controller() {
  ClientSessionController controller;
""",
"""// One data datagram of a multi-chunk AU: `stride`-byte chunks, the last one possibly shorter.
std::vector<uint8_t> make_video_chunk(uint32_t seq, uint16_t chunkIndex, uint32_t payloadSize,
                                      uint32_t stride, bool key, uint64_t generation = 1) {
  const uint16_t chunkCount = static_cast<uint16_t>((payloadSize + stride - 1u) / stride);
  const uint32_t offset = static_cast<uint32_t>(chunkIndex) * stride;
  const uint32_t chunkSize = std::min<uint32_t>(stride, payloadSize - offset);
  std::vector<uint8_t> datagram(sizeof(UdpVideoChunkHeader) + chunkSize, 0);
  auto* header = reinterpret_cast<UdpVideoChunkHeader*>(datagram.data());
  header->magic = remote60::native_poc::kMagic;
  header->kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  header->size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  header->seq = seq;
  header->codec = static_cast<uint16_t>(UdpCodec::H264);
  header->flags = static_cast<uint16_t>((key ? 0x1u : 0u) | (offset == 0 ? 0x2u : 0u) |
                                        (offset + chunkSize >= payloadSize ? 0x4u : 0u));
  header->width = 64;
  header->height = 64;
  header->payloadSize = payloadSize;
  header->chunkOffset = offset;
  header->chunkSize = chunkSize;
  header->chunkIndex = chunkIndex;
  header->chunkCount = chunkCount;
  header->chunkStride = stride;
  header->streamGeneration = generation;
  for (uint32_t i = 0; i < chunkSize; ++i) {
    datagram[sizeof(UdpVideoChunkHeader) + i] = static_cast<uint8_t>(seq * 16 + offset + i);
  }
  return datagram;
}

UdpH264AssemblyStepResult push_chunk(UdpH264FrameAssembler& a, uint32_t seq, uint16_t chunkIndex,
                                     uint32_t payloadSize, uint32_t stride, bool key, uint64_t nowUs) {
  const auto d = make_video_chunk(seq, chunkIndex, payloadSize, stride, key);
  return a.PushDatagram(d.data(), d.size(), nowUs);
}

// The shared NACK scheduler (udp_video_nack.hpp): hole vs tail grace, round spacing, the round
// cap, the keyframe-only rule while an IDR is awaited, and the restart when the blocker changes.
// The Windows viewer and the session controller both run this object. (Windows NACK wiring.)
bool test_video_nack_scheduler() {
  using remote60::native_poc::VideoNackScheduler;
  using remote60::native_poc::UdpVideoNackPacket;
  UdpH264FrameAssembler a;
  VideoNackScheduler s;  // defaults: gap 25 ms, tail 120 ms, round 25 ms, 3 rounds
  UdpVideoNackPacket pkt{};
  const uint64_t t0 = 1000000;

  // Nothing incomplete: no NACK.
  if (!expect(!s.Poll(a, true, t0, &pkt), "nack: nothing incomplete -> no packet")) return false;

  // AU 10, 3 chunks (12 bytes / stride 4): chunks 0 and 2 arrive, chunk 1 is a confirmed hole.
  (void)push_chunk(a, 10, 0, 12, 4, false, t0);
  (void)push_chunk(a, 10, 2, 12, 4, false, t0);
  if (!expect(!s.Poll(a, true, t0, &pkt), "nack: hole inside the reorder grace -> wait")) return false;
  if (!expect(!s.Poll(a, true, t0 + 24000, &pkt), "nack: 24 ms is still inside the grace")) return false;
  if (!expect(s.Poll(a, true, t0 + 25000, &pkt), "nack: hole past the grace -> round 0")) return false;
  if (!expect(pkt.seq == 10 && pkt.missingCount == 1 && pkt.missing[0] == 1 && pkt.round == 0 &&
                  pkt.chunkCount == 3,
              "nack: round 0 names the hole")) return false;
  if (!expect(!s.Poll(a, true, t0 + 40000, &pkt), "nack: inside the round spacing -> wait")) return false;
  if (!expect(s.Poll(a, true, t0 + 50000, &pkt) && pkt.round == 1, "nack: round 1 at +25 ms")) return false;
  if (!expect(s.Poll(a, true, t0 + 75000, &pkt) && pkt.round == 2, "nack: round 2 at +25 ms")) return false;
  if (!expect(!s.Poll(a, true, t0 + 100000, &pkt), "nack: rounds exhausted -> the IDR path takes over")) return false;
  if (!expect(s.stats().packetsSent == 3 && s.stats().chunksRequested == 3 &&
                  s.stats().roundsExhausted == 1,
              "nack: stats after three rounds")) return false;
  if (!expect(!s.Poll(a, true, t0 + 200000, &pkt) && s.stats().roundsExhausted == 1,
              "nack: exhaustion is counted once")) return false;

  // The hole is repaired: AU 10 completes, nothing is incomplete, the scheduler forgets it.
  auto r = push_chunk(a, 10, 1, 12, 4, false, t0 + 210000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Completed, "nack: repaired AU completes")) return false;
  if (!expect(!s.Poll(a, true, t0 + 211000, &pkt) && s.current_seq() == 0, "nack: forgotten after completion")) return false;

  // Tail: AU 11, 4 chunks, only chunk 0 in -> the rest is still in flight (>= highWater).
  const uint64_t t1 = t0 + 300000;
  (void)push_chunk(a, 11, 0, 16, 4, false, t1);
  if (!expect(!s.Poll(a, true, t1 + 25000, &pkt), "nack: in-flight tail is not asked for at the hole grace")) return false;
  if (!expect(!s.Poll(a, true, t1 + 119000, &pkt), "nack: tail still inside its long grace")) return false;
  if (!expect(s.Poll(a, true, t1 + 120000, &pkt) && pkt.missingCount == 3 && pkt.missing[0] == 1 &&
                  pkt.missing[2] == 3 && pkt.round == 0,
              "nack: tail asked for after the long grace")) return false;

  // Mixed: AU 12, 5 chunks, chunks 0 and 3 in -> 1,2 are holes (< highWater 4), 4 is tail.
  a.Reset();
  s.Reset();
  const uint64_t t2 = t0 + 500000;
  (void)push_chunk(a, 12, 0, 20, 4, false, t2);
  (void)push_chunk(a, 12, 3, 20, 4, false, t2);
  if (!expect(s.Poll(a, true, t2 + 30000, &pkt) && pkt.missingCount == 2 && pkt.missing[0] == 1 &&
                  pkt.missing[1] == 2,
              "nack: only the holes at the short grace, the tail waits")) return false;
  if (!expect(s.Poll(a, true, t2 + 125000, &pkt) && pkt.missingCount == 3 && pkt.missing[2] == 4,
              "nack: the tail joins once its grace passed")) return false;

  // Waiting for a keyframe: a non-key incomplete AU is not chased, an incomplete IDR is.
  a.Reset();
  s.Reset();
  const uint64_t t3 = t0 + 800000;
  (void)push_chunk(a, 20, 0, 12, 4, false, t3);
  (void)push_chunk(a, 20, 2, 12, 4, false, t3);
  if (!expect(!s.Poll(a, false, t3 + 50000, &pkt), "nack: non-key AU is not repaired while an IDR is awaited")) return false;
  a.Reset();
  (void)push_chunk(a, 21, 0, 12, 4, true, t3);
  (void)push_chunk(a, 21, 2, 12, 4, true, t3);
  if (!expect(s.Poll(a, false, t3 + 50000, &pkt) && pkt.seq == 21,
              "nack: an incomplete IDR is repaired even while an IDR is awaited")) return false;

  // Blocker change restarts the schedule: AU 30 exhausted its rounds, AU 31 gets fresh ones.
  a.Reset();
  s.Reset();
  const uint64_t t4 = t0 + 1000000;
  (void)push_chunk(a, 30, 0, 12, 4, false, t4);
  (void)push_chunk(a, 30, 2, 12, 4, false, t4);
  (void)s.Poll(a, true, t4 + 25000, &pkt);
  (void)s.Poll(a, true, t4 + 50000, &pkt);
  (void)s.Poll(a, true, t4 + 75000, &pkt);
  if (!expect(!s.Poll(a, true, t4 + 100000, &pkt), "nack: AU 30 exhausted")) return false;
  r = push_chunk(a, 30, 1, 12, 4, false, t4 + 101000);  // repaired late: delivered
  (void)push_chunk(a, 31, 0, 12, 4, false, t4 + 102000);
  (void)push_chunk(a, 31, 2, 12, 4, false, t4 + 102000);
  if (!expect(!s.Poll(a, true, t4 + 110000, &pkt), "nack: AU 31 starts its own grace")) return false;
  if (!expect(s.Poll(a, true, t4 + 127000, &pkt) && pkt.seq == 31 && pkt.round == 0,
              "nack: AU 31 gets a fresh round schedule")) return false;
  return true;
}

// The assembler's in-order delivery hold (ConfigureInOrderHold / PopDelivery): a completed AU
// waits behind an older incomplete one until the hold expires; the keyframe-wait rule; the legacy
// immediate path is untouched. (Windows NACK wiring.)
bool test_udp_assembler_in_order_hold() {
  UdpH264FrameAssembler a;
  a.ConfigureInOrderHold(100000, 8);
  UdpH264AssemblyStepResult out{};
  const uint64_t t0 = 5000000;

  // AU 1 half in, AU 2 complete: AU 2 is queued, not delivered, while AU 1 may still be repaired.
  auto r = push_chunk(a, 1, 0, 8, 4, true, t0);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial, "hold: AU 1 partial")) return false;
  r = push_chunk(a, 2, 0, 4, 4, false, t0 + 5000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Queued && !r.droppedPreviousIncomplete,
              "hold: a completed AU is queued, not delivered")) return false;
  if (!expect(!a.PopDelivery(t0 + 5000, true, &out), "hold: AU 2 waits behind AU 1")) return false;
  if (!expect(!a.PopDelivery(t0 + 99000, true, &out), "hold: still waiting inside the hold")) return false;
  if (!expect(a.PendingCount() == 2, "hold: both assemblies pending")) return false;
  // The retransmit lands: AU 1 completes and both go out in order, no gap reported.
  r = push_chunk(a, 1, 1, 8, 4, true, t0 + 60000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Queued, "hold: repaired AU 1 queued")) return false;
  if (!expect(a.PopDelivery(t0 + 60000, true, &out) && out.frame.header.seq == 1 &&
                  !out.droppedPreviousIncomplete && out.frame.payload.size() == 8 &&
                  (out.frame.header.flags & kEncodedFrameFlagKeyFrame) != 0,
              "hold: AU 1 delivered first, intact")) return false;
  if (!expect(a.PopDelivery(t0 + 60000, true, &out) && out.frame.header.seq == 2 &&
                  !out.droppedPreviousIncomplete,
              "hold: AU 2 delivered second, no gap")) return false;
  if (!expect(!a.PopDelivery(t0 + 60000, true, &out) && a.PendingCount() == 0, "hold: drained")) return false;

  // Hold expiry: AU 3 never completes; AU 4 goes out once AU 3 outlived the hold, carrying the gap.
  const uint64_t t1 = t0 + 200000;
  (void)push_chunk(a, 3, 0, 8, 4, false, t1);
  r = push_chunk(a, 4, 0, 4, 4, false, t1 + 5000);
  if (!expect(!a.PopDelivery(t1 + 99000, true, &out), "hold: AU 4 held while AU 3 is young")) return false;
  if (!expect(a.PopDelivery(t1 + 100000, true, &out) && out.frame.header.seq == 4 &&
                  out.droppedPreviousIncomplete,
              "hold: expired -> AU 4 delivered with the gap")) return false;
  if (!expect(a.PendingCount() == 0, "hold: the given-up AU 3 is gone")) return false;

  // A late chunk of the given-up AU is stale traffic, not a new assembly.
  r = push_chunk(a, 3, 1, 8, 4, false, t1 + 110000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Ignored && r.reorderDetected,
              "hold: late chunk of a given-up AU is ignored")) return false;

  // Waiting for an IDR: an incomplete NON-key head is not worth holding for -> released at once.
  const uint64_t t2 = t0 + 400000;
  (void)push_chunk(a, 5, 0, 8, 4, false, t2);
  (void)push_chunk(a, 6, 0, 4, 4, true, t2 + 1000);
  if (!expect(a.PopDelivery(t2 + 1000, false, &out) && out.frame.header.seq == 6 &&
                  out.droppedPreviousIncomplete,
              "hold: keyframe-wait releases past a non-key incomplete head")) return false;
  // ... but an incomplete KEY head is held even then (it is the only recovery point).
  const uint64_t t3 = t0 + 600000;
  (void)push_chunk(a, 7, 0, 8, 4, true, t3);
  (void)push_chunk(a, 8, 0, 4, 4, false, t3 + 1000);
  if (!expect(!a.PopDelivery(t3 + 50000, false, &out), "hold: an incomplete IDR head is held while an IDR is awaited")) return false;
  if (!expect(a.PopDelivery(t3 + 100000, false, &out) && out.frame.header.seq == 8 &&
                  out.droppedPreviousIncomplete,
              "hold: the IDR head is given up after the hold")) return false;

  // Sequence order beats arrival order: AU 10 arrives complete before AU 9's chunks; AU 9 goes first.
  const uint64_t t4 = t0 + 800000;
  (void)push_chunk(a, 10, 0, 4, 4, false, t4);
  (void)push_chunk(a, 9, 0, 8, 4, false, t4 + 1000);
  (void)push_chunk(a, 9, 1, 8, 4, false, t4 + 2000);
  if (!expect(a.PopDelivery(t4 + 2000, true, &out) && out.frame.header.seq == 9 &&
                  out.droppedPreviousIncomplete,
              "hold: AU 9 delivered before AU 10 (gap to 8 is real)")) return false;
  if (!expect(a.PopDelivery(t4 + 2000, true, &out) && out.frame.header.seq == 10 &&
                  !out.droppedPreviousIncomplete,
              "hold: then AU 10, contiguous")) return false;

  // The cap evicts the stuck oldest, not the newest: cap 2, AU 11 stuck, AU 12 and 13 arrive.
  UdpH264FrameAssembler b;
  b.ConfigureInOrderHold(100000, 2);
  (void)push_chunk(b, 11, 0, 8, 4, false, t0);
  (void)push_chunk(b, 12, 0, 8, 4, false, t0 + 1000);
  r = push_chunk(b, 13, 0, 4, 4, false, t0 + 2000);
  if (!expect(r.droppedPreviousIncomplete && b.PendingCount() == 2, "hold: cap evicts one")) return false;
  (void)push_chunk(b, 12, 1, 8, 4, false, t0 + 3000);
  if (!expect(b.PopDelivery(t0 + 3000, true, &out) && out.frame.header.seq == 12,
              "hold: the evicted one was AU 11 (oldest), AU 12 survived")) return false;

  // Legacy (no hold) is unchanged: AU 2 completing discards the half AU 1 and reports the gap.
  UdpH264FrameAssembler legacy;
  (void)push_chunk(legacy, 1, 0, 8, 4, false, t0);
  r = push_chunk(legacy, 2, 0, 4, 4, false, t0 + 1000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Completed && !r.droppedPreviousIncomplete,
              "legacy: first delivery has no previous to drop")) return false;
  (void)push_chunk(legacy, 3, 0, 8, 4, false, t0 + 2000);
  r = push_chunk(legacy, 4, 0, 4, 4, false, t0 + 3000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Completed && r.droppedPreviousIncomplete &&
                  legacy.PendingCount() == 0,
              "legacy: a newer completion discards the older incomplete AU at once")) return false;
  return true;
}

bool test_session_controller() {
  ClientSessionController controller;
"""),
(
"""  if (!test_udp_assembler()) return 1;
  if (!test_session_controller()) return 1;
""",
"""  if (!test_udp_assembler()) return 1;
  if (!test_video_nack_scheduler()) return 1;
  if (!test_udp_assembler_in_order_hold()) return 1;
  if (!test_session_controller()) return 1;
"""),
])
