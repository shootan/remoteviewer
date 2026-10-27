// Exercises the candidate race against real sockets, because the property that matters is not
// "does it pick one" but "does a blocked address delay the working one". The measured failure it
// guards is a client sitting on a network where the first-listed address is filtered: a
// residential ISP blocks the well-known port inbound, a company Wi-Fi blocks the high one
// outbound, and from the client both look exactly like an offline host.

#include "directory_rendezvous.hpp"
#include "poc_protocol.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
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

// hello-delay-fix r1: a candidate that receives and never answers -- it counts the punches, which is
// the number of rounds PunchAny sent.
class SilentCounter {
 public:
  bool Start() {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    int len = sizeof(addr);
    getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    DWORD timeout = 50;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    running_ = true;
    thread_ = std::thread([this]() {
      char buffer[512];
      while (running_.load()) {
        if (recv(sock_, buffer, sizeof(buffer), 0) > 0) ++count_;
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
  int count() const { return count_.load(); }

 private:
  SOCKET sock_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::atomic<int> count_{0};
  std::atomic<bool> running_{false};
  std::thread thread_;
};

// Answers punches only after `delayMs` from Start().
class LateResponder {
 public:
  bool Start(int delayMs) {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    int len = sizeof(addr);
    getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    DWORD timeout = 20;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    running_ = true;
    const auto opensAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    thread_ = std::thread([this, opensAt]() {
      char buffer[512];
      while (running_.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int n = recvfrom(sock_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0 || std::chrono::steady_clock::now() < opensAt) continue;
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

long long ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

// The field case: two candidates nobody listens on (each punch to them earns an ICMP reset that comes
// back as an immediate receive error), one that never answers. The rounds must keep the 150 ms clock.
void TestImmediateErrorsDoNotSpeedUpTheRounds() {
  std::printf("errors that come back at once do not make the rounds faster (150 ms cadence)\n");
  ObserveStub directory;
  SilentCounter silent;
  if (!directory.Start() || !silent.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
  const std::vector<RendezvousCandidate> candidates = {
      {"127.0.0.1", dead_port(), "private"},
      {"127.0.0.1", dead_port(), "private"},
      {"127.0.0.1", silent.port(), "public"},
  };
  RendezvousCandidate chosen;
  const auto start = std::chrono::steady_clock::now();
  const bool answered = rendezvous.PunchAny(candidates, 1500, &chosen, &error);
  const long long elapsed = ms_since(start);
  Sleep(100);
  const int rounds = silent.count();
  expect(!answered, "nobody answered (" + error + ")");
  expect(elapsed >= 1400 && elapsed < 1900, "the budget ended it: " + std::to_string(elapsed) + " ms of 1500");
  // 1500 / 150 = 10 rounds, plus the first at 0 and one at the edge.
  expect(rounds >= 8 && rounds <= 12, "rounds sent in 1.5 s: " + std::to_string(rounds) + " (150 ms cadence: 10-11)");
  rendezvous.Close();
  silent.Stop();
  directory.Stop();
}

// Datagrams that are not from a candidate -- and empty ones -- keep arriving; they neither end the wait
// nor start a round.
void TestUnrelatedDatagramsDoNotSpeedUpTheRounds() {
  std::printf("a stream of unrelated and empty datagrams does not make the rounds faster\n");
  ObserveStub directory;
  SilentCounter silent;
  if (!directory.Start() || !silent.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
  const uint16_t mine = static_cast<uint16_t>(std::stoi(observed.substr(observed.find(':') + 1)));
  std::atomic<bool> flooding{true};
  std::thread flood([&] {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons(mine);
    UdpHelloPacket junk{};
    junk.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
    bool empty = false;
    while (flooding.load()) {
      (void)sendto(s, reinterpret_cast<const char*>(&junk), empty ? 0 : sizeof(junk), 0,
                   reinterpret_cast<const sockaddr*>(&to), sizeof(to));
      empty = !empty;
      Sleep(1);
    }
    closesocket(s);
  });
  const std::vector<RendezvousCandidate> candidates = {{"127.0.0.1", silent.port(), "public"}};
  RendezvousCandidate chosen;
  const auto start = std::chrono::steady_clock::now();
  const bool answered = rendezvous.PunchAny(candidates, 1500, &chosen, &error);
  const long long elapsed = ms_since(start);
  flooding = false;
  flood.join();
  Sleep(100);
  const int rounds = silent.count();
  expect(!answered, "a stranger is not a candidate: nobody answered (" + error + ")");
  expect(elapsed >= 1400 && elapsed < 1900, "the budget ended it: " + std::to_string(elapsed) + " ms");
  expect(rounds >= 8 && rounds <= 12, "rounds sent in 1.5 s: " + std::to_string(rounds) + " (10-11)");
  rendezvous.Close();
  silent.Stop();
  directory.Stop();
}

// A candidate that starts answering later is taken as soon as it does -- not at the end of a round.
void TestALateAnswerIsTakenAtOnce() {
  std::printf("an answer that comes later is taken when it comes, with errors around it\n");
  ObserveStub directory;
  LateResponder host;
  if (!directory.Start() || !host.Start(500)) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
  const std::vector<RendezvousCandidate> candidates = {
      {"127.0.0.1", dead_port(), "private"},
      {"127.0.0.1", dead_port(), "private"},
      {"127.0.0.1", host.port(), "public"},
  };
  RendezvousCandidate chosen;
  const auto start = std::chrono::steady_clock::now();
  const bool answered = rendezvous.PunchAny(candidates, 4000, &chosen, &error);
  const long long elapsed = ms_since(start);
  expect(answered && chosen.port == host.port(), "the late candidate won (" + error + ")");
  // It opens at 500 ms; the next round after that reaches it within 150 ms and its reply is read at once.
  expect(elapsed >= 450 && elapsed < 800, "taken at " + std::to_string(elapsed) + " ms (opens at 500)");
  rendezvous.Close();
  host.Stop();
  directory.Stop();
}

void TestStopEndsItPromptly() {
  std::printf("a stop ends the punch promptly, errors or not\n");
  ObserveStub directory;
  SilentCounter silent;
  if (!directory.Start() || !silent.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
  const std::vector<RendezvousCandidate> candidates = {
      {"127.0.0.1", dead_port(), "private"},
      {"127.0.0.1", silent.port(), "public"},
  };
  std::atomic<bool> stop{false};
  std::thread stopper([&] {
    Sleep(300);
    stop = true;
  });
  RendezvousCandidate chosen;
  const auto start = std::chrono::steady_clock::now();
  const bool answered = rendezvous.PunchAny(candidates, 4000, &chosen, &error, &stop);
  const long long elapsed = ms_since(start);
  stopper.join();
  expect(!answered && error == "cancelled", "reported as cancelled (" + error + ")");
  expect(elapsed < 500, "within one interval of the stop: " + std::to_string(elapsed) + " ms (stop at 300)");
  rendezvous.Close();
  silent.Stop();
  directory.Stop();
}

// ---- hello-delay-fix r2 (Codex N2): errors no loopback socket gives on demand, through the test seam ----
// What the seam stands in for is the recvfrom call itself; everything around it is PunchAny's own.
std::atomic<int> gInjectError{0};          // the error to report instead of receiving (0: receive for real)
std::atomic<long long> gInjectUntilMs{0};  // ...until this steady-clock time (0: for good)
std::atomic<int> gInjected{0};

long long steady_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

int test_recv_hook(remote60::native_poc::SocketHandle s, char* buf, int len, sockaddr_in* from, int* fromLen,
                   int* err) {
  const long long until = gInjectUntilMs.load();
  const int inject = gInjectError.load();
  if (inject != 0 && (until == 0 || steady_ms() < until)) {
    ++gInjected;
    Sleep(1);  // an error comes back at once; this only keeps the test from spinning a core
    *err = inject;
    return -1;
  }
  int fl = *fromLen;
  const int n = recvfrom(s, buf, len, 0, reinterpret_cast<sockaddr*>(from), &fl);
  *fromLen = fl;
  *err = n < 0 ? WSAGetLastError() : 0;
  return n;
}

struct HookScope {
  HookScope(int error, int forMs) {
    gInjectError = error;
    gInjectUntilMs = forMs > 0 ? steady_ms() + forMs : 0;
    gInjected = 0;
    remote60::native_poc::SetPunchRecvHookForTest(&test_recv_hook);
  }
  ~HookScope() {
    remote60::native_poc::SetPunchRecvHookForTest(nullptr);
    gInjectError = 0;
  }
};

// One path's TTL expiry (WSAENETRESET on a UDP recvfrom) is about that path: the punch goes on, and the
// candidate that does answer is taken -- not a fallback to the first-listed address.
void TestTtlOnOnePathDoesNotEndThePunch() {
  std::printf("a TTL expiry on one path does not end the punch: the answering candidate is still taken\n");
  ObserveStub directory;
  Responder host;
  if (!directory.Start() || !host.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
  const std::vector<RendezvousCandidate> candidates = {
      {"127.0.0.1", dead_port(), "private"},
      {"127.0.0.1", host.port(), "public"},
  };
  RendezvousCandidate chosen;
  bool answered = false;
  long long elapsed = 0;
  {
    HookScope ttl(WSAENETRESET, 200);  // the first 200 ms of receives report the TTL expiry
    const auto start = std::chrono::steady_clock::now();
    answered = rendezvous.PunchAny(candidates, 3000, &chosen, &error);
    elapsed = ms_since(start);
  }
  expect(gInjected.load() > 0, "the TTL report was actually delivered (" + std::to_string(gInjected.load()) + " times)");
  expect(answered && chosen.port == host.port(), "the answering candidate was taken (" + error + ")");
  expect(elapsed >= 150 && elapsed < 700, "after the TTL reports, within a round: " + std::to_string(elapsed) + " ms");
  rendezvous.Close();
  host.Stop();
  directory.Stop();
}

// A socket that is not a socket any more ends the punch at once, named.
void TestARealSocketFailureEndsIt() {
  std::printf("a failure of the socket itself (WSAENOTSOCK) ends the punch at once\n");
  ObserveStub directory;
  Responder host;
  if (!directory.Start() || !host.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
  const std::vector<RendezvousCandidate> candidates = {{"127.0.0.1", host.port(), "public"}};
  RendezvousCandidate chosen;
  bool answered = true;
  long long elapsed = 0;
  {
    HookScope broken(WSAENOTSOCK, 0);
    const auto start = std::chrono::steady_clock::now();
    answered = rendezvous.PunchAny(candidates, 3000, &chosen, &error);
    elapsed = ms_since(start);
  }
  expect(!answered && error.find("punch receive failed") != std::string::npos &&
             error.find(std::to_string(WSAENOTSOCK)) != std::string::npos,
         "reported as a receive failure with its code (" + error + ")");
  expect(elapsed < 100, "at once, not after the budget: " + std::to_string(elapsed) + " ms");
  rendezvous.Close();
  host.Stop();
  directory.Stop();
}

// TTL reports that never stop: still one round per 150 ms, the budget and the stop still end it.
void TestContinuousTtlKeepsTheCadence() {
  std::printf("TTL reports that keep coming do not speed up the rounds, and the budget/stop still hold\n");
  ObserveStub directory;
  SilentCounter silent;
  if (!directory.Start() || !silent.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
  const std::vector<RendezvousCandidate> candidates = {{"127.0.0.1", silent.port(), "public"}};
  RendezvousCandidate chosen;
  {
    HookScope ttl(WSAENETRESET, 0);
    const auto start = std::chrono::steady_clock::now();
    const bool answered = rendezvous.PunchAny(candidates, 1500, &chosen, &error);
    const long long elapsed = ms_since(start);
    Sleep(100);
    const int rounds = silent.count();
    expect(!answered && error == "no candidate answered", "the budget ended it (" + error + ")");
    expect(elapsed >= 1400 && elapsed < 1900, "on time: " + std::to_string(elapsed) + " ms of 1500");
    expect(rounds >= 8 && rounds <= 12, "rounds in 1.5 s: " + std::to_string(rounds) + " (10-11)");
  }
  {
    HookScope ttl(WSAENETRESET, 0);
    std::atomic<bool> stop{false};
    std::thread stopper([&] {
      Sleep(300);
      stop = true;
    });
    const auto start = std::chrono::steady_clock::now();
    const bool answered = rendezvous.PunchAny(candidates, 4000, &chosen, &error, &stop);
    const long long elapsed = ms_since(start);
    stopper.join();
    expect(!answered && error == "cancelled" && elapsed < 500,
           "a stop during them ends it: " + std::to_string(elapsed) + " ms (" + error + ")");
  }
  rendezvous.Close();
  silent.Stop();
  directory.Stop();
}

}  // namespace

// ---- np-impl r1 (Codex NP design, contracts 1-5): the early relay race ----
//
// On a scripted clock: the clock hook returns gVirtualUs, and the receive hook either hands back the next
// scripted datagram (moving the clock to its time) or, when none falls inside the timeout PunchAny asked
// for, moves the clock to the end of that timeout and reports a timeout. So 999 / 1000 / 1001 ms are those
// exact instants, not whatever the scheduler made of them.

using remote60::native_poc::PunchRaceOptions;
using remote60::native_poc::PunchRaceReport;

struct Scripted {
  int64_t atMs;
  uint16_t port;      // the tuple it comes from (127.0.0.1:port)
  int shape;          // 0 = a valid Punch, 1 = wrong magic, 2 = 48 bytes, 3 = wrong kind, 4 = version 1, 5 = one byte
};

std::vector<Scripted> gScript;
size_t gScriptNext = 0;
std::atomic<int64_t> gVirtualUs{0};
int64_t gStopAtMs = -1;
std::atomic<bool>* gStopFlag = nullptr;

int64_t virtual_clock() { return gVirtualUs.load(); }

int scripted_recv(remote60::native_poc::SocketHandle, char* buf, int len, sockaddr_in* from, int* fromLen,
                  int* err) {
  const int64_t now = gVirtualUs.load();
  const int64_t limit = now + static_cast<int64_t>(remote60::native_poc::PunchLastRecvTimeoutMsForTest()) * 1000;
  if (gStopFlag && gStopAtMs >= 0 && limit >= gStopAtMs * 1000 &&
      (gScriptNext >= gScript.size() || gScript[gScriptNext].atMs * 1000 > gStopAtMs * 1000)) {
    gVirtualUs = (std::max)(now, gStopAtMs * 1000);
    gStopFlag->store(true);
    *err = WSAETIMEDOUT;
    return -1;
  }
  if (gScriptNext < gScript.size() && gScript[gScriptNext].atMs * 1000 <= limit) {
    const Scripted& e = gScript[gScriptNext++];
    gVirtualUs = (std::max)(now, e.atMs * 1000);
    UdpHelloPacket p{};
    p.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
    int n = static_cast<int>(sizeof(p));
    if (e.shape == 1) p.magic = 0x12345678u;
    if (e.shape == 2) n = static_cast<int>(sizeof(p)) - 1;
    if (e.shape == 3) p.kind = static_cast<uint16_t>(UdpPacketKind::Hello);
    if (e.shape == 4) p.version = 1;
    if (e.shape == 5) n = 1;
    std::memcpy(buf, &p, (std::min)(n, len));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(e.port);
    *from = a;
    *fromLen = static_cast<int>(sizeof(a));
    *err = 0;
    return n;
  }
  gVirtualUs = limit;
  *err = WSAETIMEDOUT;
  return -1;
}

struct EarlyResult {
  bool answered = false;
  RendezvousCandidate chosen;
  PunchRaceReport report;
  std::string error;
};

constexpr uint16_t kDirectPort = 41001;
constexpr uint16_t kRelayPort = 41002;
constexpr uint16_t kPrivatePort = 41003;

// One race on the scripted clock. commitMs 0 = the legacy race.
EarlyResult run_scripted(DirectoryRendezvous& rendezvous, std::vector<Scripted> script, uint32_t commitMs,
                         uint32_t budgetMs = 4000, std::atomic<bool>* stop = nullptr, int64_t stopAtMs = -1) {
  gScript = std::move(script);
  gScriptNext = 0;
  gVirtualUs = 1000000000;  // any origin; reported times are relative to T0
  for (auto& e : gScript) e.atMs += 1000000;  // the same origin, in ms
  gStopFlag = stop;
  gStopAtMs = stopAtMs >= 0 ? stopAtMs + 1000000 : -1;
  remote60::native_poc::SetPunchClockHookForTest(&virtual_clock);
  remote60::native_poc::SetPunchRecvHookForTest(&scripted_recv);
  const std::vector<RendezvousCandidate> candidates = {
      {"127.0.0.1", kPrivatePort, "private"},
      {"127.0.0.1", kDirectPort, "public"},
      {"127.0.0.1", kRelayPort, "relay"},
  };
  EarlyResult r;
  PunchRaceOptions options;
  options.relayCommitMs = commitMs;
  r.answered = rendezvous.PunchAny(candidates, budgetMs, options, &r.chosen, &r.report, &r.error, stop);
  remote60::native_poc::SetPunchRecvHookForTest(nullptr);
  remote60::native_poc::SetPunchClockHookForTest(nullptr);
  gStopFlag = nullptr;
  gStopAtMs = -1;
  return r;
}

std::string describe(const EarlyResult& r) {
  return "answered=" + std::to_string(r.answered) + " kind=" + r.chosen.kind + " decision=" + r.report.decision +
         " chosenMs=" + std::to_string(r.report.chosenMs) + " err=" + r.error;
}

void TestEarlyRaceBoundaries() {
  std::printf("early race: the commit point on a scripted clock (999 / 1000 / 1001, relay first/late/lost)\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();

  // relay at once, direct just before the commit point: direct wins at 999
  auto r = run_scripted(rendezvous, {{20, kRelayPort, 0}, {999, kDirectPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 999 && r.report.decision == "direct" &&
             r.report.relayHeld && r.report.firstRelayMs == 20,
         "relay at 20, direct at 999: direct at 999 (" + describe(r) + ")");
  // direct exactly at the commit point: the commit comes first
  r = run_scripted(rendezvous, {{20, kRelayPort, 0}, {1000, kDirectPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 1000 && r.report.decision == "relay",
         "relay at 20, direct at 1000: relay at 1000 (" + describe(r) + ")");
  // direct just after: relay at 1000, not later
  r = run_scripted(rendezvous, {{20, kRelayPort, 0}, {1001, kDirectPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 1000,
         "relay at 20, direct at 1001: relay at 1000 (" + describe(r) + ")");
  // no relay answer by the commit point: no failure at 1000, a direct answer at 1001 still wins
  r = run_scripted(rendezvous, {{1001, kDirectPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 1001 && !r.report.relayHeld,
         "no relay, direct at 1001: direct at 1001 -- nothing ends at the commit point (" + describe(r) + ")");
  // relay later than the commit point (an older server's grace, or a slow one): taken when it comes
  r = run_scripted(rendezvous, {{2500, kRelayPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 2500 &&
             r.report.decision == "relay-after-commit",
         "relay only at 2500: relay at 2500 (" + describe(r) + ")");
  // relay late, direct between commit point and relay: direct
  r = run_scripted(rendezvous, {{1500, kDirectPort, 0}, {2500, kRelayPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 1500,
         "direct at 1500, relay at 2500: direct at 1500 (" + describe(r) + ")");
  // relay lost, nothing else: the budget, not the commit point, ends it
  r = run_scripted(rendezvous, {}, 1000, 4000);
  expect(!r.answered && r.report.decision == "none" && r.error == "no candidate answered",
         "nothing answers: no answer at the budget (" + describe(r) + ")");
  // the private candidate is a direct one too
  r = run_scripted(rendezvous, {{10, kRelayPort, 0}, {30, kPrivatePort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "private" && r.report.chosenMs == 30,
         "relay at 10, private at 30: private (" + describe(r) + ")");
  // a commit point beyond the budget: a held relay answer is still taken when the budget ends
  r = run_scripted(rendezvous, {{20, kRelayPort, 0}}, 5000, 4000);
  expect(r.answered && r.chosen.kind == "relay",
         "commit beyond the budget: the held relay is taken at the budget (" + describe(r) + ")");
  rendezvous.Close();
}

void TestLegacyRaceIsUnchanged() {
  std::printf("legacy race (no relayEarly): first valid answer wins, the relay included -- as before\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  // An older server's relay answers after its grace (2500): a direct answer before it wins.
  auto r = run_scripted(rendezvous, {{360, kDirectPort, 0}, {2500, kRelayPort, 0}}, 0);
  expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 360 && r.report.decision == "direct",
         "direct at 360, relay at 2500: direct at 360 (" + describe(r) + ")");
  r = run_scripted(rendezvous, {{2500, kRelayPort, 0}}, 0);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 2500 && r.report.decision == "relay",
         "relay only at 2500: relay at 2500 (" + describe(r) + ")");
  // Why the early race is only for an accepted connect: without the hold, a relay that answers at once
  // beats a direct path that would have answered 300 ms later.
  r = run_scripted(rendezvous, {{20, kRelayPort, 0}, {300, kDirectPort, 0}}, 0);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 20,
         "legacy, relay at 20, direct at 300: relay at 20 (the reason the server keeps its grace) (" +
             describe(r) + ")");
  rendezvous.Close();
}

void TestAnswerShapeIsChecked() {
  std::printf("answers are checked for shape: garbage from a candidate is not ready (contract 5)\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  for (int mode : {0, 1000}) {
    auto r = run_scripted(rendezvous,
                          {{10, kRelayPort, 1}, {20, kRelayPort, 2}, {30, kDirectPort, 3}, {40, kDirectPort, 4},
                           {50, kRelayPort, 5}, {1500, kDirectPort, 0}},
                          static_cast<uint32_t>(mode));
    expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 1500 &&
               r.report.invalidFromCandidate == 5 && r.report.firstRelayMs < 0,
           std::string(mode ? "early" : "legacy") + ": five malformed answers ignored, the valid one at 1500 taken (" +
               describe(r) + " invalid=" + std::to_string(r.report.invalidFromCandidate) + ")");
  }
  // the pure function on its own
  UdpHelloPacket p{};
  p.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
  expect(remote60::native_poc::punch_answer_is_valid(reinterpret_cast<const char*>(&p), sizeof(p)),
         "a real Punch is valid");
  expect(!remote60::native_poc::punch_answer_is_valid(reinterpret_cast<const char*>(&p), 0), "zero bytes are not");
  expect(!remote60::native_poc::punch_answer_is_valid(nullptr, sizeof(p)), "no buffer is not");
  p.size = 48;
  expect(!remote60::native_poc::punch_answer_is_valid(reinterpret_cast<const char*>(&p), sizeof(p)),
         "a declared size that is not 49 is not");
  rendezvous.Close();
}

void TestStrangersAreCountedNotChosen() {
  std::printf("datagrams from no candidate: counted per tuple, bounded, never chosen (contract 1)\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  // A valid Punch from the host's public IP on another port (the H-a shape), twice, then the relay.
  auto r = run_scripted(rendezvous, {{100, 42000, 0}, {250, 42000, 0}, {260, 42001, 5}, {300, kRelayPort, 0}}, 1000);
  bool found = false;
  for (const auto& s : r.report.strangers) {
    if (s.port == 42000 && s.datagrams == 2 && s.validPunches == 2 && s.firstMs == 100 && s.firstBytes == 49) found = true;
  }
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 1000 && r.report.strangerDatagrams == 3 &&
             r.report.strangers.size() == 2 && found,
         "a valid Punch from 127.0.0.1:42000 is recorded (n=2 punch=2 firstMs=100), the relay is still chosen at 1000 (" +
             describe(r) + ")");
  // Twenty distinct strangers: the table keeps eight, the rest are counted.
  std::vector<Scripted> many;
  for (int i = 0; i < 20; ++i) many.push_back({10 + i, static_cast<uint16_t>(43000 + i), 0});
  r = run_scripted(rendezvous, many, 1000, 1500);
  expect(!r.answered && r.report.strangers.size() == PunchRaceReport::kMaxStrangers &&
             r.report.strangersUntracked == 12 && r.report.strangerDatagrams == 20,
         "20 strangers: 8 tracked, 12 untracked, none chosen (" + describe(r) + ")");
  rendezvous.Close();
}

void TestCancelOutranksTheChoice() {
  std::printf("a cancel outranks any choice, at the commit point too (contract 4)\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  std::atomic<bool> stop{false};
  // The relay is held; the stop lands just as the clock reaches the commit point.
  auto r = run_scripted(rendezvous, {{20, kRelayPort, 0}}, 1000, 4000, &stop, 1000);
  expect(!r.answered && r.error == "cancelled" && r.report.decision == "cancelled" && r.report.relayHeld,
         "relay held, stop at 1000: cancelled, nothing chosen (" + describe(r) + ")");
  stop = false;
  r = run_scripted(rendezvous, {{1500, kDirectPort, 0}}, 1000, 4000, &stop, 600);
  expect(!r.answered && r.error == "cancelled", "stop at 600 before any answer: cancelled (" + describe(r) + ")");
  rendezvous.Close();
}

// The same on real sockets and the real clock, with the margin the scheduler needs: a direct path that
// opens at 300 ms beats an immediate relay; a dead direct path gives the relay at about 1000 ms.
void TestEarlyRaceOnRealSockets() {
  std::printf("early race on real sockets: direct at ~300 beats an immediate relay; no direct -> relay at ~1000\n");
  {
    ObserveStub directory;
    Responder relay;
    LateResponder host;
    if (!directory.Start() || !relay.Start() || !host.Start(300)) {
      expect(false, "test rig failed to start");
      return;
    }
    DirectoryRendezvous rendezvous;
    std::string observed, error;
    rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
    const std::vector<RendezvousCandidate> candidates = {
        {"127.0.0.1", dead_port(), "private"},
        {"127.0.0.1", host.port(), "public"},
        {"127.0.0.1", relay.port(), "relay"},
    };
    PunchRaceOptions options;
    options.relayCommitMs = 1000;
    PunchRaceReport report;
    RendezvousCandidate chosen;
    const bool answered = rendezvous.PunchAny(candidates, 4000, options, &chosen, &report, &error);
    expect(answered && chosen.kind == "public" && report.relayHeld && report.chosenMs >= 250 && report.chosenMs < 700,
           "direct chosen at " + std::to_string(report.chosenMs) + " ms with the relay held since " +
               std::to_string(report.firstRelayMs) + " ms");
    rendezvous.Close();
    host.Stop();
    relay.Stop();
    directory.Stop();
  }
  {
    ObserveStub directory;
    Responder relay;
    if (!directory.Start() || !relay.Start()) {
      expect(false, "test rig failed to start");
      return;
    }
    DirectoryRendezvous rendezvous;
    std::string observed, error;
    rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
    const std::vector<RendezvousCandidate> candidates = {
        {"127.0.0.1", dead_port(), "private"},
        {"127.0.0.1", dead_port(), "public"},
        {"127.0.0.1", relay.port(), "relay"},
    };
    PunchRaceOptions options;
    options.relayCommitMs = 1000;
    PunchRaceReport report;
    RendezvousCandidate chosen;
    const auto start = std::chrono::steady_clock::now();
    const bool answered = rendezvous.PunchAny(candidates, 4000, options, &chosen, &report, &error);
    const long long elapsed = ms_since(start);
    expect(answered && chosen.kind == "relay" && report.chosenMs >= 1000 && report.chosenMs < 1100 && elapsed < 1150,
           "relay chosen at " + std::to_string(report.chosenMs) + " ms (call " + std::to_string(elapsed) + " ms)");
    rendezvous.Close();
    relay.Stop();
    directory.Stop();
  }
}

// np-impl r1/r2: the field samples, replayed. The measured set is the PC viewer's own punch-to-answer times from
// the office to the home PC (NAS viewer.log 10-26..27, 0.2.162: nine of them, 281..415 ms) -- the same T0 as this
// race. Kept apart, as references only: the host's slowest wake-to-punch (482, another measurement: the host's
// clock, from the wake) and the phone's two (554, 626, connect to chosen). With the relay answering at once (an
// accepted connect, ~5 ms) each must still be chosen direct. The relay case is a SYNTHETIC input -- the test simply
// leaves the direct answer out -- and shows the policy (chosen at the commit point instead of ~2502), not why those
// field attempts got no direct answer.
void TestFieldSamplesReplay() {
  std::printf("field samples replayed: every measured direct answer still wins; relay-won ones take ~1000 not ~2502\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  // The measured set: the PC viewer's own punch-to-answer times (the same T0 as this race) and the host's
  // slowest wake-to-punch. The phone's two are NOT the same measurement (its log spans connect to chosen,
  // a different start point -- Codex NP contract 3), so they are a separate, labelled reference below.
  const int pcSamples[] = {281, 301, 324, 338, 357, 361, 369, 382, 415};
  int pcOk = 0;
  for (int at : pcSamples) {
    auto r = run_scripted(rendezvous, {{5, kRelayPort, 0}, {at, kDirectPort, 0}}, 1000);
    if (r.answered && r.chosen.kind == "public" && r.report.chosenMs == at) ++pcOk;
    else expect(false, "sample " + std::to_string(at) + " ms: " + describe(r));
  }
  expect(pcOk == 9, "9 of 9 PC-viewer samples (281..415 ms, the race's own T0) chosen direct, at their own time");
  const int referenceSamples[] = {482, 554, 626};
  int referenceOk = 0;
  for (int at : referenceSamples) {
    auto r = run_scripted(rendezvous, {{5, kRelayPort, 0}, {at, kDirectPort, 0}}, 1000);
    if (r.answered && r.chosen.kind == "public" && r.report.chosenMs == at) ++referenceOk;
  }
  expect(referenceOk == 3, "reference only (other measurements: host wake-to-punch 482, phone 554/626): also direct");
  int relayOk = 0;
  for (int i = 0; i < 9; ++i) {  // the 9 relay-won PC samples: no direct answer at all
    auto r = run_scripted(rendezvous, {{5, kRelayPort, 0}}, 1000);
    if (r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 1000) ++relayOk;
  }
  expect(relayOk == 9, "synthetic (direct left out), 9 runs: relay chosen at 1000, where the server grace made it ~2502");
  rendezvous.Close();
}

int main() {
  std::string initError;
  if (!remote60::native_poc::initialize_sockets(&initError)) {
    std::printf("socket init failed: %s\n", initError.c_str());
    return 1;
  }

  TestBlockedFirstCandidateDoesNotDelayTheWorkingOne();
  TestAllBlockedReportsFailureWithoutHanging();
  TestUnresolvableCandidateDoesNotSinkTheOthers();
  TestImmediateErrorsDoNotSpeedUpTheRounds();
  TestUnrelatedDatagramsDoNotSpeedUpTheRounds();
  TestALateAnswerIsTakenAtOnce();
  TestStopEndsItPromptly();
  TestTtlOnOnePathDoesNotEndThePunch();
  TestARealSocketFailureEndsIt();
  TestContinuousTtlKeepsTheCadence();
  TestEarlyRaceBoundaries();
  TestLegacyRaceIsUnchanged();
  TestAnswerShapeIsChecked();
  TestStrangersAreCountedNotChosen();
  TestCancelOutranksTheChoice();
  TestEarlyRaceOnRealSockets();
  TestFieldSamplesReplay();

  if (gFailures != 0) {
    std::printf("punch_any_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("punch_any_test: PASS\n");
  return 0;
}
