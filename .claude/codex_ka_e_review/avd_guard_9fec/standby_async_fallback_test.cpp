// keepalive-r2 e (Codex codex_avd_guard_src_r1.md N3): where an activation's failure lands. A REAL StandbyLease, parked
// at a fake host on loopback, activates through the REAL ClientSessionController (the lease's ActivationArgs, as
// standby_adopt hands them over). The fake host answers the StandbyActivate as the pinned test host's fault does (an
// unknown entry) -- or superseded, or with the granted Pong and a HelloAck (the control).
//
// What is pinned here, for the APK's fallback (MainActivity.tryCommit, StandbyKeep.fallbackAfterEstablishFailure):
//   1. Connect returns true -- the generation goes back to the app -- BEFORE the host answered: the failure is not
//      a gen-0 one, it comes later, on the session (state Error), so only the establish path can see it;
//   2. its lastError then carries StandbyKeep.ACTIVATION_UNKNOWN_ENTRY exactly ("activation: entry unknown to the
//      host") for an unknown entry, and NOT for a superseded one;
//   3. the control (granted Pong + HelloAck) connects.
// Loopback only; no product process, no host, nothing outside this process.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "native_video_client_session.hpp"
#include "standby_runner.hpp"

namespace n = remote60::native_poc;
using State = n::StandbyLease::State;

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
bool wait_until(const std::function<bool()>& f, int ms) {
  const uint64_t until = mono_ms() + ms;
  while (mono_ms() < until) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return f();
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
  for (size_t i = 0; i < len; ++i) b[i] = static_cast<uint8_t>(0xB0 + i);
  return true;
}
constexpr uint32_t kGoodAck = n::kUdpFeatureVideoFec | n::kUdpFeatureDirectoryAuth | n::kUdpFeatureVideoFecInterleaved;

enum class Answer { Unknown, Superseded, Granted };

// Admits (Ack Ok), answers Pings; a StandbyActivate is answered only once the test releases it (`release`), as `answer`.
class FakeHost {
 public:
  ~FakeHost() { Stop(); }
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
      out->token = "cap-test";
      out->ip = "127.0.0.1";
      out->port = port;
      return true;
    };
  }
  std::atomic<Answer> answer{Answer::Unknown};
  std::atomic<bool> release{false};
  std::atomic<int> activates{0};
  uint16_t port = 0;

 private:
  void Send(const void* p, int len, const sockaddr_in& to) {
    sendto(s_, static_cast<const char*>(p), len, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  }
  void Pong(uint32_t gen, uint32_t status, bool granted, const sockaddr_in& to) {
    n::UdpStandbyPacket p{};
    p.size = sizeof(p);
    p.kind = static_cast<uint16_t>(n::UdpPacketKind::StandbyPong);
    p.entryId = 77;
    p.clientGen = gen;
    p.status = status;
    p.features = granted ? n::kUdpStandbyPongActivationGranted : 0;
    p.leaseMs = 30000;
    Send(&p, sizeof(p), to);
  }
  void Loop() {
    while (!stop_) {
      fd_set rd;
      FD_ZERO(&rd);
      FD_SET(s_, &rd);
      timeval tv{0, 20000};
      if (select(static_cast<int>(s_) + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
      n::UdpStandbyPacket p{};
      sockaddr_in from{};
      socklen_t fl = sizeof(from);
      const int got = static_cast<int>(recvfrom(s_, reinterpret_cast<char*>(&p), sizeof(p), 0, reinterpret_cast<sockaddr*>(&from), &fl));
      if (got != static_cast<int>(sizeof(p)) || p.magic != n::kMagic) continue;
      switch (static_cast<n::UdpPacketKind>(p.kind)) {
        case n::UdpPacketKind::StandbyHello: {
          n::UdpStandbyPacket a{};
          a.size = sizeof(a);
          a.kind = static_cast<uint16_t>(n::UdpPacketKind::StandbyAck);
          a.clientGen = p.clientGen;
          a.entryId = 77;
          for (int i = 0; i < 16; ++i) a.entrySecret[i] = static_cast<uint8_t>(i + 1);
          a.leaseMs = 30000;
          Send(&a, sizeof(a), from);
          break;
        }
        case n::UdpPacketKind::StandbyPing:
          Pong(0, n::kUdpStandbyStatusOk, false, from);
          break;
        case n::UdpPacketKind::StandbyActivate: {
          ++activates;
          const uint32_t gen = p.clientGen;
          while (!release.load() && !stop_) std::this_thread::sleep_for(std::chrono::milliseconds(5));
          const Answer a = answer.load();
          if (a == Answer::Unknown) {
            Pong(gen, n::kUdpStandbyStatusUnknown, false, from);  // what the pinned host's fail_activation_once sends
          } else if (a == Answer::Superseded) {
            Pong(gen, n::kUdpStandbyStatusSuperseded, false, from);
          } else {
            Pong(gen, n::kUdpStandbyStatusOk, true, from);
            n::UdpHelloPacket ack{};
            ack.kind = static_cast<uint16_t>(n::UdpPacketKind::HelloAck);
            ack.features = kGoodAck;
            Send(&ack, sizeof(ack), from);
          }
          break;
        }
        default:
          break;
      }
    }
  }
  n::SocketHandle s_ = n::kInvalidSocket;
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

bool park(n::StandbyLease& lease) {
  std::string error;
  if (!lease.Open(&error)) return false;
  lease.Start();
  const uint64_t until = mono_ms() + 3000;
  while (mono_ms() < until && lease.state() != State::Parked) lease.Pump(20);
  return lease.state() == State::Parked;
}

const char* name(Answer a) { return a == Answer::Unknown ? "unknown entry" : a == Answer::Superseded ? "superseded" : "granted"; }

void episode(Answer answer) {
  std::printf("\n--- the host answers the activation: %s ---\n", name(answer));
  FakeHost host;
  if (!host.Start()) return check("fake host starts", false);
  host.answer = answer;
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
  check("the lease is parked at the host", park(lease));
  n::ClientSessionConnectArgs args;
  uint64_t op = 0;
  check("activation arguments (what standby_adopt hands to Connect)", lease.ActivationArgs(&args, nullptr, &op) && op != 0);
  args.controlIntervalMs = 50;
  n::ClientSessionController c;
  const bool connected = c.Connect(args);  // the gate returns the generation right after this
  const auto s0 = c.Snapshot();
  const bool sent = wait_until([&] { return host.activates.load() > 0; }, 3000);
  const auto s1 = c.Snapshot();
  check("Connect returned true BEFORE the host answered -- the app gets its generation; the session is not established",
        connected && sent && s1.state != n::ClientSessionState::Connected && s1.state != n::ClientSessionState::Error,
        "state0=" + std::to_string(static_cast<int>(s0.state)) + " state1=" + std::to_string(static_cast<int>(s1.state)));
  host.release = true;
  if (answer == Answer::Granted) {
    const bool up = wait_until([&] { return c.Snapshot().state == n::ClientSessionState::Connected; }, 5000);
    check("control: the granted activation connects (no failure)", up, c.Snapshot().lastError);
  } else {
    const bool failed = wait_until([&] { return c.Snapshot().state == n::ClientSessionState::Error; }, 5000);
    const std::string err = c.Snapshot().lastError;
    const bool unknownText = err.find("activation: entry unknown to the host") != std::string::npos;
    if (answer == Answer::Unknown) {
      check("then the session fails (Error) -- after its generation: the establish path is the one that sees it", failed, err);
      check("...its lastError carries StandbyKeep.ACTIVATION_UNKNOWN_ENTRY exactly", unknownText, err);
    } else {
      check("superseded: the session fails too, and its lastError is NOT the unknown-entry one (no fallback)",
            failed && !unknownText && !err.empty(), err);
    }
  }
  c.Disconnect();
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("standby_async_fallback_test: where an activation's failure lands (keepalive-r2 e, N3)\n");
  episode(Answer::Granted);
  episode(Answer::Unknown);
  episode(Answer::Superseded);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
