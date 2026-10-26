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
//
// r3 (Codex codex_keepalive_r2_spike_r1.md S1-S3):
//   S1  An admission is replayed (the same answer, the same secret) ONLY for the same proof it was admitted
//       with -- the same capability (by fingerprint: the token itself is single-use and not kept) from the
//       same return endpoint, while the entry's lease is valid. Anything else needs a fresh capability; a
//       known device id and generation are public and prove nothing. Moving to another endpoint is a Ping
//       (which carries the secret), never a replayed Hello.
//   S2  Each entry keeps a monotonic activation high-water, whatever its role. 0 is never a generation; a
//       lower one is stale; the same one is answered again only while that very grant still owns the
//       session; a higher one is a new explicit activation.
//   S3  The active grant ends with its session (SessionEnded, from the control session's real end -- a
//       resume or a pause is not one), not only when another session takes the host. A device whose entry
//       holds the live session is not re-admitted or released underneath it. The CSPRNG's result is
//       checked: no entry is made from a failed draw, an id collision is retried a bounded number of times.

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace remote60::native_poc {

constexpr uint32_t kStandbyMaxEntries = 8;     // the whole host (a user's "3" is per client device)
constexpr uint32_t kStandbyLeaseMs = 20000;    // an entry lapses this long after its last Ping
constexpr uint32_t kStandbyHelloReplayMs = 30000;  // how long an admission's answer is replayed (within its lease)
constexpr uint32_t kStandbyIdAttempts = 4;     // entry id collisions retried at most this often

struct StandbyEndpoint {
  uint32_t ipNet = 0;
  uint16_t portNet = 0;
  bool operator==(const StandbyEndpoint& o) const { return ipNet == o.ipNet && portNet == o.portNet; }
};

// A fingerprint of the capability an entry was admitted with (FNV-1a 64): identifies a retransmit of the same
// admission. Not a secret and not a credential -- the capability itself was consumed when it was checked.
inline uint64_t StandbyProofFingerprint(const char* token, size_t len) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint8_t>(token[i]);
    h *= 1099511628211ull;
  }
  return h == 0 ? 1 : h;
}

struct StandbyEntry {
  uint64_t entryId = 0;
  std::array<uint8_t, 16> secret{};
  std::array<uint8_t, 16> deviceId{};
  uint32_t admitGen = 0;       // the StandbyHello generation that made it (a retransmit replays it)
  uint64_t proofFp = 0;        // r3 S1: the admitting capability's fingerprint
  StandbyEndpoint admitFrom;   // r3 S1: where that admission came from (a replay must come from there)
  uint32_t helloFeatures = 0;  // what its Hello would have asked for: used at activation
  StandbyEndpoint endpoint;    // where it last spoke from (follows a moved endpoint on Ping)
  uint64_t admittedMs = 0;
  uint64_t lastSeenMs = 0;
  uint32_t activateHigh = 0;   // r3 S2: the highest activation generation ever granted (0 = never)
  uint64_t grantId = 0;        // the grant that generation got (host-unique, never 0)
};

class StandbyTable {
 public:
  enum class HelloClass { Replay, NeedProof };
  enum class AdmitResult { Admitted, Busy, Live, Failed };
  enum class Verdict { Ok, Unknown, Superseded, Live };
  using Rng = std::function<bool(uint64_t*)>;

  // `rng` replaces the system CSPRNG (tests only: a failure or a collision on purpose).
  explicit StandbyTable(Rng rng = nullptr) : rng_(std::move(rng)) {}

  // A StandbyHello's standing BEFORE any capability is spent on it. Expired leases go first.
  //   Replay    -- the very admission again (same device, generation, capability, endpoint, valid lease):
  //                answer it as before, nothing changes.
  //   NeedProof -- anything else: only a fresh capability may admit (or replace) an entry.
  HelloClass ClassifyHello(const uint8_t deviceId[16], uint32_t gen, uint64_t proofFp, StandbyEndpoint from,
                           uint64_t nowMs, StandbyEntry* out) {
    Expire(nowMs);
    for (const StandbyEntry& e : entries_) {
      if (std::memcmp(e.deviceId.data(), deviceId, 16) != 0) continue;
      if (e.admitGen == gen && e.proofFp == proofFp && e.admitFrom == from && nowMs - e.admittedMs <= kStandbyHelloReplayMs) {
        if (out) *out = e;
        return HelloClass::Replay;
      }
      return HelloClass::NeedProof;
    }
    return HelloClass::NeedProof;
  }

  // An admission whose capability the caller has just checked. One entry per device: a new admission
  // replaces that device's old entry -- unless that entry holds the live session (Live: the device is the
  // session; nothing changes). A full table is Busy; a failed random draw is Failed (no entry at all).
  AdmitResult Admit(const uint8_t deviceId[16], uint32_t gen, uint64_t proofFp, uint32_t helloFeatures,
                    StandbyEndpoint from, uint64_t nowMs, StandbyEntry* out) {
    Expire(nowMs);
    size_t sameDevice = entries_.size();
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (std::memcmp(entries_[i].deviceId.data(), deviceId, 16) == 0) sameDevice = i;
    }
    if (sameDevice != entries_.size() && IsLive(entries_[sameDevice].entryId)) return AdmitResult::Live;
    if (sameDevice == entries_.size() && entries_.size() >= kStandbyMaxEntries) return AdmitResult::Busy;
    StandbyEntry e;
    bool idOk = false;
    for (uint32_t i = 0; i < kStandbyIdAttempts && !idOk; ++i) {
      if (!Draw(&e.entryId)) return AdmitResult::Failed;
      idOk = e.entryId != 0 && Find(e.entryId) == nullptr;
    }
    if (!idOk) return AdmitResult::Failed;
    uint64_t a = 0, b = 0;
    if (!Draw(&a) || !Draw(&b)) return AdmitResult::Failed;
    std::memcpy(e.secret.data(), &a, 8);
    std::memcpy(e.secret.data() + 8, &b, 8);
    std::memcpy(e.deviceId.data(), deviceId, 16);
    e.admitGen = gen;
    e.proofFp = proofFp;
    e.admitFrom = from;
    e.helloFeatures = helloFeatures;
    e.endpoint = from;
    e.admittedMs = e.lastSeenMs = nowMs;
    if (sameDevice != entries_.size()) {
      if (activeEntryId_ == entries_[sameDevice].entryId) ClearActive();  // a grant whose session already ended
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

  // Activate: the explicit activation. `*fresh` is false when this generation is the grant that still owns
  // the session (a retransmit: answered again, no new session). out->grantId is the grant answered for.
  Verdict Activate(uint64_t entryId, const uint8_t secret[16], uint32_t gen, StandbyEndpoint from, uint64_t nowMs,
                   bool* fresh, StandbyEntry* out) {
    Expire(nowMs);
    StandbyEntry* e = Match(entryId, secret);
    if (!e) return Verdict::Unknown;
    if (gen == 0 || gen < e->activateHigh) return Verdict::Superseded;  // never a generation / stale
    if (gen == e->activateHigh) {
      // The same generation: only the grant that still owns the session is answered again.
      if (!IsLive(entryId) || e->grantId == 0) return Verdict::Superseded;
      *fresh = false;
    } else {
      *fresh = true;
      if (grantLive_ && activeEntryId_ != entryId) {
        // A takeover: the entry that had the session is parked again (its lease runs from now).
        for (StandbyEntry& other : entries_) {
          if (other.entryId == activeEntryId_) other.lastSeenMs = nowMs;
        }
      }
      e->activateHigh = gen;
      e->grantId = ++grantSeq_;
      activeEntryId_ = entryId;
      activeGrantId_ = e->grantId;
      activeEpoch_ = 0;  // bound once the caller has admitted the session (BindActiveEpoch)
      grantLive_ = true;
    }
    e->lastSeenMs = nowMs;
    e->endpoint = from;
    if (out) *out = *e;
    return Verdict::Ok;
  }

  // The session epoch the fresh grant was admitted as (AdmitHello's result).
  void BindActiveEpoch(uint64_t grantId, uint64_t epoch) {
    if (grantLive_ && activeGrantId_ == grantId) activeEpoch_ = epoch;
  }

  // r3 S3: the control session of `epoch` really ended (SessionState::backendSessionEnded). If it was the
  // active grant's, that grant is over: its entry is parked again (its lease runs from now) and the same
  // generation no longer answers. An end of an earlier epoch, or of a Hello's session, changes nothing.
  void SessionEnded(uint64_t epoch, uint64_t nowMs) {
    if (!grantLive_ || activeEpoch_ == 0 || epoch != activeEpoch_) return;
    for (StandbyEntry& e : entries_) {
      if (e.entryId == activeEntryId_) e.lastSeenMs = nowMs;
    }
    ClearActive();
  }

  // Release: the entry goes -- unless it holds the live session (Live: the session is ended the ordinary
  // way, by its control channel; a Release does not pull the table out from under it).
  Verdict Release(uint64_t entryId, const uint8_t secret[16]) {
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (entries_[i].entryId == entryId && std::memcmp(entries_[i].secret.data(), secret, 16) == 0) {
        if (IsLive(entryId)) return Verdict::Live;
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        return Verdict::Ok;
      }
    }
    return Verdict::Unknown;
  }

  // A session that did not come from an entry (an ordinary Hello) took the host: no grant is active; the
  // entry that had it is parked again.
  void SessionTakenByHello(uint64_t nowMs) {
    for (StandbyEntry& e : entries_) {
      if (grantLive_ && e.entryId == activeEntryId_) e.lastSeenMs = nowMs;
    }
    ClearActive();
  }

  // The live grant's entry is not expired while its session runs -- its liveness is the session's.
  void Expire(uint64_t nowMs) {
    for (size_t i = 0; i < entries_.size();) {
      if (!IsLive(entries_[i].entryId) && nowMs - entries_[i].lastSeenMs > kStandbyLeaseMs) {
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
  }

  size_t size() const { return entries_.size(); }
  uint64_t activeEntryId() const { return grantLive_ ? activeEntryId_ : 0; }
  uint64_t activeGrantId() const { return grantLive_ ? activeGrantId_ : 0; }
  const StandbyEntry* Find(uint64_t entryId) const {
    for (const StandbyEntry& e : entries_) {
      if (e.entryId == entryId) return &e;
    }
    return nullptr;
  }

 private:
  bool IsLive(uint64_t entryId) const { return grantLive_ && activeEntryId_ == entryId; }
  void ClearActive() {
    activeEntryId_ = 0;
    activeGrantId_ = 0;
    activeEpoch_ = 0;
    grantLive_ = false;
  }
  StandbyEntry* Match(uint64_t entryId, const uint8_t secret[16]) {
    for (StandbyEntry& e : entries_) {
      if (e.entryId == entryId && std::memcmp(e.secret.data(), secret, 16) == 0) return &e;
    }
    return nullptr;
  }
  // Entry ids and secrets come from the system CSPRNG: a secret is what stands between a parked entry and
  // someone else activating it. r3 S3: the draw's result is checked -- a failure makes nothing.
  bool Draw(uint64_t* v) {
    if (rng_) return rng_(v);
    *v = 0;
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(v), sizeof(*v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    return false;
#endif
  }

  Rng rng_;
  std::vector<StandbyEntry> entries_;
  uint64_t activeEntryId_ = 0;
  uint64_t activeGrantId_ = 0;
  uint64_t activeEpoch_ = 0;
  bool grantLive_ = false;
  uint64_t grantSeq_ = 0;
};

}  // namespace remote60::native_poc
