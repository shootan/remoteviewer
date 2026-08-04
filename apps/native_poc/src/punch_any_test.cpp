// Exercises the candidate race against real sockets, because the property that matters is not
// "does it pick one" but "does a blocked address delay the working one". The measured failure it
// guards is a client sitting on a network where the first-listed address is filtered: a
// residential ISP blocks the well-known port inbound, a company Wi-Fi blocks the high one
// outbound, and from the client both look exactly like an offline host.

#include "directory_rendezvous.hpp"
#include "poc_protocol.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using remote60::native_poc::DirectoryRendezvous;
using remote60::native_poc::RendezvousCandidate;
using remote60::native_poc::UdpHelloPacket;
using remote60::native_poc::UdpPacketKind;

namespace {

int gFailures = 0;

void expect(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++gFailures;
  } else {
    std::printf("  ok   %s\n", what.c_str());
  }
}

// A socket that answers punches, standing in for a reachable host.
class Responder {
 public:
  bool Start() {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // let the OS choose, so the test never collides with a real service
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    int len = sizeof(addr);
    if (getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
    port_ = ntohs(addr.sin_port);

    DWORD timeout = 100;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    running_ = true;
    thread_ = std::thread([this]() {
      char buffer[512];
      while (running_.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int n = recvfrom(sock_, buffer, sizeof(buffer), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) continue;
        UdpHelloPacket reply{};
        reply.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
        (void)sendto(sock_, reinterpret_cast<const char*>(&reply), sizeof(reply), 0,
                     reinterpret_cast<const sockaddr*>(&from), fromLen);
      }
    });
    return true;
  }

  void Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (sock_ != INVALID_SOCKET) closesocket(sock_);
    sock_ = INVALID_SOCKET;
  }

  uint16_t port() const { return port_; }

 private:
  SOCKET sock_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

// A port nothing listens on stands in for a filtered address: the client sees the same silence.
uint16_t dead_port() {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  int len = sizeof(addr);
  getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
  const uint16_t port = ntohs(addr.sin_port);
  closesocket(s);
  return port;
}

// PunchAny needs a socket, and Observe is the only thing that opens one -- so the socket is
// prepared the same way here, by pointing Observe at a stub that answers the probe.
class ObserveStub {
 public:
  bool Start() {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    int len = sizeof(addr);
    getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    DWORD timeout = 100;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    running_ = true;
    thread_ = std::thread([this]() {
      char buffer[512];
      while (running_.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int n = recvfrom(sock_, buffer, sizeof(buffer), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) continue;
        char reply[128];
        const int written = _snprintf_s(reply, _TRUNCATE, "{\"ip\":\"127.0.0.1\",\"port\":%u}",
                                        ntohs(from.sin_port));
        (void)sendto(sock_, reply, written, 0, reinterpret_cast<const sockaddr*>(&from), fromLen);
      }
    });
    return true;
  }
  void Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (sock_ != INVALID_SOCKET) closesocket(sock_);
    sock_ = INVALID_SOCKET;
  }
  uint16_t port() const { return port_; }

 private:
  SOCKET sock_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

// The case the whole feature exists for: the reachable address is not the first one listed, and
// the blocked ones must not cost it any time.
void TestBlockedFirstCandidateDoesNotDelayTheWorkingOne() {
  std::printf("a working address later in the list is still found immediately\n");
  ObserveStub directory;
  Responder host;
  if (!directory.Start() || !host.Start()) {
    expect(false, "test rig failed to start");
    return;
  }

  DirectoryRendezvous rendezvous;
  std::string observed;
  std::string error;
  const bool observedOk =
      rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
  expect(observedOk, "observe prepared the socket (" + observed + ")");

  const std::vector<RendezvousCandidate> candidates = {
      {"127.0.0.1", dead_port(), "private"},
      {"127.0.0.1", dead_port(), "public"},
      {"127.0.0.1", host.port(), "public-alt"},
  };

  RendezvousCandidate chosen;
  const auto start = std::chrono::steady_clock::now();
  const bool answered = rendezvous.PunchAny(candidates, 4000, &chosen, &error);
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

  expect(answered, "a candidate answered (" + error + ")");
  expect(chosen.port == host.port(), "the reachable candidate was chosen");
  expect(chosen.kind == "public-alt", "the winner's kind is reported for the log");
  // Serially, two dead candidates would burn their full budget before the third was tried.
  expect(elapsedMs < 1000, "found in " + std::to_string(elapsedMs) + "ms, not after the dead ones");

  rendezvous.Close();
  host.Stop();
  directory.Stop();
}

void TestAllBlockedReportsFailureWithoutHanging() {
  std::printf("every address blocked fails within the budget rather than hanging\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed;
  std::string error;
  rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);

  const std::vector<RendezvousCandidate> candidates = {
      {"127.0.0.1", dead_port(), "public"},
      {"127.0.0.1", dead_port(), "public-alt"},
  };
  RendezvousCandidate chosen;
  const auto start = std::chrono::steady_clock::now();
  const bool answered = rendezvous.PunchAny(candidates, 700, &chosen, &error);
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
  expect(!answered, "reported failure");
  // The budget is for the whole attempt, not per candidate; otherwise a long list would stall
  // connect for as many multiples as it has entries.
  expect(elapsedMs < 1800, "stayed within the budget (" + std::to_string(elapsedMs) + "ms)");

  rendezvous.Close();
  directory.Stop();
}

void TestUnresolvableCandidateDoesNotSinkTheOthers() {
  std::printf("one unusable entry does not cost the others their chance\n");
  ObserveStub directory;
  Responder host;
  if (!directory.Start() || !host.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed;
  std::string error;
  rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);

  const std::vector<RendezvousCandidate> candidates = {
      {"this-host-does-not-exist.invalid", 43000, "private"},
      {"127.0.0.1", host.port(), "public"},
  };
  RendezvousCandidate chosen;
  const bool answered = rendezvous.PunchAny(candidates, 3000, &chosen, &error);
  expect(answered, "the resolvable candidate still won (" + error + ")");
  expect(chosen.port == host.port(), "chose the reachable address");

  rendezvous.Close();
  host.Stop();
  directory.Stop();
}

}  // namespace

int main() {
  std::string initError;
  if (!remote60::native_poc::initialize_sockets(&initError)) {
    std::printf("socket init failed: %s\n", initError.c_str());
    return 1;
  }

  TestBlockedFirstCandidateDoesNotDelayTheWorkingOne();
  TestAllBlockedReportsFailureWithoutHanging();
  TestUnresolvableCandidateDoesNotSinkTheOthers();

  if (gFailures != 0) {
    std::printf("punch_any_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("punch_any_test: PASS\n");
  return 0;
}
