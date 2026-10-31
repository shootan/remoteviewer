// Audit probe (untracked): state trace of the PRODUCT assembler (in-order hold) + the PRODUCT
// NACK scheduler on a virtual clock, driven in the same order the Windows receive loop uses
// (viewer_video_receiver.cpp): datagram -> PushDatagram -> drain(PopDelivery...) -> Poll;
// timeout tick (25 ms) -> drain -> Poll. Loss is applied by simply not pushing a chunk.
#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "native_video_client_shared_core.hpp"
#include "udp_video_nack.hpp"

using namespace remote60::native_poc;

namespace {

constexpr uint32_t kStride = 4;

std::vector<uint8_t> make_chunk(uint32_t seq, bool key, uint16_t index, uint16_t count) {
  std::vector<uint8_t> d(sizeof(UdpVideoChunkHeader) + kStride, 0);
  auto* h = reinterpret_cast<UdpVideoChunkHeader*>(d.data());
  h->magic = kMagic;
  h->kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  h->size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  h->seq = seq;
  h->codec = static_cast<uint16_t>(UdpCodec::H264);
  h->flags = static_cast<uint16_t>((key ? 0x1u : 0u) | (index == 0 ? 0x2u : 0u) | (index + 1 == count ? 0x4u : 0u));
  h->width = 640; h->height = 360; h->stride = 0;
  h->payloadSize = static_cast<uint32_t>(count) * kStride;
  h->chunkOffset = static_cast<uint32_t>(index) * kStride;
  h->chunkSize = kStride;
  h->chunkIndex = index;
  h->chunkCount = count;
  h->chunkStride = kStride;
  h->streamGeneration = 1;
  d[sizeof(UdpVideoChunkHeader)] = static_cast<uint8_t>(index + 1);
  return d;
}

struct Rig {
  UdpH264FrameAssembler asmb;
  VideoNackScheduler nack;
  bool waitForKey = false;
  uint64_t now = 0;
  int nacks = 0;
  std::vector<std::string> log;
  Rig() { asmb.ConfigureInOrderHold(120000, 8); }
  void drain() {
    UdpH264AssemblyStepResult r{};
    while (asmb.PopDelivery(now, !waitForKey, &r)) {
      const bool key = (r.frame.header.flags & kEncodedFrameFlagKeyFrame) != 0;
      char b[160];
      std::snprintf(b, sizeof b, "  t=%6.1fms DELIVER seq=%u %s%s", now / 1000.0, r.packetSeq, key ? "KEY" : "P",
                    r.droppedPreviousIncomplete ? " (gap: droppedPreviousIncomplete -> discontinuity/keyResync)" : "");
      log.push_back(b);
      if (r.droppedPreviousIncomplete && !key) waitForKey = true;  // handle_udp_discontinuity
      if (key) waitForKey = false;                                  // note_reference_sync after decode
    }
  }
  void poll() {
    UdpVideoNackPacket p{};
    if (nack.Poll(asmb, !waitForKey, now, &p)) {
      ++nacks;
      std::string miss;
      for (uint16_t i = 0; i < p.missingCount; ++i) miss += std::to_string(p.missing[i]) + (i + 1 < p.missingCount ? "," : "");
      char b[160];
      std::snprintf(b, sizeof b, "  t=%6.1fms NACK   seq=%u round=%u missing=[%s]", now / 1000.0, p.seq, p.round, miss.c_str());
      log.push_back(b);
    }
  }
  void datagram(uint32_t seq, bool key, uint16_t index, uint16_t count) {
    auto d = make_chunk(seq, key, index, count);
    (void)asmb.PushDatagram(d.data(), d.size(), now);
    drain();
    poll();
  }
  void tick(uint64_t untilUs) {  // 25 ms receive timeouts, as the loop does with nothing arriving
    while (now < untilUs) { now += 25000; drain(); poll(); }
  }
  void send_au(uint32_t seq, bool key, uint16_t count, int lostIndex) {
    for (uint16_t i = 0; i < count; ++i) {
      if (static_cast<int>(i) == lostIndex) continue;
      datagram(seq, key, i, count);
      now += 100;  // 0.1 ms between chunks
    }
  }
  void dump(const char* title) {
    std::printf("%s\n", title);
    for (auto& l : log) std::printf("%s\n", l.c_str());
    std::printf("  => NACK datagrams sent: %d, pending assemblies: %zu, waitForKey=%d\n\n", nacks, asmb.PendingCount(), waitForKey ? 1 : 0);
  }
};

void bootstrap(Rig& r, uint32_t seq) {  // a delivered IDR so lastDeliveredSeq_ = seq (consecutive with the scenario)
  r.send_au(seq, true, 2, -1);
  r.now = 1000000;  // 1 s later
  r.log.clear();
}

void s_tail() {
  Rig r; bootstrap(r, 9);
  r.send_au(10, false, 4, 3);      // last chunk lost (tail)
  r.now = 1016700; r.send_au(11, false, 4, -1);
  r.now = 1033400; r.send_au(12, false, 4, -1);
  r.tick(1400000);
  r.dump("[S-tail] P seq 10 loses its LAST chunk; 11, 12 complete behind it (60 fps); then timeouts");
}

void s_hole() {
  Rig r; bootstrap(r, 9);
  r.send_au(10, false, 4, 1);      // chunk 1 lost (hole)
  r.now = 1016700; r.send_au(11, false, 4, -1);
  // The host answers the first NACK ~10 ms after it was sent: replay the poll timeline.
  r.tick(1045000);                              // first NACK round fires on the 25 ms tick
  r.now += 10000; r.datagram(10, false, 1, 4);   // retransmit lands 10 ms later
  r.tick(1200000);
  r.dump("[S-hole] P seq 10 loses chunk 1 (hole); retransmit at +10 ms after the NACK");
}

void s_keywait_leading_p(bool successor) {
  Rig r; bootstrap(r, 19);
  r.waitForKey = true;             // the gate is already waiting for an IDR
  r.send_au(20, false, 4, 2);      // an incomplete P arrives first
  r.now = 1016700; r.send_au(21, true, 6, 1);   // the IDR, chunk 1 lost (hole)
  if (successor) { r.now = 1033400; r.send_au(22, false, 4, -1); }
  r.tick(1400000);
  r.dump(successor ? "[S-keywait+P] waiting for key; incomplete P 20 ahead of incomplete IDR 21; P 22 follows"
                   : "[S-keywait+P, no successor] same, but nothing arrives after the IDR (static screen)");
}

void s_reorder() {
  Rig r; bootstrap(r, 29);
  // AU 31's chunk 0 arrives BEFORE anything of AU 30 (cross-AU reorder); 31 then loses chunk 1 (hole),
  // 30 loses chunk 2 (hole). 30 is the sequence-oldest blocker.
  r.datagram(31, false, 0, 4); r.now += 100;
  r.send_au(30, false, 4, 2);
  r.datagram(31, false, 2, 4); r.now += 100; r.datagram(31, false, 3, 4);
  r.now = 1016700; r.send_au(32, false, 4, -1);
  r.tick(1300000);
  r.dump("[S-reorder] 31 seen before 30; both have holes; which seq does the NACK chase?");
}


// --- A04/N3 terminal-condition scenarios (baseline before the fix; rerun after) ---
void s_tail_no_successor_reply() {
  Rig r; bootstrap(r, 9);
  r.send_au(10, false, 4, 3);      // tail lost, NO successor (static screen)
  r.tick(1130000);                 // tail grace 120 ms -> first tail NACK expected at the 25 ms tick after 1120
  r.now += 10000; r.datagram(10, false, 3, 4);  // host reply lands 10 ms later
  r.tick(1400000);
  r.dump("[A04-1] tail loss, no successor, host answers the tail NACK: expect NACK -> delivery, no gap/no IDR request");
}
void s_tail_no_successor_unanswered() {
  Rig r; bootstrap(r, 9);
  r.send_au(10, false, 4, 3);      // tail lost, no successor, host never answers
  r.tick(1600000);
  r.dump("[A04-2] tail loss, no successor, unanswered: expect bounded fallback after last tail round + allowance (baseline: head pinned)");
}
void s_hole_exhausted_tail_remaining() {
  Rig r; bootstrap(r, 9);
  for (uint16_t i = 0; i < 6; ++i) { if (i == 1 || i == 5) continue; r.datagram(10, false, i, 6); r.now += 100; }  // hole idx 1 + tail idx 5 lost
  // ... and the tail (idx 5) lost too: chunks 0,2,3,4 arrive; hole rounds exhaust at ~75 ms, tail phase at 120 ms
  r.tick(1600000);
  r.dump("[A04-3] hole rounds exhausted but tail phase remains: expect NO early abandonment before the tail rounds");
}
void s_large_au_in_progress() {
  Rig r; bootstrap(r, 9);
  // a 300-chunk IDR arriving slowly (1 chunk per 2 ms, 600 ms total) with a hole at idx 1
  for (uint16_t i = 0; i < 300; ++i) { if (i == 1) continue; r.datagram(10, true, i, 300); r.now += 2000; }
  r.tick(1900000);
  r.dump("[A04-4] large IDR still progressing for 600 ms with one hole: expect no abandonment while chunks keep arriving");
}
}  // namespace

int main() {
  s_tail();
  s_hole();
  s_keywait_leading_p(true);
  s_keywait_leading_p(false);
  s_reorder();
  s_tail_no_successor_reply();
  s_tail_no_successor_unanswered();
  s_hole_exhausted_tail_remaining();
  s_large_au_in_progress();
  return 0;
}
