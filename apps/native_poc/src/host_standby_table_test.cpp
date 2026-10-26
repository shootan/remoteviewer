// keepalive-r2 A (spike/host-admission): the host's standby entry table, as pure logic -- and (r3) the
// Standby* handling over it (host_standby_wire.hpp), on Codex's S1-S3 counterexamples
// (codex_keepalive_r2_spike_r1.md).
// Tags: pure-logic.

#include "host_standby_wire.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace remote60::native_poc;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
struct Dev {
  uint8_t id[16] = {};
  explicit Dev(uint8_t b) {
    for (uint8_t& x : id) x = b;
  }
};
const StandbyEndpoint kA{0x0100007F, 1000}, kA2{0x0100007F, 1001}, kRelay{0x0200000A, 43000};
constexpr uint64_t kFp1 = 0x1111, kFp2 = 0x2222;

sockaddr_in Addr(uint16_t port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = 0x0100007F;
  a.sin_port = htons(port);
  return a;
}
UdpStandbyPacket Hello(uint8_t dev, uint32_t gen, const char* token) {
  UdpStandbyPacket p{};
  p.kind = static_cast<uint16_t>(UdpPacketKind::StandbyHello);
  p.size = sizeof(p);
  for (uint8_t& x : p.deviceId) x = dev;
  p.clientGen = gen;
  std::strncpy(p.authToken, token, sizeof(p.authToken) - 1);
  return p;
}
UdpStandbyPacket Op(UdpPacketKind kind, uint64_t entryId, const uint8_t secret[16], uint32_t gen) {
  UdpStandbyPacket p{};
  p.kind = static_cast<uint16_t>(kind);
  p.size = sizeof(p);
  p.entryId = entryId;
  std::memcpy(p.entrySecret, secret, 16);
  p.clientGen = gen;
  return p;
}
}  // namespace

int main() {
  std::printf("host_standby_table_test (keepalive-r2 A, r3)\n\n");
  uint64_t now = 1000000;

  StandbyTable t;
  StandbyEntry a, b, a2, again;
  check("device A admitted", t.Admit(Dev(1).id, 1, kFp1, 0x12, kA, now, &a) == StandbyTable::AdmitResult::Admitted &&
                                 a.entryId != 0);
  check("device B admitted -- from the SAME address (a relay): its own entry",
        t.Admit(Dev(2).id, 1, kFp2, 0, kA, now, &b) == StandbyTable::AdmitResult::Admitted && b.entryId != a.entryId &&
            t.size() == 2);
  check("A's Hello again (same generation, same capability, same endpoint): a REPLAY, same entry and secret",
        t.ClassifyHello(Dev(1).id, 1, kFp1, kA, now + 10, &again) == StandbyTable::HelloClass::Replay &&
            again.entryId == a.entryId && again.secret == a.secret && t.size() == 2);
  check("S1: same device + generation with ANOTHER capability: not a replay (a fresh proof is needed)",
        t.ClassifyHello(Dev(1).id, 1, kFp2, kA, now + 10, nullptr) == StandbyTable::HelloClass::NeedProof);
  check("S1: same device + generation + capability from ANOTHER endpoint: not a replay",
        t.ClassifyHello(Dev(1).id, 1, kFp1, kA2, now + 10, nullptr) == StandbyTable::HelloClass::NeedProof);

  std::printf("\n[ping]\n");
  now += 100;
  uint8_t wrong[16] = {};
  check("ping with the wrong secret: unknown", t.Ping(a.entryId, wrong, kA, now) == StandbyTable::Verdict::Unknown);
  check("ping with the right secret: ok, and the entry follows a moved endpoint",
        t.Ping(a.entryId, a.secret.data(), kA2, now) == StandbyTable::Verdict::Ok && t.Find(a.entryId)->endpoint == kA2);
  check("pinging is all it did: no entry is active", t.activeEntryId() == 0);

  std::printf("\n[one entry per device]\n");
  check("A again, a NEW admission (fresh capability): replaces A's entry",
        t.Admit(Dev(1).id, 2, kFp2, 0, kA, now + 100, &a2) == StandbyTable::AdmitResult::Admitted && a2.entryId != a.entryId &&
            t.size() == 2);
  check("...the old entry is gone", t.Ping(a.entryId, a.secret.data(), kA, now + 110) == StandbyTable::Verdict::Unknown);

  std::printf("\n[activation -- S2]\n");
  now += 200;
  bool fresh = false;
  StandbyEntry got;
  check("S2: gen 0 is never a generation: SUPERSEDED, nothing changes",
        t.Activate(a2.entryId, a2.secret.data(), 0, kA, now, &fresh, &got) == StandbyTable::Verdict::Superseded &&
            t.activeEntryId() == 0);
  check("A activates (gen 2): ok, a NEW session",
        t.Activate(a2.entryId, a2.secret.data(), 2, kA, now, &fresh, &got) == StandbyTable::Verdict::Ok && fresh &&
            t.activeEntryId() == a2.entryId && got.grantId != 0);
  const uint64_t grantA = got.grantId;
  t.BindActiveEpoch(grantA, 7);
  check("A's Activate again (same gen, its Ack lost): ok, NOT a new session, the same grant",
        t.Activate(a2.entryId, a2.secret.data(), 2, kA, now + 10, &fresh, &got) == StandbyTable::Verdict::Ok && !fresh &&
            got.grantId == grantA);
  check("S2: A's LATE gen 1 while A is active: SUPERSEDED -- the high-water does not go down",
        t.Activate(a2.entryId, a2.secret.data(), 1, kA, now + 20, &fresh, &got) == StandbyTable::Verdict::Superseded &&
            t.Find(a2.entryId)->activateHigh == 2 && t.activeEntryId() == a2.entryId);
  check("B activates (gen 1): ok, a new session -- A is parked, not dropped",
        t.Activate(b.entryId, b.secret.data(), 1, kA, now + 30, &fresh, &got) == StandbyTable::Verdict::Ok && fresh &&
            t.activeEntryId() == b.entryId && t.Find(a2.entryId) != nullptr);
  check("S2: A's OLD gen 2 again (2 -> 1 -> B -> 2): SUPERSEDED, B stays",
        t.Activate(a2.entryId, a2.secret.data(), 2, kA, now + 40, &fresh, &got) == StandbyTable::Verdict::Superseded &&
            t.activeEntryId() == b.entryId);
  check("S2: A's gen 0 after B took it (0 -> B -> 0): SUPERSEDED, B stays",
        t.Activate(a2.entryId, a2.secret.data(), 0, kA, now + 41, &fresh, &got) == StandbyTable::Verdict::Superseded &&
            t.activeEntryId() == b.entryId);
  check("A's NEW explicit activation (gen 3): ok, takes it",
        t.Activate(a2.entryId, a2.secret.data(), 3, kA, now + 50, &fresh, &got) == StandbyTable::Verdict::Ok && fresh &&
            t.activeEntryId() == a2.entryId && got.grantId != grantA);
  t.BindActiveEpoch(got.grantId, 9);
  check("activation with the wrong secret: unknown, nothing changes",
        t.Activate(b.entryId, wrong, 5, kA, now + 60, &fresh, &got) == StandbyTable::Verdict::Unknown &&
            t.activeEntryId() == a2.entryId);

  std::printf("\n[the grant ends with its session -- S3]\n");
  t.SessionEnded(7, now + 70);  // a LATE end of A's earlier grant (epoch 7)
  check("S3: a late end of an EARLIER session leaves the current grant alone", t.activeEntryId() == a2.entryId);
  check("S3: the device holding the live session is not re-admitted underneath it (Live)",
        t.Admit(Dev(1).id, 9, 0x9999, 0, kA, now + 75, &got) == StandbyTable::AdmitResult::Live && t.activeEntryId() == a2.entryId);
  check("S3: ...nor released underneath it (Live)", t.Release(a2.entryId, a2.secret.data()) == StandbyTable::Verdict::Live &&
                                                       t.Find(a2.entryId) != nullptr);
  t.SessionEnded(9, now + 80);  // A's session (epoch 9) really ended
  check("S3: its session's real end ends the grant: no entry is active", t.activeEntryId() == 0);
  check("S3: ...and the same generation no longer answers (no Ack of an ended session)",
        t.Activate(a2.entryId, a2.secret.data(), 3, kA, now + 90, &fresh, &got) == StandbyTable::Verdict::Superseded);
  t.Expire(now + 80 + kStandbyLeaseMs + 1);
  check("S3: not pinged after its session ended, A lapses like any parked entry", t.Find(a2.entryId) == nullptr);

  std::printf("\n[lease]\n");
  StandbyTable l;
  StandbyEntry la, lb;
  l.Admit(Dev(1).id, 1, kFp1, 0, kA, now, &la);
  l.Admit(Dev(2).id, 1, kFp2, 0, kA2, now, &lb);
  l.Activate(la.entryId, la.secret.data(), 1, kA, now, &fresh, &got);
  l.BindActiveEpoch(got.grantId, 3);
  const uint64_t later = now + kStandbyLeaseMs + 1;
  l.Expire(later);
  check("after the lease, the parked entry (B, not pinged) lapses; the LIVE one (A) stays",
        l.Find(lb.entryId) == nullptr && l.Find(la.entryId) != nullptr, std::to_string(l.size()) + " left");
  l.SessionTakenByHello(later);
  check("an ordinary Hello took the session: no entry is active", l.activeEntryId() == 0);
  l.Expire(later + kStandbyLeaseMs + 1);
  check("...and A, no longer the session and not pinged, lapses too", l.size() == 0);
  StandbyTable r;
  r.Admit(Dev(1).id, 1, kFp1, 0, kA, now, &la);
  check("S1: a replay after the lease (20-30 s, not swept yet) is NOT a replay -- the entry is gone",
        r.ClassifyHello(Dev(1).id, 1, kFp1, kA, now + kStandbyLeaseMs + 5000, nullptr) == StandbyTable::HelloClass::NeedProof &&
            r.size() == 0);

  std::printf("\n[bounds]\n");
  StandbyTable full;
  StandbyEntry e;
  for (uint8_t d = 1; d <= kStandbyMaxEntries; ++d) full.Admit(Dev(d).id, 1, d, 0, kRelay, now, &e);
  check("eight devices fill the host", full.size() == kStandbyMaxEntries);
  check("a ninth: BUSY, nothing changes",
        full.Admit(Dev(99).id, 1, 99, 0, kRelay, now, &e) == StandbyTable::AdmitResult::Busy && full.size() == kStandbyMaxEntries);
  check("...but a device already holding an entry may renew it (replace), not Busy",
        full.Admit(Dev(3).id, 7, 77, 0, kRelay, now + 5, &e) == StandbyTable::AdmitResult::Admitted &&
            full.size() == kStandbyMaxEntries);
  check("release frees a place", full.Release(e.entryId, e.secret.data()) == StandbyTable::Verdict::Ok &&
                                     full.Admit(Dev(99).id, 1, 99, 0, kRelay, now + 6, &e) == StandbyTable::AdmitResult::Admitted);
  check("release with the wrong secret: unknown", full.Release(e.entryId, wrong) == StandbyTable::Verdict::Unknown);
  StandbyTable lapse;
  for (uint8_t d = 1; d <= kStandbyMaxEntries; ++d) lapse.Admit(Dev(d).id, 1, d, 0, kRelay, now, &e);
  check("a full host whose entries have lapsed admits again (swept first)",
        lapse.Admit(Dev(99).id, 1, 99, 0, kRelay, now + kStandbyLeaseMs + 1, &e) == StandbyTable::AdmitResult::Admitted &&
            lapse.size() == 1);

  std::printf("\n[random draw -- S3]\n");
  StandbyTable failing([](uint64_t* v) { *v = 0; return false; });
  check("S3: the random draw fails: no entry at all (Failed), and it returns (no loop)",
        failing.Admit(Dev(1).id, 1, kFp1, 0, kA, now, &e) == StandbyTable::AdmitResult::Failed && failing.size() == 0);
  int calls = 0;
  StandbyTable zeros([&](uint64_t* v) { ++calls; *v = 0; return true; });
  const bool zerosFailed = zeros.Admit(Dev(1).id, 1, kFp1, 0, kA, now, &e) == StandbyTable::AdmitResult::Failed;
  check("S3: a draw that always gives 0 (no usable id): Failed after a bounded number of tries",
        zerosFailed && zeros.size() == 0 && calls == static_cast<int>(kStandbyIdAttempts), std::to_string(calls) + " draws");
  uint64_t seq = 0;
  StandbyTable colliding([&](uint64_t* v) { ++seq; *v = seq <= 3 ? 42 : seq; return true; });
  colliding.Admit(Dev(1).id, 1, kFp1, 0, kA, now, &e);  // takes id 42 (draws 1..3: id 42, secret parts)
  StandbyEntry e2;
  check("S3: an id collision is retried (a second device still gets its own id)",
        colliding.Admit(Dev(2).id, 1, kFp2, 0, kA, now, &e2) == StandbyTable::AdmitResult::Admitted && e2.entryId != e.entryId);
  uint64_t fails = 0;
  StandbyTable partial([&](uint64_t* v) { *v = 5; return ++fails < 2; });  // the id draws, the secret's fails
  check("S3: a secret draw that fails after the id: still no entry", partial.Admit(Dev(1).id, 1, kFp1, 0, kA, now, &e) ==
                                                                         StandbyTable::AdmitResult::Failed && partial.size() == 0);

  std::printf("\n[Standby* handling -- S1, activation result]\n");
  StandbyTable w;
  int authCalls = 0;
  std::string goodToken = "cap-good-1";
  const auto auth = [&](const std::string& tok, const sockaddr_in&) {
    ++authCalls;
    if (tok != goodToken) return false;
    goodToken.clear();  // single use
    return true;
  };
  UdpStandbyPacket h = Hello(5, 1, "cap-good-1");
  auto o = standby_handle(w, reinterpret_cast<const uint8_t*>(&h), sizeof(h), Addr(2000), false, auth, now);
  check("a StandbyHello with a valid capability: admitted, a secret issued",
        o.action == StandbyAction::Reply && o.reply.status == kUdpStandbyStatusOk && o.reply.entryId != 0 && authCalls == 1);
  const UdpStandbyPacket ack1 = o.reply;
  o = standby_handle(w, reinterpret_cast<const uint8_t*>(&h), sizeof(h), Addr(2000), false, auth, now + 50);
  check("its Ack lost, the same Hello again (same socket, same capability): the SAME Ack, no capability spent",
        o.action == StandbyAction::Reply && o.reply.entryId == ack1.entryId &&
            std::memcmp(o.reply.entrySecret, ack1.entrySecret, 16) == 0 && authCalls == 1);
  UdpStandbyPacket forged = Hello(5, 1, "wrong-token");
  o = standby_handle(w, reinterpret_cast<const uint8_t*>(&forged), sizeof(forged), Addr(2999), false, auth, now + 60);
  check("S1: ANOTHER socket + the right device/generation + a wrong token: no secret, no reply, the entry unchanged",
        o.action == StandbyAction::Silent && w.size() == 1 && w.Find(ack1.entryId) != nullptr &&
            std::memcmp(w.Find(ack1.entryId)->secret.data(), ack1.entrySecret, 16) == 0);
  UdpStandbyPacket sameTokOther = Hello(5, 1, "cap-good-1");
  o = standby_handle(w, reinterpret_cast<const uint8_t*>(&sameTokOther), sizeof(sameTokOther), Addr(2999), false, auth,
                     now + 70);
  check("S1: the same Hello replayed from ANOTHER endpoint: not answered with the secret (the capability is spent)",
        o.action == StandbyAction::Silent && o.reply.entryId == 0);
  UdpStandbyPacket ping = Op(UdpPacketKind::StandbyPing, ack1.entryId, ack1.entrySecret, 0);
  o = standby_handle(w, reinterpret_cast<const uint8_t*>(&ping), sizeof(ping), Addr(2999), false, auth, now + 80);
  check("S1: an authenticated endpoint move is a Ping (with the secret): ok, the entry follows it",
        o.reply.status == kUdpStandbyStatusOk && w.Find(ack1.entryId)->endpoint.portNet == htons(2999));
  UdpStandbyPacket act = Op(UdpPacketKind::StandbyActivate, ack1.entryId, ack1.entrySecret, 4);
  o = standby_handle(w, reinterpret_cast<const uint8_t*>(&act), sizeof(act), Addr(2999), false, auth, now + 90);
  check("an accepted activation: its result names the operation (Pong: entry, gen 4, granted flag)",
        o.action == StandbyAction::Activate && o.fresh && o.activationPong.entryId == ack1.entryId &&
            o.activationPong.clientGen == 4 && o.activationPong.status == kUdpStandbyStatusOk &&
            (o.activationPong.features & kUdpStandbyPongActivationGranted) != 0);
  w.BindActiveEpoch(o.entry.grantId, 21);
  UdpStandbyPacket stale = Op(UdpPacketKind::StandbyActivate, ack1.entryId, ack1.entrySecret, 3);
  o = standby_handle(w, reinterpret_cast<const uint8_t*>(&stale), sizeof(stale), Addr(2999), false, auth, now + 95);
  check("a stale activation: refused, and the refusal names ITS generation (3), not the grant's",
        o.action == StandbyAction::Reply && o.reply.status == kUdpStandbyStatusSuperseded && o.reply.clientGen == 3);
  o = standby_handle(w, reinterpret_cast<const uint8_t*>(&act), sizeof(act), Addr(2999), false, auth, now + 100, /*endedEpoch=*/21);
  check("S3: the session's end reaches the handling first: the same activation is then refused (Superseded)",
        o.action == StandbyAction::Reply && o.reply.status == kUdpStandbyStatusSuperseded && w.activeEntryId() == 0);

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
