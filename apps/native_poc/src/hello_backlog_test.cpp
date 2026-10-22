// hello-delay-fix r1: the Hello handshake against a socket that already holds things that are not the ack.
//
// Measured in reconnect-hello-delay r1: the punch that precedes the Hello left ~25 punch replies and up to
// ~100 ICMP resets (Windows reports an earlier punch's Port Unreachable as WSAECONNRESET on a later recv)
// queued on the viewer's socket. The handshake took one of them per Hello and paused 50 ms after each, so
// an ack that had already arrived was read seconds later -- past the host's 10 s control timeout once.
//
// Everything here is the product's udp_hello_handshake on real loopback sockets. The backlog is made the
// way the field made it: punches from the viewer's socket to ports nobody listens on (the ICMP resets),
// then punch replies and the ack from the "host" socket. What this proves and what it does not: the
// loop's handling of a queue it is given; not the host's ack timing, nor a real network's.
//
// Tags: network (loopback), pure-logic over real sockets.

#define NOMINMAX  // native_socket.hpp uses std::min; windows.h defines a macro of that name.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"

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

SOCKET bound_loopback() {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  return s;
}

sockaddr_in address_of(SOCKET s) {
  sockaddr_in addr{};
  int len = sizeof(addr);
  getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
  return addr;
}

uint16_t dead_port() {
  SOCKET s = bound_loopback();
  const uint16_t port = ntohs(address_of(s).sin_port);
  closesocket(s);
  return port;
}

UdpHelloPacket punch() {
  UdpHelloPacket p{};
  p.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
  return p;
}

UdpHelloPacket ack(uint32_t features, uint16_t version = kUdpProtocolVersion) {
  UdpHelloPacket p{};
  p.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
  p.version = version;
  p.features = features;
  return p;
}

void send_to(SOCKET from, const sockaddr_in& to, const UdpHelloPacket& p) {
  (void)sendto(from, reinterpret_cast<const char*>(&p), sizeof(p), 0, reinterpret_cast<const sockaddr*>(&to),
               sizeof(to));
}

// The pair as the viewer has it after the punch: a viewer socket connected to the host socket. With
// `resets` ICMP resets queued first (punches from the viewer's socket to dead loopback ports).
struct Pair {
  SOCKET host = INVALID_SOCKET;
  SOCKET viewer = INVALID_SOCKET;
  sockaddr_in hostAddr{};
  sockaddr_in viewerAddr{};
  explicit Pair(int resets) {
    host = bound_loopback();
    viewer = bound_loopback();
    hostAddr = address_of(host);
    viewerAddr = address_of(viewer);
    for (int i = 0; i < resets; ++i) {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      to.sin_port = htons(dead_port());
      send_to(viewer, to, punch());
    }
    Sleep(100);  // the ICMP answers come back
    connect(viewer, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
  }
  ~Pair() {
    closesocket(host);
    closesocket(viewer);
  }
};

UdpHelloOptions viewer_options() {  // viewer_udp_hello_options' timing
  UdpHelloOptions o;
  o.authToken = "capability";
  o.budgetMs = 10000;
  o.sliceMaxMs = 200;
  o.retrySleepMs = 50;
  return o;
}

UdpHelloOptions android_options() {  // native_video_client_session.cpp's: retrySleepMs 0, slice 250
  UdpHelloOptions o;
  o.authToken = "capability";
  o.budgetMs = 3000;
  return o;
}

constexpr uint32_t kGoodAck = kUdpFeatureVideoFec | kUdpFeatureDirectoryAuth;

void backlog_case(const char* who, const UdpHelloOptions& options) {
  Pair pair(100);
  for (int i = 0; i < 25; ++i) send_to(pair.host, pair.viewerAddr, punch());
  send_to(pair.host, pair.viewerAddr, ack(kGoodAck));
  UdpHelloStats stats;
  std::string error;
  uint32_t features = 0;
  const auto t0 = std::chrono::steady_clock::now();
  const bool ok = udp_hello_handshake(pair.viewer, options, nullptr, &error, &features, &stats);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  const std::string detail = std::to_string(ms) + " ms, attempts=" + std::to_string(stats.attempts) +
                             " badAcks=" + std::to_string(stats.badAcks) + " recvErrors=" + std::to_string(stats.recvErrors);
  check(std::string(who) + ": 25 punch replies + 100 resets queued ahead of the ack -> the ack is taken WITHIN 1 s",
        ok && ms < 1000, detail);
  check(std::string(who) + ": ...all 25 replies were read as not-the-ack, and counted so", stats.badAcks == 25, detail);
  check(std::string(who) + ": ...in ONE Hello round (reading faster sent nothing more)", stats.attempts == 1, detail);
  check(std::string(who) + ": ...the ack's features are the host's", features == kGoodAck);
}

// Sends a datagram to `to` every millisecond until stopped: a queue that never runs dry.
struct Flood {
  std::atomic<bool> run{true};
  std::thread t;
  Flood(SOCKET from, sockaddr_in to, UdpHelloPacket p) {
    t = std::thread([this, from, to, p] {
      while (run.load()) {
        send_to(from, to, p);
        Sleep(1);
      }
    });
  }
  ~Flood() {
    run = false;
    t.join();
  }
};

// Answers every Hello it reads with `reply`.
struct Answerer {
  std::atomic<bool> run{true};
  std::thread t;
  Answerer(SOCKET host, UdpHelloPacket reply) {
    DWORD to = 50;
    setsockopt(host, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    t = std::thread([this, host, reply] {
      char buf[512];
      while (run.load()) {
        sockaddr_in from{};
        int len = sizeof(from);
        const int n = recvfrom(host, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &len);
        if (n > 0) send_to(host, from, reply);
      }
    });
  }
  ~Answerer() {
    run = false;
    t.join();
  }
};

// r2 (Codex N1): far more than one batch (512) of not-the-ack, faster than any slice: queued up front and
// kept topped up by a sender that never sleeps. The viewer's buffer is enlarged so the queue really holds
// them (the product's is 1 MB); a run where no full batch was read proves nothing and says so.
struct Torrent {
  std::atomic<bool> run{true};
  std::thread t;
  Torrent(SOCKET from, sockaddr_in to) {
    t = std::thread([this, from, to] {
      const UdpHelloPacket p = punch();
      while (run.load()) send_to(from, to, p);
    });
  }
  ~Torrent() {
    run = false;
    t.join();
  }
};

void enlarge_receive_buffer(SOCKET s) {
  int bytes = 8 * 1024 * 1024;
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bytes), sizeof(bytes));
}

double thread_cpu_ms() {
  FILETIME c{}, e{}, k{}, u{};
  GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
  const auto ms = [](FILETIME f) {
    return static_cast<double>((static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10000.0;
  };
  return ms(k) + ms(u);
}

void torrent_case(const char* who, UdpHelloOptions options, uint32_t maxAttempts) {
  Pair pair(0);
  enlarge_receive_buffer(pair.viewer);
  for (int i = 0; i < 3000; ++i) send_to(pair.host, pair.viewerAddr, punch());
  Torrent torrent(pair.host, pair.viewerAddr);
  options.budgetMs = 1000;
  UdpHelloStats stats;
  std::string error;
  const double cpu0 = thread_cpu_ms();
  const auto t0 = std::chrono::steady_clock::now();
  const bool ok = udp_hello_handshake(pair.viewer, options, nullptr, &error, nullptr, &stats);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  const double cpu = thread_cpu_ms() - cpu0;
  const std::string detail = std::to_string(ms) + " ms, attempts=" + std::to_string(stats.attempts) +
                             " badAcks=" + std::to_string(stats.badAcks) + " fullBatches=" +
                             std::to_string(stats.fullBatches) + " cpu=" + std::to_string(static_cast<int>(cpu)) + " ms";
  check(std::string(who) + ": the torrent really filled batches (else this run proves nothing)", stats.fullBatches >= 2,
        detail);
  check(std::string(who) + ": no ack -> not accepted, the budget ends it on time", !ok && stats.budgetSpent && ms < 1500,
        detail);
  check(std::string(who) + ": a full batch does NOT bring the next Hello forward (round clock: at most " +
            std::to_string(maxAttempts) + ")",
        stats.attempts <= maxAttempts, detail);
  check(std::string(who) + ": ...and the reading is bounded: the thread used under 70% of a core", cpu < 0.7 * ms, detail);
}

}  // namespace

int main() {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  std::cout << "hello_backlog_test (hello-delay-fix r1/r2)\n\n[a queued backlog ahead of the ack]\n";
  backlog_case("viewer options (slice 200, pause 50)", viewer_options());
  backlog_case("android options (slice 250, pause 0)", android_options());

  std::cout << "\n[r2 N1: more than a batch of not-the-ack, faster than the slice]\n";
  torrent_case("viewer options (200/50)", viewer_options(), 5);
  torrent_case("android options (250/0)", android_options(), 5);
  {
    // The ack behind several batches is still found, in the same round.
    Pair pair(0);
    enlarge_receive_buffer(pair.viewer);
    for (int i = 0; i < 2000; ++i) send_to(pair.host, pair.viewerAddr, punch());
    send_to(pair.host, pair.viewerAddr, ack(kGoodAck));
    UdpHelloStats stats;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = udp_hello_handshake(pair.viewer, viewer_options(), nullptr, &error, nullptr, &stats);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check("2000 punch replies then the ack: taken in the first round, across full batches",
          ok && stats.attempts == 1 && stats.fullBatches >= 2 && ms < 1000,
          std::to_string(ms) + " ms, attempts=" + std::to_string(stats.attempts) + " badAcks=" +
              std::to_string(stats.badAcks) + " fullBatches=" + std::to_string(stats.fullBatches));
  }
  {
    Pair pair(0);
    enlarge_receive_buffer(pair.viewer);
    for (int i = 0; i < 3000; ++i) send_to(pair.host, pair.viewerAddr, punch());
    Torrent torrent(pair.host, pair.viewerAddr);
    std::atomic<bool> stop{false};
    std::thread stopper([&] {
      Sleep(300);
      stop = true;
    });
    UdpHelloStats stats;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = udp_hello_handshake(pair.viewer, android_options(), &stop, &error, nullptr, &stats);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    stopper.join();
    check("a stop during the torrent ends it promptly", !ok && !stats.budgetSpent && ms < 600,
          std::to_string(ms) + " ms, attempts=" + std::to_string(stats.attempts));
  }

  std::cout << "\n[no ack while not-the-ack keeps coming: the budget, the stop and the send rate hold]\n";
  {
    Pair pair(0);
    Flood flood(pair.host, pair.viewerAddr, punch());
    UdpHelloOptions o = viewer_options();
    o.budgetMs = 1000;
    UdpHelloStats stats;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = udp_hello_handshake(pair.viewer, o, nullptr, &error, nullptr, &stats);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    const std::string detail = std::to_string(ms) + " ms, attempts=" + std::to_string(stats.attempts) +
                               " badAcks=" + std::to_string(stats.badAcks);
    check("a continuous stream of punches, no ack: not accepted, the budget ends it", !ok && stats.budgetSpent, detail);
    check("...on time (budget 1000 ms)", ms >= 900 && ms < 1500, detail);
    check("...with Hellos only on the round clock (slice 200 + pause 50 -> at most 5-6), not one per datagram",
          stats.attempts <= 6, detail);
  }
  {
    Pair pair(0);
    Flood flood(pair.host, pair.viewerAddr, punch());
    UdpHelloOptions o = android_options();
    o.budgetMs = 1000;
    UdpHelloStats stats;
    std::string error;
    const bool ok = udp_hello_handshake(pair.viewer, o, nullptr, &error, nullptr, &stats);
    check("android options, the same stream: Hellos on the round clock (slice 250 -> at most 4-5), not hundreds",
          !ok && stats.attempts <= 5, "attempts=" + std::to_string(stats.attempts));
  }
  {
    Pair pair(0);
    Flood flood(pair.host, pair.viewerAddr, punch());
    std::atomic<bool> stop{false};
    std::thread stopper([&] {
      Sleep(300);
      stop = true;
    });
    UdpHelloStats stats;
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = udp_hello_handshake(pair.viewer, viewer_options(), &stop, &error, nullptr, &stats);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    stopper.join();
    check("a stop during the stream ends it promptly (stop at 300 ms)", !ok && !stats.budgetSpent && ms < 600,
          std::to_string(ms) + " ms");
  }

  std::cout << "\n[the ack test is unchanged: reading more never accepts more]\n";
  {
    Pair pair(0);
    Answerer host(pair.host, ack(kUdpFeatureVideoFec));  // no DirectoryAuth
    UdpHelloOptions o = viewer_options();
    o.budgetMs = 700;
    UdpHelloStats stats;
    std::string error;
    const bool ok = udp_hello_handshake(pair.viewer, o, nullptr, &error, nullptr, &stats);
    check("a token was sent and the ack lacks DirectoryAuth: refused", !ok && stats.badAcks > 0,
          "badAcks=" + std::to_string(stats.badAcks));
  }
  {
    Pair pair(0);
    Answerer host(pair.host, ack(kGoodAck, static_cast<uint16_t>(kUdpProtocolVersion + 1)));
    UdpHelloOptions o = viewer_options();
    o.budgetMs = 700;
    UdpHelloStats stats;
    std::string error;
    check("an ack of another protocol version: refused", !udp_hello_handshake(pair.viewer, o, nullptr, &error, nullptr, &stats));
  }
  {
    Pair pair(0);
    Answerer host(pair.host, ack(kUdpFeatureDirectoryAuth));  // no FEC
    UdpHelloOptions o = viewer_options();
    o.budgetMs = 700;
    UdpHelloStats stats;
    std::string error;
    check("an ack without the FEC feature: refused", !udp_hello_handshake(pair.viewer, o, nullptr, &error, nullptr, &stats));
  }
  {
    Pair pair(0);
    Answerer host(pair.host, ack(kUdpFeatureVideoFec));
    UdpHelloOptions o = viewer_options();
    o.authToken.clear();  // a LAN Hello: no token, no DirectoryAuth needed
    o.budgetMs = 2000;
    UdpHelloStats stats;
    std::string error;
    check("control: a LAN Hello (no token) and a plain ack: accepted, as before",
          udp_hello_handshake(pair.viewer, o, nullptr, &error, nullptr, &stats));
  }
  {
    Pair pair(0);
    Answerer host(pair.host, ack(kGoodAck));
    UdpHelloStats stats;
    std::string error;
    const bool ok = udp_hello_handshake(pair.viewer, viewer_options(), nullptr, &error, nullptr, &stats);
    check("control: a token and a full ack: accepted on the first Hello", ok && stats.attempts == 1,
          "attempts=" + std::to_string(stats.attempts));
  }

  std::cout << "\nRESULT: " << (gFailures ? "FAILED" : "ALL PASS") << "  (" << gChecks << " checks, " << gFailures
            << " failed)\n";
  WSACleanup();
  return gFailures ? 1 : 0;
}
