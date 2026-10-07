#pragma once

// File copy, viewer side (t-zdmsd4gb r1). Both directions; the pieces are file_copy_paste_parts.hpp.
// (agreement: file_copy_network_debate_2026-09-28.md "Codex 반론/합의" + "검증용 결론")
//
// P->R (step 1) -- files copied on this PC, pasted on the remote PC: the UI thread hands over what
//          CF_HDROP names (SubmitLocalFiles); this identifies each file AS THE USER
//          (file_copy_local_files: only plain local files, never a folder / shortcut / reparse point;
//          FileId, size, time), and on the control thread's idle turns offers the names (65/66), asks
//          every 700 ms whether a paste of the offer began (69/70), pins the files when one did (the
//          content at the moment the paste started) and sends the confirmed descriptor (71/72). While
//          the paste runs, the shared bulk sender (bulk_uplink.hpp, FileChunkServer) answers the
//          host's pulls from the pinned handles, each chunk with its own SHA-256.
// R->P (step 2) -- files copied on the remote PC, pasted on this PC: every 700 ms while the session is
//          negotiated and allowed, the control thread asks whether the remote clipboard names files
//          (67/68 -- a small "unchanged" answer until it changes, the list once). A new list is put
//          on THIS PC's clipboard by the same Medium helper the host uses (started here as this
//          process's own user -- the viewer is not elevated): an async data object, nothing
//          downloaded yet. When a consumer pastes, the host pins its files (71, R->P) and each helper
//          Read becomes pulls answered by the host (FilePullReceiver: every chunk checked --
//          identity, length, SHA-256 -- before a byte reaches the helper). The helper's PasteEnd
//          ends it here and is told to the host (73).
// Lifetimes (debate "공통 상태"): an OFFER is the future -- a new copy replaces it. A PASTE that has
//          begun runs to its end whatever is copied meanwhile; only the user, the switch, the session
//          or its lease end it.
// One bulk per session: a paste of either direction takes the session's BulkArbiter as File; while
//          an image holds it the paste is refused (Busy) -- the pre-emption rules are step 3.
// Thread:  SubmitLocalFiles / ClearLocalOffer from the UI thread; Pump from the control thread;
//          OnDatagram from the UDP receive thread; the uplink's serving thread, the receiver's thread,
//          the helper's reader and a worker for helper starts. Shared state under mu_; the pinned
//          files under pinMu_ (LocalFileTable is single-threaded).
//
// Plaintext, like the rest of the media socket: names, sizes and content travel unencrypted (A3).
// Logs carry counts, sizes and outcomes -- never a path.

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
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bulk_arbiter.hpp"
#include "bulk_uplink.hpp"
#include "file_copy_local_files.hpp"
#include "file_copy_paste_parts.hpp"
#include "file_copy_wire.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

constexpr uint64_t kFilePasteQueryIntervalUs = 700000;  // debate D4: P->R only while an offer / paste lives
constexpr uint64_t kFileOfferQueryIntervalUs = 700000;  // debate D4: R->P while negotiated and allowed
constexpr uint32_t kFilePinLeaseMs = 60000;               // renewed by every read (A4: progress, not time)
// D5: how long a paste may wait for the session's bulk (an image being stopped for it) before it is
// refused -- inside the consumer's 10 s descriptor wait, less the 700 ms query and the prepare.
constexpr uint64_t kFileBulkSwitchBudgetUs = 7000000;

class FileCopyClient {
 public:
  using SendFn = std::function<bool(const void* data, size_t len)>;
  using PingRttFn = std::function<uint64_t()>;
  using YieldFn = std::function<bool()>;
  using LogFn = std::function<void(const std::string&)>;
  using HelperLauncher = FileHelperChannel::Launcher;

  ~FileCopyClient() { Stop(); }

  /** Once per viewer process. `arbiter` is the session's one-bulk rule (shared with the image path). */
  void Start(SendFn send, PingRttFn pingRtt, YieldFn yield, uint32_t mtuBytes, BulkRateConfig rate, BulkArbiter* arbiter,
             LogFn log = nullptr);
  /**
   * Before Start: how this PC's helper is started for R->P (the product: launch_file_copy_helper_as_self
   * with the helper beside the viewer; a test: its private window station). None = no R->P here.
   */
  void SetHelperLauncher(HelperLauncher launcher) { launcher_ = std::move(launcher); }
  void Stop();

  /** HelloAck carried kUdpFeatureBulkChannel. */
  void SetBulkNegotiated(bool v) { bulkNegotiated_.store(v, std::memory_order_release); }
  /** Pong carried kCaptureFlagFileCopyV1. */
  void SetHostSupports(bool v) { hostSupports_.store(v, std::memory_order_release); }
  /** Pong carried kCaptureFlagFileOfferCopyGenV1: offers are asked with 86 and say their copy (r7). */
  void SetHostOrdersOffers(bool v) { hostOrdersOffers_.store(v, std::memory_order_release); }
  /**
   * Paste on demand r7 (D2): a remote copy of files is not published here at once. It goes to the
   * gate (the viewer's UI thread, which records this PC's copies), with the copy generation of the
   * host's copy if the host says it; the gate answers with PublishApproved and this PC's clipboard
   * revision it decided on, and the helper publishes only over that revision. No gate set: published
   * at once, as before (a client without a viewer window: the file-copy tests).
   */
  struct RemoteFilesForGate {
    file_copy::PublishRemoteFiles pub;
    bool hasCopyGen = false;  // an older host: no order
    uint64_t copyGen = 0;
  };
  void SetPublishGate(std::function<void(RemoteFilesForGate)> gate);
  void PublishApproved(file_copy::PublishRemoteFiles pub, uint32_t expectSeq);
  /**
   * The viewer's own switch (A2: file_copy_allowed on this side). Off while something runs: the
   * running paste either way ends (Disabled) -- pins released, the sender and the receiver closed,
   * the host told -- the offers are withdrawn and the helper goes, bounded, on the caller's thread;
   * after it no new byte of a file is read or sent (r2 ④).
   * NOT A PRODUCT SWITCH YET: the product has no run-time off switch. File copy is turned off only
   * at process start, from the environment once (native_video_host_main.cpp:206,249 /
   * viewer_clip_image_wiring.cpp:39). The mid-session off path (SetAllowed(false) after start) is a part exercised by tests;
   * a settings switch that calls it is a follow-up after the field-test release (t-zdmsd4gb r3).
   */
  void SetAllowed(bool v);
  bool Usable() const {
    return bulkNegotiated_.load(std::memory_order_acquire) && hostSupports_.load(std::memory_order_acquire) &&
           allowed_.load(std::memory_order_acquire);
  }

  /**
   * UI thread: this PC's clipboard now names these paths (CF_HDROP) at clipboard `revision`. The
   * files are identified here, as the user; what is not a plain local file is left out and counted.
   * Replaces any offer not yet pasted; a paste already running is untouched.
   */
  void SubmitLocalFiles(const std::vector<std::wstring>& paths, uint64_t revision);
  /** UI thread: the clipboard no longer names files -- the offer is withdrawn (not a running paste). */
  void ClearLocalOffer();

  /**
   * Paste on demand (t-y4wj64jw), UI thread: the files a Ctrl+V found on this PC's clipboard, offered
   * for paste `pasteId` -- always a new offer, even for the same files, because the remote clipboard
   * may have changed since the last one (a copy there, a helper restart). Its OfferReply is the
   * paste's outcome: Accept means the host's helper put the files on its clipboard. A copy that
   * cannot be offered at all (too many, nothing readable) is the outcome at once.
   */
  void SubmitLocalFilesForPaste(const std::vector<std::wstring>& paths, uint64_t revision, uint64_t pasteId);
  /** UI thread: the paste gave up; a later answer is not recorded. */
  void AbandonPaste(uint64_t pasteId);
  struct PasteOutcome {
    uint64_t id = 0;
    bool applied = false;  // OfferReply Accept
    uint8_t verdict = 0;   // file_copy::net::Verdict
  };
  /** Control thread: a paste's outcome, once. */
  bool TakePasteOutcome(PasteOutcome* out);

  /** Control thread, idle turn. 1 = exchanged, 0 = nothing to do, -1 = link failure. */
  int Pump(ControlLink& link);

  /** Receive thread: a file-paste bulk datagram (the router decided). True when it was one. */
  bool OnDatagram(const void* data, size_t len);

  /** The session ended: offers and any paste are over; the pins are released; the helper goes. */
  void EndSession();

  struct Counters {
    uint64_t submitted = 0, filesOffered = 0, filesExcluded = 0;
    uint64_t offersSent = 0, offersAccepted = 0, offersRefused = 0;
    uint64_t pastesBegun = 0, pastesPrepared = 0, pastesBusy = 0, pastesEnded = 0, pastesFailed = 0;
    uint64_t chunksServed = 0, bytesServed = 0, pullsRefused = 0;
    uint8_t lastVerdict = 0, lastEndReason = 0, lastPasteState = 0;
    // R->P
    uint64_t offerQueries = 0, remoteOffersSeen = 0, remoteOffersRefused = 0, remotePublished = 0, remoteCleared = 0;
    uint64_t recvBegun = 0, recvPrepared = 0, recvBusy = 0, recvRefused = 0, recvEnded = 0, recvFailed = 0;
    uint64_t chunksVerified = 0, chunksRejected = 0, bytesReceived = 0, filesWholeVerified = 0, filesChunkVerified = 0;
    uint64_t helperLaunches = 0, helperLaunchFailures = 0;
    uint64_t recvVerificationEnds = 0;  // R->P pastes ended by a failed chunk check
    uint8_t lastRecvVerdict = 0, lastRecvEndReason = 0;
    uint64_t staleHelperFrames = 0, staleHelperGones = 0;  // of a helper since replaced, not taken (r8)
    uint64_t helperSendsDropped = 0;  // frames meant for a helper since replaced, sent to nobody (r9)
    uint64_t helperSendsFailed = 0;  // frames for the current helper whose pipe was already closed (r9)
  };
  Counters GetCounters() const;
  /**
   * TEST ONLY -- called when a helper frame (point 1) or a helper's "gone" (point 2) arrives, with
   * the instance it came from, before it is looked at; and before a refusal descriptor (3), an
   * accepted descriptor (4) or a Read answer (5) is sent, with the instance it is for; and on the
   * worker, before a publish looks for / starts a helper (6, instance 0). No product
   * code calls this; the build gate checks the shipped viewer does not carry it.
   */
  void SetHelperProbeForTest(std::function<void(uint64_t instance, int point)> probe);
  /**
   * Paste on demand r5 (F3): the process id of this PC's clipboard helper now running (0 = none). A
   * clipboard owned by it holds the REMOTE PC's files (R->P) -- the only virtual files that are a
   * remote copy; the same formats from any other program are a copy made here.
   */
  DWORD HelperPid() const { return helper_.CurrentPid(); }
  /** Whether a paste of this viewer's files is running (pinned, bulk open). */
  bool PasteActive() const {
    std::lock_guard<std::mutex> lock(mu_);
    return paste_.active;
  }
  /** Whether a paste of the remote PC's files is running here. */
  bool ReceiveActive() const {
    std::lock_guard<std::mutex> lock(mu_);
    return recv_.active;
  }
  BulkUplink::Counters UplinkCounters() const { return uplink_.GetCounters(); }

  /**
   * What the transfer bar shows (D6), from the UI thread. It keeps apart what is known: an offer
   * published there / here (a paste is POSSIBLE), bytes moving (sending / receiving), a cancel asked
   * and not yet confirmed, and how the last paste ended -- "Completed" only when the consumer ended
   * it successfully (EndOperation S_OK), never merely because the bytes arrived.
   */
  struct Progress {
    bool sending = false;     // P->R paste running (bytes served to the remote PC)
    bool receiving = false;   // R->P paste running (bytes received here)
    bool cancelling = false;  // the user cancelled; the other side has not confirmed yet
    uint32_t files = 0;
    uint64_t bytesDone = 0, bytesTotal = 0, elapsedMs = 0;
    // The last paste's end (either direction), counted so a result that began and ended between two
    // polls is still shown once.
    uint64_t finished = 0;
    bool lastToRemote = false;
    uint8_t lastState = 0;    // file_copy::net::PasteState
    uint8_t lastReason = 0;   // file_copy::net::PasteEndReason (None when refused before any byte)
    uint16_t lastRefused = 0; // file_copy::Status when refused before any byte (0 = not refused)
    uint32_t lastFiles = 0;
    uint64_t lastBytes = 0, lastElapsedMs = 0;
    // Offers: this PC's files published on the remote PC / the remote PC's published here.
    uint64_t offered = 0, available = 0;
    uint32_t offeredFiles = 0, availableFiles = 0;
    // The clipboard helper could not run -- counted so the bar says so once (r3: the first update to
    // a release with the helper does not install it; without a word the user sees a broken feature).
    uint64_t noHelper = 0;
    bool noHelperHere = false;  // on this PC (R->P publish), else on the remote PC (P->R offer refused)
    // On this PC, and its helper executable was found missing (an install / update problem). The
    // remote PC's reason is not on the wire (verdict 5 says only "unavailable"): never claimed there.
    bool noHelperHereMissing = false;
  };
  Progress GetProgress() const;
  /** UI thread: the user cancels the running paste (either direction). Confirmed by the other side. */
  void CancelPaste();

  /**
   * D5, before Start: how a paste stops a running image (`preempt`: true when one was asked to stop)
   * and tells it the paste is over (`after(mayResume)`). A paste finding the bulk held waits for it,
   * bounded by kFileBulkSwitchBudgetUs; the image's own cancel confirmation frees it.
   */
  void SetImagePreemption(std::function<bool()> preempt, std::function<void(bool mayResume)> after) {
    preempt_ = std::move(preempt);
    after_ = std::move(after);
  }

 private:
  struct LocalFile {
    std::wstring path;  // never logged
    file_copy::FileId id;
    uint64_t size = 0;
    uint64_t mtime = 0;
    uint32_t attributes = 0;
  };
  struct OfferState {
    bool pending = false;   // built, not yet offered
    bool live = false;      // the host accepted it
    uint64_t offerId = 0;
    uint64_t revision = 0;
    std::vector<LocalFile> files;
    std::vector<file_copy::net::OfferItem> items;
    uint64_t nextQueryUs = 0;
  };
  struct PasteRun {  // P->R
    bool active = false;
    uint64_t offerId = 0;
    uint64_t pasteOp = 0;
    uint64_t nextQueryUs = 0;
    uint32_t files = 0;
    uint64_t bytesTotal = 0, startUs = 0, servedAtStart = 0;
  };
  struct RemoteOffer {  // R->P: what the remote clipboard names
    uint64_t revision = 0;  // the host's; 0 = nothing known
    uint64_t offerId = 0;   // 0 = no files
    std::vector<file_copy::net::OfferItem> items;
  };
  struct RecvRun {  // R->P
    bool active = false;
    uint64_t offerId = 0;
    uint64_t pasteOp = 0;
    uint64_t instance = 0;  // the helper whose paste this is (r9)
    uint32_t files = 0;
    uint64_t bytesTotal = 0, startUs = 0;
  };
  // caller holds mu_: the last paste's end, for the bar
  void RecordResultLocked(bool toRemote, file_copy::net::PasteState state, file_copy::net::PasteEndReason reason,
                          uint16_t refused, uint32_t files, uint64_t bytes, uint64_t startUs);
  struct PasteKey {
    uint64_t offerId = 0;
    uint64_t pasteOp = 0;
    file_copy::net::PasteEndReason reason = file_copy::net::PasteEndReason::None;
    uint64_t instance = 0;  // the helper this paste belongs to (r9): its descriptor goes there, or nowhere
  };

  // One request / answer on the control link. False = link failure.
  bool Exchange(ControlLink& link, file_copy::net::FileMsg type, const std::vector<uint8_t>& body,
                file_copy::net::FileMsg replyType, std::vector<uint8_t>* reply);
  int PumpOffer(ControlLink& link);
  int PumpPasteQuery(ControlLink& link, uint64_t offerId, bool forActivePaste);
  int PreparePaste(ControlLink& link, uint64_t offerId, uint64_t pasteOp);
  void EndPaste(file_copy::net::PasteState state, file_copy::net::PasteEndReason reason);  // caller holds mu_
  int PumpOfferQuery(ControlLink& link);
  int PrepareReceive(ControlLink& link, const PasteKey& k, bool mayWait = true);
  int SendReceiveEnd(ControlLink& link, const PasteKey& k);
  // `instance`: which helper spoke (FileHelperChannel::instance()); taken only if it is the current one
  void OnHelperFrame(uint64_t instance, const file_copy::PipeFrame& f);
  void OnHelperGone(uint64_t instance);
  void OnHelperPasteBegin(uint64_t instance, const file_copy::PasteBegin& m);
  void OnHelperPasteEnd(uint64_t instance, const file_copy::PasteEnd& m);
  bool HelperCurrentLocked(uint64_t instance, bool gone);  // caller holds mu_
  void EndReceiveLocked(file_copy::net::PasteEndReason reason);  // caller holds mu_; queues the End
  void RefuseDescriptor(uint64_t instance, uint64_t offerId, uint64_t pasteOp, file_copy::Status why);
  void Post(std::function<void()> task);  // runs on the worker (helper start / publish / clear)
  void PostPublish(file_copy::PublishRemoteFiles pub);
  void WorkerLoop();
  bool R2PEnabled() const { return launcher_ != nullptr; }
  void Log(const std::string& line);

  SendFn send_;
  PingRttFn pingRtt_;
  YieldFn yield_;
  LogFn log_;
  HelperLauncher launcher_;
  uint32_t mtu_ = 1200;
  BulkArbiter* arbiter_ = nullptr;

  std::atomic<bool> bulkNegotiated_{false};
  std::atomic<bool> hostSupports_{false};
  std::atomic<bool> hostOrdersOffers_{false};
  std::function<void(RemoteFilesForGate)> publishGate_;  // under mu_
  std::atomic<bool> allowed_{true};
  std::atomic<bool> running_{false};

  mutable std::mutex mu_;
  // P->R
  OfferState offer_;
  // The offer a newer copy replaced: a paste may have begun on it just before (the host names the
  // offer a paste belongs to), and its files are still the ones to pin then.
  OfferState retired_;
  uint64_t withdrawOfferId_ = 0;  // ClearLocalOffer: an End(offer) to send on the next turn
  PasteRun paste_;
  bool closeUplinkPending_ = false;
  // R->P
  RemoteOffer remote_, remoteRetired_;
  uint64_t publishedOfferId_ = 0;  // on this PC's clipboard through the helper (0 = none)
  uint64_t nextOfferQueryUs_ = 0;
  // The first answer of a session describes the remote clipboard as it was before this connection:
  // it is recorded, not published -- connecting never replaces this PC's clipboard (the text sync's
  // rule, clipboard_monitor.hpp). Only a copy made on the remote PC while connected is published.
  bool remoteBaselineTaken_ = false;
  RecvRun recv_;
  std::deque<PasteKey> prepareQueue_;  // helper PasteBegin -> prepare on the control thread
  std::deque<PasteKey> endQueue_;      // helper PasteEnd -> End on the control thread
  uint64_t preparingOp_ = 0;           // the R->P prepare in flight on the control thread
  uint64_t preparingInstance_ = 0;     // ...and the helper it is for (r10)
  bool preparingDead_ = false;         // ...whose "gone" came while it was in flight (r10)
  uint64_t lastGoneInstance_ = 0;      // the highest helper instance whose "gone" was seen (r10)
  bool cancelPending_ = false;         // the user's cancel, sent, not yet answered
  // D5: a paste waiting for the bulk (P->R or R->P), and whether an image was stopped for it.
  struct WaitBulk {
    bool on = false;
    bool toRemote = false;
    PasteKey key;
    uint64_t deadlineUs = 0;
  } waitBulk_;
  bool preemptActive_ = false;
  uint64_t revisionAtPreempt_ = 0;
  std::function<bool()> preempt_;
  std::function<void(bool)> after_;
  // caller holds mu_: a paste found the bulk taken -- start waiting (and stop an image) or keep waiting
  void WaitForBulkLocked(bool toRemote, const PasteKey& k);
  void ReleasePreemptLocked();  // caller holds mu_: the paste is over -- the stopped image may resume
  PasteKey cancelKey_;
  Progress result_;                    // the "last" / offer fields of GetProgress
  // Paste on demand: the paste the offer `pasteOfferId_` was made for (0 = none), and its outcome.
  uint64_t pasteId_ = 0;
  uint64_t pasteOfferId_ = 0;
  bool havePasteOutcome_ = false;
  PasteOutcome pasteOutcome_;
  void SettlePasteLocked(bool applied, uint8_t verdict);  // caller holds mu_
  uint64_t preparingOffer_ = 0;
  uint32_t nextSeq_ = 0;
  Counters counters_;

  std::mutex pinMu_;
  file_copy::LocalFileTable pins_;

  FileChunkServer server_;     // P->R
  BulkUplink uplink_;          // P->R
  FileHelperChannel helper_;   // R->P
  std::function<void(uint64_t, int)> helperProbe_;  // test only (SetHelperProbeForTest)
  FilePullReceiver receiver_;  // R->P

  std::thread worker_;
  std::mutex workMu_;
  std::condition_variable workCv_;
  std::deque<std::function<void()>> work_;
  bool workerRun_ = false;
};

}  // namespace remote60::native_poc
