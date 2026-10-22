#pragma once

// keepalive-r2 A (spike/host-admission): the host's control-only STANDBY entries.
//
// The host has ONE session -- one control channel, one adopted peer, one epoch, one video stream
// (host_session.hpp) -- and every authenticated Hello takes it (AdmitHello). Keeping several clients
// "ready" therefore cannot be done with Hellos. A standby entry is a client that proved itself once
// (with a directory capability, as a Hello does) and keeps a place here WITHOUT being the session:
// it owns no peer, no epoch, no control channel, no stream, no input, no clipboard, no LockCred, no
// file transfer. What it can do is renew itself (Ping), go away (Release), and -- only on an explicit
// user activation -- take the session through the very admission an authenticated Hello uses.
//
// Pure logic (no sockets, no clock of its own): host_startup_control.cpp's reader feeds it the
// datagram fields and the time, and does the I/O. Single-threaded: only the UDP reader calls it.

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace remote60::native_poc {

constexpr uint32_t kStandbyMaxEntries = 8;     // the whole host (a user's "3" is per client device)
constexpr uint32_t kStandbyLeaseMs = 20000;    // an entry lapses this long after its last Ping
constexpr uint32_t kStandbyHelloReplayMs = 30000;  // how long an admission's answer is replayed

struct StandbyEndpoint {
  uint32_t ipNet = 0;
  uint16_t portNet = 0;
  bool operator==(const StandbyEndpoint& o) const { return ipNet == o.ipNet && portNet == o.portNet; }
};

struct StandbyEntry {
  uint64_t entryId = 0;
  std::array<uint8_t, 16> secret{};
  std::array<uint8_t, 16> deviceId{};
  uint32_t admitGen = 0;     // the StandbyHello generation that made it (a retransmit replays it)
  uint32_t helloFeatures = 0;  // what its Hello would have asked for: used at activation
  StandbyEndpoint endpoint;  // where it last spoke from (follows a moved endpoint on Ping)
  uint64_t admittedMs = 0;
  uint64_t lastSeenMs = 0;
  uint32_t activateGen = 0;  // the last Activate generation it was granted (0 = never active)
};

class StandbyTable {
 public:
  enum class AdmitResult { Admitted, Replayed, Busy };
  enum class Verdict { Ok, Unknown, Superseded };

  StandbyTable() = default;

  // An authenticated StandbyHello. `authenticated` is the caller's capability check, made ONLY when this
  // is not a replay (a capability is single-use: the retransmit of an admitted Hello must not need it).
  // The caller asks IsReplay first, authenticates only if it is not, then calls Admit.
  bool IsReplay(const uint8_t deviceId[16], uint32_t gen, uint64_t nowMs, StandbyEntry* out) const {
    for (const StandbyEntry& e : entries_) {
      if (std::memcmp(e.deviceId.data(), deviceId, 16) == 0 && e.admitGen == gen &&
          nowMs - e.admittedMs <= kStandbyHelloReplayMs) {
        if (out) *out = e;
        return true;
      }
    }
    return false;
  }

  // One entry per device: a new admission from the same device replaces its old entry (the client
  // restarted). Expired entries are swept first; a full table answers Busy and changes nothing.
  AdmitResult Admit(const uint8_t deviceId[16], uint32_t gen, uint32_t helloFeatures, StandbyEndpoint from,
                    uint64_t nowMs, StandbyEntry* out) {
    if (IsReplay(deviceId, gen, nowMs, out)) return AdmitResult::Replayed;
    Expire(nowMs);
    size_t sameDevice = entries_.size();
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (std::memcmp(entries_[i].deviceId.data(), deviceId, 16) == 0) sameDevice = i;
    }
    if (sameDevice == entries_.size() && entries_.size() >= kStandbyMaxEntries) return AdmitResult::Busy;
    StandbyEntry e;
    do {
      e.entryId = Next64();
    } while (e.entryId == 0 || Find(e.entryId) != nullptr);
    const uint64_t a = Next64(), b = Next64();
    std::memcpy(e.secret.data(), &a, 8);
    std::memcpy(e.secret.data() + 8, &b, 8);
    std::memcpy(e.deviceId.data(), deviceId, 16);
    e.admitGen = gen;
    e.helloFeatures = helloFeatures;
    e.endpoint = from;
    e.admittedMs = e.lastSeenMs = nowMs;
    if (sameDevice != entries_.size()) {
      if (activeEntryId_ == entries_[sameDevice].entryId) activeEntryId_ = 0;
      entries_[sameDevice] = e;
    } else {
      entries_.push_back(e);
    }
    if (out) *out = e;
    return AdmitResult::Admitted;
  }

  // Ping: a known entry with the right secret is renewed and follows its endpoint. Nothing else.
  Verdict Ping(uint64_t entryId, const uint8_t secret[16], StandbyEndpoint from, uint64_t nowMs) {
    Expire(nowMs);
    StandbyEntry* e = Match(entryId, secret);
    if (!e) return Verdict::Unknown;
    e->lastSeenMs = nowMs;
    e->endpoint = from;
    return Verdict::Ok;
  }

  // Activate: the explicit activation. `*fresh` is false when this exact generation was already granted
  // to this entry and it is still the active one -- a retransmit, answered without a new session.
  Verdict Activate(uint64_t entryId, const uint8_t secret[16], uint32_t gen, StandbyEndpoint from, uint64_t nowMs,
                   bool* fresh, StandbyEntry* out) {
    Expire(nowMs);
    StandbyEntry* e = Match(entryId, secret);
    if (!e) return Verdict::Unknown;
    // A generation this entry was already granted, once something else has taken the session since, is a
    // LATE packet of an old activation -- never a new one. Re-activating needs a new (higher) generation,
    // which only a new explicit user activation produces: a parked client never takes the session back
    // on its own.
    if (activeEntryId_ != entryId && e->activateGen != 0 && gen <= e->activateGen) return Verdict::Superseded;
    *fresh = !(activeEntryId_ == entryId && e->activateGen == gen);
    e->lastSeenMs = nowMs;
    e->endpoint = from;
    e->activateGen = gen;
    activeEntryId_ = entryId;
    if (out) *out = *e;
    return Verdict::Ok;
  }

  Verdict Release(uint64_t entryId, const uint8_t secret[16]) {
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (entries_[i].entryId == entryId && std::memcmp(entries_[i].secret.data(), secret, 16) == 0) {
        if (activeEntryId_ == entryId) activeEntryId_ = 0;
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        return Verdict::Ok;
      }
    }
    return Verdict::Unknown;
  }

  // A session that did not come from an entry (an ordinary Hello) took the host: no entry is active.
  void SessionTakenByHello() { activeEntryId_ = 0; }

  // The active entry is never expired while it is the session -- its liveness is the session's.
  void Expire(uint64_t nowMs) {
    for (size_t i = 0; i < entries_.size();) {
      if (entries_[i].entryId != activeEntryId_ && nowMs - entries_[i].lastSeenMs > kStandbyLeaseMs) {
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
  }

  size_t size() const { return entries_.size(); }
  uint64_t activeEntryId() const { return activeEntryId_; }
  const StandbyEntry* Find(uint64_t entryId) const {
    for (const StandbyEntry& e : entries_) {
      if (e.entryId == entryId) return &e;
    }
    return nullptr;
  }

 private:
  StandbyEntry* Match(uint64_t entryId, const uint8_t secret[16]) {
    for (StandbyEntry& e : entries_) {
      if (e.entryId == entryId && std::memcmp(e.secret.data(), secret, 16) == 0) return &e;
    }
    return nullptr;
  }
  // Entry ids and secrets come from the system CSPRNG: a secret is what stands between a parked entry
  // and someone else activating it.
  static uint64_t Next64() {
    uint64_t v = 0;
#if defined(_WIN32)
    (void)BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#endif
    return v;
  }

  std::vector<StandbyEntry> entries_;
  uint64_t activeEntryId_ = 0;
};

}  // namespace remote60::native_poc
