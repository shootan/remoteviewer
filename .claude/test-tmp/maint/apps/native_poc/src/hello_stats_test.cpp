#define NOMINMAX  // native_socket.hpp uses std::min; windows.h defines a macro of that name.
#define WIN32_LEAN_AND_MEAN
// What the UDP hello handshake reports about itself. (pc2-connect-diag r1, scope C-1)
//
// "udp hello ack failed" was the whole of it, and it reads the same whether one hello went out or
// forty, whether the budget ran out or the first send failed. On 2026-09-21 that mattered: the
// viewer's budget is 10 s and the host's capability can be 25 s away, and nothing in any log said
// how many hellos had actually been sent before the viewer gave up.
//
// Against a real socket, with no peer: the only thing this needs is somewhere to send that nobody
// answers, which is what a closed loopback port is.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <iostream>
#include <string>

#include "native_video_client_tcp_control.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

/**
 * A connected UDP socket aimed at an address that will never answer and never refuse.
 *
 * Not loopback: a closed loopback port answers with ICMP port-unreachable, which the stack turns
 * into an immediate WSAECONNRESET on the next recv. The handshake then never waits its slice and
 * spins -- measured at 388 hellos in 900ms here, against the ~7 a real unanswered peer produces.
 * 192.0.2.0/24 is the documentation range (RFC 5737): packets leave and nothing comes back, which
 * is what an unreachable host actually looks like.
 */
SocketHandle silent_peer_socket(uint16_t port) {
  SocketHandle s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == kInvalidSocket) return s;
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  inet_pton(AF_INET, "192.0.2.1", &to.sin_addr);
  if (connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) != 0) {
    closesocket(s);
    return kInvalidSocket;
  }
  return s;
}

}  // namespace

int main() {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  std::cout << "hello_stats_test\n";

  {
    SocketHandle sock = silent_peer_socket(59991);
    check("a socket aimed at nobody was prepared", sock != kInvalidSocket);
    if (sock != kInvalidSocket) {
      UdpHelloOptions options;
      options.budgetMs = 900;
      options.sliceMaxMs = 120;
      options.authToken = "not-a-real-capability";

      UdpHelloStats stats;
      std::string error;
      const bool ok = udp_hello_handshake(sock, options, nullptr, &error, nullptr, &stats);

      check("a hello nobody answers does not succeed", !ok);
      check("...and the budget is what ended it", stats.budgetSpent,
            "otherwise the caller cannot tell a timeout from a send that failed");
      check("...having sent more than one", stats.attempts > 1,
            std::to_string(stats.attempts) + " attempts");
      check("...bounded by the budget and the slice", stats.attempts <= 20,
            std::to_string(stats.attempts) + " attempts in " + std::to_string(options.budgetMs) +
                "ms");
      check("...for about as long as it was given", stats.elapsedMs >= options.budgetMs - 200,
            std::to_string(stats.elapsedMs) + "ms of " + std::to_string(options.budgetMs));
      check("...and not much longer", stats.elapsedMs < options.budgetMs + 1500,
            std::to_string(stats.elapsedMs) + "ms");
      check("nobody answered, so no answer was misread", stats.badAcks == 0,
            std::to_string(stats.badAcks));
      check("and the error still says what it always said", error == "udp hello ack failed", error);
      closesocket(sock);
    }
  }

  {
    // A send that cannot even leave: the stats must not claim a spent budget, because that is a
    // different failure and points somewhere else entirely.
    SocketHandle dead = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    check("an unconnected socket was prepared", dead != kInvalidSocket);
    if (dead != kInvalidSocket) {
      UdpHelloOptions options;
      options.budgetMs = 400;
      options.retrySleepMs = 0;  // a failed send is fatal, which is what the viewer uses
      UdpHelloStats stats;
      std::string error;
      const bool ok = udp_hello_handshake(dead, options, nullptr, &error, nullptr, &stats);
      check("a hello that cannot be sent does not succeed", !ok);
      check("...and is not reported as a spent budget", !stats.budgetSpent,
            "this is the distinction the single error string could not make");
      check("...with the send failure named", error == "udp hello send failed", error);
      closesocket(dead);
    }
  }

  {
    // Stats are optional: the older call shape still compiles and runs.
    SocketHandle sock = silent_peer_socket(59992);
    if (sock != kInvalidSocket) {
      UdpHelloOptions options;
      options.budgetMs = 200;
      std::string error;
      check("the handshake still works without asking for stats",
            !udp_hello_handshake(sock, options, nullptr, &error));
      closesocket(sock);
    }
  }

  WSACleanup();
  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
