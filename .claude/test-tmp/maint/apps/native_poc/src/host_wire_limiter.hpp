#pragma once

// A single atomic token-bucket rate limiter for everything the video stream puts on the wire:
// data chunks, FEC parity, and NACK retransmits, each charged its real datagram length + 28 bytes
// (the IPv4 + UDP header the OS adds). (bitrate-hard-cap r1, plan "Codex 반론/합의 / r1".)
//
// Why one bucket, not three: the user sets a bitrate for the video stream, and the guarantee is
// that the bytes that stream puts on the wire stay under it in every 1 s / 250 ms window. Pacing
// each kind against its own budget lets the sum overrun -- the old NACK path had its own bucket at
// 15% of the peak, a free lane around the cap. Here the data sender, the parity, and the NACK
// replay all spend from the same bucket, so there is no lane around it.
//
// Burst math (RFC 3290 token bucket): R = capBps/8 bytes/s is the long-run rate; B = max(Lmax,
// R/100) bytes is the depth -- one datagram at least (so a single packet always fits), otherwise
// 10 ms of rate. With B = R/100 the envelope W(T) <= R*T + B gives, at 6 Mbps (R = 750000):
//   1 s window   <= 1 + B/R          = 1.01  (+1%)
//   250 ms window <= 1 + B/(R*0.25)  = 1.04  (+4%)
// both inside +10%. At low rates Lmax dominates B (below ~1.2 Mbps, where R/100 < Lmax); there the
// single-datagram quantum, not R/100, sets the smallest burst, so the relative 250 ms ceiling is
// larger -- the exact per-rate figure is measured and reported, not claimed uniform.
//
// The limit is the long-run average R. A sender that fell behind does NOT get to send the whole
// backlog at once to "catch up": a gap longer than it takes to fill B only fills B (Refill caps
// elapsed). Spending never drives the bucket below zero -- the blocking Acquire waits for the
// tokens instead of overdrawing, so at most one datagram-worth sits "in flight" past the envelope.
//
// Thread: the data/parity sender thread calls Acquire (blocking -- it waits for tokens rather than
//   dropping a video chunk, because a dropped delta breaks the reference chain). The reader thread
//   calls TryAcquire for NACK replay (non-blocking -- it must not stall control receive; a replay
//   that does not fit now is suppressed, exactly as the old budget did, and the client's IDR
//   fallback covers it). SetRate runs on the main loop (ApplyTarget) and on startup. All state is
//   under mu_; Acquire releases mu_ while it sleeps (through waitHook_) so TryAcquire and SetRate
//   are never blocked behind a waiting sender.
//
// Clock and sleep are injected (nowUs_, waitHook_) so the real send path runs deterministically
// under a fake clock in tests -- the test drives the same Acquire/TryAcquire/SetRate the product
// calls, not a re-implemented bucket.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace remote60::native_poc {

class WireLimiter {
 public:
  enum class Acq { Permitted, Cancelled };

  // nowUs: the monotonic clock in microseconds (qpc_now_us in production, a fake clock in tests).
  // waitHook(deadlineUs, cancelSeqAtEntry): block until nowUs() >= deadlineUs; return true when the
  //   deadline is reached, false if it should stop early (cancel_seq() changed, or the hook decides
  //   to re-evaluate). In production a cancellable sleep; in tests it advances the fake clock.
  WireLimiter(std::function<uint64_t()> nowUs, std::function<bool(uint64_t, uint64_t)> waitHook)
      : nowUs_(std::move(nowUs)), waitHook_(std::move(waitHook)) {}

  // Turn the cap on at capBps (user wire cap) with the largest datagram the wire will carry
  // (lmaxBytes = clamp_udp_mtu(mtu) + 28). capBps == 0 disables the cap (kill-switch off / legacy):
  // Acquire and TryAcquire then permit everything. Changing the rate does NOT refill the bucket:
  // the credit earned so far is settled at the old rate, then kept (a decrease only clamps it down
  // to the new depth) -- re-arming cap/ABR/fps/codec must not open a burst lane. (plan point 4.)
  void SetRate(uint64_t capBps, uint32_t lmaxBytes) {
    std::lock_guard<std::mutex> lk(mu_);
    RefillLocked();  // settle what was earned under the old rate up to now
    R_ = capBps / 8ULL;
    B_ = R_ ? std::max<uint64_t>(lmaxBytes, R_ / 100ULL) : 0ULL;
    enabled_.store(R_ != 0, std::memory_order_relaxed);
    if (tokens_ > B_) tokens_ = B_;  // a decrease removes credit (safe); never adds
    lastRefillUs_ = nowUs_();
    Wake();  // a lower rate lengthens a pending wait; a waiter re-evaluates its deadline
  }

  // Blocking: wait until `bytes` tokens are available, spend them, return Permitted. While waiting,
  // returns Cancelled if Stop() was called or the live media epoch moved off itemEpoch (the frame
  // is of a session that is over) -- the caller then abandons the send, as it does on EpochChanged.
  // liveEpoch may be null (no epoch fence). Disabled cap -> Permitted at once.
  Acq Acquire(uint64_t bytes, const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch) {
    if (!enabled_.load(std::memory_order_relaxed)) return Acq::Permitted;
    for (;;) {
      uint64_t deadline;
      {
        std::lock_guard<std::mutex> lk(mu_);
        if (R_ == 0) return Acq::Permitted;
        RefillLocked();
        if (tokens_ >= bytes) {
          tokens_ -= bytes;
          spentBytes_.fetch_add(bytes, std::memory_order_relaxed);
          return Acq::Permitted;
        }
        const uint64_t need = bytes - tokens_;
        deadline = lastRefillUs_ + (need * 1000000ULL + R_ - 1ULL) / R_;
      }
      if (ShouldCancel(liveEpoch, itemEpoch)) return Acq::Cancelled;
      waitCount_.fetch_add(1, std::memory_order_relaxed);
      const uint64_t seq = cancelSeq_.load(std::memory_order_acquire);
      waitHook_(deadline, seq);  // sleeps until deadline, or returns early on cancel/rate change
      if (ShouldCancel(liveEpoch, itemEpoch)) return Acq::Cancelled;
      // otherwise loop: refill at the current rate and re-check (the rate may have dropped)
    }
  }

  // Peek (non-consuming) for the unified send-path admission (stutter-keyframe r6 C1 / r8 D1). Returns
  // 0 == "ready NOW" (tokens available), else the absolute clock deadline at which `bytes` will be
  // available. r8 D1: it must NOT return a fresh nowUs_() for the ready case -- the caller compares
  // against its OWN `now` snapshot, and a fresh (later) clock read would make `deadline <= now` false
  // even when ready, busy-spinning the loop. A disabled cap is ready now (0).
  uint64_t NextReadyUs(uint64_t bytes) {
    if (!enabled_.load(std::memory_order_relaxed)) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    if (R_ == 0) return 0;
    RefillLocked();
    if (tokens_ >= bytes) return 0;  // ready now (sentinel, NOT a fresh clock read)
    const uint64_t need = bytes - tokens_;
    return lastRefillUs_ + (need * 1000000ULL + R_ - 1ULL) / R_;
  }

  // Non-blocking: spend `bytes` only if they are available now. Returns false (and counts a
  // suppression) when they are not -- the reader thread must not block on the wire.
  bool TryAcquire(uint64_t bytes) {
    if (!enabled_.load(std::memory_order_relaxed)) return true;
    std::lock_guard<std::mutex> lk(mu_);
    if (R_ == 0) return true;
    RefillLocked();
    if (tokens_ < bytes) {
      suppressedCount_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    tokens_ -= bytes;
    spentBytes_.fetch_add(bytes, std::memory_order_relaxed);
    return true;
  }

  // Give back tokens reserved for a datagram that then did NOT go out (r5 G2: a replay fenced by a
  // rollover/flush after its token was acquired). Clamped to the depth, so it never grants credit
  // beyond the bucket -- the spend set tokens_ to at most B_-bytes, so returning bytes lands at <= B_.
  void Refund(uint64_t bytes) {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(mu_);
    tokens_ += bytes;
    if (tokens_ > B_) tokens_ = B_;
    if (spentBytes_.load(std::memory_order_relaxed) >= bytes)
      spentBytes_.fetch_sub(bytes, std::memory_order_relaxed);
  }

  // Wake every blocked Acquire so it re-checks its fence (call on stop / epoch roll / peer change).
  void Cancel() { Wake(); }
  void Stop() {
    stop_.store(true, std::memory_order_release);
    Wake();
  }
  bool stopped() const { return stop_.load(std::memory_order_acquire); }

  uint64_t cancel_seq() const { return cancelSeq_.load(std::memory_order_acquire); }
  bool enabled() const { return enabled_.load(std::memory_order_relaxed); }

  // Telemetry (cumulative).
  uint64_t spent_bytes() const { return spentBytes_.load(std::memory_order_relaxed); }
  uint64_t wait_count() const { return waitCount_.load(std::memory_order_relaxed); }
  uint64_t suppressed_count() const { return suppressedCount_.load(std::memory_order_relaxed); }

  // Test-only: the current token level and depth, so a test can assert the bucket math without a
  // second implementation of it.
  uint64_t tokens_for_test() {
    std::lock_guard<std::mutex> lk(mu_);
    RefillLocked();
    return tokens_;
  }
  uint64_t depth_for_test() const {
    std::lock_guard<std::mutex> lk(mu_);
    return B_;
  }
  uint64_t rate_bytes_for_test() const {
    std::lock_guard<std::mutex> lk(mu_);
    return R_;
  }

 private:
  bool ShouldCancel(const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch) const {
    if (stop_.load(std::memory_order_acquire)) return true;
    return liveEpoch && liveEpoch->load(std::memory_order_acquire) != itemEpoch;
  }

  void Wake() { cancelSeq_.fetch_add(1, std::memory_order_acq_rel); }

  // Add the whole bytes R produced since lastRefillUs_, carrying the sub-byte remainder exactly (in
  // units of bytes*1e6) so no rate is lost or gained to integer truncation; cap a long idle gap at a
  // full bucket so a late wake-up cannot "catch up" a backlog. mu_ held.
  void RefillLocked() {
    const uint64_t now = nowUs_();
    if (R_ == 0 || now <= lastRefillUs_) {
      if (now > lastRefillUs_) lastRefillUs_ = now;
      return;
    }
    uint64_t elapsed = now - lastRefillUs_;
    lastRefillUs_ = now;  // time is always fully consumed; the sub-byte remainder lives in carry_
    // Enough elapsed time to more than fill B from empty: clamp to full and drop the excess (and
    // the carry), so a long idle gap gives exactly one bucket, never a backlog's worth.
    const uint64_t fillUs = (B_ * 1000000ULL) / R_ + 1000000ULL;
    if (elapsed >= fillUs) {
      tokens_ = B_;
      carryScaled_ = 0;
      return;
    }
    carryScaled_ += elapsed * R_;              // + bytes*1e6 produced in `elapsed`
    const uint64_t add = carryScaled_ / 1000000ULL;
    carryScaled_ -= add * 1000000ULL;          // keep the sub-byte remainder
    tokens_ += add;
    if (tokens_ >= B_) {
      tokens_ = B_;
      carryScaled_ = 0;  // full: drop the overflow and its fraction
    }
  }

  std::function<uint64_t()> nowUs_;
  std::function<bool(uint64_t, uint64_t)> waitHook_;

  mutable std::mutex mu_;
  uint64_t R_ = 0;              // bytes/s (capBps/8)
  uint64_t B_ = 0;             // bucket depth, bytes
  uint64_t tokens_ = 0;        // current tokens, bytes; never below 0, never above B_
  uint64_t lastRefillUs_ = 0;
  uint64_t carryScaled_ = 0;   // sub-byte refill remainder, in bytes*1e6 (< 1e6)

  std::atomic<bool> enabled_{false};
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> cancelSeq_{0};
  std::atomic<uint64_t> spentBytes_{0};
  std::atomic<uint64_t> waitCount_{0};
  std::atomic<uint64_t> suppressedCount_{0};
};

}  // namespace remote60::native_poc
