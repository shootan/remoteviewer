#pragma once

// File copy, host side (t-zdmsd4gb r1). Both directions; the pieces are file_copy_paste_parts.hpp.
//
// P->R (step 1) -- the viewer's files, pasted on this PC: the viewer's offer (65) goes to the Medium
//          clipboard helper (PublishRemoteFiles). When a consumer starts a paste the helper says
//          PasteBegin; the viewer learns it by asking (69/70, the host cannot push), pins its files
//          and sends the confirmed descriptor (71), which goes to the helper (PasteDescriptor). Each
//          helper Read becomes pulls on the bulk stream (FilePullReceiver): every chunk is checked --
//          identity, length, SHA-256 -- before a byte of it reaches the helper. PasteEnd from the
//          helper ends the paste; the viewer learns it by asking.
// R->P (step 2) -- this PC's files, pasted on the viewer's PC: the clipboard monitor hands over what
//          CF_HDROP names (OnHostClipboard: path strings only -- this elevated process opens no file).
//          The helper identifies them AS THE USER (StatFiles: plain local files only, FileId, size,
//          time); the result is the host's offer, which the viewer discovers by asking every 700 ms
//          (67/68, debate D4: kept up while the session is negotiated and allowed). When a consumer
//          pastes on the viewer's PC, the viewer asks the host to prepare (71, R->P): the helper pins
//          the files (Pin: the same FileId, a writer refused, the content at the moment the paste
//          started) and the host's paced bulk sender (bulk_uplink.hpp, FileChunkServer) answers the
//          viewer's pulls from the helper's pinned handles (ReadLocal), each chunk with its SHA-256.
//          The viewer ends it (73).
// Sender:  the host's bulk sender starts low and has a configured hard ceiling (Config::rate), reacts
//          to delay / queue / resends like the image path, and yields while video is queued
//          (Config::videoBusy) -- a paste may get slow or stop to keep the picture (debate D1).
// Allowed: one question, file_copy_allowed(), asked by every handler (A2) -- not only the Pong bit.
// One bulk: a paste of either direction takes the session's BulkArbiter as File (the image service
//          takes it as Image). A second paste, either direction, is refused (Busy), never swapped in.
// Thread:  HandleControl on the host's control thread; OnHostClipboard on the clipboard monitor's;
//          OnDatagram on the UDP receive thread; the helper's reader thread, a worker for helper
//          starts, the receiver's and the sender's threads. Shared state under mu_.
// Epoch:   two control threads can be inside HandleControl at once (host_control_session.cpp, H-28).
//          Which session the state belongs to is decided, switched and recorded under sessionMu_ in
//          one go, servedEpoch_ never goes down, and every handler asks once more under mu_ that the
//          state is still its request's before it reads or writes it (r4).
//          The epoch OWNS the shared parts (r5): the helper channel (FileHelperChannel::SetOwner --
//          an ended session's Ensure/Send do nothing) and the R->P sender (a (epoch, offerId,
//          pasteOp) key under bulkMu_: Begin/Open and End/Close by anyone else are no-ops). The P->R
//          receiver is opened and closed under mu_ together with the paste that owns it. A late
//          session-end hook or a late "helper gone" of an earlier session changes nothing.
//          Every helper is tagged with the session it was started for and with its own instance
//          number, for life (r6/r7): a start that crossed the switch is closed as soon as it is up,
//          a frame or "gone" of any earlier helper -- another session's, or one replaced within
//          this session after its pipe failed -- is not taken (decided under mu_ with what it would
//          change), and a stale request releases a reservation only if it is still its own by the
//          whole (epoch, offerId, pasteOp) key -- paste ops repeat per session and per helper.
//          Lock order: sessionMu_ -> bulkMu_ -> mu_ (never the other way; nothing waits for the
//          helper or joins a thread under mu_).
//
// Plaintext, like the rest of the media socket: names, sizes and content travel unencrypted (A3).
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
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bulk_arbiter.hpp"
#include "bulk_uplink.hpp"
#include "clip_image_core.hpp"
#include "file_copy_helper_host.hpp"
#include "file_copy_net_rules.hpp"
#include "file_copy_paste_parts.hpp"
#include "file_copy_wire.hpp"

namespace remote60::native_poc {

constexpr uint32_t kHostFilePinLeaseMs = 60000;  // the helper's lease; every ReadLocal renews it (A4)

/**
 * The host's file bulk sender: conservative start, a hard ceiling (debate D1). The ceiling and the
 * start can be set by REMOTE60_FILE_BULK_CAP_KBPS / REMOTE60_FILE_BULK_START_KBPS.
 */
BulkRateConfig host_file_bulk_rate_config_from_env();

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
    uint32_t pinWaitMs = 5000;         // PinResult bound (R->P prepare)
    uint32_t readWaitMs = 10000;       // one ReadLocal (R->P)
    uint32_t backoffFirstMs = 5000;    // after a failed launch, no new attempt before this ...
    uint32_t backoffMaxMs = 120000;    // ... doubling up to this
    uint32_t pullWindow = 4;           // outstanding pulls (each <= 64 KiB), P->R
    BulkRateConfig rate = host_file_bulk_rate_config_from_env();  // R->P sender
    std::function<bool()> videoBusy;   // R->P sender yields while this says video is queued
    std::function<uint64_t()> pingRtt; // the control link's RTT (0 = unknown)
  };

  HostFileCopyService();
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

  /** Pong 0x800: switched on and a launcher configured. (Whether the helper really starts is per use.) */
  bool Advertised() const { return file_copy_allowed(); }
  /**
   * The one allow rule (A2). Asked by every handler that starts or feeds a transfer -- Offer,
   * OfferQuery, PasteQuery, Prepare, a helper Read / PasteBegin, every bulk datagram, every ReadLocal.
   * End and Status are answered while disabled (they only end or describe). The product has no
   * view-only session; one would be added here (not implemented).
   */
  bool file_copy_allowed() const { return running_.load() && enabled_.load() && config_.launcher != nullptr; }
  /**
   * The switch at run time. Off: nothing new starts, a running paste either way ends (Disabled), the
   * offers are withdrawn, the helper is told to shut down -- bounded, on the caller's thread.
   * NOT A PRODUCT SWITCH YET: the product has no run-time off switch. File copy is turned off only
   * at process start, from the environment once (native_video_host_main.cpp:206,249 /
   * viewer_clip_image_wiring.cpp:39). The mid-session off path here is a part exercised by tests;
   * a settings switch that calls it is a follow-up after the field-test release (t-zdmsd4gb r3).
   */
  void SetEnabled(bool on);

  /**
   * One control request (65/67/69/71/73/75) -> its answer. False when `type` is not one of them or
   * the body does not parse (the caller answers nothing; the link is then out of step, as for any
   * malformed request).
   */
  bool HandleControl(uint16_t type, const std::vector<uint8_t>& body, uint64_t servedEpoch, uint16_t* replyType,
                     std::vector<uint8_t>* reply);

  /** A file-paste bulk datagram (the router decided). True when it was one. */
  bool OnDatagram(const void* data, size_t len);

  /**
   * The clipboard of this PC changed (`seq` = its sequence number): `paths` is what CF_HDROP names,
   * empty when it names nothing. Strings only -- nothing is opened here. Identified as the user by
   * the helper once a negotiated session asks (A1).
   */
  void OnHostClipboard(uint64_t seq, std::vector<std::wstring> paths);

  /**
   * The session ended or rolled over: offers of the viewer, any paste and the helper go. `newEpoch`
   * (0 = none) becomes the served epoch when it is newer: a late request of the ended session is
   * dropped from then on, never handled (r4).
   */
  void OnSessionEnd(uint64_t newEpoch);

  /**
   * TEST ONLY -- parks a control thread at a chosen point of HandleControl so a test can cross two
   * sessions' requests on purpose: `point` 1 = the epoch is decided, before the switch (under
   * sessionMu_); 2 = it is recorded, before the handler runs (not under it). In the handlers (r5,
   * no lock held): 3 = Offer, the helper is up, before Publish is sent; 4 = Offer, Publish sent,
   * before the answer is awaited; 5 = Prepare R->P, pinned and accepted, before the sender is
   * begun; 6 = End, the paste is ended under mu_, before the sender is closed; 7 = a helper frame
   * arrived (`requestEpoch` = the owner that helper was started for), before it is looked at; 8 =
   * Prepare R->P, reserved, before the helper is asked to pin. No product code calls this; the
   * build gate checks the shipped host does not carry it.
   */
  void SetEpochProbeForTest(std::function<void(uint64_t requestEpoch, int point)> probe);

  struct Counters {
    uint64_t offers = 0, offersAccepted = 0, offersRefused = 0, helperLaunches = 0, helperLaunchFailures = 0;
    uint64_t pastesBegun = 0, pastesBusy = 0, pastesPrepared = 0, pastesEnded = 0, pastesFailed = 0;
    uint64_t readsRequested = 0, readsServed = 0, readsFailed = 0, chunksVerified = 0, chunksRejected = 0;
    uint64_t bytesDelivered = 0, filesWholeVerified = 0, filesChunkVerified = 0;
    uint8_t lastVerdict = 0, lastEndReason = 0;
    // R->P
    uint64_t hostCopies = 0, hostOffers = 0, hostFilesOffered = 0, hostFilesExcluded = 0, offerQueries = 0;
    uint64_t sendPrepared = 0, sendRefused = 0, sendEnded = 0, sendFailed = 0;
    uint64_t chunksServed = 0, bytesServed = 0, pullsRefused = 0, localReadFailures = 0;
    uint64_t sendVerificationEnds = 0;  // R->P sends ended because the viewer's chunk check failed
    uint8_t lastSendVerdict = 0, lastSendEndReason = 0;
    // r5: what the shared sender is begun for (the test's window on the bulk ownership), and how
    // many helper starts / sends of an ended session were refused by the channel's owner check.
    bool sendOpen = false;
    uint64_t sendPasteOp = 0;
    uint64_t sendEpochTag = 0;       // whose session's (paste ops repeat per session)
    uint64_t staleHelperSends = 0;
    uint64_t staleHelperFrames = 0;  // frames of an earlier helper (another session's, or replaced), not taken (r6/r7)
    uint64_t staleHelperGones = 0;   // the same for "gone" (r7)
  };
  Counters GetCounters() const;
  BulkUplink::Counters UplinkCounters() const { return uplink_.GetCounters(); }
  /** The R->P sender's current rate (bits/s), 0 when no paste is being served. */
  uint32_t SendRateBps() const { return uplink_.open() ? uplink_.rate_now() : 0; }

 private:
  struct PeerOffer {  // P->R: the viewer's offer, published here
    uint64_t offerId = 0;
    std::vector<file_copy::net::OfferItem> items;
  };
  struct HostFile {   // R->P: one file of this PC's clipboard, as the user's helper identified it
    std::u16string path;  // never logged
    file_copy::FileId id;
    uint64_t size = 0;
    uint64_t mtime = 0;
  };
  struct HostOffer {  // R->P
    uint64_t revision = 0;  // the clipboard sequence it describes (0 = none yet)
    uint64_t offerId = 0;   // 0 = the clipboard names no file
    std::vector<file_copy::net::OfferItem> items;
    std::vector<HostFile> files;
  };
  enum class Dir : uint8_t { None, PtoR, RtoP };
  struct Paste {
    Dir dir = Dir::None;
    uint64_t epoch = 0;  // the session it belongs to (r5)
    uint64_t offerId = 0;
    uint64_t pasteOp = 0;
    uint32_t bulkGen = 0;
    std::vector<uint64_t> sizes;
  };
  // Who may begin / end the shared R->P sender (r5): the paste, by its session.
  struct BulkKey {
    uint64_t epoch = 0, offerId = 0, pasteOp = 0;
    bool operator==(const BulkKey& o) const { return epoch == o.epoch && offerId == o.offerId && pasteOp == o.pasteOp; }
    bool operator!=(const BulkKey& o) const { return !(*this == o); }
  };
  struct Ended {
    uint64_t offerId = 0;
    uint64_t pasteOp = 0;
    file_copy::net::PasteState state = file_copy::net::PasteState::None;
    file_copy::net::PasteEndReason reason = file_copy::net::PasteEndReason::None;
  };

  // `owner` / `instance`: whom that helper was started for, and which start it was (r7)
  void OnHelperFrame(uint64_t owner, uint64_t instance, const file_copy::PipeFrame& f);
  bool FrameAcceptedLocked(uint64_t owner, uint64_t instance);  // caller holds mu_: the frame is the current helper's
  void OnHelperGone(uint64_t owner, uint64_t instance);  // whom the pipe that went was started for, which start
  void WorkerLoop();
  void OnPasteBegin(uint64_t owner, uint64_t instance, const file_copy::PasteBegin& m);
  void OnPasteEnd(uint64_t owner, uint64_t instance, const file_copy::PasteEnd& m);
  void OnStats(uint64_t owner, uint64_t instance, const file_copy::Stats& m);
  // caller holds mu_; returns true when an R->P sender must be closed (FinishSendClose, unlocked)
  bool EndPasteLocked(file_copy::net::PasteState state, file_copy::net::PasteEndReason reason);
  BulkKey KeyOfPasteLocked() const { return BulkKey{paste_.epoch, paste_.offerId, paste_.pasteOp}; }
  // R->P sender ownership (r5), under bulkMu_ (taken before mu_, never inside it):
  // BeginSend takes the sender for `key` if the paste is still that one's and current, else false;
  // FinishSendClose ends / closes / unpins only when `key` holds it -- otherwise the pin alone.
  bool BeginSend(const BulkKey& key, const FilePasteIdentity& id, const std::vector<uint64_t>& sizes);
  void FinishSendClose(const BulkKey& key);
  // The one switch of the state to `epoch` (caller holds sessionMu_); `endPrevious` = a session ran.
  void SwitchToLocked(uint64_t epoch, bool endPrevious);
  // A request whose session ended on the way: releases what it reserved (`key`, only if the
  // reservation is still that very one's; {} = nothing), answers {} so HandleControl drops it.
  // StaleLocked: caller holds mu_; StaleAs takes it.
  std::vector<uint8_t> StaleLocked(const BulkKey& key);
  std::vector<uint8_t> StaleAs(const BulkKey& key);
  const PeerOffer* FindPeerOffer(uint64_t offerId) const;  // caller holds mu_
  const HostOffer* FindHostOffer(uint64_t offerId) const;  // caller holds mu_
  file_copy::Status ReadLocal(uint64_t epoch, uint64_t pinId, uint32_t index, uint64_t offset, uint32_t length,
                              std::vector<uint8_t>* out);
  void Log(const std::string& line);

  // Each handler answers {} -- nothing, dropped by HandleControl -- when the state is no longer its
  // request's session's (Current(), under mu_): a request that passed the epoch check and then lost
  // the session to a newer one changes nothing (r4).
  bool Current(uint64_t epoch) const { return servedEpoch_ == epoch; }  // caller holds mu_
  bool HandleKnown(uint16_t type, const std::vector<uint8_t>& body, uint64_t epoch, uint16_t* replyType,
                   std::vector<uint8_t>* reply);  // the switch of HandleControl
  std::vector<uint8_t> HandleOffer(const file_copy::net::Offer& m, uint64_t epoch);
  std::vector<uint8_t> HandleOfferQuery(const file_copy::net::OfferQuery& m, uint64_t epoch);
  std::vector<uint8_t> HandlePasteQuery(const file_copy::net::PasteQuery& m, uint64_t epoch);
  std::vector<uint8_t> HandlePrepare(const file_copy::net::Prepare& m, uint64_t epoch);
  std::vector<uint8_t> HandlePrepareRtoP(const file_copy::net::Prepare& m, uint64_t epoch);
  std::vector<uint8_t> HandleEnd(const file_copy::net::End& m, uint64_t epoch);
  std::vector<uint8_t> HandleStatus(const file_copy::net::StatusQuery& m, uint64_t epoch);

  SendFn send_;
  uint32_t mtu_ = 1200;
  Config config_;
  BulkArbiter* arbiter_ = nullptr;
  LogFn log_;
  std::atomic<bool> running_{false};
  std::atomic<bool> enabled_{false};
  uint64_t servedEpoch_ = 0;  // the control session the state belongs to (0 = none yet); under mu_, only grows
  // Held across "decide the epoch -> end the older session -> record" (HandleControl) and across
  // OnSessionEnd, so those never interleave. Taken before mu_, never inside it.
  std::mutex sessionMu_;
  std::mutex bulkMu_;    // the R->P sender's lifecycle and its owner (r5)
  BulkKey sendOwner_;    // under bulkMu_; {} = nobody
  std::function<void(uint64_t, int)> epochProbe_;  // test only (SetEpochProbeForTest), installed before any request

  // Ends whatever runs and drops the viewer's offers; `reason` is the paste's end reason;
  // `helperOwner` names whose helper is shut down (the previous session's on a switch).
  void Teardown(file_copy::net::PasteEndReason reason, uint64_t helperOwner);

  FileHelperChannel helper_;
  FilePullReceiver receiver_;  // P->R
  FileChunkServer server_;     // R->P
  BulkUplink uplink_;          // R->P

  // Helper starts and StatFiles run here, never on the control or clipboard thread.
  std::thread worker_;
  std::condition_variable workCv_;

  mutable std::mutex mu_;
  std::condition_variable replyCv_;  // PublishResult / PinResult / LocalData
  bool publishAnswered_ = false;
  file_copy::PublishResult publishResult_;
  bool pinAnswered_ = false;
  file_copy::PinResult pinResult_;
  bool localAnswered_ = false;
  file_copy::LocalData localData_;
  bool sendAborting_ = false;  // R->P: a read in flight gives up at once
  uint64_t sendBytesAtStart_ = 0;  // the server's served-bytes counter when this paste began
  uint64_t epochTag_ = 0;
  uint32_t random32_ = 0;
  PeerOffer offer_, retired_;
  bool haveBegun_ = false;
  uint64_t begunOffer_ = 0, begunOp_ = 0;
  // R->P: the newest clipboard content, the offer made of it, and the one it replaced (a paste may
  // have begun on it just before).
  uint64_t clipSeq_ = 0;
  std::vector<std::wstring> clipPaths_;
  // A session's first OfferQuery fixes its baseline: what the clipboard held before (sequence <=
  // baseline) is never offered to it -- connecting never replaces the viewer's clipboard (the text
  // sync's rule). Only a copy made here while connected is identified and offered.
  bool baselineSet_ = false;
  uint64_t baselineSeq_ = 0;
  bool statWanted_ = false;     // a negotiated session asked and the clipboard is not identified yet
  uint64_t statSeq_ = 0;        // the clipboard sequence the StatFiles in flight describes
  uint64_t statId_ = 0;         // its request id (0 = none in flight)
  uint64_t nextStatId_ = 1;
  HostOffer hostOffer_, hostRetired_;
  Paste paste_;
  Ended lastEnded_;
  BulkGenAllocator gens_;
  Counters counters_;
};

}  // namespace remote60::native_poc
