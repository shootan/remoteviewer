#pragma once

// Conditional IDR burst with a 2s rolling-window average cap (stutter-keyframe r4 B1), corrected per
// Codex 4fba8a1 review (B1-1/B1-2/B1-3).
//
// User option 1: on a clamp-detected encoder-self IDR let the IDR exceed the instantaneous strict
// rate for a moment, while the AVERAGE over every 2s window stays within the user's rate. The single
// invariant, over the COMMON ledger (original H.264 + protocol header + FEC/parity + real NACK
// retransmit + UDP/IP overhead), is:
//
//     for all t:  A(t-2s, t]  <=  2 * r         (r = capBps/8 bytes/s)
//
// B1-1: the window is the ADMISSION for EVERY B1 datagram, not just a grant quota. The send path calls
// Reserve(nowUs, bytes) before each datagram; it only sends if the window (committed + pending) has
// room, otherwise it waits (cancellably) until old bytes expire. CommitSent records the REAL send;
// CancelUnsent returns an unsent reservation (fence/fail). So the 300000 + grant 75000 + replay 20000
// = 395000 > 375000 sequence is blocked at the replay's Reserve.
//
// B1-2: a grant is OWNED by an AU identity (media epoch, generation, input epoch, seq). Permit/debit/
// end all check the owner: a different AU's replay cannot spend A's grant, and an old EndGrant cannot
// release a successor's grant. The same AU is not re-granted after its grant ends (a cooldown), so one
// IDR does not get a second bonus. Original/parity/same-AU replay share the one grant.
//
// B1-3: no free 2s credit on a cold ledger -- grants are gated by a warm-up (the ledger must have run
// kBurstWarmupUs so the window reflects real history). SetRate takes nowUs, keeps the sent history
// (epoch/codec/peer changes fence a grant but never wipe history or the cooldown), and on a downshift
// re-limits the active grant's remaining budget to the new window headroom.
//
// Thread: SetRate runs on the main loop (ApplyTarget); Reserve/CommitSent/CancelUnsent/Grant* on the
// sender thread. All state is under mu_.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>

namespace remote60::native_poc {

inline constexpr uint64_t kBurstWindowUs = 2'000'000;        // W = 2s rolling window
inline constexpr uint64_t kBurstGrantMaxBytes = 256u * 1024; // the 256 KiB arm of the per-grant cap
inline constexpr uint64_t kBurstMinIntervalUs = 1'000'000;   // >= 1s between grants
inline constexpr uint64_t kBurstPeakAbsCapBps = 12'000'000;  // the 12 Mbps arm of the peak cap
inline constexpr uint64_t kBurstWarmupUs = 2'000'000;        // no grant until the window has 2s of history

// The AU a grant is bound to (B1-2).
struct BurstAuId {
  uint64_t mediaEpoch = 0;
  uint64_t generation = 0;
  uint64_t inputEpoch = 0;
  uint32_t seq = 0;
  bool valid = false;
  bool operator==(const BurstAuId& o) const {
    return valid && o.valid && mediaEpoch == o.mediaEpoch && generation == o.generation &&
           inputEpoch == o.inputEpoch && seq == o.seq;
  }
};

class BurstLedger {
 public:
  // capBps == 0 disables (no cap / legacy). nowUs seeds the warm-up / history clock on the first call.
  void SetRate(uint64_t capBps, uint64_t nowUs) {
    std::lock_guard<std::mutex> lk(mu_);
    Prune(nowUs);
    const uint64_t newR = capBps / 8ULL;
    if (newR < r_ && grantActive_) {
      // downshift: re-limit the active grant's remaining to the new window headroom (B1-3).
      const uint64_t headroom = (2ULL * newR > windowBytes_ + pendingBytes_)
                                    ? (2ULL * newR - windowBytes_ - pendingBytes_)
                                    : 0ULL;
      grantRemaining_ = std::min(grantRemaining_, headroom);
    }
    r_ = newR;
    capBps_ = capBps;
    if (!started_) { startedUs_ = nowUs; started_ = true; }  // first rate set: warm-up start (no free credit)
  }
  uint64_t rate_bytes() const {
    std::lock_guard<std::mutex> lk(mu_);
    return r_;
  }
  uint64_t peak_bytes_per_s() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (capBps_ == 0) return 0;
    return std::max<uint64_t>(capBps_, std::min<uint64_t>(4ULL * capBps_, kBurstPeakAbsCapBps)) / 8ULL;
  }

  // B1-1 window admission. Reserve `bytes` of window budget for a datagram about to be sent; returns
  // false if committed+pending+bytes would exceed 2r (the caller then waits/defers -- see RoomAtUs).
  // A disabled cap (r==0) always permits (legacy pacing-only). Reserve/CommitSent/CancelUnsent form one
  // owner path: the caller Reserves, then CommitSent on a real send or CancelUnsent on a fence/failure.
  bool Reserve(uint64_t nowUs, uint64_t bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    Prune(nowUs);
    if (r_ == 0) { pendingBytes_ += bytes; return true; }
    if (windowBytes_ + pendingBytes_ + bytes > 2ULL * r_) return false;
    pendingBytes_ += bytes;
    return true;
  }
  void CommitSent(uint64_t nowUs, uint64_t bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    Prune(nowUs);
    if (pendingBytes_ >= bytes) pendingBytes_ -= bytes; else pendingBytes_ = 0;
    sent_.push_back({nowUs, bytes});
    windowBytes_ += bytes;
    if (windowBytes_ > maxWindowBytes_) maxWindowBytes_ = windowBytes_;
  }
  void CancelUnsent(uint64_t bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    if (pendingBytes_ >= bytes) pendingBytes_ -= bytes; else pendingBytes_ = 0;
  }
  // The earliest nowUs at which the window would have room for `bytes` (for a cancellable wait).
  uint64_t RoomAtUs(uint64_t nowUs, uint64_t bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    Prune(nowUs);
    if (r_ == 0 || windowBytes_ + pendingBytes_ + bytes <= 2ULL * r_) return nowUs;
    uint64_t need = (windowBytes_ + pendingBytes_ + bytes) - 2ULL * r_;  // bytes that must expire first
    for (const auto& e : sent_) {
      if (need == 0) return e.us + kBurstWindowUs;
      need = (need > e.bytes) ? need - e.bytes : 0;
      if (need == 0) return e.us + kBurstWindowUs;
    }
    return nowUs + kBurstWindowUs;  // fallback: a full window from now
  }

  // B1-2/B1-3 grant: warm-up-gated, owned by `owner`, one at a time, >= 1s apart, and NOT re-granted to
  // the same owner (cooldown). Returns the grant's acceleration byte budget (<= min(256KiB, r*1s) and
  // the window remaining), 0 = no grant (the IDR goes out on strict pacing + window admission).
  uint64_t GrantForIdr(uint64_t nowUs, const BurstAuId& owner) {
    std::lock_guard<std::mutex> lk(mu_);
    if (r_ == 0 || !owner.valid) return 0;
    if (grantActive_) return 0;                                                    // one at a time
    if (!started_ || nowUs < startedUs_ + kBurstWarmupUs) return 0;                 // warm-up: no cold burst
    if (everGranted_ && nowUs < lastGrantUs_ + kBurstMinIntervalUs) return 0;       // >= 1s apart
    if (lastGrantOwner_ == owner) return 0;                                         // no re-grant same AU
    Prune(nowUs);
    const uint64_t perGrantCap = std::min<uint64_t>(kBurstGrantMaxBytes, r_);       // min(256KiB, r*1s)
    const uint64_t windowRem = (2ULL * r_ > windowBytes_ + pendingBytes_) ? (2ULL * r_ - windowBytes_ - pendingBytes_) : 0ULL;
    const uint64_t grant = std::min<uint64_t>(perGrantCap, windowRem);
    if (grant == 0) return 0;
    grantActive_ = true;
    grantRemaining_ = grant;
    grantOwner_ = owner;
    lastGrantUs_ = nowUs;
    everGranted_ = true;
    burstPacerUs_ = 0;
    return grant;
  }
  // How many of `bytes` the owner's grant covers right now (0 if not the owner / spent). Does NOT
  // reserve window -- the caller still Reserves/CommitSent every datagram.
  uint64_t GrantCoverage(const BurstAuId& owner, uint64_t bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!grantActive_ || !(grantOwner_ == owner)) return 0;
    return std::min<uint64_t>(bytes, grantRemaining_);
  }
  void DebitGrant(const BurstAuId& owner, uint64_t bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!grantActive_ || !(grantOwner_ == owner)) return;
    grantRemaining_ = (grantRemaining_ > bytes) ? grantRemaining_ - bytes : 0;
    if (grantRemaining_ == 0) grantActive_ = false;
  }
  // Only the owner ends its grant (B1-2); records it for the same-AU cooldown. An old/foreign EndGrant
  // is ignored.
  void EndGrant(const BurstAuId& owner) {
    std::lock_guard<std::mutex> lk(mu_);
    if (grantActive_ && !(grantOwner_ == owner)) return;
    if (owner.valid) lastGrantOwner_ = owner;
    grantActive_ = false;
    grantRemaining_ = 0;
  }
  // Peak-pace one burst datagram: the wall time it may be sent at (<= peak), advancing the cursor.
  uint64_t BurstSendDeadlineUs(uint64_t nowUs, uint64_t bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    if (burstPacerUs_ < nowUs) burstPacerUs_ = nowUs;
    const uint64_t deadline = burstPacerUs_;
    const uint64_t peak = capBps_ ? std::max<uint64_t>(capBps_, std::min<uint64_t>(4ULL * capBps_, kBurstPeakAbsCapBps)) / 8ULL : 0ULL;
    if (peak > 0) burstPacerUs_ += (bytes * 1'000'000ULL) / peak;
    return deadline;
  }

  uint64_t window_bytes(uint64_t nowUs) {
    std::lock_guard<std::mutex> lk(mu_);
    Prune(nowUs);
    return windowBytes_;
  }
  uint64_t max_window_bytes() const {
    std::lock_guard<std::mutex> lk(mu_);
    return maxWindowBytes_;
  }

 private:
  void Prune(uint64_t nowUs) {
    while (!sent_.empty() && sent_.front().us + kBurstWindowUs <= nowUs) {
      windowBytes_ -= sent_.front().bytes;
      sent_.pop_front();
    }
  }
  struct Entry { uint64_t us; uint64_t bytes; };
  mutable std::mutex mu_;
  std::deque<Entry> sent_;
  uint64_t windowBytes_ = 0;
  uint64_t pendingBytes_ = 0;  // reserved-but-not-yet-committed
  uint64_t r_ = 0;
  uint64_t capBps_ = 0;
  uint64_t startedUs_ = 0;     // first SetRate (warm-up / no cold free credit)
  bool started_ = false;
  bool grantActive_ = false;
  uint64_t grantRemaining_ = 0;
  BurstAuId grantOwner_;
  BurstAuId lastGrantOwner_;   // the last AU granted (same-AU cooldown)
  uint64_t lastGrantUs_ = 0;
  bool everGranted_ = false;
  uint64_t burstPacerUs_ = 0;
  uint64_t maxWindowBytes_ = 0;
};

}  // namespace remote60::native_poc
