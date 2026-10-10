// udp-recv-exit r1: the UDP recv classification is the decision the shared client receive loop and the
// PC viewer ingress both make. A legal empty datagram, a truncated oversize one and a Windows
// ICMP-driven reset must all be non-terminal; only a genuinely bad socket is terminal. These are the
// removal/mutation counter-examples: if n==0 were judged from the stale socket error, or a reset were
// terminal, these checks FAIL. Pure; no socket.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native_socket.hpp"

#include <cstdio>
#include <string>

using remote60::native_poc::UdpRecvClass;
using remote60::native_poc::classify_udp_recv;
using remote60::native_poc::udp_recv_is_terminal;

namespace {
int gChecks = 0, gFailed = 0;
void check(const std::string& what, bool ok) {
  ++gChecks;
  if (!ok) ++gFailed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
}
}  // namespace

int main() {
  std::printf("udp_recv_class_test\n");

  // n > 0 -> a datagram to dispatch.
  check("n>0 is a Datagram", classify_udp_recv(1, 0) == UdpRecvClass::Datagram);
  check("a full datagram is a Datagram", classify_udp_recv(1400, 0) == UdpRecvClass::Datagram);

  // n == 0 is a LEGAL empty UDP datagram -- Empty regardless of the (stale) error value. This is the
  // original defect's counter-example: the old loop consulted last_socket_error on n<=0, so a stale
  // terminal error could tear the session down on a 0-byte datagram.
  check("n==0 is Empty, not consulting the error (err=0)", classify_udp_recv(0, 0) == UdpRecvClass::Empty);
  check("n==0 is Empty even with a terminal error latched (WSAECONNABORTED)",
        classify_udp_recv(0, WSAECONNABORTED) == UdpRecvClass::Empty);
  check("n==0 is Empty even with WSAENOTSOCK latched", classify_udp_recv(0, WSAENOTSOCK) == UdpRecvClass::Empty);
  check("n==0 is NOT Terminal", !udp_recv_is_terminal(classify_udp_recv(0, WSAENOTSOCK)));

  // SOCKET_ERROR classes.
  check("WSAEWOULDBLOCK is Retryable", classify_udp_recv(-1, WSAEWOULDBLOCK) == UdpRecvClass::Retryable);
  check("WSAETIMEDOUT is Retryable", classify_udp_recv(-1, WSAETIMEDOUT) == UdpRecvClass::Retryable);
  check("WSAEINTR is Retryable", classify_udp_recv(-1, WSAEINTR) == UdpRecvClass::Retryable);
  check("WSA_IO_PENDING (997) is Retryable, NOT terminal (a receive still in flight)",
        classify_udp_recv(-1, WSA_IO_PENDING) == UdpRecvClass::Retryable &&
            !udp_recv_is_terminal(classify_udp_recv(-1, WSA_IO_PENDING)));
  check("WSAEMSGSIZE is TruncatedDrop (oversize discarded, not terminal)",
        classify_udp_recv(-1, WSAEMSGSIZE) == UdpRecvClass::TruncatedDrop);
  check("WSAEMSGSIZE is NOT terminal", !udp_recv_is_terminal(classify_udp_recv(-1, WSAEMSGSIZE)));
  // The reset counter-example: advisory, NOT terminal. If reverted to terminal, this FAILs.
  check("WSAECONNRESET is ResetAdvisory (Windows UDP ICMP), not terminal",
        classify_udp_recv(-1, WSAECONNRESET) == UdpRecvClass::ResetAdvisory);
  check("WSAECONNRESET is NOT terminal", !udp_recv_is_terminal(classify_udp_recv(-1, WSAECONNRESET)));

  // A genuinely bad/closed socket stays terminal -- the policy is NOT "ignore all errors".
  check("WSAENOTSOCK is Terminal", classify_udp_recv(-1, WSAENOTSOCK) == UdpRecvClass::Terminal);
  check("WSAECONNABORTED is Terminal", classify_udp_recv(-1, WSAECONNABORTED) == UdpRecvClass::Terminal);
  check("WSAENOTSOCK IS terminal", udp_recv_is_terminal(classify_udp_recv(-1, WSAENOTSOCK)));

  std::printf("\nudp_recv_class_test: %s (%d checks, %d failed)\n", gFailed ? "FAIL" : "PASS", gChecks, gFailed);
  return gFailed ? 1 : 0;
}
