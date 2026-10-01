// bitrate-hard-cap r2 -- the host's deferred-NACK recovery for a hole requested during send.
//
// StoreAu caches an AU only AFTER the whole AU is on the wire, so a NACK that arrives while a
// cap-paced AU is still being sent misses the cache. Before r2 that miss was dropped and the hole
// was never replayed; now RetransmitAu DEFERS the miss (bounded, merged per (gen,seq)) and the
// sender serves it the instant StoreAu caches the AU -- one post-send recovery. This drives the real
// SenderState (DeferNack / StoreAu / ServeDeferredNacks / RetransmitAu) over a real loopback socket.
// Tag: network (loopback).

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

// Count datagrams waiting on a non-blocking socket, draining them.
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
  ioctlsocket(rx, FIONBIO, &nb);  // non-blocking recv for drain()

  std::printf("--- host deferred-NACK recovery (hole requested during send) ---\n");
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    const uint64_t gen = 7;
    const uint32_t seq = 100;
    const size_t bytes = 40u * 1024u;  // ~34 chunks at mtu 1200
    std::vector<uint8_t> payload(bytes, 0xAB);
    const uint32_t mtu = 1200;

    // 1) A NACK arrives WHILE the AU is still sending: not yet cached -> deferred, nothing replayed.
    const uint16_t missing[] = {3, 4, 5};
    sender.RetransmitAu(tx, addr, gen, seq, missing, 3);
    Sleep(30);
    const int duringSend = drain(rx);
    check("a NACK during send is not replayed (cache miss) -- it is deferred", duringSend == 0,
          "replayed=" + std::to_string(duringSend) + " nackMisses=" + std::to_string(sender.nackMisses.load()));

    // 2) The AU finishes sending and is cached; the sender serves the deferred NACK once.
    sender.StoreAu(gen, seq, base_header(seq, gen, bytes), mtu, true, payload.data(), payload.size());
    sender.ServeDeferredNacks(tx, addr, gen, seq);
    Sleep(30);
    const int served = drain(rx);
    check("the deferred NACK is served the moment the AU is cached (3 chunks replayed)", served == 3,
          "replayed=" + std::to_string(served));

    // 3) Serving again does nothing (the deferred entry was consumed).
    sender.ServeDeferredNacks(tx, addr, gen, seq);
    Sleep(20);
    check("a deferred NACK is served exactly once", drain(rx) == 0);

    // 4) A NACK for an AU already cached replays immediately (the ordinary hit path still works).
    sender.RetransmitAu(tx, addr, gen, seq, missing, 3);
    Sleep(30);
    check("a NACK for a cached AU replays at once (ordinary hit)", drain(rx) == 3);
  }

  // 5) The deferred buffer is bounded and merges per (gen,seq): many distinct AUs drop the oldest;
  //    repeated misses for one AU merge rather than grow.
  {
    SenderState sender;
    sender.nackEnabled.store(true, std::memory_order_relaxed);
    const uint16_t m1[] = {1};
    // 20 distinct uncached AUs -> only the last kMaxPendingNacks (8) survive.
    for (uint32_t s = 0; s < 20; ++s) sender.RetransmitAu(tx, addr, 1, s, m1, 1);
    size_t pending = 0;
    {
      std::lock_guard<std::mutex> lk(sender.pendingNackMu);
      pending = sender.pendingNacks.size();
    }
    check("the deferred buffer is bounded (<= kMaxPendingNacks)", pending <= SenderState::kMaxPendingNacks,
          "pending=" + std::to_string(pending));
    // Repeated misses for ONE (gen,seq) merge into one entry with the union of indices.
    const uint16_t a[] = {2, 3};
    const uint16_t b[] = {3, 9};
    sender.RetransmitAu(tx, addr, 2, 500, a, 2);
    sender.RetransmitAu(tx, addr, 2, 500, b, 2);
    size_t entrySize = 0, entries = 0;
    {
      std::lock_guard<std::mutex> lk(sender.pendingNackMu);
      for (const auto& p : sender.pendingNacks)
        if (p.generation == 2 && p.seq == 500) {
          ++entries;
          entrySize = p.missing.size();
        }
    }
    check("repeated misses for one AU merge into a single entry (union {2,3,9})", entries == 1 && entrySize == 3,
          "entries=" + std::to_string(entries) + " size=" + std::to_string(entrySize));
  }

  closesocket(rx);
  closesocket(tx);
  WSACleanup();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
