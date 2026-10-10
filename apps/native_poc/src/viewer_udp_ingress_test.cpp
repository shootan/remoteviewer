#include "viewer_udp_ingress.hpp"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <vector>

int main() {
  using remote60::native_poc::viewer::udp_ingress_recv_retryable;
  using remote60::native_poc::viewer::udp_ingress_select_retryable;
  // udp-recv-exit r2 U2: select and recv use DISTINCT error sets. select: timeout/would-block/interrupt
  // only. recv: that set PLUS the datagram advisories WSAEMSGSIZE and WSAECONNRESET (aligned with the
  // shared client's reset policy). A bad socket is terminal in both. An advisory (reset/oversize) is NOT
  // a select-retryable (select delivers no datagram).
  if (!udp_ingress_select_retryable(WSAEINTR) || !udp_ingress_select_retryable(WSAETIMEDOUT) ||
      !udp_ingress_select_retryable(WSAEWOULDBLOCK) ||
      udp_ingress_select_retryable(WSAEMSGSIZE) || udp_ingress_select_retryable(WSAECONNRESET) ||
      udp_ingress_select_retryable(WSAENOTSOCK)) return 4;
  if (!udp_ingress_recv_retryable(WSAEINTR) || !udp_ingress_recv_retryable(WSAETIMEDOUT) ||
      !udp_ingress_recv_retryable(WSAEWOULDBLOCK) || !udp_ingress_recv_retryable(WSAEMSGSIZE) ||
      !udp_ingress_recv_retryable(WSAECONNRESET) || udp_ingress_recv_retryable(WSAENOTSOCK)) return 5;
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2,2), &wsa)) return 1;
  SOCKET rx = socket(AF_INET, SOCK_DGRAM, 0), tx = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (rx == INVALID_SOCKET || tx == INVALID_SOCKET ||
      bind(rx, reinterpret_cast<sockaddr*>(&address), sizeof(address))) return 2;
  int length = sizeof(address);
  getsockname(rx, reinterpret_cast<sockaddr*>(&address), &length);
  SOCKET rx2 = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in address2 = address;
  address2.sin_port = 0;
  if (rx2 == INVALID_SOCKET || bind(rx2, reinterpret_cast<sockaddr*>(&address2), sizeof(address2))) return 3;
  length = sizeof(address2);
  getsockname(rx2, reinterpret_cast<sockaddr*>(&address2), &length);
  std::atomic<int> control{0};
  std::atomic<int> control2{0};
  bool pass = false;
  {
    remote60::native_poc::viewer::UdpIngress ingress(rx,
        [&](const uint8_t* p, size_t n) {
          if (n == 1 && p[0] == 9) { ++control; return true; }
          return false;
        }, [] {});
    remote60::native_poc::viewer::UdpIngress ingress2(rx2,
        [&](const uint8_t* p, size_t n) {
          if (n == 1 && p[0] == 7) { ++control2; return true; }
          return false;
        }, [] {});
    const char video = 1, ack = 9;
    sendto(tx, &video, 1, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    sendto(tx, &ack, 1, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    const char otherSession = 7;
    sendto(tx, &otherSession, 1, 0, reinterpret_cast<sockaddr*>(&address2), sizeof(address2));
    // Do not consume video: model a decoder that is held indefinitely. ACK must still arrive.
    for (int i = 0; i < 100 && (control.load() == 0 || control2.load() == 0); ++i) Sleep(10);
    uint8_t output[1600]{};
    pass = control.load() == 1 && control2.load() == 1 &&
           ingress.Pop(output, sizeof(output)) == 1 && output[0] == 1;
    const char tooLargeForConsumer[2] = {1, 2};
    sendto(tx, tooLargeForConsumer, 2, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    for (int i = 0; i < 40 && ingress.dropped() == 0; ++i) {
      if (ingress.Pop(output, 1) != 0) pass = false;
    }
    pass = pass && ingress.dropped() == 1;
    sendto(tx, &video, 1, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    int count = 0;
    for (int i = 0; i < 40 && count == 0; ++i) count = ingress.Pop(output, sizeof(output));
    pass = pass && count == 1 && output[0] == 1;
  } // join without closing the shared socket or waiting for another incoming packet
  {
    remote60::native_poc::viewer::UdpIngress failed(rx,
        [](const uint8_t*, size_t) { return false; },
        [] { throw std::runtime_error("injected tick failure"); });
    uint8_t output[1600]{};
    int result = 0;
    for (int i = 0; i < 40 && result == 0; ++i) result = failed.Pop(output, sizeof(output));
    pass = pass && result == -1; // exception wakes the consumer and still joins safely
  }
  // r3/r4 V2(b): a bad-datagram (oversize) flood advances the loop at the ARRIVAL rate; AFTER the flood
  // the SAME ingress must still deliver a normal control marker and a video packet (the worker thread is
  // alive and processing, not dead-after-the-flood -- r4 T1), and once the supply stops the loop returns
  // to the idle select wait (no busy spin). The idle rate is sampled only AFTER the queue has drained
  // (the post-flood video has been Pop'd), so residual packets are not misread as a spin. Tick counts
  // loop iterations -- a spin would be orders of magnitude higher. The dtor's Stop must join promptly.
  {
    SOCKET rx3 = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a3{};
    a3.sin_family = AF_INET; a3.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a3.sin_port = 0;
    bool floodPass = false;
    if (rx3 != INVALID_SOCKET && bind(rx3, reinterpret_cast<sockaddr*>(&a3), sizeof(a3)) == 0) {
      int l3 = sizeof(a3); getsockname(rx3, reinterpret_cast<sockaddr*>(&a3), &l3);
      std::atomic<uint64_t> ticks{0};
      std::atomic<int> controlAfterFlood{0};
      {
        remote60::native_poc::viewer::UdpIngress ing(
            rx3,
            [&](const uint8_t* p, size_t n) {
              if (n == 1 && p[0] == 9) { controlAfterFlood.fetch_add(1, std::memory_order_relaxed); return true; }
              return false;  // p[0]==1 video falls through to the queue
            },
            [&] { ticks.fetch_add(1, std::memory_order_relaxed); });
        Sleep(120);
        const uint64_t idle1 = ticks.load();                 // ~120ms/25ms ticks when idle (not a spin)
        std::vector<char> big(2000, 7);                       // > 1600 recv buffer -> WSAEMSGSIZE -> dropped
        for (int i = 0; i < 40; ++i)
          sendto(tx, big.data(), static_cast<int>(big.size()), 0, reinterpret_cast<sockaddr*>(&a3), sizeof(a3));
        Sleep(40);  // let the loop drain the flood before the markers, so the socket buffer cannot drop them
        const uint64_t afterFlood = ticks.load();
        // After the flood: a control marker (ACK, 9) and a video packet (1). UDP is in order on loopback,
        // so once the video is Pop'd the whole flood has drained.
        const char ack = 9, video = 1;
        sendto(tx, &ack, 1, 0, reinterpret_cast<sockaddr*>(&a3), sizeof(a3));
        sendto(tx, &video, 1, 0, reinterpret_cast<sockaddr*>(&a3), sizeof(a3));
        uint8_t out[1600]{};
        int got = 0;
        for (int i = 0; i < 100 && got != 1; ++i) { got = ing.Pop(out, sizeof(out)); if (got <= 0) got = 0; }
        const bool aliveAfterFlood = controlAfterFlood.load() >= 1 && got == 1 && out[0] == 1;
        // Idle rate AFTER the queue drained (the video arrived): must be bounded, not spinning.
        const uint64_t drained = ticks.load();
        Sleep(120);
        const uint64_t idle2 = ticks.load();
        floodPass = idle1 <= 50 &&                            // idle is NOT spinning (would be thousands)
                    afterFlood >= idle1 &&                    // the flood advanced the loop (arrival rate)
                    aliveAfterFlood &&                        // r4 T1: thread alive -- control + video processed
                    (idle2 - drained) <= 50;                  // after the drain, idle again (no spin)
      }  // ing dtor: Stop must join promptly, or this test would hang
      closesocket(rx3);
    }
    pass = pass && floodPass;
  }
  closesocket(rx); closesocket(rx2); closesocket(tx); WSACleanup();
  std::printf("viewer_udp_ingress_test: %s (two isolated sockets; control bypasses stalled video consumer)\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
