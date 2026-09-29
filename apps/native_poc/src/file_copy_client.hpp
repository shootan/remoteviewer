#pragma once

// File copy, viewer side. Step 1 (t-zdmsd4gb r1): P->R -- files copied on this PC, pasted on the
// remote PC. (agreement: file_copy_network_debate_2026-09-28.md "Codex 반론/합의" + "검증용 결론")
//
// Role:    FileCopyClient -- the UI thread hands over what CF_HDROP names (SubmitLocalFiles); this
//          identifies each file AS THE USER (file_copy_local_files: only plain local files, never a
//          folder / shortcut / reparse point; FileId, size, time), and on the control thread's idle
//          turns offers the names (65/66), asks every 700 ms whether a paste of the offer began
//          (69/70), pins the files when one did (the content at the moment the paste started) and
//          sends the confirmed descriptor (71/72). While the paste runs, the shared bulk sender
//          (bulk_uplink.hpp, the rate rules of the image path) answers the host's pulls from the
//          pinned handles, each chunk with its own SHA-256 (file_copy_wire.hpp D3).
// Lifetimes (debate "공통 상태"): an OFFER is the future -- a new copy replaces it. A PASTE that has
//          begun is pinned and runs to its end whatever is copied meanwhile; only the user, the
//          switch, the session or its lease end it.
// One bulk per session: a paste takes the session's BulkArbiter as File; while an image holds it the
//          paste is refused (Busy) -- the pre-emption rules are step 3.
// Thread:  SubmitLocalFiles / ClearLocalOffer from the UI thread; Pump from the control thread;
//          OnDatagram from the UDP receive thread; the uplink's serving thread calls OnPull. Shared
//          state under mu_; the pinned files under pinMu_ (LocalFileTable is single-threaded).
//
// Plaintext, like the rest of the media socket: names, sizes and content travel unencrypted (A3).
// Logs carry counts, sizes and outcomes -- never a path.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "bulk_arbiter.hpp"
#include "bulk_uplink.hpp"
#include "file_copy_local_files.hpp"
#include "file_copy_wire.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

constexpr uint64_t kFilePasteQueryIntervalUs = 700000;  // debate D4: only while an offer / paste lives
constexpr uint32_t kFilePinLeaseMs = 60000;               // renewed by every read (A4: progress, not time)

class FileCopyClient : private BulkUplinkSource {
 public:
  using SendFn = std::function<bool(const void* data, size_t len)>;
  using PingRttFn = std::function<uint64_t()>;
  using YieldFn = std::function<bool()>;
  using LogFn = std::function<void(const std::string&)>;

  ~FileCopyClient() { Stop(); }

  /** Once per viewer process. `arbiter` is the session's one-bulk rule (shared with the image path). */
  void Start(SendFn send, PingRttFn pingRtt, YieldFn yield, uint32_t mtuBytes, BulkRateConfig rate, BulkArbiter* arbiter,
             LogFn log = nullptr);
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

  /** The session ended: the offer and any paste are over; the pins are released. */
  void EndSession();

  struct Counters {
    uint64_t submitted = 0, filesOffered = 0, filesExcluded = 0;
    uint64_t offersSent = 0, offersAccepted = 0, offersRefused = 0;
    uint64_t pastesBegun = 0, pastesPrepared = 0, pastesBusy = 0, pastesEnded = 0, pastesFailed = 0;
    uint64_t chunksServed = 0, bytesServed = 0, pullsRefused = 0;
    uint8_t lastVerdict = 0, lastEndReason = 0, lastPasteState = 0;
  };
  Counters GetCounters() const {
    std::lock_guard<std::mutex> lock(mu_);
    return counters_;
  }
  /** Whether a paste of this viewer's files is running (pinned, bulk open). */
  bool PasteActive() const {
    std::lock_guard<std::mutex> lock(mu_);
    return paste_.active;
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
  struct PasteRun {
    bool active = false;
    uint64_t offerId = 0;
    uint64_t pasteOp = 0;
    uint64_t epochTag = 0;
    uint32_t bulkGen = 0;
    std::vector<uint64_t> sizes;  // confirmed at pin time, by file index
    uint64_t nextQueryUs = 0;
    // Chunks sent and not yet confirmed by a later pull (goodput / RTT evidence), by request id.
    std::deque<std::pair<uint64_t, uint32_t>> sent;
  };

  // BulkUplinkSource
  bool OnPull(const std::vector<uint8_t>& msg, uint64_t nowUs, std::vector<uint8_t>* out, BulkServed* served) override;
  bool ChunkKeyOf(const uint8_t* message, size_t len, uint64_t* key) override;

  // One request / answer on the control link. False = link failure.
  bool Exchange(ControlLink& link, file_copy::net::FileMsg type, const std::vector<uint8_t>& body,
                file_copy::net::FileMsg replyType, std::vector<uint8_t>* reply);
  int PumpOffer(ControlLink& link);
  int PumpPasteQuery(ControlLink& link, uint64_t offerId, bool forActivePaste);
  int PreparePaste(ControlLink& link, uint64_t offerId, uint64_t pasteOp);
  void EndPaste(file_copy::net::PasteState state, file_copy::net::PasteEndReason reason);  // caller holds mu_
  void Log(const std::string& line);

  SendFn send_;
  PingRttFn pingRtt_;
  YieldFn yield_;
  LogFn log_;
  uint32_t mtu_ = 1200;
  BulkArbiter* arbiter_ = nullptr;

  std::atomic<bool> bulkNegotiated_{false};
  std::atomic<bool> hostSupports_{false};
  std::atomic<bool> allowed_{true};
  std::atomic<bool> running_{false};

  mutable std::mutex mu_;
  OfferState offer_;
  // The offer a newer copy replaced: a paste may have begun on it just before (the host names the
  // offer a paste belongs to), and its files are still the ones to pin then.
  OfferState retired_;
  uint64_t withdrawOfferId_ = 0;  // ClearLocalOffer: an End(offer) to send on the next turn
  PasteRun paste_;
  bool closeUplinkPending_ = false;
  uint32_t nextSeq_ = 0;
  Counters counters_;

  std::mutex pinMu_;
  file_copy::LocalFileTable pins_;

  BulkUplink uplink_;
};

}  // namespace remote60::native_poc
