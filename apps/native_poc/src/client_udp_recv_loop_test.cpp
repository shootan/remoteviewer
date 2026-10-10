// udp-recv-exit r1/r2: the headline defect was the SHARED client receive loop ending the whole session
// (`udp video receive failed`) on a legal empty UDP datagram, a truncated oversize one, or a Windows
// ICMP-driven reset. This drives the REAL ClientSessionController::VideoReceiveMain loop and checks the
// MANDATORY behaviours (Codex r2 U1): over a REAL loopback socket an empty and an oversize datagram are
// survived and a following VALID key frame is actually DELIVERED to the sink; a scripted reset is
// survived and the following key frame is delivered (real Windows ICMP reproduction is noted separately);
// an external Stop breaks a blocked loop; a genuine terminal error ends the session; a reset flood is
// rate-bounded. Tag: network (loopback), deterministic recv seam for the non-sendable cases.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "native_video_client_session.hpp"
#include "native_video_client_shared_core.hpp"
#include "poc_protocol.hpp"

using namespace remote60::native_poc;

namespace {
int gFailed = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  if (!ok) ++gFailed;
}

class MinimalSink : public ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(UdpH264AssembledFrame&&) override { frames_.fetch_add(1, std::memory_order_relaxed); }
  void OnVideoStreamReset() override {}
  void OnVideoDiscontinuity() override { discontinuities_.fetch_add(1, std::memory_order_relaxed); }
  uint64_t frames() const { return frames_.load(std::memory_order_relaxed); }
  uint64_t discontinuities() const { return discontinuities_.load(std::memory_order_relaxed); }
 private:
  std::atomic<uint64_t> frames_{0};
  std::atomic<uint64_t> discontinuities_{0};
};

// First chunk of a TWO-chunk key AU: it is held incomplete (owes a NACK), so repeated drops must drive
// the maintenance tick without ever delivering it. Returns the datagram length.
size_t build_incomplete_key_first_chunk(uint8_t* buf, size_t cap, uint32_t seq) {
  const uint32_t chunkBytes = 24;
  const size_t total = sizeof(UdpVideoChunkHeader) + chunkBytes;
  if (cap < total) return 0;
  UdpVideoChunkHeader h{};
  h.seq = seq;
  h.flags = 0x1u | 0x2u;  // key | firstChunk (NOT lastChunk -> incomplete)
  h.width = 64; h.height = 64; h.stride = 64;
  h.payloadSize = chunkBytes * 2;  // the AU is two chunks; only chunk 0 arrives
  h.chunkOffset = 0; h.chunkSize = chunkBytes; h.chunkIndex = 0; h.chunkCount = 2; h.chunkStride = chunkBytes;
  std::memcpy(buf, &h, sizeof(h));
  std::memset(buf + sizeof(h), 0x42, chunkBytes);
  return total;
}

// A single-chunk H.264 key AU the assembler completes and delivers immediately: key|first|last, one
// chunk that is the whole payload. Returns the datagram length.
size_t build_key_frame(uint8_t* buf, size_t cap, uint32_t seq) {
  const uint32_t payload = 24;
  const size_t total = sizeof(UdpVideoChunkHeader) + payload;
  if (cap < total) return 0;
  UdpVideoChunkHeader h{};  // defaults set magic/kind/size/codec
  h.seq = seq;
  h.flags = 0x1u | 0x2u | 0x4u;  // key | firstChunk | lastChunk
  h.width = 64; h.height = 64; h.stride = 64;
  h.payloadSize = payload;
  h.chunkOffset = 0; h.chunkSize = payload; h.chunkIndex = 0; h.chunkCount = 1; h.chunkStride = payload;
  std::memcpy(buf, &h, sizeof(h));
  std::memset(buf + sizeof(h), 0x41, payload);  // opaque H264 bytes (the sink does not decode here)
  return total;
}

SOCKET make_loopback_udp(sockaddr_in* boundAddr = nullptr) {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == INVALID_SOCKET) return s;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = 0;
  bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  if (boundAddr) {
    int len = sizeof(*boundAddr);
    getsockname(s, reinterpret_cast<sockaddr*>(boundAddr), &len);
  }
  return s;
}

template <typename Fn>
bool wait_until(Fn&& fn, int ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (fn()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return fn();
}
}  // namespace

int main() {
  std::printf("client_udp_recv_loop_test\n");
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa)) { std::printf("WSAStartup failed\n"); return 1; }

  // --- A (U1.1/U1.2/U1.4): REAL loopback recv -- empty + oversize datagrams are survived and a following
  //     VALID key frame is actually DELIVERED to the sink; an external Stop then breaks the blocked loop.
  {
    sockaddr_in rxAddr{};
    SOCKET rx = make_loopback_udp(&rxAddr);
    SOCKET tx = make_loopback_udp();
    ClientSessionController c;
    MinimalSink sink;
    std::thread t([&] { c.RunVideoReceiveForTest(static_cast<SocketHandle>(rx), &sink); });

    auto sendTo = [&](const void* p, int len) {
      sendto(tx, static_cast<const char*>(p), len, 0, reinterpret_cast<sockaddr*>(&rxAddr), sizeof(rxAddr));
    };
    sendTo("", 0);                                    // a legal EMPTY datagram (n==0)
    std::vector<uint8_t> oversize(2000, 0x5A);
    sendTo(oversize.data(), static_cast<int>(oversize.size()));  // > 1600 buffer -> WSAEMSGSIZE on recv
    uint8_t frame[1600];
    const size_t flen = build_key_frame(frame, sizeof(frame), 1);
    sendTo(frame, static_cast<int>(flen));            // a VALID key frame AFTER the drops

    const bool delivered = wait_until([&] { return sink.frames() > 0; }, 2000);
    check("REAL recv: a valid key frame is DELIVERED after an empty + an oversize datagram (survived)",
          delivered, "frames=" + std::to_string(sink.frames()));
    check("REAL recv: no terminal failure across empty/oversize",
          c.Snapshot().state != ClientSessionState::Error, "state=" + std::to_string((int)c.Snapshot().state));

    // External Stop: request stop from THIS thread and wake the blocked recv with an empty datagram.
    // If Stop did not break the loop, the join below would hang the test.
    c.RequestStopForTest();
    sendTo("", 0);
    t.join();
    check("external Stop breaks the blocked receive loop (join returns)", true);
    closesocket(rx); closesocket(tx);
  }

  // --- B (U1.2 reset): a scripted WSAECONNRESET is survived and a following VALID key frame is delivered.
  //     Real Windows ICMP reproduction is environment-dependent; the deterministic seam proves the
  //     receiver-loop -> dispatch survival, which a pure enum check does not.
  {
    SOCKET s = make_loopback_udp();
    ClientSessionController c;
    MinimalSink sink;
    std::atomic<int> idx{0};
    c.SetUdpRecvHookForTest([&](uint8_t* buf, size_t cap, int& err) -> int {
      const int i = idx.fetch_add(1, std::memory_order_relaxed);
      err = 0;
      switch (i) {
        case 0: err = WSAECONNRESET; return -1;                       // advisory reset
        case 1: return static_cast<int>(build_key_frame(buf, cap, 2)); // VALID key frame after the reset
        default: c.RequestStopForTest(); return 0;
      }
    });
    std::thread t([&] { c.RunVideoReceiveForTest(static_cast<SocketHandle>(s), &sink); });
    t.join();
    check("reset is survived and the following valid key frame is DELIVERED",
          sink.frames() > 0 && c.Snapshot().state != ClientSessionState::Error,
          "frames=" + std::to_string(sink.frames()));
    closesocket(s);
  }

  // --- C (U1): a genuine terminal recv error still ends the session ---------------------------------
  {
    SOCKET s = make_loopback_udp();
    ClientSessionController c;
    MinimalSink sink;
    c.SetUdpRecvHookForTest([&](uint8_t*, size_t, int& err) -> int { err = WSAENOTSOCK; return -1; });
    std::thread t([&] { c.RunVideoReceiveForTest(static_cast<SocketHandle>(s), &sink); });
    t.join();
    check("a terminal recv error (WSAENOTSOCK) ends the session",
          c.Snapshot().state == ClientSessionState::Error &&
              c.Snapshot().lastError == "udp video receive failed");
    closesocket(s);
  }

  // --- D (U1/U2): a reset flood is rate-bounded -- no CPU spin / log flood --------------------------
  {
    SOCKET s = make_loopback_udp();
    ClientSessionController c;
    MinimalSink sink;
    std::atomic<uint64_t> calls{0};
    const auto start = std::chrono::steady_clock::now();
    c.SetUdpRecvHookForTest([&](uint8_t*, size_t, int& err) -> int {
      if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(80)) {
        c.RequestStopForTest();
        return 0;
      }
      calls.fetch_add(1, std::memory_order_relaxed);
      err = WSAECONNRESET;  // an immediate-return advisory, over and over
      return -1;
    });
    std::thread t([&] { c.RunVideoReceiveForTest(static_cast<SocketHandle>(s), &sink); });
    t.join();
    check("a reset flood is rate-bounded (~1 kHz anti-spin, no CPU peg)", calls.load() <= 400,
          "calls=" + std::to_string(calls.load()));
    closesocket(s);
  }

  // --- E (U1.3): with NACK/hold ENABLED, a stream of drops after an INCOMPLETE head keeps the
  //     maintenance tick running (NACK rounds / hold / stuck-head give-up) and never delivers the
  //     incomplete AU -- discards do not update healthy/ACK. (The exact NACK-round / hold-expiry timing
  //     is covered by the pipeline's own tests and udp_control_e2e; here the point is that the receive
  //     loop keeps ticking the pipeline while recv results are discarded.) ---------------------------
  {
    SOCKET s = make_loopback_udp();
    ClientSessionController c;
    MinimalSink sink;
    c.SetHostSupportsNackForTest(true);  // hold enabled -> the loop's tick is the maintenance heartbeat
    std::atomic<int> idx{0};
    const auto start = std::chrono::steady_clock::now();
    c.SetUdpRecvHookForTest([&](uint8_t* buf, size_t cap, int& err) -> int {
      const int i = idx.fetch_add(1, std::memory_order_relaxed);
      err = 0;
      if (i == 0) return static_cast<int>(build_incomplete_key_first_chunk(buf, cap, 1));  // owes a NACK
      if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(300)) {
        c.RequestStopForTest();
        return 0;
      }
      // alternate empty and reset drops -- both non-terminal, both discarded
      if (i & 1) { err = WSAECONNRESET; return -1; }
      return 0;
    });
    std::thread t([&] { c.RunVideoReceiveForTest(static_cast<SocketHandle>(s), &sink); });
    t.join();
    check("U1.3: NACK/hold on -- the incomplete AU is NEVER delivered through drops (no false healthy)",
          sink.frames() == 0, "frames=" + std::to_string(sink.frames()));
    check("U1.3: the session stays alive through the drop stream (maintenance ticks, no terminal)",
          c.Snapshot().state != ClientSessionState::Error);
    // The give-up/discontinuity may or may not fire within 300 ms depending on the NACK schedule; report
    // it either way (not an assertion), the pipeline's own tests gate its exact timing.
    std::printf("       (U1.3 note: discontinuities observed during drops = %llu)\n",
                static_cast<unsigned long long>(sink.discontinuities()));
    closesocket(s);
  }

  WSACleanup();
  std::printf("\nclient_udp_recv_loop_test: %s (%d failed)\n", gFailed ? "FAIL" : "PASS", gFailed);
  return gFailed ? 1 : 0;
}
