#include "viewer_udp_ingress.hpp"
#include <cstdio>
#include <stdexcept>

int main() {
  using remote60::native_poc::viewer::udp_ingress_retryable_error;
  if (!udp_ingress_retryable_error(WSAEINTR) || !udp_ingress_retryable_error(WSAETIMEDOUT) ||
      !udp_ingress_retryable_error(WSAEWOULDBLOCK) || !udp_ingress_retryable_error(WSAEMSGSIZE) ||
      udp_ingress_retryable_error(WSAECONNRESET)) return 4;
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
  closesocket(rx); closesocket(rx2); closesocket(tx); WSACleanup();
  std::printf("viewer_udp_ingress_test: %s (two isolated sockets; control bypasses stalled video consumer)\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
