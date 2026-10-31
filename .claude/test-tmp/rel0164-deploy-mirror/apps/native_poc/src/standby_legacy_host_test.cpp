// An OLDER host -- one that predates standby -- against the product's real StandbyLease / StandbyRunner, on loopback
// (2026-10-28, Codex Finding-3 (4), verifier r5: the pure stand-in for the --old-host cases that are NOT RUN here).
//
// The legacy host is a UDP socket that does what a pre-standby GNLinkStream does with the standby kinds (311..316):
// nothing -- an unknown kind is dropped. It never answers, and it never sends an "Unsupported" (no such thing exists on
// the old wire). It records every datagram it receives, by kind. The lease's behaviour is the product's.
//   * admission: exactly kHelloAttempts StandbyHellos, then the lease says Unsupported -- and nothing more is sent
//     (no further Hello, no Activate, no ordinary Hello);
//   * activation 0: an Unsupported lease gives no activation arguments, and the host never sees an Activate;
//   * cancel: a lease released while it is still admitting stops at once -- nothing more reaches the host;
//   * the runner: StatusOf says Unsupported (the state the app reads: a tap then goes the ordinary way --
//     StandbyKeep.onTap(UNSUPPORTED) == Cold(releaseFirst = true), StandbyKeepTest.kt), TakeActivation refuses.
// What it is NOT: a run of the published 0.2.163 host, nor a recovery of any withheld EVIDENCE -- a model of the old
// wire's silence, with the product's client code against it. Pure: loopback only, no host process, no input, no power.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "standby_runner.hpp"

namespace n = remote60::native_poc;
using State = n::StandbyEntryClient::State;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
uint64_t mono_ms() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct MemStore : n::StandbyGenerationStore {
  std::array<uint8_t, 16> id{};
  uint32_t last = 0;
  bool have = false;
  bool Load(std::array<uint8_t, 16>* d, uint32_t* l) override {
    if (!have) return false;
    *d = id;
    *l = last;
    return true;
  }
  bool Save(const std::array<uint8_t, 16>& d, uint32_t l) override {
    id = d;
    last = l;
    have = true;
    return true;
  }
};
bool rnd(uint8_t* b, size_t len) {
  for (size_t i = 0; i < len; ++i) b[i] = static_cast<uint8_t>(0xC0 + i);
  return true;
}

// A host that predates standby: it receives, counts by kind, and answers nothing of 311..316.
class LegacyHost {
 public:
  ~LegacyHost() { Stop(); }
  bool Start() {
    std::string error;
    if (!n::initialize_sockets(&error)) return false;
    s_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (bind(s_, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) return false;
    socklen_t l = sizeof(a);
    getsockname(s_, reinterpret_cast<sockaddr*>(&a), &l);
    port = ntohs(a.sin_port);
    thread_ = std::thread([this] { Loop(); });
    return true;
  }
  void Stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    n::close_socket(&s_);
  }
  n::StandbyLease::CapabilityFn capability() {
    return [this](n::SocketHandle, n::StandbyCapability* out) {
      out->token = "cap-legacy";
      out->ip = "127.0.0.1";
      out->port = port;
      return true;
    };
  }
  int count(n::UdpPacketKind k) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = kinds_.find(static_cast<uint16_t>(k));
    return it == kinds_.end() ? 0 : it->second;
  }
  int total() {
    std::lock_guard<std::mutex> l(mu_);
    int t = 0;
    for (const auto& kv : kinds_) t += kv.second;
    return t;
  }
  uint16_t port = 0;

 private:
  void Loop() {
    while (!stop_) {
      fd_set rd;
      FD_ZERO(&rd);
      FD_SET(s_, &rd);
      timeval tv{0, 20000};
      if (select(static_cast<int>(s_) + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
      uint8_t buf[2048];
      sockaddr_in from{};
      socklen_t fl = sizeof(from);
      const int got = recvfrom(s_, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
      if (got < 6) continue;
      uint16_t kind = 0;
      std::memcpy(&kind, buf + 4, 2);
      std::lock_guard<std::mutex> l(mu_);
      ++kinds_[kind];  // and nothing is sent back: the old host does not know these kinds
    }
  }
  n::SocketHandle s_ = n::kInvalidSocket;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::mutex mu_;
  std::map<uint16_t, int> kinds_;
};
}  // namespace

int main() {
  LegacyHost host;
  if (!host.Start()) {
    std::printf("FAIL  the legacy host could not bind 127.0.0.1\nRESULT FAIL\n");
    return 1;
  }

  std::printf("--- admission against a host that predates standby ---\n");
  {
    MemStore store;
    n::GenerationAllocator gens(store, rnd);
    n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
    std::string error;
    check("the lease opens", lease.Open(&error), error);
    lease.Start();
    const uint64_t until = mono_ms() + 20000;
    while (mono_ms() < until && lease.state() != State::Unsupported) lease.Pump(20);
    const int hellos = host.count(n::UdpPacketKind::StandbyHello);
    check("no answer: Unsupported, after exactly kHelloAttempts StandbyHellos",
          lease.state() == State::Unsupported && hellos == n::StandbyEntryClient::kHelloAttempts, "hellos=" + std::to_string(hellos));
    const int total0 = host.total();
    const uint64_t quiet = mono_ms() + 2500;
    while (mono_ms() < quiet) lease.Pump(50);
    check("...then nothing more is sent (no further Hello, no Ping)", host.total() == total0,
          std::to_string(host.total() - total0) + " more");
    n::ClientSessionConnectArgs args;
    check("activation 0: an Unsupported lease gives no activation arguments", !lease.ActivationArgs(&args));
    check("...the host never saw an Activate, nor an ordinary Hello (never a legacy Hello from the lease)",
          host.count(n::UdpPacketKind::StandbyActivate) == 0 && host.count(n::UdpPacketKind::Hello) == 0);
  }

  std::printf("\n--- cancel while still admitting ---\n");
  {
    MemStore store;
    n::GenerationAllocator gens(store, rnd);
    n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
    std::string error;
    lease.Open(&error);
    const int hellos0 = host.count(n::UdpPacketKind::StandbyHello);
    lease.Start();
    const uint64_t until = mono_ms() + 5000;
    while (mono_ms() < until && host.count(n::UdpPacketKind::StandbyHello) < hellos0 + 2) lease.Pump(20);
    check("two Hellos out, still admitting", host.count(n::UdpPacketKind::StandbyHello) >= hellos0 + 2 &&
                                                 lease.state() != State::Unsupported);
    lease.Release();
    const int afterRelease = host.count(n::UdpPacketKind::StandbyHello);
    const uint64_t quiet = mono_ms() + 3000;
    while (mono_ms() < quiet) lease.Pump(50);
    check("released: no further Hello in 3 s, no Activate", host.count(n::UdpPacketKind::StandbyHello) == afterRelease &&
                                                                 host.count(n::UdpPacketKind::StandbyActivate) == 0,
          std::to_string(host.count(n::UdpPacketKind::StandbyHello) - afterRelease) + " more");
    n::ClientSessionConnectArgs args;
    check("...and no activation arguments from it", !lease.ActivationArgs(&args));
  }

  std::printf("\n--- the runner (what the app reads) ---\n");
  {
    MemStore store;
    n::GenerationAllocator gens(store, rnd);
    n::StandbyRunner runner(gens, n::kUdpFeatureVideoFec, [&](const std::string&) { return host.capability(); }, mono_ms);
    check("a lease for the old PC starts", runner.Start("legacy"));
    const uint64_t until = mono_ms() + 20000;
    while (mono_ms() < until && runner.StatusOf("legacy").state != State::Unsupported) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check("StatusOf: Unsupported (the app releases it and a tap goes the ordinary way -- StandbyKeep.onTap)",
          runner.StatusOf("legacy").state == State::Unsupported);
    n::ClientSessionConnectArgs args;
    uint64_t op = 0;
    check("TakeActivation refuses: activation 0", !runner.TakeActivation("legacy", &args, &op));
    runner.Release("legacy");
    check("released: gone from the runner", runner.size() == 0);
    check("the legacy host never saw an Activate in this whole run", host.count(n::UdpPacketKind::StandbyActivate) == 0);
  }

  host.Stop();
  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT PASS" : "RESULT FAIL");
  return gFailures == 0 ? 0 : 1;
}
