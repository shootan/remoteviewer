#pragma once

// Clipboard image v1: the bulk sender's pacer (plan r2 §2).
//
// Role:    every datagram the viewer's bulk channel puts on the wire -- first sends, retransmits,
//          and its acknowledgements -- waits here for tokens at the rate the BulkRateController
//          allows. The channel hands datagrams over without blocking (its SendFn runs under its
//          own mutex, which the UDP ingress also needs); a pacer thread drains them.
//          Waiting is on a high-resolution waitable timer: sleep_for rounds to ~15.6 ms on Windows,
//          which is exactly what capped the control channel at ~15 Mbps (file-copy ⑩).
//          Acknowledgements (ControlAck / ControlNack) jump the queue: the host's pulls ride a
//          head-only channel, so an ack stuck behind a second of queued data would stall the very
//          pull that asks for more. They still pay -- in debt the next data datagram repays.
//          A resend of a datagram that left less than the resend guard ago is dropped too: the
//          receiver's NACK wait (90 ms, the control channel's, unchanged by agreement) is shorter than
//          the gap between datagrams at low rates, so its NACK races fragments still in flight --
//          resending them would only duplicate them and read as loss. A real loss is asked for again
//          by the next NACK, after the guard.
//          A resend of a datagram that is still queued is a no-op -- at 64 kbps a 16 KiB message
//          takes two seconds to leave, and the channel's retry would otherwise double it; a resend
//          of one that already left counts as loss, the controller's first evidence.
// Thread:  Enqueue from any thread (the channel's send path); one pacer thread of its own.
// Callers: viewer_clip_image.cpp (the viewer's bulk sender), bulk_pacer_test.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <map>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

namespace remote60::native_poc {

/** Byte token bucket. Pure; the pacer thread owns one. */
class BulkTokenBucket {
 public:
  /** Refills for the time since the last call at `rateBps`; capacity bounds the burst. */
  void Refill(uint64_t nowUs, uint32_t rateBps) {
    if (lastUs_ == 0 || nowUs < lastUs_) {
      lastUs_ = nowUs;
      return;
    }
    const uint64_t elapsed = nowUs - lastUs_;
    lastUs_ = nowUs;
    if (rateBps == 0) {  // paused: nothing accrues, and nothing saved up survives the pause
      tokens_ = 0;
      return;
    }
    tokens_ += static_cast<double>(elapsed) * rateBps / 8.0 / 1e6;
    const double cap = Capacity(rateBps);
    if (tokens_ > cap) tokens_ = cap;
  }
  bool TryTake(size_t bytes) {
    if (tokens_ + 1e-9 < static_cast<double>(bytes)) return false;
    tokens_ -= static_cast<double>(bytes);
    return true;
  }
  /** Takes regardless, running into debt the next data datagrams repay (acknowledgements). */
  void TakeOwed(size_t bytes) { tokens_ -= static_cast<double>(bytes); }
  /** Microseconds until `bytes` are available at `rateBps` (UINT64_MAX while paused). */
  uint64_t WaitUs(size_t bytes, uint32_t rateBps) const {
    if (rateBps == 0) return UINT64_MAX;
    const double need = static_cast<double>(bytes) - tokens_;
    if (need <= 0) return 0;
    return static_cast<uint64_t>(need * 8.0 * 1e6 / rateBps) + 1;
  }
  /** A fixed burst (agreed): four full datagrams, whatever the rate. */
  static constexpr double kBurstBytes = 4 * 1500.0;
  static double Capacity(uint32_t /*rateBps*/) { return kBurstBytes; }
  double tokens() const { return tokens_; }

 private:
  double tokens_ = 0;
  uint64_t lastUs_ = 0;
};

class BulkPacer {
 public:
  using RawSendFn = std::function<bool(const void* data, size_t len)>;
  using RateFn = std::function<uint32_t(uint64_t nowUs)>;  // bits/s; 0 = paused
  using YieldFn = std::function<bool()>;                   // true: control traffic waits -- let it go first
  /** After a datagram left: its bytes, when, and whether it was a resend of one that had left. */
  using TransmittedFn = std::function<void(const uint8_t* data, size_t len, uint64_t nowUs, bool resend)>;

  struct Stats {
    uint64_t datagramsSent = 0;
    uint64_t bytesSent = 0;
    uint64_t resendsAfterTransmit = 0;  // loss evidence
    uint64_t resendsCoalesced = 0;      // a retry of a datagram still queued: dropped as a duplicate
    uint64_t droppedQueueFull = 0;
    uint64_t yields = 0;
    uint64_t idleUs = 0;                // time with nothing queued (app-limited evidence)
    uint64_t resendsTooSoon = 0;        // resends of a datagram that had only just left: dropped
  };

  ~BulkPacer() { Stop(); }

  bool Start(RawSendFn send, RateFn rate, YieldFn yield, TransmittedFn transmitted);
  void Stop();

  /** A resend of a datagram sent less than this long ago is dropped (0 = no guard). */
  void SetResendGuardUs(uint64_t us) { resendGuardUs_.store(us, std::memory_order_relaxed); }

  /** Non-blocking: queue one datagram (the channel's SendFn). False when full or stopped. */
  bool Enqueue(const void* data, size_t len);

  Stats GetStats() const {
    std::lock_guard<std::mutex> lock(mu_);
    return stats_;
  }
  size_t Queued() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
  }

  static constexpr size_t kMaxQueuedDatagrams = 256;

  static uint64_t NowUs();

 private:
  using Key = std::tuple<uint32_t, uint32_t, uint16_t>;  // streamId, messageSeq, fragIndex
  static bool DataKey(const uint8_t* data, size_t len, Key* key);
  void Run();

  RawSendFn send_;
  RateFn rate_;
  YieldFn yield_;
  TransmittedFn transmitted_;

  mutable std::mutex mu_;
  std::deque<std::vector<uint8_t>> queue_;
  size_t queuedAcks_ = 0;  // acknowledgements at the front of queue_
  std::set<Key> queuedKeys_;
  std::map<Key, uint64_t> sentKeys_;  // when each recent datagram last left; pruned by message sequence
  std::atomic<uint64_t> resendGuardUs_{0};
  uint32_t newestSeq_ = 0;
  Stats stats_;
  std::atomic<bool> running_{false};
  HANDLE wake_ = nullptr;
  HANDLE timer_ = nullptr;
  std::thread thread_;
};

}  // namespace remote60::native_poc
