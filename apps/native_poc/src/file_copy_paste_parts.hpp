#pragma once

// The three pieces both ends of a file paste are made of (t-zdmsd4gb r1, step 2). A paste has a
// RECEIVING side -- the PC the consumer pastes on, whose helper asks for bytes -- and a SERVING side
// -- the PC the files are on. P->R: the host receives, the viewer serves. R->P: the viewer receives,
// the host serves. The rules are the same either way, so they are written once:
//
//   FileHelperChannel  the Medium clipboard helper: started on demand (never as anyone but the
//                      user the launcher names -- no High / SYSTEM fallback), backoff after a failed
//                      start, one reader thread handing every frame to the owner.
//   FilePullReceiver   the receiving side's bulk: pulls of <= 64 KiB (77); each chunk (78) is checked
//                      against its pull -- identity, length, SHA-256 -- BEFORE a byte of it is kept;
//                      a failed check fails the Read waiting on it ("전송 데이터 검증 실패"). It reads
//                      AHEAD, sequentially: up to 1 MiB past what the consumer has read of one file is
//                      pulled and verified while the consumer is busy with what it has, so the bulk
//                      is not idle between the shell's 256 KiB Reads. A Read somewhere else (a Seek,
//                      another file) starts over there; what was read ahead of the old position is
//                      dropped (bounded waste). Verified completions are named back to the serving side
//                      one per pull, oldest first (its rate control counts goodput by them).
//   FileChunkServer    the serving side's answer to a pull: the paste's identity and the range
//                      checked, the bytes read from the source the owner names (a pinned handle on
//                      the viewer, the helper's pin on the host), their SHA-256 attached. It is the
//                      BulkUplinkSource of the shared paced sender (bulk_uplink.hpp).
//
// Nothing here names a path; logs are the owners' and carry counts and outcomes only.

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

#include "bulk_uplink.hpp"
#include "file_copy_helper_host.hpp"
#include "file_copy_net_rules.hpp"
#include "file_copy_pipe.hpp"
#include "file_copy_wire.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

// ------------------------------------------------------------------------------ helper channel

class FileHelperChannel {
 public:
  /** Starts the helper and completes its handshake on `link`; false = unavailable now. */
  using Launcher = std::function<bool(file_copy::HelperLink* link, std::string* why)>;
  /** Every frame the helper sends, on the reader thread. */
  using FrameFn = std::function<void(const file_copy::PipeFrame&)>;
  /** The pipe is gone (the helper exited or was stopped), on the reader thread. */
  using GoneFn = std::function<void()>;

  struct Config {
    Launcher launcher;               // null = unavailable
    uint32_t backoffFirstMs = 5000;  // after a failed start, no new attempt before this ...
    uint32_t backoffMaxMs = 120000;  // ... doubling up to this
  };

  ~FileHelperChannel() { Stop(); }

  void Configure(Config config, FrameFn onFrame, GoneFn onGone);
  bool configured() const { return config_.launcher != nullptr; }

  /** Running, or started now (one start at a time, backoff honoured). */
  bool Ensure(std::string* why);
  bool Send(const file_copy::PipeFrame& f);
  bool Running() const;
  /** Drops the pipe: the helper clears what it published and exits (its contract). */
  void Disconnect();
  void Stop();

  uint64_t launches() const { return launches_.load(); }
  uint64_t launchFailures() const { return launchFailures_.load(); }

 private:
  void ReaderLoop();

  Config config_;
  FrameFn onFrame_;
  GoneFn onGone_;
  mutable std::mutex mu_;      // start / stop
  std::mutex sendMu_;
  file_copy::HelperLink link_;
  std::thread reader_;
  std::atomic<bool> readerRun_{false};
  uint64_t nextLaunchMs_ = 0;
  uint32_t backoffMs_ = 0;
  std::atomic<uint64_t> launches_{0}, launchFailures_{0};
};

// ------------------------------------------------------------------------------ the paste's identity

struct FilePasteIdentity {
  uint64_t epochTag = 0;
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  uint32_t bulkGen = 0;
};

// ------------------------------------------------------------------------------ receiving side

class FilePullReceiver {
 public:
  using SendFn = std::function<bool(const void* data, size_t len)>;
  /** A finished (or failed) helper Read, to go back to the helper -- called with no lock held. */
  using AnswerFn = std::function<void(const file_copy::ReadData&)>;

  struct Counters {
    uint64_t readsRequested = 0, readsServed = 0, readsFailed = 0, chunksVerified = 0, chunksRejected = 0;
    uint64_t bytesDelivered = 0;
  };

  explicit FilePullReceiver(uint32_t pullWindow = 4) : pullWindow_(pullWindow) {}

  /**
   * A4 (stall): a Read waiting while not one chunk has been verified for this long fails (Timeout),
   * and the paste's failure is Idle. Progress -- a verified chunk -- restarts the clock, so a long
   * transfer that moves is never cut; a serving side that acknowledges pulls but never answers them
   * (its source read failing, say) no longer holds the consumer for ever. Default 30 s.
   */
  void SetStallTimeoutUs(uint64_t us) { stallUs_.store(us); }
  ~FilePullReceiver() { Stop(); }

  void Start(AnswerFn answer);
  void Stop();

  /** A prepared paste: the bulk stream opens (tx carries pulls, rx the chunks). */
  void Open(SendFn send, uint32_t txStreamId, uint32_t rxStreamId, uint32_t mtuBytes, const FilePasteIdentity& id,
            const std::vector<uint64_t>& sizes);
  /** The paste is over: pending Reads fail with `why`, the stream closes. */
  void Close(file_copy::Status why);
  bool IsOpen() const;

  /** A helper Read of the open paste (or a refusal answered at once). */
  void Submit(const file_copy::ReadRequest& m);
  /** A bulk datagram of this paste's rx stream. */
  bool OnDatagram(const void* data, size_t len) { return bulk_.OnPacket(data, len); }

  /** Why it went wrong, if it did: Verification (a chunk failed its check) / Session (the peer went quiet). */
  file_copy::net::PasteEndReason failure() const;
  uint64_t bytesDelivered() const;
  /**
   * Per file: whether the kept, verified chunks covered 0..size once, ascending ("whole file verified":
   * every chunk of the whole range checked -- not a whole-file SHA) -- else "chunk verified".
   */
  std::vector<bool> wholeFileVerified() const;
  Counters GetCounters() const;

 private:
  struct Job {  // one read-ahead range (<= 256 KiB), pulled as <= 64 KiB pieces
    uint32_t fileIndex = 0;
    uint64_t offset = 0;
    std::vector<file_copy::net::Pull> pulls;
    size_t nextPull = 0;
    size_t done = 0;
    std::vector<uint8_t> data;
    bool failed = false;
  };
  static constexpr uint64_t kAheadBytes = 1u << 20;  // how far past the consumer's position
  void Loop();
  void PumpPullsLocked();
  // Answers what the verified window covers, restarts the window where a Read is not sequential, and
  // issues read-ahead up to kAheadBytes past the consumer's position.
  void ProcessLocked(std::vector<file_copy::ReadData>* answers);
  void ResetAheadLocked(uint32_t fileIndex, uint64_t offset);
  void FailAllLocked(file_copy::Status why, std::vector<file_copy::ReadData>* answers);

  const uint32_t pullWindow_;
  AnswerFn answer_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  bool open_ = false;
  FilePasteIdentity id_;
  std::vector<uint64_t> sizes_;
  std::vector<file_copy::net::CoverageTracker> coverage_;
  std::deque<std::unique_ptr<Job>> jobs_;
  std::map<uint64_t, std::pair<Job*, size_t>> outstanding_;  // request id -> job, pull index
  std::deque<uint64_t> completed_;  // verified, not yet named to the serving side
  std::deque<file_copy::ReadRequest> waiting_;  // helper Reads not answered yet, in arrival order
  // The read-ahead window: verified bytes of file aheadFile_ from aheadStart_ (the consumer's next
  // byte), and how far pulls have been issued.
  bool aheadOn_ = false;
  uint32_t aheadFile_ = 0;
  uint64_t aheadStart_ = 0;
  std::vector<uint8_t> ahead_;
  uint64_t aheadIssuedEnd_ = 0;
  uint64_t nextRequestId_ = 1;
  uint64_t bytesDelivered_ = 0;
  file_copy::net::PasteEndReason failure_ = file_copy::net::PasteEndReason::None;
  std::atomic<uint64_t> stallUs_{30000000};
  uint64_t lastProgressUs_ = 0;  // a verified chunk, or a Read starting to wait on an idle receiver
  Counters counters_;
  UdpControlChannel bulk_;
};

// ------------------------------------------------------------------------------ serving side

class FileChunkServer : public BulkUplinkSource {
 public:
  /** Reads [offset, offset + length) of file `index` from the paste's pinned source. */
  using ReadFn = std::function<file_copy::Status(uint32_t index, uint64_t offset, uint32_t length, std::vector<uint8_t>* out)>;

  struct Counters {
    uint64_t chunksServed = 0, bytesServed = 0, pullsRefused = 0;
  };

  /** A prepared paste: pulls naming this identity (and a range inside `sizes`) are answered. */
  void Begin(const FilePasteIdentity& id, std::vector<uint64_t> sizes, ReadFn read);
  /** Nothing more is answered. */
  void End();
  Counters GetCounters() const;

  // BulkUplinkSource
  bool OnPull(const std::vector<uint8_t>& msg, uint64_t nowUs, std::vector<uint8_t>* out, BulkServed* served) override;
  bool ChunkKeyOf(const uint8_t* message, size_t len, uint64_t* key) override;

 private:
  mutable std::mutex mu_;
  bool active_ = false;
  FilePasteIdentity id_;
  std::vector<uint64_t> sizes_;
  ReadFn read_;
  // Chunks sent and not yet confirmed by a later pull (goodput / RTT evidence), by request id.
  std::deque<std::pair<uint64_t, uint32_t>> sent_;
  Counters counters_;
};

}  // namespace remote60::native_poc
