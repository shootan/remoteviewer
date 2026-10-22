#pragma once

// keepalive-multi r1 (Codex codex_keepalive_multi_r1.md 2-4, 6): the PC connections the phone keeps, with their
// resources -- the glue between SessionRegistry (what may happen), the one decoder (who is on screen) and each PC's
// own session. native_bridge.cpp instantiates it with the real entry (controller + rendezvous + connect gate + sink
// adapter); session_table_test.cpp with a scripted one, so the transitions are tested without a phone.
//
// Entry (duck-typed) provides:
//   int ParkPhaseCode()         ClientParkPhase: 0 none, 1-3 confirming, 4 parked, 5 failed, 6 expired
//   bool Alive()                its session is not ended (connecting or connected)
//   int Established()           1 = its authenticated Hello and control are up (Connected), 0 = still connecting,
//                               -1 = ended (Disconnected / Error) -- the only proof a commit may evict for
//   uint64_t Generation()       its session's connection generation (which Connect made it)
//   bool BeginPark()            inhibit -> release -> pause -> Pong fence (the session's own control loop)
//   bool ResumeFromPark()       only from a confirmed park
//   void SetParkLimitMs(ms)     the policy's TTL, enforced by the session's own control loop
//   void SetBindingToken(t) / uint64_t BindingToken()   its sink adapter's token (0 = not on screen)
//   void Close()                blocking teardown (joins its threads; never called under a table lock)
// Binder: Bind(token) / bool Unbind(token) on the one decoder (owner checks inside the decoder's own lock).
//
// Locks: mu_ guards the map and the ended list (lookups copy a shared_ptr and let go). transitionMu_ serializes
// every change of who is active / parked / closing (Reserve, Commit, Fail, Activate, BeginPark, End, EndAll,
// Reap) -- held only for bookkeeping and the decoder's bind/unbind, never across a Close. A closing entry leaves
// the map at once (Find refuses new work on it) and the registry only after its Close finished, through the
// closer (a detached thread in the app), holding the shared_ptr so it outlives any call still in flight.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "session_registry.hpp"

namespace remote60::native_poc {

struct EndedEntry {
  uint64_t handle = 0;
  std::string key;
  std::string reason;
  uint64_t atMs = 0;
};

template <class Entry>
class SessionTable {
 public:
  struct Binder {
    std::function<void(uint64_t)> bind;
    std::function<bool(uint64_t)> unbind;
  };
  // Runs a teardown away from the caller (the app: a detached thread). Tests may run it inline or hold it.
  using Closer = std::function<void(std::function<void()>)>;

  // A connection that came up but was never shown (a newer tap won) is ended after this long.
  static constexpr uint64_t kUnshownGraceMs = 10000;

  SessionTable(Binder binder, Closer closer, KeepalivePolicy policy = {})
      : registry_(policy), binder_(std::move(binder)), closer_(std::move(closer)) {}

  void SetPolicy(KeepalivePolicy p) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    registry_.SetPolicy(p);
    for (auto& e : Live()) e->SetParkLimitMs(p.parkTtlMs);
  }
  KeepalivePolicy Policy() const { return registry_.Policy(); }
  uint64_t Epoch() const { return registry_.Epoch(); }
  SessionRegistry& Registry() { return registry_; }

  // A tap (or a manual connect): reuse the live entry for the key, or reserve a new one -- made by `make`.
  SessionRegistry::Reservation Reserve(const std::string& key, bool manual, uint64_t epochSeen,
                                       const std::function<std::shared_ptr<Entry>(uint64_t)>& make) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    auto r = registry_.Reserve(key, manual, epochSeen);
    if (r.kind == SessionRegistry::Reservation::New) {
      std::shared_ptr<Entry> e = make(r.handle);
      e->SetParkLimitMs(registry_.Policy().parkTtlMs);
      std::lock_guard<std::mutex> lk(mu_);
      map_[r.handle] = std::move(e);
    }
    return r;
  }

  // A live (not closing) entry, held for the call. null = gone or closing: no new work for it.
  std::shared_ptr<Entry> Find(uint64_t handle) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(handle);
    return it == map_.end() ? nullptr : it->second;
  }
  // The entry on screen with exactly this binding (captured by the app when the action was taken).
  std::shared_ptr<Entry> FindByBinding(uint64_t token) const {
    if (token == 0) return nullptr;
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& kv : map_) {
      if (kv.second->BindingToken() == token) return kv.second;
    }
    return nullptr;
  }

  struct CommitResult {
    enum Kind { Committed, Pending, Failed } kind = Failed;
    std::vector<uint64_t> closed;  // Committed: the entries its reservation evicted (LRU), closed now
    std::string why;               // Failed: why (the entry is given back -- nothing evicted for it)
  };

  // The new connection is up: its eviction (if the limit needed one) happens now -- and ONLY once its session is
  // established (authenticated Hello + control: Connected) on the connection generation the connect made
  // (keepalive-multi r2 M1). Connect returns as soon as its worker starts; a commit then would evict a parked PC
  // for a connection whose Hello may still be refused or time out. Pending = not yet established: nothing changes,
  // the caller asks again (no wait here). Failed = it ended before it was established, or it is not this
  // connection any more: it is given back (Fail), and no parked PC was touched.
  CommitResult Commit(uint64_t handle, uint64_t generation, uint64_t nowMs) {
    CommitResult out;
    std::lock_guard<std::mutex> tl(transitionMu_);
    std::shared_ptr<Entry> e = Find(handle);
    RegistryEntry re;
    if (!e || !registry_.Get(handle, &re) || re.phase != EntryPhase::Connecting) {
      out.why = "not_connecting";
      return out;
    }
    if (generation != 0 && e->Generation() != generation) {
      out.why = "generation_changed";
      FailLocked(handle, out.why, nowMs);
      return out;
    }
    const int est = e->Established();
    if (est == 0) {
      out.kind = CommitResult::Pending;
      return out;
    }
    if (est < 0) {
      out.why = "not_established";
      FailLocked(handle, out.why, nowMs);
      return out;
    }
    out.closed = registry_.Commit(handle);
    for (uint64_t v : out.closed) DetachAndCloseLocked(v, "lru", nowMs);
    if (registry_.Get(handle, &re) && re.phase == EntryPhase::Parked) committedMs_[handle] = nowMs;
    out.kind = CommitResult::Committed;
    return out;
  }

  // The connection failed or went stale: its own resources go; nothing else is evicted.
  void Fail(uint64_t handle, const std::string& reason, uint64_t nowMs) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    FailLocked(handle, reason, nowMs);
  }

  // Counts for the record (multi r2 M1): live = kept within the limit (not closing, not marked to give way),
  // reserved = connections not yet established (counted in live), marked = parked PCs a reservation will close
  // if its connection is established, closing = teardowns still running.
  struct Counts {
    uint32_t live = 0, reserved = 0, marked = 0, closing = 0;
  };
  Counts CountsNow() const {
    Counts c;
    for (const auto& re : registry_.Snapshot()) {
      if (re.phase == EntryPhase::Closing) continue;
      if (re.evictionFor != 0) {
        ++c.marked;
        continue;
      }
      ++c.live;
      if (re.phase == EntryPhase::Connecting) ++c.reserved;
    }
    c.closing = static_cast<uint32_t>(PendingCloseCount());
    return c;
  }

 private:
  void FailLocked(uint64_t handle, const std::string& reason, uint64_t nowMs) {
    std::shared_ptr<Entry> e = Detach(handle);
    registry_.Fail(handle);
    committedMs_.erase(handle);
    if (e) {
      RecordEnded(handle, std::string(), reason, nowMs);
      closer_([e] { e->Close(); });
    }
  }

 public:

  // On screen: a new binding. Refused (0) while another entry is Active or still confirming its park, or when a
  // parked entry cannot resume (it is ended). The decoder is bound before the session resumes, so the episode
  // start and the first frames of the resume reach it.
  uint64_t Activate(uint64_t handle, uint64_t nowMs) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    ReapLocked(nowMs);
    std::shared_ptr<Entry> e = Find(handle);
    if (!e) return 0;
    {
      const int own = e->ParkPhaseCode();
      if (own >= 1 && own <= 3) return 0;  // its own pause is not fenced yet: shown again only once it is
    }
    for (auto& o : Live()) {
      if (o == e) continue;
      const int ph = o->ParkPhaseCode();
      if (ph >= 1 && ph <= 3) return 0;  // A's pause is not fenced yet: B waits (the transition's order)
    }
    if (!registry_.Activate(handle, nowMs)) return 0;
    const uint64_t token = ++bindingCounter_;
    binder_.bind(token);
    e->SetBindingToken(token);
    committedMs_.erase(handle);
    if (e->ParkPhaseCode() == 4 && !e->ResumeFromPark()) {
      e->SetBindingToken(0);
      binder_.unbind(token);
      DetachAndCloseLocked(handle, "resume_refused", nowMs);
      return 0;
    }
    return token;
  }

  // Leaving the PC on screen: no frame of it reaches the decoder from here on; its park (and TTL) starts now.
  // false = not parkable (not active, manual, or the session refused -- an old host without the pause): the
  // caller ends it.
  bool BeginPark(uint64_t handle, uint64_t nowMs) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    std::shared_ptr<Entry> e = Find(handle);
    if (!e) return false;
    RegistryEntry re;
    if (!registry_.Get(handle, &re) || re.phase != EntryPhase::Active || re.manual) return false;
    const uint64_t token = e->BindingToken();
    e->SetBindingToken(0);
    if (token) binder_.unbind(token);
    if (!e->BeginPark()) {
      // the session would not pause: back on screen is not an option either (the decoder is unbound) -- the
      // caller ends it; registry left Active so the caller's End names the reason.
      return false;
    }
    registry_.Park(handle, nowMs);
    return true;
  }

  // One PC ends; the others are untouched.
  bool End(uint64_t handle, const std::string& reason, uint64_t nowMs) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    RegistryEntry re;
    if (!registry_.Get(handle, &re) || re.phase == EntryPhase::Closing) return false;
    DetachAndCloseLocked(handle, reason, nowMs);
    return true;
  }

  // Everything ends: a new lifecycle epoch first (late reservations made before it are refused), then each entry
  // leaves the map, then the teardowns run away from every lock. Returns the new epoch.
  uint64_t EndAll(const std::string& reason, uint64_t nowMs) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    const std::vector<uint64_t> handles = registry_.EndAll(reason);
    for (uint64_t h : handles) CloseDetachedLocked(h, reason, nowMs);
    return registry_.Epoch();
  }

  // Sessions that ended on their own (park expired / pause not confirmed / peer lost) and connections never
  // shown are ended here; called on every table step the app makes. The expiry itself is the session's control
  // loop (ClientParkFence) -- this only takes the ended entry off the table.
  void Reap(uint64_t nowMs) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    ReapLocked(nowMs);
  }

  std::vector<RegistryEntry> Snapshot() const { return registry_.Snapshot(); }
  std::vector<EndedEntry> Ended() const {
    std::lock_guard<std::mutex> lk(mu_);
    return ended_;
  }
  // The binding token of the entry on screen now (0 = none). Diagnostics only: actions carry their own.
  uint64_t ActiveBinding() const {
    const uint64_t h = registry_.ActiveHandle();
    std::shared_ptr<Entry> e = h ? Find(h) : nullptr;
    return e ? e->BindingToken() : 0;
  }
  size_t LiveCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return map_.size();
  }
  size_t PendingCloseCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return closing_;
  }

 private:
  std::vector<std::shared_ptr<Entry>> Live() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::shared_ptr<Entry>> out;
    for (const auto& kv : map_) out.push_back(kv.second);
    return out;
  }
  std::shared_ptr<Entry> Detach(uint64_t handle) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(handle);
    if (it == map_.end()) return nullptr;
    std::shared_ptr<Entry> e = std::move(it->second);
    map_.erase(it);
    return e;
  }
  void RecordEnded(uint64_t handle, const std::string& key, const std::string& reason, uint64_t nowMs) {
    std::lock_guard<std::mutex> lk(mu_);
    ended_.push_back(EndedEntry{handle, key, reason, nowMs});
    if (ended_.size() > 16) ended_.erase(ended_.begin());
  }
  // registry: -> Closing (reason) ; map: out ; decoder: unbound if it was on screen ; teardown: off-lock.
  void DetachAndCloseLocked(uint64_t handle, const std::string& reason, uint64_t nowMs) {
    registry_.End(handle, reason);  // false if already Closing (Commit / Expire / EndAll marked it): fine
    CloseDetachedLocked(handle, reason, nowMs);
  }
  void CloseDetachedLocked(uint64_t handle, const std::string& reason, uint64_t nowMs) {
    committedMs_.erase(handle);
    RegistryEntry re;
    registry_.Get(handle, &re);
    std::shared_ptr<Entry> e = Detach(handle);
    RecordEnded(handle, re.key, reason, nowMs);
    if (!e) {
      registry_.Remove(handle);
      return;
    }
    const uint64_t token = e->BindingToken();
    e->SetBindingToken(0);
    if (token) binder_.unbind(token);
    {
      std::lock_guard<std::mutex> lk(mu_);
      ++closing_;
    }
    closer_([this, e, handle] {
      e->Close();
      registry_.Remove(handle);
      std::lock_guard<std::mutex> lk(mu_);
      --closing_;
    });
  }
  void ReapLocked(uint64_t nowMs) {
    for (uint64_t h : registry_.Expire(nowMs)) CloseDetachedLocked(h, "ttl", nowMs);
    for (const RegistryEntry& re : registry_.Snapshot()) {
      if (re.phase == EntryPhase::Closing || re.phase == EntryPhase::Connecting) continue;
      std::shared_ptr<Entry> e = Find(re.handle);
      if (!e) continue;
      if (!e->Alive()) {
        const int ph = e->ParkPhaseCode();
        DetachAndCloseLocked(re.handle, ph == 6 ? "ttl" : ph == 5 ? "pause_failed" : "peer_lost", nowMs);
        continue;
      }
      auto c = committedMs_.find(re.handle);
      if (re.phase == EntryPhase::Parked && c != committedMs_.end() && nowMs >= c->second + kUnshownGraceMs) {
        DetachAndCloseLocked(re.handle, "never_shown", nowMs);
      }
    }
  }

  SessionRegistry registry_;
  Binder binder_;
  Closer closer_;
  mutable std::mutex transitionMu_;
  mutable std::mutex mu_;
  std::map<uint64_t, std::shared_ptr<Entry>> map_;
  std::map<uint64_t, uint64_t> committedMs_;  // committed, never shown yet (transitionMu_)
  std::vector<EndedEntry> ended_;
  size_t closing_ = 0;
  uint64_t bindingCounter_ = 0;
};

}  // namespace remote60::native_poc
