// udp-recv-exit r1: the headline defect was the SHARED client receive loop ending the whole session
// (`udp video receive failed`) on a legal empty UDP datagram, a truncated oversize one, or a Windows
// ICMP-driven reset. This drives the REAL ClientSessionController::VideoReceiveMain loop with a scripted
// recv hook (deterministic, no dependence on a flaky field error) and checks: the session SURVIVES
// zero/oversize/reset and dispatches the following datagram; a genuine terminal error still ends it; a
// reset flood is rate-bounded (no CPU spin). The old `n<=0 -> terminate` behaviour fails checks 1-2.
// Tag: network (loopback dummy socket), deterministic recv seam.

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

#include "native_video_client_session.hpp"
#include "native_video_client_shared_core.hpp"

using namespace remote60::native_poc;

namespace {
int gFailed = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  if (!ok) ++gFailed;
}

class MinimalSink : public ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(UdpH264AssembledFrame&&) override { ++frames_; }
  void OnVideoStreamReset() override {}
  uint64_t frames() const { return frames_.load(); }
 private:
  std::atomic<uint64_t> frames_{0};
};

SOCKET make_loopback_udp() {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = 0;
  if (s != INVALID_SOCKET) bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  return s;
}
}  // namespace

int main() {
  std::printf("client_udp_recv_loop_test\n");
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa)) { std::printf("WSAStartup failed\n"); return 1; }

  // --- 1/2: SURVIVE zero/oversize/reset, then dispatch the following datagram -----------------------
  {
    SOCKET s = make_loopback_udp();
    ClientSessionController c;
    MinimalSink sink;
    std::atomic<int> idx{0};
    c.SetUdpRecvHookForTest([&](uint8_t* buf, size_t cap, int& err) -> int {
      const int i = idx.fetch_add(1, std::memory_order_relaxed);
      err = 0;
      switch (i) {
        case 0: return 0;                                 // n==0: a legal empty datagram (NOT a TCP EOF)
        case 1: err = WSAEMSGSIZE; return -1;             // truncated oversize -> discard
        case 2: err = WSAECONNRESET; return -1;           // Windows UDP ICMP reset -> advisory
        case 3: {                                          // a normal datagram AFTER the drops
          const size_t nbytes = sizeof(UdpVideoChunkHeader) + 8;
          if (cap >= nbytes) std::memset(buf, 0, nbytes);
          return static_cast<int>(nbytes);
        }
        default: c.RequestStopForTest(); return 0;         // end the loop cleanly after the sequence
      }
    });
    std::thread t([&] { c.RunVideoReceiveForTest(static_cast<SocketHandle>(s), &sink); });
    t.join();
    check("survives zero/oversize/reset and REACHES the following datagram (bytes dispatched)",
          c.SessionBytesReceived() > 0, "bytes=" + std::to_string(c.SessionBytesReceived()));
    check("no terminal session failure on zero/oversize/reset",
          c.Snapshot().state != ClientSessionState::Error, "state=" + std::to_string((int)c.Snapshot().state));
    if (s != INVALID_SOCKET) closesocket(s);
  }

  // --- 3: a genuine terminal recv error still ends the session --------------------------------------
  {
    SOCKET s = make_loopback_udp();
    ClientSessionController c;
    MinimalSink sink;
    c.SetUdpRecvHookForTest([&](uint8_t*, size_t, int& err) -> int { err = WSAENOTSOCK; return -1; });
    std::thread t([&] { c.RunVideoReceiveForTest(static_cast<SocketHandle>(s), &sink); });
    t.join();
    check("a terminal recv error (WSAENOTSOCK) ends the session",
          c.Snapshot().state == ClientSessionState::Error);
    check("the terminal error is reported as the receive failure",
          c.Snapshot().lastError == "udp video receive failed", c.Snapshot().lastError);
    if (s != INVALID_SOCKET) closesocket(s);
  }

  // --- 4: a reset flood is rate-bounded -- no CPU spin / log flood ----------------------------------
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
    // The anti-spin gap (~1 kHz) caps immediate drops to ~80 in 80 ms, not the tens of thousands a
    // tight loop would do.
    check("a reset flood is rate-bounded (anti-spin, no CPU peg)", calls.load() <= 400,
          "calls=" + std::to_string(calls.load()));
    if (s != INVALID_SOCKET) closesocket(s);
  }

  WSACleanup();
  std::printf("\nclient_udp_recv_loop_test: %s (%d failed)\n", gFailed ? "FAIL" : "PASS", gFailed);
  return gFailed ? 1 : 0;
}
