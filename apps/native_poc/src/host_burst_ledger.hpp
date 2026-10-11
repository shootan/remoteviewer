#pragma once

// Conditional IDR burst with a 2s rolling-window average cap (stutter-keyframe r4 B1), corrected per
// Codex 4fba8a1 review (B1-1/B1-2/B1-3).
//
// User option 1: on a clamp-detected encoder-self IDR let the IDR exceed the instantaneous strict
// rate for a moment, while the AVERAGE over every 2s window stays within the user's rate. The invariant,
// over the COMMON ledger (original H.264 + protocol header + FEC/parity + real NACK retransmit + UDP/IP
// overhead), is an ADMISSION rule at a CONSTANT rate:
//
//     no datagram is admitted that makes A(t-2s, t] exceed 2 * r      (r = capBps/8 bytes/s)
//
// At a constant rate this keeps A(t-2s,t] <= 2r for all t. Across a rate DOWNSHIFT it does NOT hold
// retroactively for the trailing window: bytes sent legally under the old higher rate stay in the window
// for up to 2s (transition DEBT -- never refunded), so the trailing sum can exceed the new rate's 2r
// until that debt ages out. The rule's guarantee is that NO NEW admission grows the window past the
// stricter of {mixed integral, new flat 2r} (S2, AdmissionBudgetBytes), so the debt only drains and a
// full 2s window made solely of new-rate sends is back within the new 2r. See the S2 transition rule and
// its arithmetic counter-example in host_burst_ledger_test.cpp.
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
// r4 R5 R3 (Codex e1bc633): the window cap is NOT a flat 2*r_current applied retroactively across the
// whole 2s (that gives an upshift free retroactive credit, and leaves a downshift's old 256 KiB grant
// oversized). It is the time-weighted integral of the MIXED rate history over the window:
//
//     Budget(t) = integral over tau in (t-2s, t] of r(tau) d(tau)   (== 2r for a constant rate)
//
// so a higher rate only earns budget for the time AFTER the change (no retroactive credit), a lower rate
// earns less going forward, and the span before the first SetRate contributes r=0 (this subsumes the
// cold-start/warm-up ramp). On a downshift the active grant's remaining is additionally clamped to the
// new per-key cap min(256KiB, newR) minus what it has already spent, and to the new window headroom.
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
    const uint64_t prevR = r_;
    r_ = newR;
    capBps_ = capBps;
    if (!started_) { startedUs_ = nowUs; started_ = true; }  // first rate set: warm-up start (no free credit)
    // Record the rate change in the mixed-rate history (coalesce a repeated same-rate SetRate so a
    // frequent ApplyTarget caller does not pile up identical segments).
    if (rateHist_.empty() || rateHist_.back().r != newR) rateHist_.push_back({nowUs, newR});
    if (newR < prevR && grantActive_) {
      // downshift (B1-3 / R3): clamp the active grant's remaining to BOTH the new per-key cap
      // (minus what it has already spent) AND the new admission headroom (S2: flat new 2r, not the
      // still-high mixed integral).
      const uint64_t budget = AdmissionBudgetBytes(nowUs);
      const uint64_t headroom =
          (budget > windowBytes_ + pendingBytes_) ? (budget - windowBytes_ - pendingBytes_) : 0ULL;
      const uint64_t newPerKey = std::min<uint64_t>(kBurstGrantMaxBytes, newR);
      const uint64_t spent = (grantGranted_ > grantRemaining_) ? grantGranted_ - grantRemaining_ : 0ULL;
      const uint64_t perKeyRem = (newPerKey > spent) ? newPerKey - spent : 0ULL;
      grantRemaining_ = std::min<uint64_t>({grantRemaining_, perKeyRem, headroom});
      grantGranted_ = spent + grantRemaining_;
      if (grantRemaining_ == 0) grantActive_ = false;
    }
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
    if (windowBytes_ + pendingBytes_ + bytes > AdmissionBudgetBytes(nowUs)) return false;
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
  // r4 R6 C1 (Codex 89c08de): peek -- is there 2s-window room for `bytes` right now, WITHOUT reserving?
  // The unified send-path admission (host_net_io) peeks the window + the rate together and does one wait,
  // then Reserves once both are ready. A disabled cap always has room.
  bool HasRoom(uint64_t nowUs, uint64_t bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    Prune(nowUs);
    if (r_ == 0) return true;
    return windowBytes_ + pendingBytes_ + bytes <= AdmissionBudgetBytes(nowUs);
  }
  // Peek the grant peak-pacer's next send instant WITHOUT advancing it (the cursor advances once, at
  // commit, via BurstSendDeadlineUs). <= nowUs means "ready now".
  uint64_t PeakReadyUs(uint64_t nowUs) {
    std::lock_guard<std::mutex> lk(mu_);
    return burstPacerUs_ < nowUs ? nowUs : burstPacerUs_;
  }
  // The earliest nowUs at which the window would have room for `bytes` (for a cancellable wait).
  uint64_t RoomAtUs(uint64_t nowUs, uint64_t bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    Prune(nowUs);
    const uint64_t budget = AdmissionBudgetBytes(nowUs);
    if (r_ == 0 || windowBytes_ + pendingBytes_ + bytes <= budget) return nowUs;
    uint64_t need = (windowBytes_ + pendingBytes_ + bytes) - budget;  // bytes that must expire first
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
    const uint64_t budget = AdmissionBudgetBytes(nowUs);
    const uint64_t windowRem = (budget > windowBytes_ + pendingBytes_) ? (budget - windowBytes_ - pendingBytes_) : 0ULL;
    const uint64_t grant = std::min<uint64_t>(perGrantCap, windowRem);
    if (grant == 0) return 0;
    grantActive_ = true;
    grantRemaining_ = grant;
    grantGranted_ = grant;
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
  // r4 R6 S3 (Codex 72a22d2): the peak stays the original contract -- max(R, min(4R, 12Mbps)), a fixed
  // function of the current cap R, NOT changed here. The congestion chain is explicit and twofold:
  // (1) the SENDER gates GrantForIdr on the existing loss signal (client NACK requests + pending
  // replays): a path that lost packets within kBurstCongestionWindowUs gets NO grant, so a burst never
  // accelerates into fresh loss (strict fallback -- see host_encoded_sender.cpp). (2) the adaptive rate
  // controller lowers capBps on sustained loss, which lowers this peak proportionally and, via a
  // downshift SetRate, re-limits the in-flight grant (R3/S2). The 2s window admission bounds the average
  // regardless. No new BWE probe. This is a real observed-signal brake, not just a comment.
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
    // Drop rate segments that the window has fully slid past, but always keep the one that is active at
    // the window's start (its rate still governs the oldest edge of the window).
    const uint64_t winStart = nowUs >= kBurstWindowUs ? nowUs - kBurstWindowUs : 0ULL;
    while (rateHist_.size() >= 2 && rateHist_[1].us <= winStart) rateHist_.pop_front();
  }
  // R3 mixed-rate budget: integral of r(tau) over (nowUs-2s, nowUs], using the rate history. Units: r is
  // bytes/s and the window is 2s, so for a CONSTANT rate this equals r * 2s == 2r -- the same flat "2r"
  // byte budget as before (the "2" in "2r" is the 2-second window times r). The span before the first
  // recorded rate contributes 0 (cold-start ramp / no retroactive credit on a cold or upshifted ledger).
  // Lock held.
  uint64_t WindowBudgetBytes(uint64_t nowUs) const {
    if (rateHist_.empty()) return 0;
    const uint64_t winStart = nowUs >= kBurstWindowUs ? nowUs - kBurstWindowUs : 0ULL;
    uint64_t budget = 0;
    for (size_t i = 0; i < rateHist_.size(); ++i) {
      const uint64_t segStart = rateHist_[i].us;
      const uint64_t segEnd = (i + 1 < rateHist_.size()) ? rateHist_[i + 1].us : nowUs;
      const uint64_t a = std::max<uint64_t>(segStart, winStart);
      const uint64_t b = std::min<uint64_t>(segEnd, nowUs);
      if (b > a) budget += rateHist_[i].r * (b - a) / 1'000'000ULL;
    }
    return budget;
  }
  // r4 R6 S2 (Codex 72a22d2): the ADMISSION budget is min(mixed integral, flat new-rate 2r). The mixed
  // integral alone is wrong for a DOWNSHIFT: the high-rate history still in the window makes the integral
  // larger than the new rate's flat 2r, so a new byte admitted against the integral could exceed the new
  // cap once that history ages out. Capping admission at the current rate's flat 2r means a downshift
  // admits NOTHING new until the trailing window drains below 2r (the committed high-rate bytes are
  // transition DEBT -- legal when sent, never refunded, drained by expiry, and never grown by a new
  // admission). For an UPSHIFT the integral is the smaller term (it has not yet earned the higher rate),
  // so min() keeps the no-retroactive-credit ramp. A full 2s window of one rate makes both terms 2r.
  uint64_t AdmissionBudgetBytes(uint64_t nowUs) const {
    return std::min<uint64_t>(WindowBudgetBytes(nowUs), 2ULL * r_);
  }
  struct Entry { uint64_t us; uint64_t bytes; };
  struct RateSeg { uint64_t us; uint64_t r; };
  mutable std::mutex mu_;
  std::deque<Entry> sent_;
  std::deque<RateSeg> rateHist_;  // R3 mixed-rate: (us, r) step history for the window budget integral
  uint64_t windowBytes_ = 0;
  uint64_t pendingBytes_ = 0;  // reserved-but-not-yet-committed
  uint64_t r_ = 0;
  uint64_t capBps_ = 0;
  uint64_t startedUs_ = 0;     // first SetRate (warm-up / no cold free credit)
  bool started_ = false;
  bool grantActive_ = false;
  uint64_t grantRemaining_ = 0;
  uint64_t grantGranted_ = 0;  // the grant's original size (for the downshift spent-vs-remaining split)
  BurstAuId grantOwner_;
  BurstAuId lastGrantOwner_;   // the last AU granted (same-AU cooldown)
  uint64_t lastGrantUs_ = 0;
  bool everGranted_ = false;
  uint64_t burstPacerUs_ = 0;
  uint64_t maxWindowBytes_ = 0;
};

}  // namespace remote60::native_poc
