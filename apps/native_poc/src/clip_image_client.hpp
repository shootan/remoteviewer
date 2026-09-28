#pragma once

// Clipboard image v1, viewer side of direction A (viewer -> host), plan r1 + r2.
//
// Role:    ClipImageClient -- takes a clipboard snapshot the UI thread copied, turns it into the
//          package on its own worker (DIB -> PNG through WIC, same-copy text appended, SHA-256), and
//          then, on the control thread's idle turns, offers it (55/56), cancels a superseded one first
//          (57/58), and polls the host's verdict every 500 ms (61/62) -- the host cannot push. While
//          a transfer is accepted it owns the bulk stream: a second UdpControlChannel whose datagrams
//          all leave through the BulkPacer at the rate BulkRateController allows, answering the
//          host's pulls from a serving thread.
// Thread:  SubmitSnapshot from the UI thread; Pump from the control thread (it owns the control
//          link); OnDatagram from the UDP receive thread; its own package worker, serving thread
//          and pacer thread. Shared state under mu_.
// Callers: viewer_window_proc.cpp (snapshot), viewer_control_client.cpp (Pump, the Pong bit),
//          viewer_video_receiver.cpp (demux), viewer_startup.cpp / viewer_shutdown.cpp (lifetime),
//          clip_image_e2e_test.
//
// Plaintext, like the rest of the UDP media socket (poc_protocol.hpp). Logs carry sizes, the first
// 8 hex of the digest and outcomes -- never content.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "bulk_pacer.hpp"
#include "bulk_rate_controller.hpp"
#include "clip_image_clipboard.hpp"
#include "clip_image_transfer.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

/**
 * The viewer's bulk channel timing: only the whole-message resend interval differs from the control
 * channel's (clip_bulk_retransmit_us, from the pacing rate); the NACK wait is the control channel's
 * own, unchanged by agreement.
 */
inline UdpControlChannel::Timings clip_bulk_timings() {
  UdpControlChannel::Timings t;
  t.retransmitIntervalUs = 1000000;
  t.maxAttempts = 30;
  return t;
}

/**
 * The sender's whole-message resend interval at `rateBps`: never shorter than twice the time one
 * chunk takes to leave the pacer, plus half a second. A fixed interval would, at the floor rate
 * (16 KiB = 2 s at 64 kbps), resend every chunk before it had even left -- and each resend of a
 * datagram already sent reads as loss, which lowers the rate further.
 */
inline uint64_t clip_bulk_retransmit_us(uint32_t rateBps, uint64_t rttUs = 0) {
  if (rateBps == 0) return 5000000;  // paused
  const uint64_t drainUs = static_cast<uint64_t>(kClipImageChunkBytes) * 8ull * 1000000ull / rateBps;
  // A lost acknowledgement costs this whole interval, so it is not padded more than the evidence
  // needs: two drains (the message and one queued ahead of it), four round trips, 200 ms.
  const uint64_t rtt = rttUs ? rttUs : 100000;  // unmeasured: assume a long path
  const uint64_t us = 2 * drainUs + 4 * rtt + 200000;
  return us < 300000 ? 300000 : us;
}

/**
 * BulkRateConfig for the viewer's uplink: the agreed values (bulk_rate_controller.hpp defaults), each
 * reachable without a rebuild for measurement -- REMOTE60_CLIP_BULK_EVAL=wall, _SS=num/den,
 * _CA_PCT, _CAP_BPS, _START_BPS, _LOSS_HIGH_PM.
 */
BulkRateConfig clip_bulk_rate_config_from_env();

enum class ClipPackageResult : uint8_t { Ok = 0, NotAnImage, TooLarge, ColorProfile, EncodeFailed, HashFailed };

struct ClipPackage {
  std::shared_ptr<const std::vector<uint8_t>> bytes;  // [PNG][UTF-16LE text]
  ClipImageOffer offer;                               // transferId left 0 (the client assigns)
  std::u16string text;                                // kept for the text v1 fallback
};

/** Snapshot -> package (WIC encode for a DIB, gate, text, SHA-256). Needs COM on the thread. */
ClipPackageResult clip_build_package(const ClipSnapshot& snap, ClipPackage* out);

class ClipImageClient {
 public:
  using SendFn = std::function<bool(const void* data, size_t len)>;
  using PingRttFn = std::function<uint64_t()>;   // the control channel's latest RTT, 0 if stale
  using YieldFn = std::function<bool()>;         // control has something waiting to go out
  using LogFn = std::function<void(const std::string&)>;

  ~ClipImageClient() { Stop(); }

  /** Starts the package worker; call once per viewer process. */
  void Start(SendFn send, PingRttFn pingRtt, YieldFn yield, uint32_t mtuBytes, BulkRateConfig rate,
             LogFn log = nullptr);
  void Stop();

  /** HelloAck carried kUdpFeatureBulkChannel (this viewer asked). Per connection. */
  void SetBulkNegotiated(bool v) { bulkNegotiated_.store(v, std::memory_order_release); }
  /**
   * The same-direction (viewer uplink) wire budget less media, FEC, retransmission and control, in
   * bits/s; 0 = send nothing. Unknown (ClearUplinkBudget, the default) means the configured ceiling alone
   * (16 Mbps) with congestion deciding -- the video the host sends the OTHER way is never subtracted.
   * A shrink applies at once.
   */
  void SetUplinkBudgetBps(uint32_t bps) { budgetBps_.store(bps, std::memory_order_relaxed); }
  /** Back to "no budget known" (the configured ceiling alone). */
  void ClearUplinkBudget() { budgetBps_.store(kBudgetUnknown, std::memory_order_relaxed); }
  static constexpr uint32_t kBudgetUnknown = 0xFFFFFFFFu;

  /** Pong carried kCaptureFlagClipboardImageV1. */
  void SetHostSupports(bool v) { hostSupports_.store(v, std::memory_order_release); }
  bool Usable() const {
    return bulkNegotiated_.load(std::memory_order_acquire) && hostSupports_.load(std::memory_order_acquire);
  }

  /** UI thread: a local copy that holds an image. Supersedes anything pending or running. */
  void SubmitSnapshot(ClipSnapshot snap);

  /**
   * UI thread: a local copy WITHOUT an image. Whatever image is pending or running is older than
   * what the user has now, so it must not land on the host after it.
   */
  void CancelForNewerCopy();

  /**
   * Control thread, idle turn. 1 = exchanged messages, 0 = nothing to do, -1 = link failure (the
   * stream is desynchronised, as for every other exchange on the link).
   */
  int Pump(ControlLink& link);

  /** Receive thread: a datagram off the media socket. True when it was a bulk-stream datagram. */
  bool OnDatagram(const void* data, size_t len);

  /** The session ended: drop everything, pending included. */
  void EndSession();

  /** Text of an image copy the host refused, for the caller to send by text v1 instead. */
  bool TakeFallbackText(std::u16string* out);

  struct Counters {
    uint64_t submitted = 0, packaged = 0, offered = 0, accepted = 0, refused = 0;
    uint64_t published = 0, failed = 0, superseded = 0, cancelled = 0;
    uint64_t pullsServed = 0, pullsDropped = 0;
    uint64_t lastTransferMs = 0;
    uint32_t lastRateBps = 0;
    uint64_t srttUs = 0;
    // Rate decisions this transfer (diagnostics for the report).
    uint64_t raises = 0, lowers = 0, pauses = 0, recoveryHolds = 0, evaluations = 0;
    uint64_t lossEvents = 0, channelFragmentRetransmits = 0, rtoEvents = 0;
    uint8_t lastState = 0, lastReason = 0;
  };
  Counters GetCounters() const {
    std::lock_guard<std::mutex> lock(mu_);
    return counters_;
  }
  bool Active() const {
    std::lock_guard<std::mutex> lock(mu_);
    return sender_.Active();
  }
  BulkPacer::Stats PacerStats() const { return pacer_.GetStats(); }

 private:
  void PackageWorker();
  void OpenBulk();    // caller holds mu_
  void CloseBulk();   // NOT under mu_ (joins the serving thread)
  void ServeLoop();
  void OnTransmitted(const uint8_t* data, size_t len, uint64_t nowUs, bool resend);
  void Log(const std::string& line);
  void EndActive(ClipImageState finalState, ClipImageReason why);  // caller holds mu_; bulk closed after

  SendFn send_;
  PingRttFn pingRtt_;
  YieldFn yield_;
  LogFn log_;
  uint32_t mtu_ = 1200;
  BulkRateConfig rateConfig_;

  std::atomic<bool> bulkNegotiated_{false};
  std::atomic<bool> hostSupports_{false};
  std::atomic<bool> running_{false};

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::thread packageWorker_;
  // One slot each: the newest snapshot waiting to be packaged, the newest package waiting to be
  // offered. A newer one replaces the older (only the latest copy matters).
  bool haveSnapshot_ = false;
  ClipSnapshot snapshot_;
  uint64_t snapshotGen_ = 0;
  bool havePending_ = false;
  ClipPackage pending_;
  uint64_t pendingGen_ = 0;
  std::u16string fallbackText_;
  bool haveFallback_ = false;

  ClipImageSender sender_;
  uint64_t nextStatusUs_ = 0;
  uint32_t nextSeq_ = 0;
  bool bulkClosePending_ = false;
  bool cancelActive_ = false;  // a newer non-image copy: cancel the running transfer
  Counters counters_;

  // Bulk stream (while a transfer is accepted).
  UdpControlChannel bulk_;
  BulkPacer pacer_;
  std::thread serveThread_;
  std::atomic<bool> serving_{false};
  std::atomic<uint32_t> rateNow_{0};
  std::atomic<uint32_t> budgetBps_{kBudgetUnknown};  // 0 is a real budget: send nothing (④)
  uint32_t txStreamId_ = 0;

  // Rate evidence (serving thread + pacer callback).
  std::mutex evMu_;
  std::map<uint32_t, uint32_t> seqToOffset_;   // bulk message seq -> chunk offset
  std::map<uint32_t, uint64_t> lastTxUs_;      // chunk offset -> its last datagram's first send
  std::set<uint32_t> tainted_;                 // chunks with a resend: no RTT sample (Karn)
  // Messages with at least one resend since the last evaluation: the loss evidence counts EVENTS, not
  // datagrams. One lost acknowledgement makes the channel resend a whole message -- ~15 datagrams --
  // and counting each of them as a loss read one lost ack as 15 % loss (measured: 40 ms / 1 % path).
  std::set<uint32_t> resentSeqsInWindow_;
  // The loss ratio's parts (② / 3rd agreement ①), since the last evaluation: see BulkLossCounter.
  BulkLossCounter loss_;
  // Chunks the host has already confirmed (a pull named them as its trigger). A timer resend of one of
  // these is a lost ACK, not lost progress: neither an RTO nor loss (the resends are charged to the
  // pacer's budget all the same).
  std::set<uint32_t> confirmedOffsets_;
  // Diagnostics (REMOTE60_CLIP_BULK_TRACE=2): each chunk's timeline, QPC microseconds.
  struct ChunkTimes {
    uint64_t pullAt = 0, enqAt = 0, firstTx = 0, lastTx = 0, lastAnyTx = 0;
    bool resent = false;
  };
  std::map<uint32_t, ChunkTimes> chunkTimes_;
};

}  // namespace remote60::native_poc
