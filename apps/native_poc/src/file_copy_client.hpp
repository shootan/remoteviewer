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
  /** The viewer's own switch (A2: file_copy_allowed on this side). */
  void SetAllowed(bool v) { allowed_.store(v, std::memory_order_release); }
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
    uint8_t lastRecvVerdict = 0, lastRecvEndReason = 0;
  };
  Counters GetCounters() const;
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
  };
  struct PasteKey {
    uint64_t offerId = 0;
    uint64_t pasteOp = 0;
    file_copy::net::PasteEndReason reason = file_copy::net::PasteEndReason::None;
  };

  // One request / answer on the control link. False = link failure.
  bool Exchange(ControlLink& link, file_copy::net::FileMsg type, const std::vector<uint8_t>& body,
                file_copy::net::FileMsg replyType, std::vector<uint8_t>* reply);
  int PumpOffer(ControlLink& link);
  int PumpPasteQuery(ControlLink& link, uint64_t offerId, bool forActivePaste);
  int PreparePaste(ControlLink& link, uint64_t offerId, uint64_t pasteOp);
  void EndPaste(file_copy::net::PasteState state, file_copy::net::PasteEndReason reason);  // caller holds mu_
  int PumpOfferQuery(ControlLink& link);
  int PrepareReceive(ControlLink& link, const PasteKey& k);
  int SendReceiveEnd(ControlLink& link, const PasteKey& k);
  void OnHelperFrame(const file_copy::PipeFrame& f);
  void OnHelperGone();
  void OnHelperPasteBegin(const file_copy::PasteBegin& m);
  void OnHelperPasteEnd(const file_copy::PasteEnd& m);
  void EndReceiveLocked(file_copy::net::PasteEndReason reason);  // caller holds mu_; queues the End
  void RefuseDescriptor(uint64_t offerId, uint64_t pasteOp, file_copy::Status why);
  void Post(std::function<void()> task);  // runs on the worker (helper start / publish / clear)
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
  uint32_t nextSeq_ = 0;
  Counters counters_;

  std::mutex pinMu_;
  file_copy::LocalFileTable pins_;

  FileChunkServer server_;     // P->R
  BulkUplink uplink_;          // P->R
  FileHelperChannel helper_;   // R->P
  FilePullReceiver receiver_;  // R->P

  std::thread worker_;
  std::mutex workMu_;
  std::condition_variable workCv_;
  std::deque<std::function<void()>> work_;
  bool workerRun_ = false;
};

}  // namespace remote60::native_poc
