// keepalive-r2 A (spike/host-admission): the host's standby entry table, as pure logic.
// Tags: pure-logic.

#include "host_standby_table.hpp"

#include <cstdio>
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
}  // namespace

int main() {
  std::printf("host_standby_table_test (keepalive-r2 A)\n\n");
  uint64_t now = 1000000;

  StandbyTable t;
  StandbyEntry a, b, a2, again;
  check("device A admitted", t.Admit(Dev(1).id, 1, 0x12, kA, now, &a) == StandbyTable::AdmitResult::Admitted &&
                                 a.entryId != 0);
  check("device B admitted -- from the SAME address (a relay): its own entry", t.Admit(Dev(2).id, 1, 0, kA, now, &b) ==
                                                                                    StandbyTable::AdmitResult::Admitted &&
                                                                                b.entryId != a.entryId && t.size() == 2);
  check("A's Hello again (same generation, a retransmit): replayed, same entry and secret",
        t.Admit(Dev(1).id, 1, 0x12, kA, now + 10, &again) == StandbyTable::AdmitResult::Replayed &&
            again.entryId == a.entryId && again.secret == a.secret && t.size() == 2);
  check("the replay is known BEFORE authenticating (a capability is single-use)", t.IsReplay(Dev(1).id, 1, now + 20, nullptr));
  check("...but not after the replay window", !t.IsReplay(Dev(1).id, 1, now + kStandbyHelloReplayMs + 1, nullptr));

  std::printf("\n[ping]\n");
  uint8_t wrong[16] = {};
  check("ping with the wrong secret: unknown", t.Ping(a.entryId, wrong, kA, now + 100) == StandbyTable::Verdict::Unknown);
  check("ping with the right secret: ok, and the entry follows a moved endpoint",
        t.Ping(a.entryId, a.secret.data(), kA2, now + 100) == StandbyTable::Verdict::Ok && t.Find(a.entryId)->endpoint == kA2);
  check("pinging is all it did: no entry is active", t.activeEntryId() == 0);

  std::printf("\n[one entry per device]\n");
  check("A again with a NEW generation (restarted client): replaces A's entry",
        t.Admit(Dev(1).id, 2, 0, kA, now + 200, &a2) == StandbyTable::AdmitResult::Admitted && a2.entryId != a.entryId &&
            t.size() == 2);
  check("...the old entry is gone", t.Ping(a.entryId, a.secret.data(), kA, now + 210) == StandbyTable::Verdict::Unknown);

  std::printf("\n[activation]\n");
  bool fresh = false;
  StandbyEntry got;
  check("A activates (gen 1): ok, a NEW session", t.Activate(a2.entryId, a2.secret.data(), 1, kA, now + 300, &fresh, &got) ==
                                                         StandbyTable::Verdict::Ok &&
                                                     fresh && t.activeEntryId() == a2.entryId);
  check("A's Activate again (same gen, a retransmit): ok, NOT a new session",
        t.Activate(a2.entryId, a2.secret.data(), 1, kA, now + 310, &fresh, &got) == StandbyTable::Verdict::Ok && !fresh);
  check("B activates (gen 1): ok, a new session -- A is parked, not dropped",
        t.Activate(b.entryId, b.secret.data(), 1, kA, now + 400, &fresh, &got) == StandbyTable::Verdict::Ok && fresh &&
            t.activeEntryId() == b.entryId && t.Find(a2.entryId) != nullptr);
  check("A's LATE Activate gen 1 (after B took it): SUPERSEDED, B stays", t.Activate(a2.entryId, a2.secret.data(), 1, kA,
                                                                                    now + 410, &fresh, &got) ==
                                                                             StandbyTable::Verdict::Superseded &&
                                                                         t.activeEntryId() == b.entryId);
  check("A's NEW explicit activation (gen 2): ok, takes it", t.Activate(a2.entryId, a2.secret.data(), 2, kA, now + 420,
                                                                        &fresh, &got) == StandbyTable::Verdict::Ok &&
                                                                 fresh && t.activeEntryId() == a2.entryId);
  check("activation with the wrong secret: unknown, nothing changes",
        t.Activate(b.entryId, wrong, 5, kA, now + 430, &fresh, &got) == StandbyTable::Verdict::Unknown &&
            t.activeEntryId() == a2.entryId);

  std::printf("\n[lease]\n");
  const uint64_t later = now + 420 + kStandbyLeaseMs + 1;
  t.Expire(later);
  check("after the lease, the parked entry (B, not pinged) lapses; the ACTIVE one (A) stays",
        t.Find(b.entryId) == nullptr && t.Find(a2.entryId) != nullptr, std::to_string(t.size()) + " left");
  t.SessionTakenByHello();
  check("an ordinary Hello took the session: no entry is active", t.activeEntryId() == 0);
  t.Expire(later + kStandbyLeaseMs + 1);
  check("...and A, no longer the session and not pinged, lapses too", t.size() == 0);

  std::printf("\n[bounds]\n");
  StandbyTable full;
  StandbyEntry e;
  for (uint8_t d = 1; d <= kStandbyMaxEntries; ++d) full.Admit(Dev(d).id, 1, 0, kRelay, now, &e);
  check("eight devices fill the host", full.size() == kStandbyMaxEntries);
  check("a ninth: BUSY, nothing changes", full.Admit(Dev(99).id, 1, 0, kRelay, now, &e) == StandbyTable::AdmitResult::Busy &&
                                               full.size() == kStandbyMaxEntries);
  check("...but a device already holding an entry may renew it (replace), not Busy",
        full.Admit(Dev(3).id, 7, 0, kRelay, now + 5, &e) == StandbyTable::AdmitResult::Admitted && full.size() == kStandbyMaxEntries);
  check("release frees a place", full.Release(e.entryId, e.secret.data()) == StandbyTable::Verdict::Ok &&
                                     full.Admit(Dev(99).id, 1, 0, kRelay, now + 6, &e) == StandbyTable::AdmitResult::Admitted);
  check("release with the wrong secret: unknown", full.Release(e.entryId, wrong) == StandbyTable::Verdict::Unknown);
  StandbyTable lapse;
  for (uint8_t d = 1; d <= kStandbyMaxEntries; ++d) lapse.Admit(Dev(d).id, 1, 0, kRelay, now, &e);
  check("a full host whose entries have lapsed admits again (swept first)",
        lapse.Admit(Dev(99).id, 1, 0, kRelay, now + kStandbyLeaseMs + 1, &e) == StandbyTable::AdmitResult::Admitted &&
            lapse.size() == 1);

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
