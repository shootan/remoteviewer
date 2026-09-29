#pragma once

// File copy, host side. Step 1 (t-zdmsd4gb r1): P->R -- the viewer's files, pasted on this PC.
//
// Role:    HostFileCopyService -- the viewer's offer (65) goes to the Medium clipboard helper
//          (PublishRemoteFiles; the helper is started on the first offer, with backoff after a
//          failure, never as anyone but the interactive user -- no High / SYSTEM fallback). When a
//          consumer starts a paste the helper says PasteBegin; the viewer learns it by asking
//          (69/70, the host cannot push), pins its files and sends the confirmed descriptor (71),
//          which goes to the helper (PasteDescriptor). Each helper Read (<= 256 KiB) becomes pulls on
//          the bulk stream (<= 64 KiB, 77); each chunk (78) is checked against its pull -- identity,
//          length, SHA-256 -- BEFORE a byte of it reaches the helper; a failed check fails that Read
//          ("전송 데이터 검증 실패"). PasteEnd from the helper ends the paste; the viewer learns it by
//          asking.
// Allowed: one question, file_copy_allowed(), asked by every handler (A2) -- not only the Pong bit.
// One bulk: a paste takes the session's BulkArbiter as File (the image service takes it as Image).
//          Two pastes at once: the second is refused before any byte (the helper's descriptor
//          fails), never swapped in.
// Thread:  Handle* on the host's control thread; OnDatagram on the UDP receive thread; its own
//          helper-pipe reader and bulk threads. Shared state under mu_.
//
// Logs carry counts, sizes, ids and outcomes -- never a path or a name.

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
#include <string>
#include <thread>
#include <vector>

#include "bulk_arbiter.hpp"
#include "clip_image_core.hpp"
#include "file_copy_helper_host.hpp"
#include "file_copy_net_rules.hpp"
#include "file_copy_wire.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

class HostFileCopyService {
 public:
  using SendFn = std::function<bool(const void* data, size_t len)>;
  /**
   * Starts the helper and completes its handshake on `link`. The product passes
   * launch_file_copy_helper (the interactive user's linked token); a test passes the Medium launch
   * on a private window station. False = the feature is unavailable now (the service backs off).
   */
  using HelperLauncher = std::function<bool(file_copy::HelperLink* link, std::string* why)>;
  using LogFn = std::function<void(const std::string&)>;

  struct Config {
    bool enabled = false;              // the switch (settings)
    HelperLauncher launcher;           // null = unavailable
    uint32_t publishWaitMs = 5000;     // PublishResult bound
    uint32_t backoffFirstMs = 5000;    // after a failed launch, no new attempt before this ...
    uint32_t backoffMaxMs = 120000;    // ... doubling up to this
    uint32_t pullWindow = 4;           // outstanding pulls (each <= 64 KiB)
  };

  ~HostFileCopyService() { Stop(); }

  /** Once, from main(): the switch, the launcher, the session's one-bulk arbiter. */
  void Configure(Config config, BulkArbiter* arbiter, LogFn log = nullptr);
  /** When the media socket is up (each session's control start): where bulk datagrams go. */
  void StartTransport(SendFn send, uint32_t mtuBytes);
  /** Both, for a test. */
  bool Start(SendFn send, uint32_t mtuBytes, Config config, BulkArbiter* arbiter, LogFn log = nullptr) {
    Configure(std::move(config), arbiter, std::move(log));
    StartTransport(std::move(send), mtuBytes);
    return true;
  }
  void Stop();

  /** Pong 0x800: switched on and a launcher configured. (Whether the helper really starts is per offer.) */
  bool Advertised() const { return file_copy_allowed(); }
  /** The one allow rule every handler asks (A2). A view-only session would be added here. */
  bool file_copy_allowed() const { return running_.load() && config_.enabled && config_.launcher != nullptr; }

  /**
   * One control request (65/67/69/71/73/75) -> its answer. False when `type` is not one of them or
   * the body does not parse (the caller answers nothing; the link is then out of step, as for any
   * malformed request).
   */
  bool HandleControl(uint16_t type, const std::vector<uint8_t>& body, uint64_t servedEpoch, uint16_t* replyType,
                     std::vector<uint8_t>* reply);

  /** A file-paste bulk datagram (the router decided). True when it was one. */
  bool OnDatagram(const void* data, size_t len);

  /** The session ended or rolled over: the offer, any paste and the helper go. */
  void OnSessionEnd(uint64_t newEpoch);

  struct Counters {
    uint64_t offers = 0, offersAccepted = 0, offersRefused = 0, helperLaunches = 0, helperLaunchFailures = 0;
    uint64_t pastesBegun = 0, pastesBusy = 0, pastesPrepared = 0, pastesEnded = 0, pastesFailed = 0;
    uint64_t readsRequested = 0, readsServed = 0, readsFailed = 0, chunksVerified = 0, chunksRejected = 0;
    uint64_t bytesDelivered = 0, filesWholeVerified = 0, filesChunkVerified = 0;
    uint8_t lastVerdict = 0, lastEndReason = 0;
  };
  Counters GetCounters() const {
    std::lock_guard<std::mutex> lock(mu_);
    return counters_;
  }

 private:
  struct OfferRec {
    uint64_t offerId = 0;
    std::vector<file_copy::net::OfferItem> items;
  };
  struct Job {  // one helper ReadRequest
    file_copy::ReadRequest req;
    std::vector<file_copy::net::Pull> pulls;
    size_t nextPull = 0;
    size_t done = 0;
    std::vector<uint8_t> data;
    bool failed = false;
  };
  struct Paste {
    bool active = false;
    uint64_t offerId = 0;
    uint64_t pasteOp = 0;
    uint32_t bulkGen = 0;
    std::vector<uint64_t> sizes;
    std::vector<file_copy::net::CoverageTracker> coverage;
    uint64_t bytesDelivered = 0;
    file_copy::net::PasteEndReason failure = file_copy::net::PasteEndReason::None;
  };
  struct Ended {
    uint64_t offerId = 0;
    uint64_t pasteOp = 0;
    file_copy::net::PasteState state = file_copy::net::PasteState::None;
    file_copy::net::PasteEndReason reason = file_copy::net::PasteEndReason::None;
  };

  bool EnsureHelper(std::string* why);  // caller holds helperMu_
  bool SendHelper(const file_copy::PipeFrame& f);
  void ReaderLoop();
  void BulkLoop();
  void OnPasteBegin(const file_copy::PasteBegin& m);
  void OnReadRequest(const file_copy::ReadRequest& m);
  void OnPasteEnd(const file_copy::PasteEnd& m);
  void PumpPullsLocked();                        // caller holds mu_
  void FailJobsLocked(file_copy::Status why);    // caller holds mu_
  void EndPasteLocked(file_copy::net::PasteState state, file_copy::net::PasteEndReason reason);  // caller holds mu_
  const OfferRec* FindOffer(uint64_t offerId) const;  // caller holds mu_
  void Log(const std::string& line);

  std::vector<uint8_t> HandleOffer(const file_copy::net::Offer& m);
  std::vector<uint8_t> HandlePasteQuery(const file_copy::net::PasteQuery& m);
  std::vector<uint8_t> HandlePrepare(const file_copy::net::Prepare& m);
  std::vector<uint8_t> HandleEnd(const file_copy::net::End& m);
  std::vector<uint8_t> HandleStatus(const file_copy::net::StatusQuery& m);

  SendFn send_;
  uint32_t mtu_ = 1200;
  Config config_;
  BulkArbiter* arbiter_ = nullptr;
  LogFn log_;
  std::atomic<bool> running_{false};

  // The helper: launched lazily, one at a time, with backoff.
  std::mutex helperMu_;
  file_copy::HelperLink link_;
  std::mutex helperSendMu_;
  std::thread reader_;
  std::atomic<bool> readerRun_{false};
  uint64_t nextLaunchMs_ = 0;
  uint32_t backoffMs_ = 0;

  mutable std::mutex mu_;
  std::condition_variable publishCv_;
  bool publishAnswered_ = false;
  file_copy::PublishResult publishResult_;
  uint64_t epochTag_ = 0;
  uint32_t random32_ = 0;
  OfferRec offer_, retired_;
  bool haveBegun_ = false;
  uint64_t begunOffer_ = 0, begunOp_ = 0;
  Paste paste_;
  Ended lastEnded_;
  std::deque<std::unique_ptr<Job>> jobs_;
  std::map<uint64_t, std::pair<Job*, size_t>> outstanding_;  // request id -> job, pull index
  uint64_t nextRequestId_ = 1;
  // Verified chunks not yet named to the viewer; each pull names one (its rate control counts
  // goodput by them -- a completion never named is goodput the viewer never sees).
  std::deque<uint64_t> completed_;
  BulkGenAllocator gens_;
  Counters counters_;

  UdpControlChannel bulk_;
  bool bulkOpen_ = false;
  std::thread bulkThread_;
  std::condition_variable bulkCv_;
};

}  // namespace remote60::native_poc
