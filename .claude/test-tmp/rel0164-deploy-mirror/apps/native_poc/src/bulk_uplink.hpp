#pragma once

// The viewer's bulk sender, shared by the image and the file paths: a second UdpControlChannel whose
// datagrams all leave through the BulkPacer at the rate BulkRateController allows, answering the
// host's pulls from a serving thread. (Extracted unchanged from ClipImageClient for t-zdmsd4gb r1;
// the rate rules are the 2nd / 3rd rate agreements'.)
//
// Role:    Open / Close one stream pair; a BulkUplinkSource answers each pull (the image package, or
//          a pinned file) and names the chunks it sends by a 64-bit key -- the image path's offset, a
//          file paste's request id -- so the loss, RTT (Karn) and goodput evidence is the same code
//          for both.
// Thread:  Open / Close from the owner's thread (not under the owner's lock: Close joins); the
//          serving thread calls the source; OnDatagram from the UDP receive thread; the pacer thread
//          calls OnTransmitted.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "bulk_pacer.hpp"
#include "bulk_rate_controller.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

constexpr uint64_t kBulkNoKey = ~0ull;

/**
 * The viewer's bulk channel timing: only the whole-message resend interval differs from the control
 * channel's (bulk_retransmit_us, from the pacing rate); the NACK wait is the control channel's own,
 * unchanged by agreement.
 */
inline UdpControlChannel::Timings bulk_uplink_timings() {
  UdpControlChannel::Timings t;
  t.retransmitIntervalUs = 1000000;
  t.maxAttempts = 30;
  return t;
}

/**
 * The sender's whole-message resend interval at `rateBps` for chunks of `chunkBytes`: never shorter
 * than twice the time one chunk takes to leave the pacer, plus half a second. A fixed interval
 * would, at the floor rate (16 KiB = 2 s at 64 kbps), resend every chunk before it had even left --
 * and each resend of a datagram already sent reads as loss, which lowers the rate further.
 */
inline uint64_t bulk_retransmit_us(uint32_t rateBps, uint64_t rttUs, uint32_t chunkBytes) {
  if (rateBps == 0) return 5000000;  // paused
  const uint64_t drainUs = static_cast<uint64_t>(chunkBytes) * 8ull * 1000000ull / rateBps;
  // A lost acknowledgement costs this whole interval, so it is not padded more than the evidence
  // needs: two drains (the message and one queued ahead of it), four round trips, 200 ms.
  const uint64_t rtt = rttUs ? rttUs : 100000;  // unmeasured: assume a long path
  const uint64_t us = 2 * drainUs + 4 * rtt + 200000;
  return us < 300000 ? 300000 : us;
}

/** What a source did with one pull. */
struct BulkServed {
  bool completedChunk = false;  // the pull confirmed an earlier chunk (its trigger): goodput + RTT sample
  uint32_t completedBytes = 0;  // that chunk's size
  uint64_t triggerKey = kBulkNoKey;  // the confirmed chunk's key
  uint64_t chunkKey = kBulkNoKey;    // the key of the chunk sent now
};

class BulkUplinkSource {
 public:
  virtual ~BulkUplinkSource() = default;
  /**
   * A message off the bulk stream. Fill `out` with the whole answer message and `served`; false =
   * not a pull this source answers (dropped, counted).
   */
  virtual bool OnPull(const std::vector<uint8_t>& msg, uint64_t nowUs, std::vector<uint8_t>* out, BulkServed* served) = 0;
  /** The key of an outgoing chunk message, from its first bytes (the first fragment's payload). */
  virtual bool ChunkKeyOf(const uint8_t* message, size_t len, uint64_t* key) = 0;
};

class BulkUplink {
 public:
  using SendFn = std::function<bool(const void* data, size_t len)>;
  using PingRttFn = std::function<uint64_t()>;
  using YieldFn = std::function<bool()>;

  struct Counters {
    uint64_t pullsServed = 0, pullsDropped = 0;
    uint32_t lastRateBps = 0;
    uint64_t srttUs = 0;
    uint64_t raises = 0, lowers = 0, pauses = 0, recoveryHolds = 0, evaluations = 0;
    uint64_t lossEvents = 0, channelFragmentRetransmits = 0, rtoEvents = 0;
  };

  ~BulkUplink() { Close(); }

  /** Once, before the first Open. `chunkBytes` sizes the resend interval (the source's chunk). */
  void Configure(SendFn send, PingRttFn pingRtt, YieldFn yield, uint32_t mtuBytes, BulkRateConfig rate,
                 uint32_t chunkBytes) {
    send_ = std::move(send);
    pingRtt_ = std::move(pingRtt);
    yield_ = std::move(yield);
    mtu_ = mtuBytes;
    rateConfig_ = rate;
    chunkBytes_ = chunkBytes;
    bulk_.SetTimings(bulk_uplink_timings());
  }

  /** Opens `txStream` / `rxStream` for `source` (which must outlive the Close). Resets the evidence. */
  void Open(uint32_t txStream, uint32_t rxStream, BulkUplinkSource* source);
  /** Stops serving and the pacer; the channel closes. Not under the owner's lock (joins). */
  void Close();
  bool open() const { return serving_.load(); }

  /** A bulk datagram (the router already decided it is this uplink's kind). */
  bool OnDatagram(const void* data, size_t len) { return bulk_.OnPacket(data, len); }

  /** The same-direction budget (bits/s; kBudgetUnknown = the configured ceiling alone; 0 = nothing). */
  void SetBudgetBps(uint32_t bps) { budgetBps_.store(bps, std::memory_order_relaxed); }
  static constexpr uint32_t kBudgetUnknown = 0xFFFFFFFFu;

  Counters GetCounters() const {
    std::lock_guard<std::mutex> lock(countersMu_);
    return counters_;
  }
  /** Resets the per-transfer rate counters (the image path counts per transfer). */
  void ResetRateCounters() {
    std::lock_guard<std::mutex> lock(countersMu_);
    counters_.raises = counters_.lowers = counters_.pauses = counters_.recoveryHolds = counters_.evaluations = 0;
    counters_.lossEvents = counters_.channelFragmentRetransmits = counters_.rtoEvents = 0;
  }
  uint64_t confirmed_bytes() const { return confirmedBytes_.load(std::memory_order_relaxed); }
  uint32_t rate_now() const { return rateNow_.load(std::memory_order_relaxed); }
  BulkPacer::Stats PacerStats() const { return pacer_.GetStats(); }

 private:
  void ServeLoop();
  void OnTransmitted(const uint8_t* data, size_t len, uint64_t nowUs, bool resend);

  SendFn send_;
  PingRttFn pingRtt_;
  YieldFn yield_;
  uint32_t mtu_ = 1200;
  BulkRateConfig rateConfig_;
  uint32_t chunkBytes_ = 16u * 1024u;

  UdpControlChannel bulk_;
  BulkPacer pacer_;
  std::thread serveThread_;
  std::atomic<bool> serving_{false};
  std::atomic<uint32_t> rateNow_{0};
  std::atomic<uint32_t> budgetBps_{kBudgetUnknown};  // 0 is a real budget: send nothing (④)
  std::atomic<uint64_t> confirmedBytes_{0};
  uint32_t txStreamId_ = 0;
  BulkUplinkSource* source_ = nullptr;

  mutable std::mutex countersMu_;
  Counters counters_;

  // Rate evidence (serving thread + pacer callback).
  std::mutex evMu_;
  std::map<uint32_t, uint64_t> seqToKey_;      // bulk message seq -> chunk key
  std::map<uint64_t, uint64_t> lastTxUs_;      // chunk key -> its last datagram's first send
  std::set<uint64_t> tainted_;                 // chunks with a resend: no RTT sample (Karn)
  // Messages with at least one resend since the last evaluation: the loss evidence counts EVENTS, not
  // datagrams. One lost acknowledgement makes the channel resend a whole message -- ~15 datagrams --
  // and counting each of them as a loss read one lost ack as 15 % loss (measured: 40 ms / 1 % path).
  std::set<uint32_t> resentSeqsInWindow_;
  // The loss ratio's parts (② / 3rd agreement ①), since the last evaluation: see BulkLossCounter.
  BulkLossCounter loss_;
  // Chunks the host has already confirmed (a pull named them as its trigger). A timer resend of one of
  // these is a lost ACK, not lost progress: neither an RTO nor loss (the resends are charged to the
  // pacer's budget all the same).
  std::set<uint64_t> confirmedKeys_;
  // Diagnostics (REMOTE60_CLIP_BULK_TRACE=2): each chunk's timeline, QPC microseconds.
  struct ChunkTimes {
    uint64_t pullAt = 0, enqAt = 0, firstTx = 0, lastTx = 0, lastAnyTx = 0;
    bool resent = false;
  };
  std::map<uint64_t, ChunkTimes> chunkTimes_;
};

}  // namespace remote60::native_poc
