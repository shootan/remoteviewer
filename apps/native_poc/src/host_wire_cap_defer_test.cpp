// bitrate-hard-cap r3 F1 / r4 R1+R2 -- the host's sender-driven NACK replay under the hard cap, over
// loopback. This drives the real SenderState functions (RecordReplayRequest / DrainPendingReplays /
// StoreAu / MarkAuStartedOnWire / ClearReplayStateForRollover) with the cap ON. The wake-up and
// long-AU interleave behaviour of the REAL sender thread is covered separately by
// host_wire_cap_sender_thread_test; this file pins the replay bookkeeping, the deadline/merge/bound,
// the cap-off fallback, and the r4 R2 session-ownership fences.
// Tag: network (loopback), pure-logic (bucket timing uses the real qpc clock).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "host_encoded_sender.hpp"
#include "host_net_io.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

using namespace remote60::native_poc;

namespace {
int g_checks = 0, g_failed = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

UdpVideoChunkHeader base_header(uint32_t seq, uint64_t gen, size_t payloadSize) {
  UdpVideoChunkHeader h{};
  h.magic = kMagic;
  h.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  h.size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  h.seq = seq;
  h.codec = static_cast<uint16_t>(UdpCodec::H264);
  h.flags = 0;
  h.width = 1920;
  h.height = 1080;
  h.payloadSize = static_cast<uint32_t>(payloadSize);
  h.streamGeneration = gen;
  return h;
}

int drain(SOCKET s) {
  char buf[2048];
  int n = 0;
  for (;;) {
    const int r = recv(s, buf, sizeof(buf), 0);
    if (r <= 0) break;
    ++n;
  }
  return n;
}

size_t pending_count(SenderState& s) {
  std::lock_guard<std::mutex> lk(s.pendingReplayMu);
  return s.pendingReplays.size();
}
size_t pending_missing(SenderState& s, uint64_t gen, uint32_t seq) {
  std::lock_guard<std::mutex> lk(s.pendingReplayMu);
  for (const auto& p : s.pendingReplays)
    if (p.generation == gen && p.seq == seq) return p.missing.size();
  return 0;
}

// Store a cached AU for THIS session and mark it started on the wire (what a real send does), so it is
// eligible for replay (r4 R2 first-packet fence).
void store_started(SenderState& s, uint64_t gen, uint32_t seq, uint64_t mediaEpoch, const sockaddr_in& peer,
                   const std::vector<uint8_t>& payload, uint32_t mtu, uint64_t inputEpoch = 0) {
  s.StoreAu(gen, seq, base_header(seq, gen, payload.size()), mtu, true, mediaEpoch, inputEpoch, peer,
            payload.data(), payload.size());
  s.MarkAuStartedOnWire(gen, seq);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  SOCKET rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  SOCKET tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(rx, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  int alen = sizeof(addr);
  getsockname(rx, reinterpret_cast<sockaddr*>(&addr), &alen);
  u_long nb = 1;
  ioctlsocket(rx, FIONBIO, &nb);

  const uint64_t gen = 7;
  const uint32_t seq = 100;
  const size_t bytes = 40u * 1024u;
  std::vector<uint8_t> payload(bytes, 0xAB);
  const uint32_t mtu = 1200;
  const uint64_t capBps = 2'000'000;
  const uint64_t kEpoch = 1;  // SenderState default mediaSessionEpoch

  std::printf("--- F1: replay survives a full cap bucket and goes out as tokens refill ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    store_started(sender, gen, seq, kEpoch, addr, payload, mtu);

    const uint16_t missing[] = {3, 4, 5};
    sender.RecordReplayRequest(gen, seq, addr, kEpoch, missing, 3);
    sender.DrainPendingReplays(tx, addr, qpc_now_us(), kEpoch);
    const int immediate = drain(rx);
    check("an empty bucket serves 0 chunks but the request is NOT dropped", immediate == 0 &&
          pending_missing(sender, gen, seq) == 3 && sender.replayDroppedRequests.load() == 0,
          "sentNow=" + std::to_string(immediate) + " stillMissing=" + std::to_string(pending_missing(sender, gen, seq)));

    for (int i = 0; i < 5; ++i) sender.DrainPendingReplays(tx, addr, qpc_now_us(), kEpoch);
    check("repeated drains against a full bucket neither send nor drop the request", drain(rx) == 0 &&
          pending_missing(sender, gen, seq) == 3 && sender.replayDroppedRequests.load() == 0);

    int served = 0;
    for (int i = 0; i < 40 && pending_count(sender) > 0; ++i) {
      Sleep(10);
      sender.DrainPendingReplays(tx, addr, qpc_now_us(), kEpoch);
      served += drain(rx);
    }
    check("across refill loops all 3 chunks are REALLY sent and fully served (residual < 1 chunk)",
          served == 3 && pending_count(sender) == 0 && sender.replayServedChunks.load() == 3 &&
          sender.replayDroppedRequests.load() == 0,
          "served=" + std::to_string(served) + " pending=" + std::to_string(pending_count(sender)));
  }

  std::printf("--- F1: two chunks of the same FEC group are both replayed under the cap ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    store_started(sender, gen, seq, kEpoch, addr, payload, mtu);
    const uint16_t group0[] = {0, 1};
    sender.RecordReplayRequest(gen, seq, addr, kEpoch, group0, 2);
    int served = 0;
    for (int i = 0; i < 40 && pending_count(sender) > 0; ++i) {
      Sleep(10);
      sender.DrainPendingReplays(tx, addr, qpc_now_us(), kEpoch);
      served += drain(rx);
    }
    check("both same-group chunks are replayed (not just one)", served == 2 &&
          sender.replayServedChunks.load() == 2 && pending_count(sender) == 0,
          "served=" + std::to_string(served));
  }

  std::printf("--- F1: an uncached (still-sending) AU keeps the request until it is cached+started ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    const uint16_t missing[] = {2};
    sender.RecordReplayRequest(gen, seq, addr, kEpoch, missing, 1);  // not cached yet
    Sleep(40);
    sender.DrainPendingReplays(tx, addr, qpc_now_us(), kEpoch);
    check("a replay for a not-yet-cached AU sends nothing but is retained", drain(rx) == 0 &&
          pending_missing(sender, gen, seq) == 1);
    store_started(sender, gen, seq, kEpoch, addr, payload, mtu);
    sender.DrainPendingReplays(tx, addr, qpc_now_us(), kEpoch);
    check("the same request is served the moment the AU is cached+started", drain(rx) == 1 &&
          pending_count(sender) == 0);
  }

  std::printf("--- F1: a request outlives its deadline and is dropped exactly once ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    const uint16_t missing[] = {1};
    sender.RecordReplayRequest(gen, seq, addr, kEpoch, missing, 1);  // never cached -> deadline ends it
    const uint64_t pastDeadline = qpc_now_us() + SenderState::kReplayDeadlineUs + 1'000'000ULL;
    sender.DrainPendingReplays(tx, addr, pastDeadline, kEpoch);
    check("past its deadline the request is dropped, nothing sent", drain(rx) == 0 &&
          pending_count(sender) == 0 && sender.replayDroppedRequests.load() == 1);
    sender.DrainPendingReplays(tx, addr, pastDeadline + 1, kEpoch);
    check("the dropped request is counted exactly once", sender.replayDroppedRequests.load() == 1);
  }

  std::printf("--- F1: the pending list is bounded and merges per (gen,seq) ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    const uint16_t m1[] = {1};
    for (uint32_t s = 0; s < 20; ++s) sender.RecordReplayRequest(1, s, addr, kEpoch, m1, 1);
    check("the pending list is bounded (<= kMaxPendingReplays)", pending_count(sender) <= SenderState::kMaxPendingReplays,
          "pending=" + std::to_string(pending_count(sender)));
    const uint16_t a[] = {2, 3};
    const uint16_t b[] = {3, 9};
    sender.RecordReplayRequest(2, 500, addr, kEpoch, a, 2);
    sender.RecordReplayRequest(2, 500, addr, kEpoch, b, 2);
    check("repeated misses for one AU merge into a single entry (union {2,3,9})",
          pending_missing(sender, 2, 500) == 3, "size=" + std::to_string(pending_missing(sender, 2, 500)));
  }

  std::printf("--- F1 (cap OFF): the fallback bucket path still replays a cached+started AU ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/false);
    store_started(sender, gen, seq, kEpoch, addr, payload, mtu);
    const uint16_t missing[] = {3, 4, 5};
    sender.RecordReplayRequest(gen, seq, addr, kEpoch, missing, 3);
    sender.DrainPendingReplays(tx, addr, qpc_now_us(), kEpoch);
    const int served = drain(rx);
    check("with the cap off the fallback bucket replays the 3 chunks", served == 3 &&
          sender.replayServedChunks.load() == 3, "served=" + std::to_string(served));
  }

  // ----------------------------------------------------------------- r4 R2 session-ownership fences
  std::printf("--- R2: an AU that never put a packet on the wire (startedOnWire=false) is NOT replayed ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    // Cache at send-start but DO NOT mark started (the F3 fence aborted the original at 0 datagrams).
    sender.StoreAu(gen, seq, base_header(seq, gen, bytes), mtu, true, kEpoch, /*inputEpoch=*/0, addr, payload.data(), payload.size());
    const uint16_t missing[] = {0};
    sender.RecordReplayRequest(gen, seq, addr, kEpoch, missing, 1);
    for (int i = 0; i < 20; ++i) { Sleep(5); sender.DrainPendingReplays(tx, addr, qpc_now_us(), kEpoch); }
    check("NEGATIVE: an unstarted (aborted) AU is never replayed", drain(rx) == 0 &&
          sender.replayServedChunks.load() == 0,
          "pending=" + std::to_string(pending_count(sender)));
    // Now it actually starts -> the same request can be served.
    sender.MarkAuStartedOnWire(gen, seq);
    for (int i = 0; i < 40 && pending_count(sender) > 0; ++i) { Sleep(10); sender.DrainPendingReplays(tx, addr, qpc_now_us(), kEpoch); }
    check("POSITIVE: once the AU has started on the wire the same chunk replays", drain(rx) == 1 &&
          sender.replayServedChunks.load() == 1);
  }

  std::printf("--- R2: a rollover (A->B) drops A's cache+pending so nothing of A reaches B ---\n");
  {
    SenderState senderB;  // model peer B
    SenderState sender;
    (void)senderB;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    const uint64_t epochA = sender.mediaSessionEpoch.load();
    store_started(sender, gen, seq, epochA, addr, payload, mtu);
    const uint16_t missing[] = {4, 5};
    sender.RecordReplayRequest(gen, seq, addr, epochA, missing, 2);  // A's NACK, tokens not yet available
    sender.DrainPendingReplays(tx, addr, qpc_now_us(), epochA);  // empty bucket -> nothing out, kept
    check("A's replay is pending before the rollover", pending_count(sender) == 1);
    // Rollover: the sender's barrier clears A's cache + pending (ClearReplayStateForRollover).
    sender.ClearReplayStateForRollover();
    const uint64_t epochB = epochA + 1;  // the new session's epoch
    (void)drain(rx);  // clear anything already on the wire
    for (int i = 0; i < 20; ++i) { Sleep(5); sender.DrainPendingReplays(tx, addr, qpc_now_us(), epochB); }
    check("after the rollover A's pending/cache are gone -> 0 datagrams to the new session", drain(rx) == 0 &&
          pending_count(sender) == 0 && sender.replayServedChunks.load() == 0);
  }

  std::printf("--- R2: a request of a finished epoch is dropped, not replayed to the new session ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    const uint64_t epochA = sender.mediaSessionEpoch.load();
    store_started(sender, gen, seq, epochA, addr, payload, mtu);
    const uint16_t missing[] = {6};
    sender.RecordReplayRequest(gen, seq, addr, epochA, missing, 1);
    // The sender now serves a DIFFERENT (newer) epoch: the A-epoch request must be dropped on sight.
    const uint64_t epochB = epochA + 1;
    for (int i = 0; i < 10 && pending_count(sender) > 0; ++i) { Sleep(5); sender.DrainPendingReplays(tx, addr, qpc_now_us(), epochB); }
    check("a past-epoch request is dropped without replay", drain(rx) == 0 && pending_count(sender) == 0 &&
          sender.replayServedChunks.load() == 0);
  }

  std::printf("--- H2: an input-fenced head request is retired at once and does NOT block a valid tail ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    std::atomic<uint64_t> liveInput{8};  // an input flush has moved the live input epoch to 8
    sender.inputEpochRef = &liveInput;
    const uint64_t ep = sender.mediaSessionEpoch.load();
    // P_old: a started AU of the OLD input epoch 7 (now fenced); P_new: a started AU of the live epoch 8.
    const uint32_t seqOld = 100, seqNew = 200;
    store_started(sender, gen, seqOld, ep, addr, payload, mtu, /*inputEpoch=*/7);
    store_started(sender, gen, seqNew, ep, addr, payload, mtu, /*inputEpoch=*/8);
    const uint16_t m1[] = {0};
    sender.RecordReplayRequest(gen, seqOld, addr, ep, m1, 1);  // recorded FIRST -> at the head
    sender.RecordReplayRequest(gen, seqNew, addr, ep, m1, 1);
    int served = 0;
    for (int i = 0; i < 40 && pending_count(sender) > 0; ++i) {
      Sleep(10);
      sender.DrainPendingReplays(tx, addr, qpc_now_us(), ep);
      served += drain(rx);
    }
    check("H2: the fenced old-input head is retired and the new-input tail IS replayed (not blocked)",
          served == 1 && sender.replayServedChunks.load() == 1 && pending_count(sender) == 0,
          "served=" + std::to_string(served) + " pending=" + std::to_string(pending_count(sender)));
    sender.inputEpochRef = nullptr;
  }

  std::printf("--- H2: the live fence applies with the cap OFF too (kill-switch) ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/false);  // cap OFF -> fallback bucket, but fence still wired
    std::atomic<uint64_t> liveInput{9};
    sender.inputEpochRef = &liveInput;
    const uint64_t ep = sender.mediaSessionEpoch.load();
    store_started(sender, gen, seq, ep, addr, payload, mtu, /*inputEpoch=*/8);  // stale input epoch
    const uint16_t m1[] = {0};
    sender.RecordReplayRequest(gen, seq, addr, ep, m1, 1);
    for (int i = 0; i < 10 && pending_count(sender) > 0; ++i) { Sleep(5); sender.DrainPendingReplays(tx, addr, qpc_now_us(), ep); }
    check("H2 cap-OFF: a flushed AU's replay is fenced and retired (not sent)", drain(rx) == 0 &&
          sender.replayServedChunks.load() == 0 && pending_count(sender) == 0);
    // NEGATIVE control: with the live input epoch matching, the same cap-OFF replay DOES go out.
    SenderState ok;
    ok.nackEnabled.store(true, std::memory_order_relaxed);
    ok.StartWireCap(capBps, mtu, /*enabled=*/false);
    std::atomic<uint64_t> liveInput2{8};
    ok.inputEpochRef = &liveInput2;
    const uint64_t ep2 = ok.mediaSessionEpoch.load();
    store_started(ok, gen, seq, ep2, addr, payload, mtu, /*inputEpoch=*/8);  // matches live
    ok.RecordReplayRequest(gen, seq, addr, ep2, m1, 1);
    ok.DrainPendingReplays(tx, addr, qpc_now_us(), ep2);
    check("H2 cap-OFF negative control: a current-epoch replay still goes out", drain(rx) == 1 &&
          ok.replayServedChunks.load() == 1);
    ok.inputEpochRef = nullptr;
    sender.inputEpochRef = nullptr;
  }

  closesocket(rx);
  closesocket(tx);
  WSACleanup();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
