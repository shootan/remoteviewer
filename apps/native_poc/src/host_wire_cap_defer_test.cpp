// bitrate-hard-cap r3 F1 -- the host's sender-driven NACK replay under the hard cap, over loopback.
//
// r2 replayed on the READER thread with a single TryAcquire: under a full cap bucket that one spend
// failed, the request was erased, and the hole was lost for ever (Codex F1). r3 moves the replay to
// the SENDER thread: the reader only RECORDS the request (RecordReplayRequest, bounded + merged per
// (gen,seq) + deadline), and DrainPendingReplays retries it every loop through the SAME wire bucket
// until the chunks have ACTUALLY been sent, or the deadline passes, or the epoch ends. This test
// drives the real SenderState with the cap ON (StartWireCap -- the r2 test never called it, so it
// was really exercising the cap-OFF fallback and proved nothing about the capped path) over a real
// loopback socket, and the cap-OFF fallback bucket as a second leg.
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

// Drain and count datagrams on a non-blocking socket.
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
  return 0;  // 0 == not present or fully served
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
  const size_t bytes = 40u * 1024u;  // ~34 data chunks at mtu 1200
  std::vector<uint8_t> payload(bytes, 0xAB);
  const uint32_t mtu = 1200;
  // A deliberately low cap so the empty bucket cannot serve a chunk immediately: at 2 Mbps the bucket
  // refills ~250 B/ms, and one chunk is ~1228 B, so ~5 ms of refill is one chunk. The test waits in
  // real time for the bucket to earn the tokens, exactly as the product would.
  const uint64_t capBps = 2'000'000;

  std::printf("--- F1: sender-driven replay survives a full cap bucket and goes out as tokens refill ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    sender.StoreAu(gen, seq, base_header(seq, gen, bytes), mtu, true, payload.data(), payload.size());

    // Record a replay for 3 chunks, then drain with the bucket still empty: nothing fits now, but the
    // request is KEPT (this is the whole r3 F1 fix -- r2 would have erased it here and lost the hole).
    const uint16_t missing[] = {3, 4, 5};
    sender.RecordReplayRequest(gen, seq, missing, 3);  // recording never touches the wire
    sender.DrainPendingReplays(tx, addr, qpc_now_us(), sender.mediaSessionEpoch.load());
    const int immediate = drain(rx);
    check("an empty bucket serves 0 chunks but the request is NOT dropped", immediate == 0 &&
          pending_missing(sender, gen, seq) == 3 && sender.replayDroppedRequests.load() == 0,
          "sentNow=" + std::to_string(immediate) + " stillMissing=" + std::to_string(pending_missing(sender, gen, seq)));

    // Rounds exhausted: keep draining against the empty bucket; the request still survives (bounded by
    // deadline, not by a single short bucket). This is the "replay arrives even after rounds exhausted".
    for (int i = 0; i < 5; ++i) { sender.DrainPendingReplays(tx, addr, qpc_now_us(), sender.mediaSessionEpoch.load()); }
    check("repeated drains against a full bucket neither send nor drop the request", drain(rx) == 0 &&
          pending_missing(sender, gen, seq) == 3 && sender.replayDroppedRequests.load() == 0);

    // Let the bucket earn tokens and drain across several loops: the cap bucket depth B caps one
    // drain's burst, so a full recovery is RETRIED loop-to-loop as tokens refill until none remain.
    int served = 0;
    for (int i = 0; i < 40 && pending_count(sender) > 0; ++i) {
      Sleep(10);
      sender.DrainPendingReplays(tx, addr, qpc_now_us(), sender.mediaSessionEpoch.load());
      served += drain(rx);
    }
    check("across refill loops all 3 chunks are REALLY sent and the request is fully served (residual < 1 chunk)",
          served == 3 && pending_count(sender) == 0 && sender.replayServedChunks.load() == 3 &&
          sender.replayDroppedRequests.load() == 0,
          "served=" + std::to_string(served) + " pending=" + std::to_string(pending_count(sender)));
  }

  std::printf("--- F1: two chunks of the same FEC group are both replayed under the cap ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    sender.StoreAu(gen, seq, base_header(seq, gen, bytes), mtu, true, payload.data(), payload.size());
    const uint16_t group0[] = {0, 1};  // two data chunks in the first FEC group (group size 8)
    sender.RecordReplayRequest(gen, seq, group0, 2);
    int served = 0;
    for (int i = 0; i < 40 && pending_count(sender) > 0; ++i) {
      Sleep(10);
      sender.DrainPendingReplays(tx, addr, qpc_now_us(), sender.mediaSessionEpoch.load());
      served += drain(rx);
    }
    check("both same-group chunks are replayed (not just one)", served == 2 &&
          sender.replayServedChunks.load() == 2 && pending_count(sender) == 0,
          "served=" + std::to_string(served));
  }

  std::printf("--- F1: an uncached (still-sending) AU keeps the request until it is cached ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    const uint16_t missing[] = {2};
    sender.RecordReplayRequest(gen, seq, missing, 1);  // not cached yet
    Sleep(40);  // bucket has tokens, but there is nothing to copy
    sender.DrainPendingReplays(tx, addr, qpc_now_us(), sender.mediaSessionEpoch.load());
    check("a replay for a not-yet-cached AU sends nothing but is retained", drain(rx) == 0 &&
          pending_missing(sender, gen, seq) == 1);
    sender.StoreAu(gen, seq, base_header(seq, gen, bytes), mtu, true, payload.data(), payload.size());
    sender.DrainPendingReplays(tx, addr, qpc_now_us(), sender.mediaSessionEpoch.load());
    check("the same request is served the moment the AU is cached", drain(rx) == 1 &&
          pending_count(sender) == 0);
  }

  std::printf("--- F1: a request outlives its deadline and is dropped exactly once (client IDR recovers) ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/true);
    // Record with a createdUs far in the past by recording, then draining with a now past the deadline.
    const uint16_t missing[] = {1};
    sender.RecordReplayRequest(gen, seq, missing, 1);  // never cached -> only the deadline can end it
    const uint64_t pastDeadline = qpc_now_us() + SenderState::kReplayDeadlineUs + 1'000'000ULL;
    sender.DrainPendingReplays(tx, addr, pastDeadline, sender.mediaSessionEpoch.load());
    check("past its deadline the request is dropped, nothing sent", drain(rx) == 0 &&
          pending_count(sender) == 0 && sender.replayDroppedRequests.load() == 1);
    sender.DrainPendingReplays(tx, addr, pastDeadline + 1, sender.mediaSessionEpoch.load());
    check("the dropped request is counted exactly once", sender.replayDroppedRequests.load() == 1);
  }

  std::printf("--- F1: the pending list is bounded and merges per (gen,seq) ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    const uint16_t m1[] = {1};
    for (uint32_t s = 0; s < 20; ++s) sender.RecordReplayRequest(1, s, m1, 1);  // 20 distinct AUs
    check("the pending list is bounded (<= kMaxPendingReplays)", pending_count(sender) <= SenderState::kMaxPendingReplays,
          "pending=" + std::to_string(pending_count(sender)));
    const uint16_t a[] = {2, 3};
    const uint16_t b[] = {3, 9};
    sender.RecordReplayRequest(2, 500, a, 2);
    sender.RecordReplayRequest(2, 500, b, 2);
    check("repeated misses for one AU merge into a single entry (union {2,3,9})",
          pending_missing(sender, 2, 500) == 3,
          "size=" + std::to_string(pending_missing(sender, 2, 500)));
  }

  std::printf("--- F1 (cap OFF): the fallback bucket path still replays a cached AU ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    sender.StartWireCap(capBps, mtu, /*enabled=*/false);  // cap OFF -> NackFallbackTryAcquire path
    sender.StoreAu(gen, seq, base_header(seq, gen, bytes), mtu, true, payload.data(), payload.size());
    const uint16_t missing[] = {3, 4, 5};
    sender.RecordReplayRequest(gen, seq, missing, 3);
    sender.DrainPendingReplays(tx, addr, qpc_now_us(), sender.mediaSessionEpoch.load());
    const int served = drain(rx);
    check("with the cap off the fallback bucket replays the 3 chunks", served == 3 &&
          sender.replayServedChunks.load() == 3,
          "served=" + std::to_string(served));
  }

  closesocket(rx);
  closesocket(tx);
  WSACleanup();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
