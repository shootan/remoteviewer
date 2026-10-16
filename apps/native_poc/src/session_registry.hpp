#pragma once

// keepalive-multi r1 (Codex codex_keepalive_multi_r1.md 1-3, 6): which PC connections the phone keeps, and why one
// ends. Pure bookkeeping -- no sockets, no threads: the bridge owns the resources of each entry and asks this table
// what may happen; tests drive it directly.
//
//   - An entry is one PC connection: logical key = directory origin + account id + hostId (never a name, an address or
//     a token), and a process-unique handle of its own -- a new connection to the same PC is a new handle.
//   - At most one entry is Active (on screen). The others are Parked (paused, control alive) or in transition.
//   - Capacity (policy.maxHosts, Active included) is reserved atomically when a NEW connection starts. Reaching the
//     limit names one victim: the Parked entry used longest ago, never the Active one nor the one being activated. The
//     victim is only marked; it closes when the new connection is committed. A failed connection cancels the mark --
//     a failure alone never evicts anything.
//   - A park lasts policy.parkTtlMs from the moment that PC was left (BeginPark). Nothing extends it -- not the list,
//     not heartbeats, not using another PC; only coming back to it and leaving again starts a new one.
//   - The lifecycle epoch ends everything at once (sign-out, account / directory change, background, network change,
//     "disconnect all"); per-entry endings (that PC's disconnect, peer lost, refused, TTL, LRU, replaced, a failed
//     pause) leave the others alone. A PC missing from one directory list refresh is not an ending at all.

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace remote60::native_poc {

struct KeepalivePolicy {
  // Proposed values (the user's answer pending): 3 PCs with the active one, 5 minutes per PC.
  uint32_t maxHosts = 3;
  uint64_t parkTtlMs = 5ull * 60ull * 1000ull;
};

enum class EntryPhase : int { Connecting = 0, Active = 1, Parked = 2, Closing = 3 };

inline const char* entry_phase_name(EntryPhase p) {
  switch (p) {
    case EntryPhase::Connecting: return "connecting";
    case EntryPhase::Active: return "active";
    case EntryPhase::Parked: return "parked";
    case EntryPhase::Closing: return "closing";
  }
  return "?";
}

struct RegistryEntry {
  uint64_t handle = 0;
  std::string key;  // origin|account|hostId
  EntryPhase phase = EntryPhase::Connecting;
  uint64_t lastUsedMs = 0;      // the last time it was really on screen (Active)
  uint64_t parkStartMs = 0;     // BeginPark of the current episode
  uint64_t parkDeadlineMs = 0;  // parkStartMs + ttl
  uint64_t parkEpisode = 0;
  uint64_t epoch = 0;           // the lifecycle epoch it was created in
  uint64_t evictionFor = 0;     // !=0: marked to close when that new connection commits
  bool manual = false;          // a manual (IP) connection: never parked
  std::string endReason;        // set when Closing
};

class SessionRegistry {
 public:
  explicit SessionRegistry(KeepalivePolicy policy = {}) : policy_(policy) {}

  void SetPolicy(KeepalivePolicy p) {
    std::lock_guard<std::mutex> lk(mu_);
    policy_ = p;
  }
  KeepalivePolicy Policy() const {
    std::lock_guard<std::mutex> lk(mu_);
    return policy_;
  }
  uint64_t Epoch() const {
    std::lock_guard<std::mutex> lk(mu_);
    return epoch_;
  }

  struct Reservation {
    enum Kind { Reuse, New, Refused } kind = Refused;
    uint64_t handle = 0;   // Reuse: the live entry; New: the reserved one
    uint64_t victim = 0;   // New at the limit: the Parked entry that closes when this one commits (0 = none)
    std::string why;       // Refused: the reason
  };

  // A tap on a PC (or a manual connect). A live entry for the key is reused; otherwise a new one is reserved -- the
  // count (every entry not Closing, eviction-marked ones excluded) and the reservation are one step.
  Reservation Reserve(const std::string& key, bool manual, uint64_t epochSeen) {
    std::lock_guard<std::mutex> lk(mu_);
    Reservation r;
    if (epochSeen != epoch_) {
      r.why = "stale_epoch";
      return r;
    }
    if (!manual) {
      for (auto& e : entries_) {
        if (e.key == key && !e.manual && (e.phase == EntryPhase::Parked || e.phase == EntryPhase::Active)) {
          r.kind = Reservation::Reuse;
          r.handle = e.handle;
          return r;
        }
        if (e.key == key && !e.manual && e.phase == EntryPhase::Connecting) {
          r.kind = Reservation::Refused;
          r.why = "already_connecting";
          r.handle = e.handle;
          return r;
        }
      }
    }
    const uint32_t live = LiveCountLocked();
    uint64_t victim = 0;
    if (live >= policy_.maxHosts) {
      const RegistryEntry* lru = nullptr;
      for (auto& e : entries_) {
        if (e.phase != EntryPhase::Parked || e.evictionFor != 0) continue;
        if (!lru || e.lastUsedMs < lru->lastUsedMs) lru = &e;
      }
      if (!lru) {
        r.why = "limit";  // nothing parked to give way: the Active one and connections in progress are never victims
        return r;
      }
      victim = lru->handle;
    }
    RegistryEntry n;
    n.handle = ++handleCounter_;
    n.key = key;
    n.phase = EntryPhase::Connecting;
    n.epoch = epoch_;
    n.manual = manual;
    entries_.push_back(n);
    if (victim) FindLocked(victim)->evictionFor = n.handle;
    r.kind = Reservation::New;
    r.handle = n.handle;
    r.victim = victim;
    return r;
  }

  // The new connection is up: the eviction it reserved happens now. Returns the entries to close (LRU), usually one
  // or none. A marked victim that was used again meanwhile is spared, and then the next Parked LRU gives way instead
  // -- the limit holds either way; the Active one and connections in progress are never closed for it.
  std::vector<uint64_t> Commit(uint64_t handle) {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<uint64_t> closed;
    RegistryEntry* e = FindLocked(handle);
    if (!e || e->phase != EntryPhase::Connecting) return closed;
    for (auto& v : entries_) {
      if (v.evictionFor != handle) continue;
      v.evictionFor = 0;
      if (v.phase == EntryPhase::Parked) {
        v.phase = EntryPhase::Closing;
        v.endReason = "lru";
        closed.push_back(v.handle);
      }
    }
    e->phase = EntryPhase::Parked;  // up, not yet on screen; Activate puts it there
    e->lastUsedMs = 0;
    while (LiveCountLocked() > policy_.maxHosts) {
      RegistryEntry* lru = nullptr;
      for (auto& v : entries_) {
        if (v.phase != EntryPhase::Parked || v.handle == handle || v.evictionFor != 0) continue;
        if (!lru || v.lastUsedMs < lru->lastUsedMs) lru = &v;
      }
      if (!lru) break;
      lru->phase = EntryPhase::Closing;
      lru->endReason = "lru";
      closed.push_back(lru->handle);
    }
    return closed;
  }

  // The connection failed or was abandoned: the reservation goes, and so does its eviction mark (no eviction).
  void Fail(uint64_t handle) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& v : entries_) {
      if (v.evictionFor == handle) v.evictionFor = 0;
    }
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const RegistryEntry& e) { return e.handle == handle; }),
                   entries_.end());
  }

  // On screen. Only one entry is Active: the caller parks the previous one first (false otherwise).
  bool Activate(uint64_t handle, uint64_t nowMs) {
    std::lock_guard<std::mutex> lk(mu_);
    RegistryEntry* e = FindLocked(handle);
    if (!e || e->phase != EntryPhase::Parked) return false;
    for (auto& o : entries_) {
      if (o.phase == EntryPhase::Active && o.handle != handle) return false;
    }
    e->phase = EntryPhase::Active;
    e->lastUsedMs = nowMs;
    e->parkDeadlineMs = 0;
    if (e->evictionFor) e->evictionFor = 0;  // used again: not a victim any more
    return true;
  }

  // Left: the PC's own park episode starts now, with its own deadline.
  bool Park(uint64_t handle, uint64_t nowMs) {
    std::lock_guard<std::mutex> lk(mu_);
    RegistryEntry* e = FindLocked(handle);
    if (!e || e->phase != EntryPhase::Active || e->manual) return false;
    e->phase = EntryPhase::Parked;
    e->lastUsedMs = nowMs;
    e->parkStartMs = nowMs;
    e->parkDeadlineMs = nowMs + policy_.parkTtlMs;
    ++e->parkEpisode;
    return true;
  }

  // Parked entries whose own deadline passed: marked Closing (reason "ttl") and returned.
  std::vector<uint64_t> Expire(uint64_t nowMs) {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<uint64_t> out;
    for (auto& e : entries_) {
      if (e.phase == EntryPhase::Parked && e.parkDeadlineMs != 0 && nowMs >= e.parkDeadlineMs) {
        e.phase = EntryPhase::Closing;
        e.endReason = "ttl";
        out.push_back(e.handle);
      }
    }
    return out;
  }

  // One PC ends (its disconnect, peer lost, refused, a failed pause, replaced). Others untouched.
  bool End(uint64_t handle, const std::string& reason) {
    std::lock_guard<std::mutex> lk(mu_);
    RegistryEntry* e = FindLocked(handle);
    if (!e || e->phase == EntryPhase::Closing) return false;
    e->phase = EntryPhase::Closing;
    e->endReason = reason;
    for (auto& v : entries_) {
      if (v.evictionFor == handle) v.evictionFor = 0;
    }
    return true;
  }

  // Everything ends: a new lifecycle epoch (a reservation or a tap made before it is refused from now on).
  std::vector<uint64_t> EndAll(const std::string& reason) {
    std::lock_guard<std::mutex> lk(mu_);
    ++epoch_;
    std::vector<uint64_t> out;
    for (auto& e : entries_) {
      if (e.phase == EntryPhase::Closing) continue;
      e.phase = EntryPhase::Closing;
      e.endReason = reason;
      e.evictionFor = 0;
      out.push_back(e.handle);
    }
    return out;
  }

  // Its resources are released: the entry leaves the table.
  void Remove(uint64_t handle) {
    std::lock_guard<std::mutex> lk(mu_);
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const RegistryEntry& e) { return e.handle == handle; }),
                   entries_.end());
  }

  bool Get(uint64_t handle, RegistryEntry* out) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& e : entries_) {
      if (e.handle == handle) {
        if (out) *out = e;
        return true;
      }
    }
    return false;
  }
  std::vector<RegistryEntry> Snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return entries_;
  }
  uint64_t ActiveHandle() const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& e : entries_) {
      if (e.phase == EntryPhase::Active) return e.handle;
    }
    return 0;
  }

 private:
  uint32_t LiveCountLocked() const {
    uint32_t n = 0;
    for (const auto& e : entries_) {
      if (e.phase != EntryPhase::Closing && e.evictionFor == 0) ++n;
    }
    return n;
  }
  RegistryEntry* FindLocked(uint64_t handle) {
    for (auto& e : entries_) {
      if (e.handle == handle) return &e;
    }
    return nullptr;
  }

  mutable std::mutex mu_;
  KeepalivePolicy policy_;
  std::vector<RegistryEntry> entries_;
  uint64_t handleCounter_ = 0;
  uint64_t epoch_ = 1;
};

}  // namespace remote60::native_poc
