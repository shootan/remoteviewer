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

#include "bulk_arbiter.hpp"
#include "bulk_pacer.hpp"
#include "bulk_rate_controller.hpp"
#include "bulk_uplink.hpp"
#include "clip_image_clipboard.hpp"
#include "clip_image_transfer.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

/** The image path's bulk timing (bulk_uplink.hpp: shared with the file path). */
inline UdpControlChannel::Timings clip_bulk_timings() { return bulk_uplink_timings(); }

/** The resend interval for the image path's 16 KiB chunks (bulk_uplink.hpp bulk_retransmit_us). */
inline uint64_t clip_bulk_retransmit_us(uint32_t rateBps, uint64_t rttUs = 0) {
  return bulk_retransmit_us(rateBps, rttUs, kClipImageChunkBytes);
}

/**
 * BulkRateConfig for the viewer's uplink: the agreed values (bulk_rate_controller.hpp defaults), each
 * reachable without a rebuild for measurement -- REMOTE60_CLIP_BULK_EVAL=wall, _SS=num/den,
 * _CA_PCT, _CAP_BPS, _START_BPS, _LOSS_HIGH_PM.
 */
BulkRateConfig clip_bulk_rate_config_from_env();

enum class ClipPackageResult : uint8_t {
  Ok = 0,
  NotAnImage,
  TooLarge,
  ColorProfile,
  EncodeFailed,
  HashFailed,
  ReadFailed,  // the clipboard held an image that could not be read (busy, unreadable)
};

/**
 * How the last image copy ended, as the user is told (the transfer bar). One per copy that reached
 * the image path; `detail` says why where there is a why.
 */
enum class ClipOutcome : uint8_t {
  None = 0,
  Published,          // on the remote PC's clipboard
  Cancelled,          // stopped, and the host confirmed it; detail = ClipImageReason (User / Superseded / Session)
  CancelTooLate,      // the cancel reached the host after it had already published the image
  CancelUnconfirmed,  // the cancel went (or could not go) and the host's answer never came
  HostSuperseded,     // the host's clipboard changed first: nothing was published
  Failed,             // detail = ClipImageReason
  NotSent,            // never left this PC; detail = ClipPackageResult
  Refused,            // the host did not take it; detail = ClipImageVerdict
};
constexpr uint64_t kClipCancelConfirmBudgetUs = 15000000;  // how long a cancel's outcome is waited for

struct ClipPackage {
  std::shared_ptr<const std::vector<uint8_t>> bytes;  // [PNG][UTF-16LE text]
  ClipImageOffer offer;                               // transferId left 0 (the client assigns)
  std::u16string text;                                // kept for the text v1 fallback
};

/** Snapshot -> package (WIC encode for a DIB, gate, text, SHA-256). Needs COM on the thread. */
ClipPackageResult clip_build_package(const ClipSnapshot& snap, ClipPackage* out);

class ClipImageClient : private BulkUplinkSource {
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

  /**
   * The session's one-bulk rule, shared with the file path (bulk_arbiter.hpp). Null = no other bulk
   * user. An image is offered only once it holds the bulk as Image; while a file paste holds it the
   * newest image waits (only the latest copy matters).
   */
  void SetBulkArbiter(BulkArbiter* a) { arbiter_ = a; }

  /** HelloAck carried kUdpFeatureBulkChannel (this viewer asked). Per connection. */
  void SetBulkNegotiated(bool v) { bulkNegotiated_.store(v, std::memory_order_release); }
  /**
   * The same-direction (viewer uplink) wire budget less media, FEC, retransmission and control, in
   * bits/s; 0 = send nothing. Unknown (ClearUplinkBudget, the default) means the configured ceiling alone
   * (16 Mbps) with congestion deciding -- the video the host sends the OTHER way is never subtracted.
   * A shrink applies at once.
   */
  void SetUplinkBudgetBps(uint32_t bps) { uplink_.SetBudgetBps(bps); }
  /** Back to "no budget known" (the configured ceiling alone). */
  void ClearUplinkBudget() { uplink_.SetBudgetBps(BulkUplink::kBudgetUnknown); }
  static constexpr uint32_t kBudgetUnknown = BulkUplink::kBudgetUnknown;

  /** Pong carried kCaptureFlagClipboardImageV1. */
  void SetHostSupports(bool v) { hostSupports_.store(v, std::memory_order_release); }
  bool Usable() const {
    return bulkNegotiated_.load(std::memory_order_acquire) && hostSupports_.load(std::memory_order_acquire);
  }

  /** UI thread: a local copy that holds an image. Supersedes anything pending or running. */
  void SubmitSnapshot(ClipSnapshot snap);

  /**
   * Paste on demand (t-y4wj64jw), UI thread: the image a Ctrl+V found on this PC's clipboard, sent
   * for paste `pasteId`. The same path as SubmitSnapshot -- package, offer, transfer -- with one
   * difference: its end is recorded as that paste's outcome (TakePasteOutcome), and only a Published
   * end (the host's clipboard holds it) counts as applied. A paste still unsettled is superseded.
   */
  void SubmitSnapshotForPaste(ClipSnapshot snap, uint64_t pasteId);
  /** UI thread: the paste gave up (timeout, cancel): its transfer is stopped, nothing is recorded. */
  void AbandonPaste(uint64_t pasteId);
  struct PasteOutcome {
    uint64_t id = 0;
    bool applied = false;                     // Published: on the host's clipboard
    ClipOutcome outcome = ClipOutcome::None;  // how it ended (see ClipOutcome)
    uint8_t detail = 0;
  };
  /** Control thread: a paste's end, once. */
  bool TakePasteOutcome(PasteOutcome* out);

  /**
   * UI thread: a local copy WITHOUT an image. Whatever image is pending or running is older than
   * what the user has now, so it must not land on the host after it.
   */
  void CancelForNewerCopy();

  /**
   * UI thread: this PC's clipboard changed to an image that cannot be sent (too large, unreadable).
   * Called after CancelForNewerCopy, which already made everything older void; this only tells the
   * user that the new copy did not go.
   */
  void NoteLocalCopyNotSent(ClipPackageResult why);

  /**
   * UI thread: the user pressed Cancel on the transfer bar (3rd rate agreement ④). The running
   * transfer is cancelled on the host with reason User (the existing Cancel message), and a copy
   * still being packaged or waiting to be offered is dropped with it -- it is the same copy.
   */
  void CancelByUser();

  /**
   * File copy D5 (any thread): the user pasted files -- an explicit act, before an automatic image
   * sync. No new image is offered until AfterFilePaste, and a running one is asked to stop (the
   * existing Cancel, reason Superseded on the wire). The bulk is free only once the host has
   * CONFIRMED the image's end (the arbiter is released at that point, as for any cancel) -- the file
   * waits for that, never for the request alone. True when a running image was asked to stop.
   * The stopped image is kept (shared bytes) as the one candidate to resume.
   */
  bool PreemptForFilePaste();
  /**
   * The paste is over (either way). The kept image is offered again -- a new transfer id, from the
   * start -- only when `mayResume` (the caller: switch on, same session, the remote clipboard not
   * changed since), the host CONFIRMED it cancelled (never an image that was already published), no
   * newer copy exists here, and it is under a minute old. Otherwise it is dropped.
   */
  void AfterFilePaste(bool mayResume);

  /** What the transfer bar shows: one consistent snapshot. */
  struct Progress {
    bool active = false;          // an offer is out or its chunks are being served
    uint64_t bytesTotal = 0;      // the package (PNG + text)
    uint64_t bytesConfirmed = 0;  // chunks the host has confirmed (never counts a resend twice)
    uint64_t elapsedMs = 0;       // since the offer; for a finished one, how long it took
    bool cancelling = false;      // stopped here; the host's answer to the cancel is awaited
    uint8_t cancellingWhy = 0;    // ClipImageReason of that cancel (User / Superseded)
    uint64_t finished = 0;        // outcomes so far: a change means a new one below
    ClipOutcome outcome = ClipOutcome::None;  // the last one
    uint8_t detail = 0;                       // its why (see ClipOutcome)
    bool heldForFile = false;                 // D5: an image waits (or is being stopped) for a file paste
  };
  Progress GetProgress() const;

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
    Counters c;
    {
      std::lock_guard<std::mutex> lock(mu_);
      c = counters_;
    }
    // The sender's own counts live with the shared uplink (bulk_uplink.hpp).
    const BulkUplink::Counters u = uplink_.GetCounters();
    c.pullsServed = u.pullsServed;
    c.pullsDropped = u.pullsDropped;
    c.lastRateBps = u.lastRateBps;
    c.srttUs = u.srttUs;
    c.raises = u.raises;
    c.lowers = u.lowers;
    c.pauses = u.pauses;
    c.recoveryHolds = u.recoveryHolds;
    c.evaluations = u.evaluations;
    c.lossEvents = u.lossEvents;
    c.channelFragmentRetransmits = u.channelFragmentRetransmits;
    c.rtoEvents = u.rtoEvents;
    return c;
  }
  bool Active() const {
    std::lock_guard<std::mutex> lock(mu_);
    return sender_.Active();
  }
  BulkPacer::Stats PacerStats() const { return uplink_.PacerStats(); }

 private:
  void PackageWorker();
  void OpenBulk();    // caller holds mu_
  void CloseBulk();   // NOT under mu_ (joins the serving thread)
  // BulkUplinkSource: the image package answers the host's pulls; a chunk's key is its offset.
  bool OnPull(const std::vector<uint8_t>& msg, uint64_t nowUs, std::vector<uint8_t>* out, BulkServed* served) override;
  bool ChunkKeyOf(const uint8_t* message, size_t len, uint64_t* key) override;
  void Log(const std::string& line);
  void EndActive(ClipImageState finalState, ClipImageReason why);  // caller holds mu_; bulk closed after
  void RecordOutcome(ClipOutcome o, uint8_t detail);  // caller holds mu_
  // caller holds mu_: the copy `gen` ended; when it is the paste's, that is the paste's outcome
  void SettlePaste(uint64_t gen, bool applied, ClipOutcome o, uint8_t detail);
  void SubmitSnapshotLocked(ClipSnapshot snap);  // caller holds mu_
  // A cancel's answer (or a later Status) for the awaited transfer. Non-terminal: keep waiting.
  void ApplyCancelAnswer(const ControlClipImageStatusReplyMessage& r);  // caller holds mu_
  void SettleAwaiting(ClipOutcome o, uint8_t detail);  // caller holds mu_: the cancel's outcome, then any deferred notice
  // Caller holds mu_. A copy that never left (read refused / package failed): shown at once, or --
  // while an older transfer's cancel is unsettled -- held with its copy's generation and shown after
  // it, only if that copy is still the newest (P2).
  void NoteNotSent(ClipPackageResult why, uint64_t gen);
  void FlushDeferredNotSent();  // caller holds mu_
  bool CancelPending() const {  // caller holds mu_
    return awaiting_.on || (cancelTargetId_ != 0 && sender_.Active() && sender_.transferId() == cancelTargetId_);
  }
  void RequestCancel(ClipImageReason why);  // caller holds mu_: cancel the running transfer, bound to its id
  void ClearCancelFor(uint64_t transferId);  // caller holds mu_: that transfer ended -- its cancel goes with it

  SendFn send_;
  PingRttFn pingRtt_;
  YieldFn yield_;
  LogFn log_;
  uint32_t mtu_ = 1200;
  BulkRateConfig rateConfig_;
  BulkArbiter* arbiter_ = nullptr;

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
  uint64_t fallbackGen_ = 0;  // the copy the fallback text belongs to: stale once a newer one exists
  uint64_t offerGen_ = 0;     // the copy the transfer on offer belongs to

  ClipImageSender sender_;
  uint64_t nextStatusUs_ = 0;
  uint32_t nextSeq_ = 0;
  bool bulkClosePending_ = false;
  // The transfer a cancel was asked for (0 = none). Bound to its id, not a flag: a cancel meant for
  // transfer A must never outlive A -- refused, ended by the host, or the session -- and cancel the
  // next copy's transfer B (Codex review of da24d9f, P1).
  uint64_t cancelTargetId_ = 0;
  ClipImageReason cancelReason_ = ClipImageReason::Superseded;  // User when the bar's Cancel asked
  // D5: a file paste has the bulk's priority; the image it stopped, kept to resume under conditions.
  bool fileHold_ = false;
  ClipPackage lastOffered_;          // the package now on offer (shared bytes)
  bool havePreempted_ = false;
  ClipPackage preempted_;
  uint64_t preemptedGen_ = 0, preemptedAtUs_ = 0, preemptedId_ = 0;
  bool preemptedConfirmedCancelled_ = false;
  uint64_t lastBytesTotal_ = 0;                // the last finished transfer's size (under mu_)
  // A transfer stopped here whose cancel the host has not settled yet. Until it has, nothing new is
  // offered (the host would answer Busy and the newest copy would be lost) and only replies naming
  // THIS transfer id count.
  struct Awaiting {
    bool on = false;
    uint64_t transferId = 0;
    uint64_t epochTag = 0;
    ClipImageReason why = ClipImageReason::Superseded;
    uint64_t nextStatusUs = 0;
    uint64_t deadlineUs = 0;
  };
  Awaiting awaiting_;
  uint64_t outcomes_ = 0;
  // "The new copy did not go" while the older transfer's cancel is still being settled: shown after
  // that settles, so the user's last line is about the copy they just made.
  bool deferredNotSent_ = false;
  uint8_t deferredNotSentWhy_ = 0;
  uint64_t deferredNotSentGen_ = 0;
  ClipOutcome lastOutcome_ = ClipOutcome::None;
  uint8_t lastDetail_ = 0;
  Counters counters_;
  // Paste on demand: the paste the copy `pasteGen_` was submitted for (0 = none), and its end.
  uint64_t pasteId_ = 0;
  uint64_t pasteGen_ = 0;
  bool havePasteOutcome_ = false;
  PasteOutcome pasteOutcome_;

  // The bulk sender (while a transfer is accepted): shared code with the file path.
  BulkUplink uplink_;
};

}  // namespace remote60::native_poc
