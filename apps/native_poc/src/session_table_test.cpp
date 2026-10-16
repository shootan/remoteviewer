// keepalive-multi r1: SessionTable (session_table.hpp) -- the transition glue between the registry, the one decoder
// and each PC's session, with scripted sessions. Deterministic: the closer queues teardowns so a test decides when
// they run (and can see that none ran under a table lock).
//
// Negative controls: the registry alone (what the table adds the fence check to) lets B on screen while A's pause
// is still unconfirmed; a closer that ran inline would have closed B inside End.
#include <atomic>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "session_table.hpp"

using namespace remote60::native_poc;

namespace {
int gPass = 0, gFail = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  (ok ? gPass : gFail)++;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}

struct FakeEntry {
  std::string name;
  std::atomic<int> phase{0};
  std::atomic<bool> alive{true};
  std::atomic<bool> pauseHonoured{true};
  std::atomic<bool> resumeOk{true};
  std::atomic<uint64_t> token{0};
  std::atomic<int> closes{0};
  std::atomic<int> resumes{0};
  std::atomic<uint64_t> limitMs{0};
  int ParkPhaseCode() { return phase.load(); }
  bool Alive() { return alive.load(); }
  bool BeginPark() {
    if (!pauseHonoured.load()) return false;
    phase = 1;
    return true;
  }
  bool ResumeFromPark() {
    if (phase.load() != 4 || !resumeOk.load()) return false;
    phase = 0;
    ++resumes;
    return true;
  }
  void SetParkLimitMs(uint64_t ms) { limitMs = ms; }
  void SetBindingToken(uint64_t t) { token = t; }
  uint64_t BindingToken() { return token.load(); }
  void Close() {
    ++closes;
    alive = false;
  }
};

struct Rig {
  std::atomic<uint64_t> bound{0};
  int binds = 0, unbinds = 0, staleUnbinds = 0;
  std::deque<std::function<void()>> pending;
  std::mutex pmu;
  std::map<std::string, std::shared_ptr<FakeEntry>> made;
  SessionTable<FakeEntry> table;
  explicit Rig(KeepalivePolicy p = {})
      : table(SessionTable<FakeEntry>::Binder{[this](uint64_t t) { bound = t; ++binds; },
                                              [this](uint64_t t) {
                                                if (t == 0 || bound.load() != t) {
                                                  ++staleUnbinds;
                                                  return false;
                                                }
                                                bound = 0;
                                                ++unbinds;
                                                return true;
                                              }},
              [this](std::function<void()> f) {
                std::lock_guard<std::mutex> lk(pmu);
                pending.push_back(std::move(f));
              },
              p) {}
  ~Rig() { RunCloses(); }
  void RunCloses() {
    for (;;) {
      std::function<void()> f;
      {
        std::lock_guard<std::mutex> lk(pmu);
        if (pending.empty()) return;
        f = std::move(pending.front());
        pending.pop_front();
      }
      f();
    }
  }
  size_t Pending() {
    std::lock_guard<std::mutex> lk(pmu);
    return pending.size();
  }
  // reserve + commit, as a directory connect does
  uint64_t Connect(const std::string& key, uint64_t now, std::vector<uint64_t>* closed = nullptr) {
    auto r = table.Reserve(key, false, table.Epoch(), [&](uint64_t) {
      auto e = std::make_shared<FakeEntry>();
      e->name = key;
      made[key] = e;
      return e;
    });
    if (r.kind != SessionRegistry::Reservation::New) return 0;
    auto c = table.Commit(r.handle, now);
    if (closed) *closed = c;
    return r.handle;
  }
  std::shared_ptr<FakeEntry> E(const std::string& key) { return made[key]; }
  // the app's transition: park the active one, wait for its fence, show the next
  uint64_t Switch(uint64_t from, uint64_t to, uint64_t now) {
    if (from) {
      if (!table.BeginPark(from, now)) return 0;
      for (auto& kv : made) {
        if (kv.second->phase.load() >= 1 && kv.second->phase.load() <= 3) kv.second->phase = 4;  // fence Pong
      }
    }
    return table.Activate(to, now);
  }
};
EntryPhase phaseOf(SessionTable<FakeEntry>& t, uint64_t h) {
  RegistryEntry e;
  return t.Registry().Get(h, &e) ? e.phase : EntryPhase::Closing;
}
}  // namespace

int main() {
  std::printf("[1] A -> B: B is bound only after A's pause is fenced; A's frames stop at BeginPark\n");
  {
    Rig rig;
    const uint64_t a = rig.Connect("o|u|A", 1);
    const uint64_t tA = rig.table.Activate(a, 1);
    check("A on screen with a binding", tA != 0 && rig.bound == tA && rig.E("o|u|A")->token == tA);
    const uint64_t b = rig.Connect("o|u|B", 2);
    check("B cannot be shown while A is still active", rig.table.Activate(b, 2) == 0 && rig.bound == tA);
    check("BeginPark(A)", rig.table.BeginPark(a, 3));
    check("A's adapter token is 0 from BeginPark (its frames no longer reach the decoder), decoder unbound",
          rig.E("o|u|A")->token == 0 && rig.bound == 0);
    check("B refused while A's park is confirming (phase 1)", rig.table.Activate(b, 3) == 0);
    rig.E("o|u|A")->phase = 3;
    check("...and while fencing (phase 3)", rig.table.Activate(b, 3) == 0);
    rig.E("o|u|A")->phase = 4;  // the fence Pong
    const uint64_t tB = rig.table.Activate(b, 4);
    check("A fenced: B on screen with a NEW binding", tB != 0 && tB != tA && rig.bound == tB);
    // A tapped again while ITS OWN pause is still confirming: refused by the table itself, not only by the screen
    rig.table.BeginPark(b, 5);
    rig.E("o|u|B")->phase = 2;
    check("B (its own park confirming) cannot be shown again yet", rig.table.Activate(b, 5) == 0 && rig.bound == 0 &&
                                                                    rig.E("o|u|B")->resumes == 0);
    rig.E("o|u|B")->phase = 4;
    check("...and can once fenced (resumed)", rig.table.Activate(b, 6) != 0 && rig.E("o|u|B")->resumes == 1);
    rig.table.BeginPark(b, 7);
    rig.E("o|u|B")->phase = 4;
    check("A parked, alive, not closed", phaseOf(rig.table, a) == EntryPhase::Parked && rig.E("o|u|A")->closes == 0);
    // NEGATIVE: the registry alone does not know about the fence
    SessionRegistry reg;
    auto ra = reg.Reserve("A", false, reg.Epoch());
    reg.Commit(ra.handle);
    reg.Activate(ra.handle, 1);
    auto rb = reg.Reserve("B", false, reg.Epoch());
    reg.Commit(rb.handle);
    reg.Park(ra.handle, 2);  // A only began parking; its pause is not confirmed
    check("NEGATIVE: the registry alone lets B on screen while A's pause is unconfirmed", reg.Activate(rb.handle, 2));
  }

  std::printf("[2] A -> B -> A -> B: every return is a resume of the same entry, with a new binding each time\n");
  {
    Rig rig;
    const uint64_t a = rig.Connect("o|u|A", 1);
    uint64_t t = rig.table.Activate(a, 1);
    const uint64_t b = rig.Connect("o|u|B", 2);
    std::vector<uint64_t> tokens{t};
    uint64_t cur = a;
    for (int i = 0; i < 3; ++i) {
      const uint64_t next = cur == a ? b : a;
      t = rig.Switch(cur, next, 10 + i);
      tokens.push_back(t);
      cur = next;
    }
    bool distinct = true;
    for (size_t i = 0; i < tokens.size(); ++i)
      for (size_t j = i + 1; j < tokens.size(); ++j) distinct = distinct && tokens[i] != tokens[j] && tokens[i] != 0;
    check("4 activations, 4 distinct bindings", distinct);
    check("A resumed once, B once (no new connection)", rig.E("o|u|A")->resumes == 1 && rig.E("o|u|B")->resumes == 1,
          std::to_string(rig.E("o|u|A")->resumes.load()) + "/" + std::to_string(rig.E("o|u|B")->resumes.load()));
    check("two entries, none closed", rig.table.LiveCount() == 2 && rig.E("o|u|A")->closes == 0 && rig.E("o|u|B")->closes == 0);
    auto again = rig.table.Reserve("o|u|A", false, rig.table.Epoch(), [](uint64_t) { return std::make_shared<FakeEntry>(); });
    check("a tap on A is a reuse", again.kind == SessionRegistry::Reservation::Reuse && again.handle == a);
  }

  std::printf("[3] one PC ends; the others stay -- and its teardown runs away from the table\n");
  {
    Rig rig;
    const uint64_t a = rig.Connect("o|u|A", 1);
    rig.table.Activate(a, 1);
    const uint64_t b = rig.Connect("o|u|B", 2);
    const uint64_t tB = rig.Switch(a, b, 2);
    check("End(active B)", rig.table.End(b, "disconnect", 3));
    check("B's teardown is queued, not run inside End", rig.E("o|u|B")->closes == 0 && rig.Pending() == 1);
    check("B off the map at once (no new work), decoder unbound", !rig.table.Find(b) && rig.table.FindByBinding(tB) == nullptr && rig.bound == 0);
    rig.RunCloses();
    check("B closed once; A parked and untouched", rig.E("o|u|B")->closes == 1 && rig.E("o|u|A")->closes == 0 &&
                                                       phaseOf(rig.table, a) == EntryPhase::Parked);
    check("B left the registry after its close", !rig.table.Registry().Get(b, nullptr));
    auto ended = rig.table.Ended();
    check("the ending is recorded with its reason", !ended.empty() && ended.back().reason == "disconnect" && ended.back().key == "o|u|B");
    check("the parked A can be shown again", rig.table.Activate(a, 4) != 0 && rig.E("o|u|A")->resumes == 1);
  }

  std::printf("[4] everything ends: new epoch first, then the teardowns off-lock; a late connect is refused\n");
  {
    Rig rig;
    const uint64_t a = rig.Connect("o|u|A", 1);
    rig.table.Activate(a, 1);
    const uint64_t b = rig.Connect("o|u|B", 2);
    rig.Switch(a, b, 2);
    const uint64_t epoch0 = rig.table.Epoch();
    auto late = rig.table.Reserve("o|u|C", false, epoch0, [&](uint64_t) {
      auto e = std::make_shared<FakeEntry>();
      rig.made["o|u|C"] = e;
      return e;
    });  // a connect started before the sign-out, still in flight
    const uint64_t epoch1 = rig.table.EndAll("sign_out", 3);
    check("new epoch", epoch1 == epoch0 + 1);
    check("3 teardowns queued, none run under the table", rig.Pending() == 3 && rig.E("o|u|A")->closes == 0);
    check("no entry is findable any more, decoder unbound", rig.table.LiveCount() == 0 && rig.bound == 0);
    check("the in-flight connect's commit does nothing", rig.table.Commit(late.handle, 4).empty() && rig.table.Activate(late.handle, 4) == 0);
    auto stale = rig.table.Reserve("o|u|D", false, epoch0, [](uint64_t) { return std::make_shared<FakeEntry>(); });
    check("a reservation with the old epoch is refused", stale.kind == SessionRegistry::Reservation::Refused);
    rig.RunCloses();
    check("each closed once", rig.E("o|u|A")->closes == 1 && rig.E("o|u|B")->closes == 1 && rig.E("o|u|C")->closes == 1);
  }

  std::printf("[5] the limit: the 4th PC closes the parked LRU, never the active one; failures evict nothing\n");
  {
    Rig rig;
    const uint64_t a = rig.Connect("k|A", 1);
    rig.table.Activate(a, 1);
    const uint64_t b = rig.Connect("k|B", 2);
    rig.Switch(a, b, 2);
    const uint64_t c = rig.Connect("k|C", 3);
    rig.Switch(b, c, 3);
    rig.Switch(c, a, 4);  // A used again: B is now the oldest parked
    for (int i = 0; i < 3; ++i) {
      auto r = rig.table.Reserve("k|D", false, rig.table.Epoch(), [](uint64_t) { return std::make_shared<FakeEntry>(); });
      rig.table.Fail(r.handle, "connect_failed", 5);
    }
    rig.RunCloses();
    check("3 failed D connects: A, B, C all alive", rig.E("k|A")->closes == 0 && rig.E("k|B")->closes == 0 && rig.E("k|C")->closes == 0);
    std::vector<uint64_t> closed;
    const uint64_t d = rig.Connect("k|D", 6, &closed);
    check("D up: B (parked, used longest ago) closed", closed.size() == 1 && closed[0] == b);
    rig.RunCloses();
    check("B torn down; A (active) and C kept", rig.E("k|B")->closes == 1 && rig.E("k|A")->closes == 0 && rig.E("k|C")->closes == 0);
    check("the active A still owns the decoder", rig.bound == rig.E("k|A")->token && rig.bound != 0);
    check("D shown after A parks", rig.Switch(a, d, 7) != 0);
    auto ended = rig.table.Ended();
    bool lru = false;
    for (auto& x : ended) lru = lru || (x.key == "k|B" && x.reason == "lru");
    check("B's ending says lru", lru);
  }

  std::printf("[6] a session that ended on its own is taken off; the others stay\n");
  {
    Rig rig;
    const uint64_t a = rig.Connect("k|A", 1);
    rig.table.Activate(a, 1);
    const uint64_t b = rig.Connect("k|B", 2);
    rig.Switch(a, b, 2);
    const uint64_t c = rig.Connect("k|C", 3);
    rig.Switch(b, c, 3);
    rig.E("k|A")->phase = 6;  // A's control loop: park expired, session ended
    rig.E("k|A")->alive = false;
    rig.E("k|B")->alive = false;  // B: peer lost while parked
    rig.table.Reap(4);
    rig.RunCloses();
    std::string ra, rb;
    for (auto& x : rig.table.Ended()) {
      if (x.key == "k|A") ra = x.reason;
      if (x.key == "k|B") rb = x.reason;
    }
    check("A ended: ttl; B ended: peer_lost", ra == "ttl" && rb == "peer_lost", ra + "/" + rb);
    check("C (active) kept and bound", rig.table.Find(c) != nullptr && rig.bound == rig.E("k|C")->token && rig.bound != 0);
    check("no automatic reconnect: A, B gone from the table", !rig.table.Find(a) && !rig.table.Find(b) && rig.table.LiveCount() == 1);
    check("the policy TTL reached each session", rig.E("k|A")->limitMs == 300000 && rig.E("k|C")->limitMs == 300000);
  }

  std::printf("[7] resume refused / pause not honoured / a connection never shown\n");
  {
    Rig rig;
    const uint64_t a = rig.Connect("k|A", 1);
    rig.table.Activate(a, 1);
    const uint64_t b = rig.Connect("k|B", 2);
    rig.Switch(a, b, 2);
    rig.E("k|A")->resumeOk = false;  // A's park could not be resumed (expired at the moment of adoption)
    rig.table.BeginPark(b, 3);
    rig.E("k|B")->phase = 4;
    check("A's resume refused: 0, A ended, decoder not left bound to it", rig.table.Activate(a, 4) == 0 && !rig.table.Find(a) && rig.bound == 0);
    check("B (parked) untouched by A's failure", rig.table.Find(b) != nullptr && rig.E("k|B")->closes == 0);
    check("B can still be shown", rig.table.Activate(b, 5) != 0);
    rig.E("k|B")->pauseHonoured = false;  // an old host: no pause
    check("BeginPark refused for a host without the pause (the caller ends it)", !rig.table.BeginPark(b, 6));
    check("...and it ends with that reason", rig.table.End(b, "pause_unsupported", 6));
    const uint64_t c = rig.Connect("k|C", 7);  // came up, but a newer tap won: never shown
    rig.table.Reap(7 + SessionTable<FakeEntry>::kUnshownGraceMs - 1);
    check("unshown C kept within the grace", rig.table.Find(c) != nullptr);
    rig.table.Reap(7 + SessionTable<FakeEntry>::kUnshownGraceMs);
    check("...and ended after it (never_shown)", rig.table.Find(c) == nullptr);
  }

  std::printf("[8] races: binding lookups and actions against switches and endings (no crash, no stale owner)\n");
  {
    Rig rig;
    const uint64_t a = rig.Connect("k|A", 1);
    rig.table.Activate(a, 1);
    const uint64_t b = rig.Connect("k|B", 2);
    std::atomic<bool> stop{false};
    std::atomic<int> wrong{0}, lookups{0};
    std::thread reader([&] {
      while (!stop) {
        const uint64_t t = rig.bound.load();
        auto e = rig.table.FindByBinding(t);
        if (e) {
          const uint64_t now = e->BindingToken();
          if (now != t && now != 0 && now < t) ++wrong;  // tokens only grow: an older one would be a stale owner
        }
        ++lookups;
      }
    });
    while (lookups.load() == 0) std::this_thread::yield();  // the reader is running before the switches start
    uint64_t cur = a;
    for (int i = 0; i < 2000; ++i) {
      const uint64_t next = cur == a ? b : a;
      if (rig.Switch(cur, next, 10 + i) == 0) break;
      cur = next;
    }
    stop = true;
    reader.join();
    check("2000 switches: a binding never resolved to another binding's entry", wrong == 0 && lookups.load() > 1000,
          std::to_string(lookups.load()) + " lookups");
    check("still two entries, both alive", rig.table.LiveCount() == 2 && rig.E("k|A")->closes == 0);
  }

  std::printf("\nsession_table_test: %d passed, %d failed\nRESULT: %s\n", gPass, gFail, gFail == 0 ? "PASS" : "FAIL");
  return gFail == 0 ? 0 : 1;
}
