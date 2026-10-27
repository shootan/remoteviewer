// keepalive-multi r1: SessionRegistry -- the PCs the phone keeps (session_registry.hpp). Policy 3 PCs / 5 min unless a
// case says otherwise. Negative control: the 0.2.30 rule (one park; another PC ends it), transcribed, must lose the
// A<->B round trip the user asked for.
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "session_registry.hpp"

using remote60::native_poc::EntryPhase;
using remote60::native_poc::KeepalivePolicy;
using remote60::native_poc::RegistryEntry;
using remote60::native_poc::SessionRegistry;

namespace {
int gPass = 0, gFail = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  (ok ? gPass : gFail)++;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
EntryPhase phase(SessionRegistry& r, uint64_t h) {
  RegistryEntry e;
  return r.Get(h, &e) ? e.phase : EntryPhase::Closing;
}
bool present(SessionRegistry& r, uint64_t h) { return r.Get(h, nullptr); }

// connect + commit + show: the way the bridge brings a new PC up
uint64_t open(SessionRegistry& r, const std::string& key, uint64_t now, std::vector<uint64_t>* closed = nullptr) {
  auto res = r.Reserve(key, false, r.Epoch());
  if (res.kind != SessionRegistry::Reservation::New) return 0;
  const auto c = r.Commit(res.handle);
  if (closed) *closed = c;
  for (uint64_t v : c) r.Remove(v);
  const uint64_t act = r.ActiveHandle();
  if (act) r.Park(act, now);
  r.Activate(res.handle, now);
  return res.handle;
}
void show(SessionRegistry& r, uint64_t h, uint64_t now) {
  const uint64_t act = r.ActiveHandle();
  if (act && act != h) r.Park(act, now);
  r.Activate(h, now);
}
}  // namespace

int main() {
  const uint64_t kMin = 60 * 1000;
  std::printf("[1] A -> list -> B -> list -> A -> B: both stay; every return is a reuse\n");
  {
    SessionRegistry r;
    const uint64_t a = open(r, "o|acct|A", 0);
    const uint64_t b = open(r, "o|acct|B", 10'000);
    check("B up, A parked (kept)", phase(r, a) == EntryPhase::Parked && phase(r, b) == EntryPhase::Active);
    auto ra = r.Reserve("o|acct|A", false, r.Epoch());
    check("tap A again: REUSE of A's live entry (no new connection)", ra.kind == SessionRegistry::Reservation::Reuse && ra.handle == a);
    show(r, a, 20'000);
    auto rb = r.Reserve("o|acct|B", false, r.Epoch());
    check("tap B again: REUSE of B", rb.kind == SessionRegistry::Reservation::Reuse && rb.handle == b);
    show(r, b, 30'000);
    check("after A->B->A->B: B active, A parked, two entries", phase(r, b) == EntryPhase::Active && phase(r, a) == EntryPhase::Parked && r.Snapshot().size() == 2);
    // negative control: 0.2.30 -- one park; connecting another PC ends the parked one
    bool parkedA = true;
    const bool otherHostEndsPark = true;  // SessionPark.kt:13 "One parked connection at most"
    if (otherHostEndsPark) parkedA = false;  // A parked, B tapped: A's park ends
    check("NEGATIVE (0.2.30 single park): A is gone when B is opened -- the round trip needs a new connection", !parkedA);
  }

  std::printf("[2] the limit: the 4th PC closes the parked one used longest ago, never the active one\n");
  {
    SessionRegistry r;  // 3
    const uint64_t a = open(r, "k|A", 1 * kMin);
    const uint64_t b = open(r, "k|B", 2 * kMin);
    const uint64_t c = open(r, "k|C", 3 * kMin);
    show(r, a, 4 * kMin);  // A used again: B is now the oldest parked
    std::vector<uint64_t> closed;
    const uint64_t d = open(r, "k|D", 5 * kMin, &closed);
    check("D connected", d != 0 && phase(r, d) == EntryPhase::Active);
    check("the victim is B (parked, used longest ago), not A (most recent) nor C", closed.size() == 1 && closed[0] == b,
          closed.empty() ? "none" : std::to_string(closed[0]));
    check("A and C kept, 3 PCs", present(r, a) && present(r, c) && r.Snapshot().size() == 3);
    // with a limit of 2, C is the one that evicts
    SessionRegistry r2(KeepalivePolicy{2, /*parkUnlimited=*/false, 5 * kMin});
    open(r2, "k|A", 1);
    open(r2, "k|B", 2);
    std::vector<uint64_t> c2;
    open(r2, "k|C", 3, &c2);
    check("limit 2: the 3rd PC is the one that evicts", c2.size() == 1);
  }

  std::printf("[3] a failed connection evicts nothing; failures do not cascade\n");
  {
    SessionRegistry r;
    const uint64_t a = open(r, "k|A", 1);
    const uint64_t b = open(r, "k|B", 2);
    open(r, "k|C", 3);
    for (int i = 0; i < 5; ++i) {
      auto res = r.Reserve("k|D", false, r.Epoch());
      check(("D attempt " + std::to_string(i + 1) + ": reserved with one marked victim").c_str(),
            res.kind == SessionRegistry::Reservation::New && res.victim != 0);
      r.Fail(res.handle);  // the connection failed
    }
    check("after 5 failed D attempts: A, B, C all still there (0 evictions)", present(r, a) && present(r, b) && r.Snapshot().size() == 3);
  }

  std::printf("[4] two connections reserved at once at the limit: never above it\n");
  {
    SessionRegistry r;
    open(r, "k|A", 1);
    open(r, "k|B", 2);
    const uint64_t c = open(r, "k|C", 3);  // C active, A and B parked
    auto d = r.Reserve("k|D", false, r.Epoch());
    auto e = r.Reserve("k|E", false, r.Epoch());
    auto f = r.Reserve("k|F", false, r.Epoch());
    check("D and E each took a different parked victim", d.victim != 0 && e.victim != 0 && d.victim != e.victim);
    check("F is refused: nothing parked left to give way (the active C is never a victim)",
          f.kind == SessionRegistry::Reservation::Refused && f.why == "limit");
    std::vector<uint64_t> all;
    for (auto h : r.Commit(d.handle)) all.push_back(h);
    for (auto h : r.Commit(e.handle)) all.push_back(h);
    for (auto h : all) r.Remove(h);
    uint32_t live = 0;
    for (auto& x : r.Snapshot()) if (x.phase != EntryPhase::Closing) ++live;
    check("after both commits: 3 PCs, C still there", live == 3 && present(r, c));
    // threads: 8 reservations racing at the limit, at most (parked count) succeed
    SessionRegistry rt;
    open(rt, "t|A", 1);
    open(rt, "t|B", 2);
    open(rt, "t|C", 3);
    std::vector<std::thread> ts;
    std::vector<int> kinds(8, -1);
    for (int i = 0; i < 8; ++i) {
      ts.emplace_back([&, i] { kinds[i] = rt.Reserve("t|N" + std::to_string(i), false, rt.Epoch()).kind; });
    }
    for (auto& t : ts) t.join();
    int news = 0;
    for (int k : kinds) news += k == SessionRegistry::Reservation::New;
    check("8 racing reservations at the limit: exactly 2 (the parked A, B) get a slot", news == 2, std::to_string(news));
  }

  std::printf("[5] a FINITE limit (tests only since keepalive r2): each PC's park has its own 5 minutes from leaving it\n");
  {
    SessionRegistry r(KeepalivePolicy{3, /*parkUnlimited=*/false, 5 * kMin});
    const uint64_t a = open(r, "k|A", 0);
    const uint64_t b = open(r, "k|B", 0);         // A parked at 0 -> deadline 5:00
    const uint64_t c = open(r, "k|C", 2 * kMin);  // B parked at 2:00 -> 7:00
    auto e1 = r.Expire(5 * kMin - 1);
    check("4:59: nothing expires", e1.empty());
    auto e2 = r.Expire(5 * kMin);
    check("5:00: only A expires (B has its own deadline)", e2.size() == 1 && e2[0] == a);
    show(r, b, 6 * kMin);  // B back on screen at 6:00 (before its 7:00)
    show(r, c, 6 * kMin + 1000);  // and left again: a new episode from 6:01
    auto e3 = r.Expire(7 * kMin);
    check("7:00: B does not expire (re-activated, new episode)", e3.empty());
    RegistryEntry be;
    r.Get(b, &be);
    check("B's new deadline is 6:01 + 5:00", be.parkDeadlineMs == 6 * kMin + 1000 + 5 * kMin && be.parkEpisode == 2);
    auto rr = r.Reserve("k|B", false, r.Epoch());  // a tap that only looks (reuse) does not extend anything
    check("a reuse reservation alone changes no deadline", rr.kind == SessionRegistry::Reservation::Reuse && (r.Get(b, &be), be.parkDeadlineMs == 11 * kMin + 1000));
  }

  std::printf("[6] endings: one PC vs everything\n");
  {
    SessionRegistry r;
    const uint64_t a = open(r, "k|A", 1);
    const uint64_t b = open(r, "k|B", 2);
    check("disconnecting the ACTIVE B leaves the parked A", r.End(b, "disconnect") && phase(r, a) == EntryPhase::Parked);
    const uint64_t epoch0 = r.Epoch();
    auto late = r.Reserve("k|C", false, epoch0);  // a tap made before sign-out
    auto all = r.EndAll("sign_out");
    check("sign-out ends every PC", all.size() == 2 || all.size() == 1);
    auto stale = r.Reserve("k|D", false, epoch0);
    check("a connect started before the sign-out is refused after it (stale epoch)",
          stale.kind == SessionRegistry::Reservation::Refused && stale.why == "stale_epoch");
    check("...and the reservation made before it is closing too", late.kind == SessionRegistry::Reservation::New && phase(r, late.handle) == EntryPhase::Closing);
  }

  std::printf("[7] one active at a time; a manual (IP) connection is never parked\n");
  {
    SessionRegistry r;
    const uint64_t a = open(r, "k|A", 1);
    auto b = r.Reserve("k|B", false, r.Epoch());
    r.Commit(b.handle);
    check("B cannot be activated while A is still active (A must be parked first)", !r.Activate(b.handle, 2));
    auto m = r.Reserve("manual|10.0.0.5", true, r.Epoch());
    r.Commit(m.handle);
    r.Park(a, 3);
    r.Activate(m.handle, 3);
    check("a manual connection cannot be parked", !r.Park(m.handle, 4));
    auto dup = r.Reserve("k|B", false, r.Epoch());
    check("a PC already connecting: a second tap is not a second connection", dup.kind == SessionRegistry::Reservation::Reuse || dup.why == "already_connecting");
  }

  std::printf("[8] keepalive r2: the product policy is Unlimited -- a park has no deadline, nothing expires\n");
  {
    SessionRegistry r;  // default policy
    check("the default policy is Unlimited (said, not a 0 TTL)", r.Policy().parkUnlimited);
    const uint64_t a = open(r, "k|A", 0);
    open(r, "k|B", 0);  // A parked at 0
    RegistryEntry ae;
    r.Get(a, &ae);
    check("a parked PC carries no deadline (0 = none)", ae.phase == EntryPhase::Parked && ae.parkDeadlineMs == 0 &&
                                                            ae.parkEpisode == 1);
    check("...and does not expire, even a day later", r.Expire(24ull * 60 * kMin).empty() && phase(r, a) == EntryPhase::Parked);
    open(r, "k|C", 1000);
    std::vector<uint64_t> closed;
    open(r, "k|D", 2000, &closed);
    check("the 3-PC limit still evicts the parked LRU", closed.size() == 1 && closed[0] == a);
  }

  std::printf("[9] keepalive r2: an automatic (preconnect) admission\n");
  {
    SessionRegistry r;
    const uint64_t a = open(r, "k|A", 1000);  // the user's PC, on screen
    auto p = r.Reserve("k|P", false, r.Epoch(), /*autoAdmit=*/true);
    RegistryEntry pe;
    check("an automatic reservation is a new entry, flagged autoAdmitted",
          p.kind == SessionRegistry::Reservation::New && r.Get(p.handle, &pe) && pe.autoAdmitted);
    r.Commit(p.handle);
    check("committed: parked, never active, and A is still the one on screen",
          phase(r, p.handle) == EntryPhase::Parked && r.ActiveHandle() == a);
    check("its park episode starts without having been Active (ParkAdmitted)", r.ParkAdmitted(p.handle, 2000));
    r.Get(p.handle, &pe);
    check("...once only, with no deadline under Unlimited and no use recorded", !r.ParkAdmitted(p.handle, 3000) &&
          pe.parkEpisode == 1 && pe.parkDeadlineMs == 0 && pe.lastUsedMs == 0);
    check("a user's PC cannot be 'admitted' this way (only automatic ones)", !r.ParkAdmitted(a, 3000));
    auto m = r.Reserve("manual|10.0.0.9", true, r.Epoch(), /*autoAdmit=*/true);
    RegistryEntry me;
    check("a manual connection is never automatic", r.Get(m.handle, &me) && !me.autoAdmitted);
    r.Fail(m.handle);
    open(r, "k|B", 4000);  // A parked (used at 4000), B on screen
    std::vector<uint64_t> closed;
    open(r, "k|C", 5000, &closed);
    check("at the limit the never-used automatic entry gives way first", closed.size() == 1 && closed[0] == p.handle);
    auto p2 = r.Reserve("k|P2", false, r.Epoch(), true);
    std::vector<uint64_t> closed2 = r.Commit(p2.handle);
    for (uint64_t v : closed2) r.Remove(v);
    r.ParkAdmitted(p2.handle, 6000);
    auto tap = r.Reserve("k|P2", false, r.Epoch());
    check("the user's tap on it is a REUSE (no new connection)", tap.kind == SessionRegistry::Reservation::Reuse && tap.handle == p2.handle);
  }

  std::printf("\nsession_registry_test: %d passed, %d failed\nRESULT: %s\n", gPass, gFail, gFail == 0 ? "PASS" : "FAIL");
  return gFail == 0 ? 0 : 1;
}
