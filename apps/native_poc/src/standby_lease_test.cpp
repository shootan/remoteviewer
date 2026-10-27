// keepalive-r2 ② r3 (Codex codex_keepalive_r2_scope2.md N1, N2): a StandbyLease and the StandbyRunner against a fake
// host on loopback (a UDP socket this test answers from) -- the activation's HelloAck test, and every lifetime
// counterexample Codex names: the arguments outliving a ReleaseAll, a Release during the handshake wait, an early
// SessionOver, a late SessionOver / Abandon after a new lease under the same key, and the pump leaving the socket alone
// while handed off. Loopback only (127.0.0.1); no product process, no host, nothing outside this process.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

// A host on 127.0.0.1 that admits (Ack Ok), answers Pings and records Activates / Releases. Activates are answered
// only by the test (Pong / HelloAck datagrams it sends at will).
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
  // to the client that sent the last datagram
  void SendRaw(const void* p, int len) {
    std::lock_guard<std::mutex> l(mu_);
    sendto(s_, static_cast<const char*>(p), len, 0, reinterpret_cast<const sockaddr*>(&peer_), sizeof(peer_));
  }
  void Pong(uint32_t gen, bool granted) {
    n::UdpStandbyPacket p{};
    p.size = sizeof(p);
    p.kind = static_cast<uint16_t>(n::UdpPacketKind::StandbyPong);
    p.entryId = 77;
    p.clientGen = gen;
    p.features = granted ? n::kUdpStandbyPongActivationGranted : 0;
    SendRaw(&p, sizeof(p));
  }
  void HelloAck(uint32_t features, uint32_t version = n::kUdpProtocolVersion, uint16_t declared = sizeof(n::UdpHelloPacket),
                int len = sizeof(n::UdpHelloPacket)) {
    n::UdpHelloPacket a{};
    a.kind = static_cast<uint16_t>(n::UdpPacketKind::HelloAck);
    a.version = version;
    a.size = declared;
    a.features = features;
    SendRaw(&a, len);
  }
  std::atomic<int> pings{0}, releases{0}, activates{0};
  std::atomic<uint32_t> lastActivateGen{0};
  uint16_t port = 0;

 private:
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
      {
        std::lock_guard<std::mutex> l(mu_);
        peer_ = from;
      }
      switch (static_cast<n::UdpPacketKind>(p.kind)) {
        case n::UdpPacketKind::StandbyHello: {
          n::UdpStandbyPacket a{};
          a.size = sizeof(a);
          a.kind = static_cast<uint16_t>(n::UdpPacketKind::StandbyAck);
          a.clientGen = p.clientGen;
          a.entryId = 77;
          for (int i = 0; i < 16; ++i) a.entrySecret[i] = static_cast<uint8_t>(i + 1);
          a.leaseMs = 30000;
          SendRaw(&a, sizeof(a));
          break;
        }
        case n::UdpPacketKind::StandbyPing:
          ++pings;
          Pong(0, false);
          break;
        case n::UdpPacketKind::StandbyActivate:
          ++activates;
          lastActivateGen = p.clientGen;
          break;
        case n::UdpPacketKind::StandbyRelease:
          ++releases;
          break;
        default:
          break;
      }
    }
  }
  n::SocketHandle s_ = n::kInvalidSocket;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::mutex mu_;
  sockaddr_in peer_{};
};

// The connect thread of a session: runs the handshake of `args` as ClientSessionController::Connect would.
struct Handshaking {
  std::atomic<bool> stop{false}, done{false};
  bool ok = false;
  uint32_t features = 0xFFFFFFFFu;
  std::string error;
  std::thread t;
  using Fn = decltype(n::ClientSessionConnectArgs::udpHandshake);
  explicit Handshaking(Fn fn, n::SocketHandle s) {
    t = std::thread([this, fn = std::move(fn), s] {
      ok = fn(s, stop, &error, &features);
      done = true;
    });
  }
  ~Handshaking() {
    stop = true;
    if (t.joinable()) t.join();
  }
};

bool park(n::StandbyLease& lease) {
  std::string error;
  if (!lease.Open(&error)) return false;
  lease.Start();
  const uint64_t until = mono_ms() + 3000;
  while (mono_ms() < until && lease.state() != State::Parked) lease.Pump(20);
  return lease.state() == State::Parked;
}

void hello_ack_test(FakeHost& host) {
  std::printf("\n--- N1: the activation's HelloAck passes the ordinary test, or it is nothing ---\n");
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
  check("parked at the fake host", park(lease));
  n::ClientSessionConnectArgs args;
  uint64_t op = 0;
  auto keep = std::make_shared<int>(1);
  check("activation arguments, an operation number, and the handshake holds the owner",
        lease.ActivationArgs(&args, keep, &op) && op != 0 && keep.use_count() == 2);
  {
    const int before = host.activates.load();
    Handshaking h(args.udpHandshake, args.preparedUdpSocket);
    check("the handshake sends this activation's StandbyActivate", wait_until([&] { return host.activates > before; }, 2000));
    const uint32_t gen = host.lastActivateGen;
    host.HelloAck(kGoodAck, n::kUdpProtocolVersion + 1);                    // another protocol version
    host.HelloAck(n::kUdpFeatureVideoFec);                                    // no DirectoryAuth
    host.HelloAck(n::kUdpFeatureDirectoryAuth);                               // no VideoFec
    host.HelloAck(kGoodAck, n::kUdpProtocolVersion, sizeof(n::UdpHelloPacket) - 4);  // a declared size that is not its own
    host.HelloAck(kGoodAck, n::kUdpProtocolVersion, sizeof(n::UdpHelloPacket), sizeof(n::UdpHelloPacket) - 4);  // short
    host.Pong(gen, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    check("invalid HelloAcks + the granted Pong: NOT activated (no invalid one counted)", !h.done && lease.state() == State::Activating,
          "state=" + std::to_string(static_cast<int>(lease.state())));
    check("...early SessionOver during the handshake: refused, still handed off", !lease.SessionOver(op) && lease.handedOff());
    check("...Abandon during the handshake: refused (the session's handle stays open)",
          !lease.Abandon(op, nullptr) && args.preparedUdpSocket != n::kInvalidSocket);
    host.HelloAck(kGoodAck | 0x40000000u);
    check("a valid HelloAck: activated, with ITS features", wait_until([&] { return h.done.load(); }, 2000) && h.ok &&
          h.features == (kGoodAck | 0x40000000u) && lease.state() == State::Active, h.error);
  }
  n::close_socket(&args.preparedUdpSocket);  // the session's handle (the session closes it)
  check("SessionOver of another operation: nothing", !lease.SessionOver(op + 1000) && lease.handedOff());
  check("SessionOver of its own: the lease has its socket again", lease.SessionOver(op) && !lease.handedOff());
  args = n::ClientSessionConnectArgs{};
  check("the arguments gone: the owner is held no more", keep.use_count() == 1);

  std::printf("\n--- N1: A cancelled, then B; A's late answers; B's own Pong completes B ---\n");
  const uint64_t until = mono_ms() + 7000;
  while (mono_ms() < until && lease.state() != State::Parked) lease.Pump(20);
  n::ClientSessionConnectArgs argsA;
  uint64_t opA = 0;
  check("A: activation arguments", lease.ActivationArgs(&argsA, nullptr, &opA));
  uint32_t genA = 0;
  {
    const int before = host.activates.load();
    Handshaking h(argsA.udpHandshake, argsA.preparedUdpSocket);
    wait_until([&] { return host.activates > before; }, 2000);
    genA = host.lastActivateGen;
    h.stop = true;  // the user gave up (Disconnect during Connect)
    check("A cancelled: the handshake returns false, the entry is parked", wait_until([&] { return h.done.load(); }, 2000) &&
          !h.ok && lease.state() == State::Parked);
  }
  n::close_socket(&argsA.preparedUdpSocket);
  check("...A's session over (Connect failed)", lease.SessionOver(opA));
  n::ClientSessionConnectArgs argsB;
  uint64_t opB = 0;
  check("B: a new operation", lease.ActivationArgs(&argsB, nullptr, &opB) && opB != opA);
  {
    const int before = host.activates.load();
    Handshaking h(argsB.udpHandshake, argsB.preparedUdpSocket);
    wait_until([&] { return host.activates > before; }, 2000);
    const uint32_t genB = host.lastActivateGen;
    host.Pong(genA, true);         // A's late grant
    host.HelloAck(kGoodAck | 0x10);  // the late HelloAck (untagged: the host's session-admission answer)
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    check("A's late Pong + a late HelloAck: B is NOT activated (only B's own Pong grants)", genB > genA && !h.done &&
          lease.state() == State::Activating);
    host.Pong(genB, true);
    check("B's own Pong: B active, with the HelloAck's features (validated)", wait_until([&] { return h.done.load(); }, 2000) &&
          h.ok && h.features == (kGoodAck | 0x10) && lease.state() == State::Active, h.error);
  }
  n::close_socket(&argsB.preparedUdpSocket);
  lease.SessionOver(opB);

  std::printf("\n--- events are bounded ---\n");
  n::StandbyLease none(gens, 0, [](n::SocketHandle, n::StandbyCapability*) { return false; }, mono_ms);
  std::string error;
  none.Open(&error);
  for (int i = 0; i < 200; ++i) none.Start();
  check("200 notes: at most 32 kept", none.events().size() <= 32, std::to_string(none.events().size()));
}

void runner_test(FakeHost& host) {
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  n::StandbyRunner runner(gens, n::kUdpFeatureVideoFec, [&](const std::string&) { return host.capability(); }, mono_ms);
  auto parked = [&](const std::string& k) {
    return wait_until([&] { const auto s = runner.StatusOf(k); return s.state == State::Parked && !s.handedOff; }, 7000);
  };

  std::printf("\n--- N2: the arguments outlive ReleaseAll ---\n");
  const int slots0 = n::StandbyRunner::liveSlots();
  check("a lease parks", runner.Start("k1") && parked("k1"));
  n::ClientSessionConnectArgs args;
  uint64_t op = 0;
  check("TakeActivation", runner.TakeActivation("k1", &args, &op));
  check("...a second one while handed off is refused", [&] {
    n::ClientSessionConnectArgs again;
    return !runner.TakeActivation("k1", &again);
  }());
  const int releases0 = host.releases;
  runner.ReleaseAll();
  check("ReleaseAll: gone from the runner, but the slot lives on in the arguments", runner.size() == 0 &&
        n::StandbyRunner::liveSlots() == slots0 + 1);
  {
    std::atomic<bool> stop{false};
    std::string error;
    uint32_t f = 0;
    const int a0 = host.activates;
    const bool ok = args.udpHandshake(args.preparedUdpSocket, stop, &error, &f);
    check("...its handshake run now: false (cancelled), nothing sent, no use after free", !ok && host.activates == a0, error);
  }
  check("...nothing was released at the host (handed off: the grant, if any, ends with its session)", host.releases == releases0);
  n::close_socket(&args.preparedUdpSocket);
  args = n::ClientSessionConnectArgs{};
  check("the arguments gone: the slot goes", n::StandbyRunner::liveSlots() == slots0);

  std::printf("\n--- N2: a Release during the handshake wait; an early SessionOver ---\n");
  check("a lease parks", runner.Start("k2") && parked("k2"));
  uint64_t op2 = 0;
  check("TakeActivation", runner.TakeActivation("k2", &args, &op2));
  {
    const int before = host.activates.load();
    Handshaking h(args.udpHandshake, args.preparedUdpSocket);
    check("the handshake waits for the host (it never answers)", wait_until([&] { return host.activates > before; }, 2000));
    check("...early SessionOver: refused, still handed off (the pump stays off the socket)",
          !runner.SessionOver("k2", op2) && runner.StatusOf("k2").handedOff);
    check("...early Abandon: refused", !runner.Abandon("k2", op2, nullptr));
    const uint64_t t0 = mono_ms();
    runner.Release("k2");
    const bool returned = wait_until([&] { return h.done.load(); }, 1500);
    check("Release: the handshake in flight returns false at once", returned && !h.ok,
          "after " + std::to_string(mono_ms() - t0) + " ms: " + (returned ? h.error : std::string("still running")));
  }
  n::close_socket(&args.preparedUdpSocket);
  args = n::ClientSessionConnectArgs{};
  check("...and its slot is gone with the arguments", n::StandbyRunner::liveSlots() == slots0);

  std::printf("\n--- N2: a late SessionOver / Abandon after a new lease under the same key ---\n");
  check("a lease parks", runner.Start("k3") && parked("k3"));
  uint64_t oldOp = 0;
  check("TakeActivation (the old one)", runner.TakeActivation("k3", &args, &oldOp));
  runner.Release("k3");  // the screen was left mid-activation
  n::close_socket(&args.preparedUdpSocket);
  args = n::ClientSessionConnectArgs{};
  check("a NEW lease under the same key parks", runner.Start("k3") && parked("k3"));
  check("the late SessionOver of the old operation: nothing", !runner.SessionOver("k3", oldOp) && runner.StatusOf("k3").state ==
        State::Parked);
  uint64_t newOp = 0;
  check("TakeActivation (the new one)", runner.TakeActivation("k3", &args, &newOp) && newOp != oldOp);
  check("...a late SessionOver / Abandon of the old operation: refused, still handed off",
        !runner.SessionOver("k3", oldOp) && !runner.Abandon("k3", oldOp, nullptr) && runner.StatusOf("k3").handedOff);
  check("...its session handle still open", args.preparedUdpSocket != n::kInvalidSocket);

  std::printf("\n--- N2: handed off, the pump leaves the socket alone; Abandon gives it back ---\n");
  // a Ping the pump sent just before the hand-off may still be in flight: let it land before counting
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const int pings0 = host.pings;
  std::this_thread::sleep_for(std::chrono::milliseconds(6000));
  check("6 s handed off: no Ping from the pump (the session's reader alone may read)", host.pings == pings0,
        std::to_string(host.pings - pings0) + " pings");
  auto stale = args.udpHandshake;  // a copy of the arguments' handshake, kept past an Abandon
  check("Abandon of its own operation: the session handle closed, the lease has its socket again",
        runner.Abandon("k3", newOp, &args) && args.preparedUdpSocket == n::kInvalidSocket && !runner.StatusOf("k3").handedOff);
  {
    std::atomic<bool> stop{false};
    std::string error;
    uint32_t f = 0;
    const int a0 = host.activates;
    // a real socket (the abandoned handle is closed): anything the stale handshake sent would reach the host
    n::SocketHandle s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const uint64_t t0 = mono_ms();
    const bool ran = stale(s, stop, &error, &f);
    const uint64_t took = mono_ms() - t0;
    n::close_socket(&s);
    check("a stale copy of the abandoned handshake: false at once, nothing sent, the lease still parked (not activating)",
          !ran && took < 500 && host.activates == a0 && runner.StatusOf("k3").state == State::Parked,
          error + " after " + std::to_string(took) + " ms");
  }
  stale = nullptr;
  check("...the lease pings again on its own", wait_until([&] { return host.pings > pings0; }, 7000) && parked("k3"));
  runner.ReleaseAll();
  check("ReleaseAll: every slot gone", runner.size() == 0 && n::StandbyRunner::liveSlots() == slots0);
}
}  // namespace

int main() {
  FakeHost host;
  if (!host.Start()) {
    std::printf("FAIL  the fake host could not bind 127.0.0.1\n");
    return 1;
  }
  hello_ack_test(host);
  runner_test(host);
  host.Stop();
  std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
  std::printf(gFailures == 0 ? "RESULT PASS\n" : "RESULT FAIL\n");
  return gFailures == 0 ? 0 : 1;
}
