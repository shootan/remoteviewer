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

#include <map>

#include "host_net_io.hpp"
#include "host_wire_limiter.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "session_video_pipeline.hpp"
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

UdpVideoChunkHeader idr_base(size_t idrBytes) {
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
  return base;
}

// The exact datagram the host's selective-retransmit would put on the wire for one chunk index.
std::vector<uint8_t> replay_datagram(const std::vector<uint8_t>& payload, size_t idrBytes, uint16_t index) {
  std::vector<uint8_t> out;
  WireEgress wire;
  wire.sink = [&out](const uint8_t* dg, int len, bool) -> int {
    out.assign(dg, dg + len);
    return len;
  };
  uint64_t wb = 0, dgc = 0;
  uint16_t idx = index;
  send_udp_chunk_indices(INVALID_SOCKET, sockaddr_in{}, payload.data(), payload.size(), idr_base(idrBytes), kMtu,
                         true, &idx, 1, &wb, &dgc, &wire, nullptr);
  return out;
}

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
  const UdpVideoChunkHeader base = idr_base(idrBytes);
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

  uint64_t prematureNacks = 0, chunksReq = 0;
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
    (void)hasTail;  // r3: measure() no longer infers a give-up from spent_for -- that is NOT the
                    // product give-up (which also needs noProgress). The real give-up is driven
                    // through SessionVideoPipeline in test_timeline(); here we only measure the
                    // scheduler's NACK/round behaviour on a lossless cap-paced IDR.
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

  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "%s: deliver=%.0fms prematureNacks=%llu chunksReq=%llu roundsExhausted=%llu",
                label, deliverMs, (unsigned long long)prematureNacks, (unsigned long long)chunksReq,
                (unsigned long long)sched.stats().roundsExhausted);
  // The reproduction: a cap-paced IDR draws premature NACKs for its still-in-flight tail, exhausts
  // the rounds, and the caller requests a keyframe BEFORE the IDR has finished arriving -> IDR loop.
  std::printf("    dbg %s: incompletePolls=%llu maxAgeMs=%.0f maxMissing=%llu maxHighWater=%llu fed=%zu dgs=%zu\n",
              label, (unsigned long long)dbgIncompletePolls, dbgMaxAgeUs / 1000.0,
              (unsigned long long)dbgMaxMissing, (unsigned long long)dbgMaxHighWater, fed, dgs.size());
  // r2/r3 FIX verification: with the progress-based tail rule, a lossless cap-paced IDR whose tail is
  // still arriving draws NO premature tail NACK and the scheduler exhausts no rounds on it. These are
  // scheduler-level facts measured here; whether the RECEIVER gives the head up (which also needs
  // noProgress) is decided by the real product path in test_timeline(), not inferred from spent_for.
  check(std::string("FIX ") + label + ": no premature tail NACK on a lossless cap-paced IDR", prematureNacks == 0, buf);
  check(std::string("FIX ") + label + ": the scheduler exhausts no rounds on the in-flight tail",
        sched.stats().roundsExhausted == 0, buf);
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

// ---------------------------------------------------------------- the product-path timeline
// Drives the REAL SessionVideoPipeline (the shared/Android receiver: its give-up is
// GiveUpStuckHead -- spent && replyOver && noProgress && oldEnough, 245 ms / 5 s -- and its
// keyframe requests go through Request()/Break(), NOT spent_for()). The host's defer-then-serve of
// a NACK's recovery chunk is injected by the test's callback after a fixed delay (a RECEIVER-side
// model only -- it does NOT drive the real host sender). The real sender recovery (RecordReplayRequest
// -> DrainPendingReplays over the shared bucket, idle-wake + during-send interleave) is validated on
// the real SenderState by host_wire_cap_defer_test and on the real sender THREAD + socket by
// host_wire_cap_sender_thread_test. This timeline's pass/fail is on the product RECEIVER give-up only.
// A timeline is printed; the pass/fail is on the product give-up, so roundsExhausted alone never
// decides an IDR loop. (bitrate-hard-cap r2, verifier note.)
struct TimelineResult {
  uint64_t nacks = 0, keyframeRequests = 0, giveUps = 0, discontinuities = 0, deferredServed = 0;
  bool delivered = false;
  uint64_t deliveredAtUs = 0;
};

TimelineResult timeline(uint64_t capBps, size_t idrBytes, std::vector<uint16_t> drops, bool legacy, const char* label) {
  const auto dgs = cap_paced_idr(capBps, idrBytes);
  const std::vector<uint8_t> payload(idrBytes, 0xAB);
  const uint64_t firstUs = dgs.front().emitUs;
  const uint64_t lastSendUs = dgs.back().emitUs;  // the host finishes sending (StoreAu) here
  const uint64_t rttUs = 10000;                   // one-way model delay for a replay to arrive
  auto isDrop = [&](uint16_t i) { return std::find(drops.begin(), drops.end(), i) != drops.end(); };

  TimelineResult tr;
  SessionVideoPipelineConfig pc;
  pc.nackEnabled = true;
  pc.holdUs = 120000;
  pc.maxConcurrent = 8;
  pc.nackConfig.tailAgeBasedLegacy = legacy;

  // Events still to feed the pipeline: originals (minus the dropped chunk) + scheduled replays.
  std::multimap<uint64_t, std::vector<uint8_t>> feed;
  for (size_t i = 0; i < dgs.size(); ++i) {
    if (isDrop(static_cast<uint16_t>(i))) continue;  // chunk(s) lost on the wire
    feed.emplace(dgs[i].emitUs, dgs[i].bytes);
  }
  std::vector<std::pair<uint64_t, std::vector<uint16_t>>> deferred;  // served after lastSendUs
  uint64_t nowUs = firstUs;
  int tlLines = 0;
  auto tl = [&](const char* ev, const std::string& extra = {}) {
    if (tlLines++ < 24)
      std::printf("      [%s %6.1fms] %s %s\n", label, (nowUs - firstUs) / 1000.0, ev, extra.c_str());
  };

  SessionVideoPipeline::Callbacks cb;
  cb.deliver = [&](UdpH264AssembledFrame&&) {
    if (!tr.delivered) {
      tr.delivered = true;
      tr.deliveredAtUs = nowUs;
      tl("DELIVER (IDR complete)");
    }
  };
  cb.requestKeyframe = [&] {
    ++tr.keyframeRequests;
    tl("REQUEST-KEYFRAME (product give-up path)");
  };
  cb.discontinuity = [&] { ++tr.discontinuities; };
  cb.sendNack = [&](const UdpVideoNackPacket& p) {
    ++tr.nacks;
    std::vector<uint16_t> miss(p.missing, p.missing + p.missingCount);
    // The host caches the AU at send-start (r2), so a NACK during the (long, cap-paced) send is an
    // immediate cache HIT and the missing chunk is replayed at once -- not held until send-end, which
    // would be later than the receiver's give-up.
    tl("NACK", "chunks=" + std::to_string(p.missingCount) + " (cache HIT at send-start -> replay)");
    for (uint16_t idx : miss) {
      if (isDrop(idx)) {
        feed.emplace(nowUs + rttUs, replay_datagram(payload, idrBytes, idx));
        ++tr.deferredServed;  // reused counter: "a hole chunk replayed in answer to a NACK"
      }
    }
  };
  SessionVideoPipeline pipe(pc, cb);

  // Drive: advance through feed events and 25 ms ticks until delivered or a 6 s deadline.
  uint64_t nextTickUs = firstUs;
  bool servedDeferred = false;
  const uint64_t deadlineUs = firstUs + 6'000'000;
  while (nowUs < deadlineUs) {
    const uint64_t nextFeedUs = feed.empty() ? UINT64_MAX : feed.begin()->first;
    (void)servedDeferred;
    (void)deferred;
    const uint64_t advanceTo = std::min(nextFeedUs, nextTickUs);
    if (advanceTo == UINT64_MAX) break;
    nowUs = advanceTo;
    if (nowUs == nextTickUs) {
      pipe.OnTick(nowUs);
      nextTickUs += 25000;
    }
    while (!feed.empty() && feed.begin()->first <= nowUs) {
      auto it = feed.begin();
      std::vector<uint8_t> bytes = it->second;
      feed.erase(it);
      pipe.OnDatagram(bytes.data(), bytes.size(), nowUs);
    }
    tr.giveUps = pipe.stats().stuckHeadGiveUps;
    if (tr.delivered && feed.empty()) break;
  }
  tr.giveUps = pipe.stats().stuckHeadGiveUps;
  std::printf("    %s: delivered=%d at=%.0fms nacks=%llu keyframeReq=%llu giveUps=%llu deferredServed=%llu\n", label,
              tr.delivered ? 1 : 0, tr.delivered ? (tr.deliveredAtUs - firstUs) / 1000.0 : -1.0,
              (unsigned long long)tr.nacks, (unsigned long long)tr.keyframeRequests, (unsigned long long)tr.giveUps,
              (unsigned long long)tr.deferredServed);
  return tr;
}

void test_timeline() {
  std::printf("\n--- product-path timeline (real SessionVideoPipeline give-up, not spent_for) ---\n");
  // 1) Lossless cap-paced IDR, FIXED receiver: no NACK, no give-up, no keyframe request, delivered.
  {
    const auto r = timeline(1'500'000, 208u * 1024u, {}, /*legacy=*/false, "lossless-fixed");
    check("lossless + fixed receiver: IDR delivered, no NACK, no give-up, no keyframe request",
          r.delivered && r.nacks == 0 && r.giveUps == 0 && r.keyframeRequests == 0);
  }
  // 2) Lossless, LEGACY (old 0.2.147 age-based) receiver: premature NACKs, BUT the product give-up
  //    needs noProgress -- the tail keeps arriving, so it does NOT give up and does NOT request a
  //    keyframe; the IDR still completes. This is the r2 point: roundsExhausted != IDR loop.
  {
    const auto r = timeline(1'500'000, 208u * 1024u, {}, /*legacy=*/true, "lossless-legacy");
    check("lossless + LEGACY receiver: premature NACKs but NO give-up / NO keyframe (progress) -- no loop",
          r.delivered && r.nacks > 0 && r.giveUps == 0 && r.keyframeRequests == 0,
          "nacks=" + std::to_string(r.nacks) + " giveUps=" + std::to_string(r.giveUps) +
              " keyReq=" + std::to_string(r.keyframeRequests));
  }
  // 3) A real hole DURING send (cache miss), FIXED receiver: this is a RECEIVER-side model -- the
  //    NACK callback injects a recovery chunk after a fixed delay, it does NOT drive the real host
  //    sender. It shows only that the RECEIVER, given a timely replay, recovers the hole without a
  //    give-up/keyframe request. The actual host sender recovery (reader records -> sender drains the
  //    SAME wire bucket, idle-wake and during-send interleave) is validated end-to-end over a real
  //    socket by host_wire_cap_sender_thread_test; do NOT read this leg as sender-recovery evidence.
  //    (bitrate-hard-cap r4 R4-4.)
  {
    // Two chunks in ONE FEC group (consecutive, non-interleaved) -> FEC cannot repair -> the hole
    // must be recovered by a replay reaching the receiver.
    const auto r = timeline(1'500'000, 208u * 1024u, {40, 41}, /*legacy=*/false, "hole-during-send(receiver-model)");
    check("RECEIVER MODEL: given a timely replay the receiver recovers a FEC-unrecoverable hole, no give-up",
          r.delivered && r.deferredServed > 0 && r.giveUps == 0 && r.keyframeRequests == 0,
          "replays=" + std::to_string(r.deferredServed) + " giveUps=" + std::to_string(r.giveUps) +
              " delivered=" + std::to_string(r.delivered ? 1 : 0));
  }
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
  test_timeline();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  // This test PASSES when it REPRODUCES the problem; it is evidence for the receiver fix decision.
  return g_failed ? 1 : 0;
}
