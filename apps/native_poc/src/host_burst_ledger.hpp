#pragma once

// Conditional IDR burst with a 2s rolling-window average cap (stutter-keyframe r4 B1).
//
// User decision = option 1: on a clamp-detected encoder-self IDR, let the IDR exceed the instantaneous
// strict cap for a moment, while the AVERAGE over every 2s window stays within the user's rate. The
// contract (Codex 0daf0a5 section 5) is ONE invariant, enforced over the COMMON ledger (original H.264
// + protocol header + FEC/parity + real NACK retransmit + UDP/IP overhead):
//
//     for all t:  A(t-2s, t]  <=  2 * r         (r = capBps/8 bytes/s)
//
// A token bucket (r*T + B) does NOT give this (it allows 2r+B over 2s and grows B with the window), so
// admission is judged by a real rolling-window ledger of recently-sent bytes, not a bucket. A burst is
// only granted from the window's REMAINING budget -- "borrow now, send less later" does not satisfy a
// rolling window, because an already-full 2s budget has no room. Every sent datagram is Recorded
// (permanently -- no refund of bytes that went out, no free credit on NACK/epoch/reconnect); the
// history is what the window sums.
//
// Burst bounds (conservative starting values; a measurement fixes the final ones):
//   - per-IDR accelerated bytes:  grant <= min(256 KiB, r*1s)  AND  <= window remaining under 2r.
//   - send peak:                  peak  <= max(R, min(4R, 12 Mbps))  (the sender paces to this; the
//                                 ledger exposes it, the socket loop does not dump an AU unpaced).
//   - one grant at a time, >= 1s between grants, bound to the AU (its original/parity/replay share it).
//
// Pure: the clock is an explicit `nowUs` argument, so the window invariant and the ledger-removed
// negative control are unit tests (host_burst_ledger_test.cpp). Not wired into the live sender here --
// this is the reviewed contract logic the sender will call.

#include <algorithm>
#include <cstdint>
#include <deque>

namespace remote60::native_poc {

inline constexpr uint64_t kBurstWindowUs = 2'000'000;        // W = 2s rolling window
inline constexpr uint64_t kBurstGrantMaxBytes = 256u * 1024; // the 256 KiB arm of the per-grant cap
inline constexpr uint64_t kBurstMinIntervalUs = 1'000'000;   // >= 1s between grants
inline constexpr uint64_t kBurstPeakAbsCapBps = 12'000'000;  // the 12 Mbps arm of the peak cap

class BurstLedger {
 public:
  // capBps == 0 disables (no cap / legacy): no window limit, no grants.
  void SetRate(uint64_t capBps) { r_ = capBps / 8ULL; capBps_ = capBps; }
  uint64_t rate_bytes() const { return r_; }

  // The send peak the burst pacer must stay under: max(R, min(4R, 12 Mbps)), in bytes/s.
  uint64_t peak_bytes_per_s() const {
    if (capBps_ == 0) return 0;
    const uint64_t peakBps = std::max<uint64_t>(capBps_, std::min<uint64_t>(4ULL * capBps_, kBurstPeakAbsCapBps));
    return peakBps / 8ULL;
  }

  // Record `bytes` put on the wire at nowUs -- EVERY datagram (data, parity, retransmit), each its
  // real length + the 28-byte IPv4/UDP header the caller already adds. Permanent; prunes >2s history.
  void Record(uint64_t nowUs, uint64_t bytes) {
    sent_.push_back({nowUs, bytes});
    windowBytes_ += bytes;
    Prune(nowUs);
    if (windowBytes_ > maxWindowBytes_) maxWindowBytes_ = windowBytes_;  // telemetry: the 2s-window peak
  }
  uint64_t max_window_bytes() const { return maxWindowBytes_; }  // peak A(t-2s,t] observed (<= 2r target)

  uint64_t window_bytes(uint64_t nowUs) {
    Prune(nowUs);
    return windowBytes_;
  }

  // Budget remaining under the 2r ceiling over (nowUs-2s, nowUs].
  uint64_t window_remaining(uint64_t nowUs) {
    Prune(nowUs);
    const uint64_t cap = 2ULL * r_;
    return windowBytes_ >= cap ? 0ULL : cap - windowBytes_;
  }

  // Grant accelerated bytes for a clamp-detected real self-IDR of `auWireBytes` (the whole AU's wire
  // cost incl its parity/header estimate). 0 = no grant -> the IDR goes out on normal strict pacing.
  // Bounded by min(256 KiB, r*1s), the window remaining, one-at-a-time, and >= 1s since the last grant.
  uint64_t GrantForIdr(uint64_t nowUs, uint64_t auWireBytes) {
    if (r_ == 0) return 0;
    if (grantActive_) return 0;                                                      // one at a time
    if (everGranted_ && nowUs < lastGrantUs_ + kBurstMinIntervalUs) return 0;         // >= 1s apart
    const uint64_t perGrantCap = std::min<uint64_t>(kBurstGrantMaxBytes, r_);         // min(256KiB, r*1s)
    const uint64_t grant = std::min<uint64_t>({auWireBytes, perGrantCap, window_remaining(nowUs)});
    if (grant == 0) return 0;                                                         // window full -> strict
    grantActive_ = true;
    grantRemaining_ = grant;
    lastGrantUs_ = nowUs;
    everGranted_ = true;
    burstPacerUs_ = 0;  // reset the peak pacer for this grant
    return grant;
  }

  // Peak-pace one burst datagram: return the wall time it may be sent at (<= peak_bytes_per_s),
  // advancing the per-grant cursor. Called on the sender thread while a grant is active, so the
  // mutable cursor lives here (the WireEgress the send path sees is const). nowUs seeds the cursor.
  uint64_t BurstSendDeadlineUs(uint64_t nowUs, uint64_t bytes) {
    if (burstPacerUs_ < nowUs) burstPacerUs_ = nowUs;
    const uint64_t deadline = burstPacerUs_;
    const uint64_t peak = peak_bytes_per_s();
    if (peak > 0) burstPacerUs_ += (bytes * 1'000'000ULL) / peak;
    return deadline;
  }

  // While an AU's datagrams go out under a grant, debit the grant. Returns how many of `bytes` the
  // grant covers (the rest, if any, is normal-paced). Does NOT Record -- the caller Records every
  // datagram exactly once regardless of whether it rode the grant.
  uint64_t DebitGrant(uint64_t bytes) {
    if (!grantActive_) return 0;
    const uint64_t use = std::min<uint64_t>(bytes, grantRemaining_);
    grantRemaining_ -= use;
    if (grantRemaining_ == 0) grantActive_ = false;  // grant spent
    return use;
  }
  void EndGrant() {  // the AU finished / was cancelled: release the single grant (no refund of sent bytes)
    grantActive_ = false;
    grantRemaining_ = 0;
  }
  bool grant_active() const { return grantActive_; }
  uint64_t grant_remaining() const { return grantActive_ ? grantRemaining_ : 0; }

 private:
  void Prune(uint64_t nowUs) {
    while (!sent_.empty() && sent_.front().us + kBurstWindowUs <= nowUs) {
      windowBytes_ -= sent_.front().bytes;
      sent_.pop_front();
    }
  }
  struct Entry { uint64_t us; uint64_t bytes; };
  std::deque<Entry> sent_;
  uint64_t windowBytes_ = 0;
  uint64_t r_ = 0;         // bytes/s
  uint64_t capBps_ = 0;
  bool grantActive_ = false;
  uint64_t grantRemaining_ = 0;
  uint64_t lastGrantUs_ = 0;
  bool everGranted_ = false;
  uint64_t burstPacerUs_ = 0;  // peak-pacer cursor for the active grant
  uint64_t maxWindowBytes_ = 0;  // telemetry
};

}  // namespace remote60::native_poc
