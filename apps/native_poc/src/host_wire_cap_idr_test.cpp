// bitrate-hard-cap r1 -- the large IDR vs the receiver, measured (plan "계약 7 충돌").
//
// The hard wire cap paces an IDR's datagrams out at the user's bitrate, so a 155-208 KB IDR takes
// ~200 ms-1.1 s to fully arrive. The real receiver (UdpH264FrameAssembler + VideoNackScheduler) runs
// its NACK tail grace from the AU's FIRST packet (120 ms) and gives up after maxRounds, requesting a
// keyframe -- which starts another cap-paced IDR: the loop the contract's point 7 warns about. This
// test REPRODUCES that with the current, unmodified receiver and reports the numbers, so the fix (a
// progress-based tail rule, Codex-gated) can be judged against them. No receiver product code is
// changed here; the assembler already exposes lastProgressUs, which a progress-based rule would use.
//
// It is deterministic: the real host chunker emits the IDR through the wire limiter on a fake clock
// (so the datagram arrival schedule is exactly the cap's), and those datagrams are replayed into the
// real assembler at those times while the real scheduler is polled. Tag: pure-logic.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "host_net_io.hpp"
#include "host_wire_limiter.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "udp_video_nack.hpp"

using namespace remote60::native_poc;

namespace {
int g_checks = 0, g_failed = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}
constexpr uint32_t kMtu = 1200;
constexpr uint64_t kHdr28 = 28;

struct Datagram {
  uint64_t emitUs;
  std::vector<uint8_t> bytes;
};

// Emit one IDR through the real chunker + the wire limiter on a fake clock; capture the datagrams
// and the virtual time each was permitted (= the cap-paced arrival schedule).
std::vector<Datagram> cap_paced_idr(uint64_t capBps, size_t idrBytes) {
  std::vector<Datagram> out;
  uint64_t nowUs = 1'000'000;
  WireLimiter lim([&nowUs] { return nowUs; },
                  [&nowUs](uint64_t deadlineUs, uint64_t) -> bool {
                    if (deadlineUs > nowUs) nowUs = deadlineUs;
                    return true;
                  });
  lim.SetRate(capBps, clamp_udp_mtu(kMtu) + 28u);
  std::vector<uint8_t> payload(idrBytes, 0xAB);
  UdpVideoChunkHeader base{};
  base.magic = kMagic;
  base.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  base.size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  base.seq = 100;
  base.codec = static_cast<uint16_t>(UdpCodec::H264);
  base.flags = 0x1u;  // keyframe
  base.width = 1920;
  base.height = 1080;
  base.payloadSize = static_cast<uint32_t>(idrBytes);
  base.streamGeneration = 7;
  base.captureQpcUs = 1000000;
  base.sendQpcUs = 1000030;
  WireEgress wire;
  wire.limiter = &lim;
  wire.sink = [&out, &nowUs](const uint8_t* dg, int len, bool) -> int {
    out.push_back({nowUs, std::vector<uint8_t>(dg, dg + len)});
    return len;
  };
  SendPathStats st{};
  UdpEgressConfig egress;  // no intra-frame pacing; the limiter paces
  send_udp_chunks_timed(INVALID_SOCKET, sockaddr_in{}, payload.data(), payload.size(), base, kMtu, &st,
                        nullptr, 0, egress, &wire);
  return out;
}

void measure(uint64_t capBps, size_t idrBytes, const char* label) {
  const auto dgs = cap_paced_idr(capBps, idrBytes);
  const uint64_t completeUs = dgs.empty() ? 0 : dgs.back().emitUs;
  const uint64_t firstUs = dgs.empty() ? 0 : dgs.front().emitUs;
  const double deliverMs = (completeUs - firstUs) / 1000.0;

  UdpH264FrameAssembler asm1;
  asm1.ConfigureInOrderHold(120000, 8);  // the viewer's in-order hold (~120 ms at 60 fps)
  VideoNackScheduler sched;              // default config: gap 25, tail 120, round 25, maxRounds 3

  uint64_t prematureNacks = 0, chunksReq = 0, keyframeRequestUs = 0;
  bool gaveUp = false;
  size_t fed = 0;
  uint64_t dbgIncompletePolls = 0, dbgMaxAgeUs = 0, dbgMaxMissing = 0, dbgMaxHighWater = 0;
  // Replay datagrams at their cap-paced arrival times; between arrivals poll at the 25 ms receive
  // cadence so the scheduler's rounds advance exactly as in the viewer loop.
  uint64_t pollUs = firstUs;
  auto poll = [&](uint64_t nowUs) {
    UdpVideoNackPacket nack{};
    if (sched.Poll(asm1, /*repairNonKey=*/true, nowUs, &nack)) {
      ++prematureNacks;  // no loss in this stream: every NACK is for a chunk still in flight
      chunksReq += nack.missingCount;
    }
    // The caller gives the AU up and requests a keyframe once the rounds are spent on the tail.
    uint16_t miss[kUdpVideoNackMaxMissing];
    UdpH264FrameAssembler::IncompleteAuInfo info{};
    const bool hasIncomplete = asm1.OldestIncomplete(miss, kUdpVideoNackMaxMissing, &info);
    const bool hasTail = hasIncomplete && info.missingTotal > 0;
    if (hasIncomplete) {
      ++dbgIncompletePolls;
      const uint64_t age = nowUs >= info.firstPacketUs ? nowUs - info.firstPacketUs : 0;
      dbgMaxAgeUs = std::max(dbgMaxAgeUs, age);
      dbgMaxMissing = std::max<uint64_t>(dbgMaxMissing, info.missingTotal);
      dbgMaxHighWater = std::max<uint64_t>(dbgMaxHighWater, info.highWater);
    }
    if (!gaveUp && hasIncomplete && sched.spent_for(hasTail)) {
      gaveUp = true;
      keyframeRequestUs = nowUs;
    }
  };
  for (const auto& d : dgs) {
    // advance the 25 ms poll cadence up to this arrival
    while (pollUs + 25000 <= d.emitUs) {
      pollUs += 25000;
      poll(pollUs);
    }
    UdpH264AssemblyStepResult r = asm1.PushDatagram(d.bytes.data(), d.bytes.size(), d.emitUs);
    (void)r;
    ++fed;
    poll(d.emitUs);
  }
  // a couple of polls after the last datagram (the viewer keeps polling on timeouts)
  for (int k = 0; k < 6; ++k) {
    pollUs = completeUs + static_cast<uint64_t>(k + 1) * 25000;
    poll(pollUs);
  }

  const double keyframeAtMs = gaveUp ? (keyframeRequestUs - firstUs) / 1000.0 : 0.0;
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "%s: deliver=%.0fms prematureNacks=%llu chunksReq=%llu roundsExhausted=%llu gaveUp=%d keyframeAt=%.0fms",
                label, deliverMs, (unsigned long long)prematureNacks, (unsigned long long)chunksReq,
                (unsigned long long)sched.stats().roundsExhausted, gaveUp ? 1 : 0, keyframeAtMs);
  // The reproduction: a cap-paced IDR draws premature NACKs for its still-in-flight tail, exhausts
  // the rounds, and the caller requests a keyframe BEFORE the IDR has finished arriving -> IDR loop.
  std::printf("    dbg %s: incompletePolls=%llu maxAgeMs=%.0f maxMissing=%llu maxHighWater=%llu fed=%zu dgs=%zu\n",
              label, (unsigned long long)dbgIncompletePolls, dbgMaxAgeUs / 1000.0,
              (unsigned long long)dbgMaxMissing, (unsigned long long)dbgMaxHighWater, fed, dgs.size());
  // r2 FIX verification: with the progress-based tail rule, a lossless cap-paced IDR whose tail is
  // still arriving draws NO premature tail NACK and the scheduler spends no rounds on it -- so the
  // caller never gives it up (spent_for stays false). (The full product give-up path -- viewer
  // stuck-head + frame-gate -- is exercised by the mixed-version timeline test.)
  check(std::string("FIX ") + label + ": no premature tail NACK on a lossless cap-paced IDR", prematureNacks == 0, buf);
  check(std::string("FIX ") + label + ": the scheduler spends no rounds, so spent_for stays false", !gaveUp, buf);
}

// Mixed-version comparison: the SAME cap-paced lossless IDR through the real scheduler in its
// pre-0.2.147/legacy age-based tail mode vs the r2 progress-based mode. The old receiver fires
// premature tail NACKs (wasteful, but note: it does NOT prove a give-up -- the product give-up also
// needs noProgress, and the tail keeps progressing); the fixed receiver fires none.
void measure_legacy_vs_fixed() {
  std::printf("\n--- mixed version: legacy (age-based) vs fixed (progress-based) tail, same cap-paced IDR ---\n");
  const auto dgs = cap_paced_idr(1'500'000, 208u * 1024u);  // the worst case: ~1.4 s send
  auto run = [&](bool legacy) {
    UdpH264FrameAssembler a;
    a.ConfigureInOrderHold(120000, 8);
    VideoNackConfig cfg;
    cfg.tailAgeBasedLegacy = legacy;
    VideoNackScheduler sched(cfg);
    uint64_t nacks = 0;
    uint64_t pollUs = dgs.front().emitUs;
    for (const auto& d : dgs) {
      while (pollUs + 25000 <= d.emitUs) {
        pollUs += 25000;
        UdpVideoNackPacket p{};
        if (sched.Poll(a, true, pollUs, &p)) ++nacks;
      }
      a.PushDatagram(d.bytes.data(), d.bytes.size(), d.emitUs);
      UdpVideoNackPacket p{};
      if (sched.Poll(a, true, d.emitUs, &p)) ++nacks;
    }
    return nacks;
  };
  const uint64_t legacyNacks = run(true);
  const uint64_t fixedNacks = run(false);
  check("legacy (old 0.2.147) age-based tail: premature NACKs on the lossless cap-paced IDR",
        legacyNacks > 0, "legacyNacks=" + std::to_string(legacyNacks));
  check("fixed (r2) progress-based tail: no premature NACK on the same IDR",
        fixedNacks == 0, "fixedNacks=" + std::to_string(fixedNacks));
}

// Negative control: a tail that genuinely STOPS arriving (the host stalls) IS still NACKed after
// tailGrace of no progress -- the progress rule must not disable real tail recovery.
void measure_stalled_tail() {
  std::printf("\n--- negative control: a genuinely stalled tail is still NACKed ---\n");
  auto dgs = cap_paced_idr(6'000'000, 155u * 1024u);
  // Deliver only the first 60% of the datagrams, then stop (the tail never comes).
  const size_t keep = dgs.size() * 6 / 10;
  UdpH264FrameAssembler asm1;
  asm1.ConfigureInOrderHold(120000, 8);
  VideoNackScheduler sched;
  uint64_t tailNacks = 0;
  const uint64_t firstUs = dgs.front().emitUs;
  uint64_t lastEmit = firstUs;
  for (size_t i = 0; i < keep; ++i) {
    asm1.PushDatagram(dgs[i].bytes.data(), dgs[i].bytes.size(), dgs[i].emitUs);
    lastEmit = dgs[i].emitUs;
  }
  // Now poll with the clock advancing past the stall, well beyond tailGrace of no progress.
  for (int k = 1; k <= 12; ++k) {
    const uint64_t nowUs = lastEmit + static_cast<uint64_t>(k) * 25000;
    UdpVideoNackPacket nack{};
    if (sched.Poll(asm1, true, nowUs, &nack)) ++tailNacks;
  }
  check("a stalled tail (no progress for > tailGrace) IS asked for", tailNacks > 0,
        "tailNacks=" + std::to_string(tailNacks));
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("--- large IDR under the cap: progress-based tail rule holds (r2 fix) ---\n");
  measure(6'000'000, 155u * 1024u, "155KB@6Mbps");
  measure(6'000'000, 208u * 1024u, "208KB@6Mbps");
  measure(1'500'000, 155u * 1024u, "155KB@1.5Mbps");
  measure(1'500'000, 208u * 1024u, "208KB@1.5Mbps");
  measure_legacy_vs_fixed();
  measure_stalled_tail();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  // This test PASSES when it REPRODUCES the problem; it is evidence for the receiver fix decision.
  return g_failed ? 1 : 0;
}
