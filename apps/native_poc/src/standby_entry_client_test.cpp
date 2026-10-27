// keepalive-r2 ② (a, b): the client side of a standby entry, without a socket -- every counterexample the contract
// (docs/keepalive_host_standby_contract.md §2.2, §2.6, §3) and Codex r4 name for the client: the activation answer in
// either order, either answer lost, a bare / late / foreign answer, a cancel, Busy and Superseded with a bounded
// backoff, an older host (no answer = Unsupported, never a legacy Hello), an entry the host forgot, a lease whose proof
// is lost, and generations that never go back across a restart.

#include <cstdio>
#include <cstring>
#include <string>

#include "standby_entry_client.hpp"

namespace n = remote60::native_poc;
using Client = n::StandbyEntryClient;
using Effect = Client::Effect;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

struct MemStore : n::StandbyGenerationStore {
  std::array<uint8_t, 16> id{};
  uint32_t last = 0;
  bool have = false;
  bool failSave = false;
  int saves = 0;
  bool Load(std::array<uint8_t, 16>* d, uint32_t* l) override {
    if (!have) return false;
    *d = id;
    *l = last;
    return true;
  }
  bool Save(const std::array<uint8_t, 16>& d, uint32_t l) override {
    if (failSave) return false;
    id = d;
    last = l;
    have = true;
    ++saves;
    return true;
  }
};
bool rnd(uint8_t* b, size_t n) {
  for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0xA0 + i);
  return true;
}

int count(const Client::Effects& fx, Effect::Kind k) {
  int c = 0;
  for (const auto& e : fx) c += e.kind == k ? 1 : 0;
  return c;
}
const n::UdpStandbyPacket* sent(const Client::Effects& fx, n::UdpPacketKind kind) {
  for (const auto& e : fx)
    if (e.kind == Effect::Send && e.packet.kind == static_cast<uint16_t>(kind)) return &e.packet;
  return nullptr;
}
n::UdpStandbyPacket ack(uint32_t gen, uint32_t status, uint64_t entry = 77) {
  n::UdpStandbyPacket p{};
  p.size = sizeof(p);
  p.kind = static_cast<uint16_t>(n::UdpPacketKind::StandbyAck);
  p.clientGen = gen;
  p.status = status;
  if (status == n::kUdpStandbyStatusOk) {
    p.entryId = entry;
    for (int i = 0; i < 16; ++i) p.entrySecret[i] = static_cast<uint8_t>(i + 1);
  }
  return p;
}
n::UdpStandbyPacket pong(uint64_t entry, uint32_t gen, uint32_t status, bool granted) {
  n::UdpStandbyPacket p{};
  p.size = sizeof(p);
  p.kind = static_cast<uint16_t>(n::UdpPacketKind::StandbyPong);
  p.entryId = entry;
  p.clientGen = gen;
  p.status = status;
  p.features = granted ? n::kUdpStandbyPongActivationGranted : 0;
  return p;
}

// a client already parked on entry 77 at t=1000 (admission gen returned)
uint32_t park(Client& c, uint64_t t = 1000) {
  auto fx = c.Admit("cap-abc", t);
  const auto* h = sent(fx, n::UdpPacketKind::StandbyHello);
  const uint32_t gen = h ? h->clientGen : 0;
  c.OnStandbyPacket(ack(gen, n::kUdpStandbyStatusOk), t + 10);
  return gen;
}

void admission() {
  std::printf("\n--- admission ---\n");
  MemStore s;
  n::GenerationAllocator g(s, rnd);
  Client c(g, n::kUdpFeatureVideoFec);
  auto fx = c.Admit("cap-abc", 1000);
  const auto* h = sent(fx, n::UdpPacketKind::StandbyHello);
  check("Admit sends a StandbyHello with the capability, the device id and a saved generation",
        h && std::string(h->authToken) == "cap-abc" && h->clientGen == 1 && s.last == 1 && h->deviceId[0] == 0xA0);
  fx = c.Tick(1400);
  check("no retransmit before its time", fx.empty());
  fx = c.Tick(1500);
  const auto* h2 = sent(fx, n::UdpPacketKind::StandbyHello);
  check("a retransmit repeats the SAME Hello (gen, capability): the host's replay", h2 && h2->clientGen == 1 &&
        std::string(h2->authToken) == "cap-abc");
  check("an Ack of another generation is not this admission's", c.OnStandbyPacket(ack(9, 0), 1510).empty() &&
        c.state() == Client::State::Admitting);
  c.OnStandbyPacket(ack(1, n::kUdpStandbyStatusOk), 1520);
  check("Ack Ok: parked on the issued entry", c.state() == Client::State::Parked && c.entryId() == 77);
  fx = c.Tick(6520);
  const auto* ping = sent(fx, n::UdpPacketKind::StandbyPing);
  check("parked: a Ping every 5 s with the entry and its secret (no capability)", ping && ping->entryId == 77 &&
        ping->entrySecret[0] == 1 && ping->authToken[0] == 0);
}

void older_host() {
  std::printf("\n--- an older host (never answers) ---\n");
  MemStore s;
  n::GenerationAllocator g(s, rnd);
  Client c(g, 0);
  c.Admit("cap", 0);
  int hellos = 1, unsupported = 0;
  for (uint64_t t = 1; t <= 10000; t += 50) {
    auto fx = c.Tick(t);
    hellos += sent(fx, n::UdpPacketKind::StandbyHello) ? 1 : 0;
    unsupported += count(fx, Effect::Unsupported);
    if (count(fx, Effect::NeedCapability)) unsupported += 100;  // must never ask for another admission by itself
  }
  check("6 Hellos, then Unsupported once -- no further admission, no capability asked",
        hellos == Client::kHelloAttempts && unsupported == 1 && c.state() == Client::State::Unsupported,
        "hellos=" + std::to_string(hellos) + " unsupported=" + std::to_string(unsupported));
  check("Unsupported refuses a new Admit (no automatic connect to it)", c.Admit("cap2", 20000).empty());
}

void busy_and_superseded() {
  std::printf("\n--- Busy / Superseded: a bounded backoff, then a new capability ---\n");
  MemStore s;
  n::GenerationAllocator g(s, rnd);
  Client c(g, 0);
  auto fx = c.Admit("cap", 0);
  c.OnStandbyPacket(ack(1, n::kUdpStandbyStatusBusy), 10);
  check("Busy: waiting, no secret", c.state() == Client::State::Waiting && c.entryId() == 0);
  check("...nothing at once", c.Tick(500).empty());
  fx = c.Tick(1010);
  check("...a new capability after 1 s", count(fx, Effect::NeedCapability) == 1 && c.state() == Client::State::Idle);
  fx = c.Admit("cap2", 1020);
  const auto* h = sent(fx, n::UdpPacketKind::StandbyHello);
  check("the new admission is a NEW, larger generation", h && h->clientGen == 2);
  c.OnStandbyPacket(ack(2, n::kUdpStandbyStatusSuperseded), 1030);
  check("Superseded: waiting again, 2 s this time", c.Tick(2900).empty() && count(c.Tick(3030), Effect::NeedCapability) == 1);
  uint64_t t = 3100;
  for (int i = 0; i < 8; ++i) {  // keep failing: the step stays at 30 s
    c.Admit("capx", t);
    c.OnStandbyPacket(ack(s.last, n::kUdpStandbyStatusBusy), t + 1);
    uint64_t waited = 0;
    while (count(c.Tick(t + 1 + waited), Effect::NeedCapability) == 0 && waited < 100000) waited += 100;
    t += 1 + waited;
    if (i == 7) check("the backoff stops growing at 30 s", waited >= 30000 && waited <= 30100, std::to_string(waited));
  }
}

void lost_entry() {
  std::printf("\n--- an entry the host forgot, a lease whose proof is lost ---\n");
  MemStore s;
  n::GenerationAllocator g(s, rnd);
  Client c(g, 0);
  park(c, 0);
  auto fx = c.OnStandbyPacket(pong(77, 0, n::kUdpStandbyStatusUnknown, false), 6000);
  check("Ping answered Unknown (host restarted / expired): Lost, waiting for a new admission",
        count(fx, Effect::Lost) == 1 && c.state() == Client::State::Waiting && c.entryId() == 0);
  check("...then a new capability (bounded backoff)", count(c.Tick(7100), Effect::NeedCapability) == 1);
  // proof lost: pings go out, nothing comes back
  Client d(g, 0);
  park(d, 100000);
  int lost = 0;
  bool readyAfter = false;
  for (uint64_t t = 100010; t < 140000; t += 100) {
    auto f = d.Tick(t);
    lost += count(f, Effect::Lost);
    if (lost && d.state() == Client::State::Parked) readyAfter = true;
  }
  check("Pings unanswered past 16 s: Lost once -- the entry is never kept as ready", lost == 1 && !readyAfter,
        "lost=" + std::to_string(lost));
  Client e(g, 0);
  park(e, 200000);
  for (uint64_t t = 200010; t < 260000; t += 1000) {
    if (t % 5000 < 1000) e.OnStandbyPacket(pong(77, 0, n::kUdpStandbyStatusOk, false), t);
    e.Tick(t);
  }
  check("Pongs keep the entry parked (a minute)", e.state() == Client::State::Parked);
}

void activation() {
  std::printf("\n--- activation: the Pong of THIS generation and the HelloAck, either order ---\n");
  MemStore s;
  n::GenerationAllocator g(s, rnd);
  for (int order = 0; order < 2; ++order) {
    Client c(g, 0);
    const uint32_t admitGen = park(c);
    auto fx = c.Activate(2000);
    const auto* a = sent(fx, n::UdpPacketKind::StandbyActivate);
    check(order ? "Activate (HelloAck first)" : "Activate (Pong first): a larger generation than the admission",
          a && a->clientGen > admitGen && a->entryId == 77 && c.state() == Client::State::Activating);
    const uint32_t gen = a ? a->clientGen : 0;
    Client::Effects f1, f2;
    if (order == 0) {
      f1 = c.OnStandbyPacket(pong(77, gen, 0, true), 2010);
      f2 = c.OnHelloAck(2011);
    } else {
      f1 = c.OnHelloAck(2010);
      f2 = c.OnStandbyPacket(pong(77, gen, 0, true), 2011);
    }
    check("...not complete on the first answer alone", count(f1, Effect::Activated) == 0);
    check("...complete on the second", count(f2, Effect::Activated) == 1 && c.state() == Client::State::Active);
    check("active: no Ping (the session is the liveness)", sent(c.Tick(30000), n::UdpPacketKind::StandbyPing) == nullptr);
    c.SessionEnded(31000);
    check("the session ended: parked again, pinging", c.state() == Client::State::Parked &&
          sent(c.Tick(31001), n::UdpPacketKind::StandbyPing) != nullptr);
  }
  Client c(g, 0);
  park(c);
  check("a bare HelloAck (no activation) adopts nothing", c.OnHelloAck(1500).empty() && c.state() == Client::State::Parked);
  auto fx = c.Activate(2000);
  const uint32_t gen = sent(fx, n::UdpPacketKind::StandbyActivate)->clientGen;
  c.OnHelloAck(2005);
  check("HelloAck + a Pong of ANOTHER generation: not complete", count(c.OnStandbyPacket(pong(77, gen - 1, 0, true), 2006),
        Effect::Activated) == 0 && c.state() == Client::State::Activating);
  check("HelloAck + a Pong of ANOTHER entry: not complete", count(c.OnStandbyPacket(pong(78, gen, 0, true), 2007),
        Effect::Activated) == 0);
  check("HelloAck + a plain Ping Pong: not complete", count(c.OnStandbyPacket(pong(77, 0, 0, false), 2008),
        Effect::Activated) == 0);
  fx = c.Tick(2400);
  const auto* again = sent(fx, n::UdpPacketKind::StandbyActivate);
  check("the answer lost: Activate again with the SAME generation", again && again->clientGen == gen);
  check("...its Pong then completes it", count(c.OnStandbyPacket(pong(77, gen, 0, true), 2410), Effect::Activated) == 1);
}

void refused_and_cancelled() {
  std::printf("\n--- refused, timed out, cancelled ---\n");
  MemStore s;
  n::GenerationAllocator g(s, rnd);
  Client c(g, 0);
  park(c);
  auto fx = c.Activate(2000);
  uint32_t gen = sent(fx, n::UdpPacketKind::StandbyActivate)->clientGen;
  fx = c.OnStandbyPacket(pong(77, gen, n::kUdpStandbyStatusSuperseded, false), 2010);
  check("refused (Superseded, naming this generation): failed, still parked",
        count(fx, Effect::ActivationFailed) == 1 && c.state() == Client::State::Parked);
  fx = c.Activate(3000);
  gen = sent(fx, n::UdpPacketKind::StandbyActivate)->clientGen;
  fx = c.OnStandbyPacket(pong(77, gen, n::kUdpStandbyStatusUnknown, false), 3010);
  check("refused (Unknown entry): failed... the entry is lost", count(fx, Effect::Lost) == 1 &&
        c.state() == Client::State::Waiting);
  Client d(g, 0);
  park(d);
  d.Activate(2000);
  int failed = 0, lost = 0, sends = 1;
  for (uint64_t t = 2001; t < 9000; t += 10) {
    auto f = d.Tick(t);
    failed += count(f, Effect::ActivationFailed);
    lost += count(f, Effect::Lost);
    sends += sent(f, n::UdpPacketKind::StandbyActivate) ? 1 : 0;
  }
  check("no answer: 10 Activates, then failed once (and the entry lost) -- bounded", failed == 1 && lost == 1 &&
        sends == Client::kActivateAttempts, "sends=" + std::to_string(sends));
  Client e(g, 0);
  park(e);
  fx = e.Activate(2000);
  gen = sent(fx, n::UdpPacketKind::StandbyActivate)->clientGen;
  e.CancelActivation(2100);
  check("cancel: parked again", e.state() == Client::State::Parked);
  check("...its late Pong adopts nothing", count(e.OnStandbyPacket(pong(77, gen, 0, true), 2200), Effect::Activated) == 0);
  check("...its late HelloAck adopts nothing", e.OnHelloAck(2201).empty() && e.state() == Client::State::Parked);
  fx = e.Activate(3000);
  const auto* a2 = sent(fx, n::UdpPacketKind::StandbyActivate);
  check("the next activation: a larger generation", a2 && a2->clientGen > gen);
  check("...the cancelled one's Pong still adopts nothing", count(e.OnStandbyPacket(pong(77, gen, 0, true), 3005),
        Effect::Activated) == 0 && e.OnHelloAck(3006).empty());
}

void release() {
  std::printf("\n--- release (the app leaves the screen, the PC is evicted, sign-out) ---\n");
  MemStore s;
  n::GenerationAllocator g(s, rnd);
  Client c(g, 0);
  park(c);
  auto fx = c.Release(3000);
  const auto* r = sent(fx, n::UdpPacketKind::StandbyRelease);
  check("parked: ONE StandbyRelease with the entry and its secret", r && r->entryId == 77 && r->entrySecret[0] == 1 &&
        count(fx, Effect::Send) == 1);
  check("...then idle: no entry, no Ping, nothing more", c.state() == Client::State::Idle && c.entryId() == 0 &&
        c.Tick(20000).empty() && c.Tick(60000).empty());
  check("...a late Pong of it changes nothing", c.OnStandbyPacket(pong(77, 0, n::kUdpStandbyStatusOk, false), 61000).empty() &&
        c.state() == Client::State::Idle);
  Client d(g, 0);
  park(d);
  auto a = d.Activate(2000);
  const uint32_t gen = sent(a, n::UdpPacketKind::StandbyActivate)->clientGen;
  d.OnStandbyPacket(pong(77, gen, 0, true), 2010);
  d.OnHelloAck(2011);
  fx = d.Release(2100);
  check("active: no Release (a granted entry refuses it; its session ends by itself)", fx.empty() &&
        d.state() == Client::State::Active);
  MemStore s2;
  n::GenerationAllocator g2(s2, rnd);
  Client e(g2, 0);
  e.Admit("cap", 0);
  for (uint64_t t = 1; t <= 10000; t += 50) e.Tick(t);
  e.Release(10001);
  check("an older host (Unsupported) stays known as such after a release", e.state() == Client::State::Unsupported);
}

void generations() {
  std::printf("\n--- generations across a restart ---\n");
  MemStore s;
  {
    n::GenerationAllocator g(s, rnd);
    Client c(g, 0);
    park(c);
    c.Activate(2000);
  }
  const uint32_t before = s.last;
  n::GenerationAllocator g2(s, rnd);  // the app restarted
  Client c(g2, 0);
  auto fx = c.Admit("cap", 0);
  const auto* h = sent(fx, n::UdpPacketKind::StandbyHello);
  check("after a restart the next generation is larger than any sent before", h && h->clientGen == before + 1,
        std::to_string(h ? h->clientGen : 0) + " after " + std::to_string(before));
  check("...and the same device id", h && h->deviceId[0] == 0xA0 && s.id[0] == 0xA0);
  MemStore bad;
  bad.failSave = true;
  n::GenerationAllocator g3(bad, rnd);
  Client d(g3, 0);
  fx = d.Admit("cap", 0);
  check("the store cannot save: nothing is sent under an unsaved generation", sent(fx, n::UdpPacketKind::StandbyHello) ==
        nullptr && d.state() == Client::State::Waiting);
  MemStore damaged;
  damaged.have = true;  // a zero id = nothing usable
  damaged.last = 500;
  n::GenerationAllocator g4(damaged, rnd);
  uint32_t gen = 0;
  check("nothing usable stored: a new device id, generations from 1", g4.Next(&gen) && gen == 1 && damaged.id[0] == 0xA0);
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("standby_entry_client_test (keepalive-r2 ②)\n");
  admission();
  older_host();
  busy_and_superseded();
  lost_entry();
  activation();
  refused_and_cancelled();
  release();
  generations();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
