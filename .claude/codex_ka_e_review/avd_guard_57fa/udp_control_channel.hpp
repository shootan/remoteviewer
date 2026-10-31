#pragma once

// Reliable, message-framed control channel over the media UDP socket.
//
// The control protocol used to have its own TCP connection. That works on a LAN but cannot
// reach a host behind NAT: only the UDP socket gets hole-punched, so control has to ride the
// same path. This provides just enough on top of UDP for that protocol to work unchanged:
// whole messages, delivered intact and in order.
//
// The design leans on the protocol's shape. Control is strictly request/response, so there is
// no need for a sliding window or byte-stream semantics; each message is sent as a burst of
// fragments and the receiver asks for whatever did not arrive.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

#include "native_socket.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

// Why a channel stopped. The distinction matters because only one of these means the process is
// finished: a peer that went away and a session being replaced both close the channel, and a host
// that treats either as shutdown stops serving control for the rest of its life.
enum class ControlCloseReason : uint8_t {
  None = 0,
  PeerLost,          // retransmits exhausted -- the client stopped answering
  SessionRollover,   // a new client authenticated; this channel belongs to the previous one
  Shutdown,          // the process is going away
  ResourceLimit,
  MalformedMessage,
};

const char* to_string(ControlCloseReason reason);

/**
 * Whether the peer's channel is still answering. (item 8, C3 r2)
 *
 * Not the same question as "has anything arrived lately". A session can be idle for seconds
 * and be perfectly healthy, and a one-way failure leaves the peer hearing everything while
 * nothing it says gets back -- so silence is not evidence and this asks instead.
 */
enum class ControlProbeState : uint8_t {
  Idle = 0,  // not asked
  Pending,   // asked, waiting
  Alive,     // the peer's channel answered
  Dead,      // it did not, within the budget
};

const char* to_string(ControlProbeState state);

class UdpControlChannel {
 public:
  // Transmits one datagram to the peer. Called from whichever thread is sending; must be safe
  // to call concurrently with the socket's receive path (sendto is).
  using SendFn = std::function<bool(const void* data, size_t len)>;

  void Configure(SendFn send, uint32_t txStreamId, uint32_t rxStreamId, uint32_t mtuBytes);

  /**
   * Retransmit and NACK timing, per instance. The defaults are the control channel's and the
   * control channel never calls this. The clipboard-image bulk channel does: its datagrams leave
   * through a pacer at as little as 64 kbps, where one 16 KiB message takes seconds to drain, so
   * the control channel's 250 ms whole-message resend and 90 ms NACK would only duplicate what is
   * still waiting in the pacer's queue -- and the duplicates would read as loss.
   */
  struct Timings {
    uint64_t retransmitIntervalUs = 250000;
    uint32_t maxAttempts = 24;
    uint64_t nackDelayUs = 90000;
    uint64_t nackIntervalUs = 90000;
  };
  void SetTimings(const Timings& timings);

  /** True while a message is queued or awaiting its acknowledgement. */
  bool TxPending() const {
    std::lock_guard<std::mutex> lock(mu_);
    return !txQueue_.empty();
  }

  /** Feed a datagram that the media protocol did not recognise. True when it was ours. */
  bool OnPacket(const void* data, size_t len);

  /**
   * True iff this datagram is a control datagram (magic + ControlData/Ack/Nack kind) -- i.e. one that
   * OnPacket would CLAIM. Stateless and lock-free, so the caller can decide, BEFORE OnPacket touches any
   * channel state, whether a datagram from a non-adopted wire peer must be dropped (udp-control-peer ①).
   */
  static bool IsControlDatagram(const void* data, size_t len);

  /** Queue and transmit one whole control message. Returns false once the channel is closed. */
  bool Send(const void* data, size_t len);

  /**
   * Block until the next complete inbound message is available.
   * Returns false on timeout or close; check IsClosed() to tell them apart.
   */
  bool Receive(std::vector<uint8_t>* out, uint32_t timeoutMs);

  /** Drives retransmission and gap recovery. Safe to call often; cheap when idle. */
  void Tick();

  void Close(ControlCloseReason reason = ControlCloseReason::Shutdown);
  void Reset();
  /**
   * Reset onto a re-keyed pair of stream ids, as one step. (item 8)
   *
   * Reset() followed by a separate change of ids would leave a window in which the channel is
   * cleared but still answering on the old ids, and that window is exactly what a datagram
   * delayed across the break needs to be delivered as though it were new. Both peers call this
   * with ids derived from the same resumeId, so each stops recognising the old stream at the
   * same moment it starts listening for the new one.
   */
  // Returns false, and changes nothing, for a channel closed by Shutdown: that is not a channel
  // to repair but one being torn down. (C5)
  bool ResumeWith(uint32_t txStreamId, uint32_t rxStreamId);

  /**
   * The stream ids in force right now. (item 8, C3)
   *
   * Asked by the host when it is deciding whether to repeat an answer it already gave: that
   * answer described a re-key, and repeating it is only honest while the re-key it described is
   * still the one the channel is carrying.
   */
  struct StreamPair {
    uint32_t tx = 0;
    uint32_t rx = 0;
  };
  StreamPair StreamIds() const {
    std::lock_guard<std::mutex> lock(mu_);
    return StreamPair{txStreamId_, rxStreamId_};
  }

  /**
   * Ask the peer's channel to answer, without handing its application anything. (item 8, C3 r2)
   *
   * The trick is already in HandleData: a chunk whose messageSeq the peer has ALREADY
   * delivered is acknowledged and dropped ("the peer did not see our ack, so repeat it and
   * drop the data", :193). So re-sending one chunk of the last message this side had
   * acknowledged produces a real round trip through the peer's channel and nothing at all
   * above it -- no delivery, no new state, and not one line of change at the other end.
   *
   * That last part is why it is done this way. The control protocol is strict
   * request/response: an unsolicited message would be read as the answer to whatever the peer
   * asks next, fail its type check, and break the very link this is trying to establish is
   * healthy. A probe that can do that is not a probe.
   *
   * Budget in attempts and interval, because "no answer for N ms" on its own would call an
   * idle session dead. Retransmits are driven by Tick(), like everything else here.
   */
  void StartProbe(uint32_t maxAttempts, uint64_t intervalUs);
  ControlProbeState ProbeStatus() const {
    std::lock_guard<std::mutex> lock(mu_);
    // A closed channel has no verdict worth reading, whoever closed it and for whatever
    // reason. Answered here rather than cleared in Close(), which is called both with and
    // without this lock held and so cannot touch the fields safely.
    if (closed_.load(std::memory_order_relaxed)) return ControlProbeState::Idle;
    return probeState_;
  }
  /**
   * When the current verdict was reached, so a caller can ask how old it is.
   *
   * A probe answers about the instant it ran, and a channel that was alive a moment ago can be
   * gone now -- which is the ordinary shape of the failure this feature repairs. Without this,
   * a single Alive would be re-read for every retry of the same recovery and the repair would
   * be refused for the whole ceiling. 0 while Idle or Pending.
   */
  /**
   * How long ago the current verdict was reached, measured by the clock that stamped it.
   *
   * The age is computed HERE rather than handed out as a timestamp for a caller to subtract
   * from its own clock. This channel stamps with steady_clock and its callers keep time with
   * QueryPerformanceCounter; those two have different origins, so that subtraction produces a
   * number with no meaning -- one that happens to look right on this machine, which is the
   * worst kind. 0 when there is no verdict.
   */
  uint64_t ProbeAgeUs() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_.load(std::memory_order_relaxed)) return 0;
    if (probeDecidedUs_ == 0) return 0;
    const uint64_t now = ProbeClockUs();
    return now >= probeDecidedUs_ ? now - probeDecidedUs_ : 0;
  }
  uint64_t ProbeDecidedUs() const {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_.load(std::memory_order_relaxed)) return 0;
    return probeDecidedUs_;
  }
  uint32_t ProbeAttempts() const {
    std::lock_guard<std::mutex> lock(mu_);
    return probeAttempts_;
  }
  void ClearProbe() {
    std::lock_guard<std::mutex> lock(mu_);
    probeState_ = ControlProbeState::Idle;
    probeAttempts_ = 0;
    probeDecidedUs_ = 0;
  }

  bool IsClosed() const { return closed_.load(std::memory_order_relaxed); }
  ControlCloseReason CloseReason() const {
    return closeReason_.load(std::memory_order_relaxed);
  }

  struct Stats {
    uint64_t messagesSent = 0;
    uint64_t messagesReceived = 0;
    uint64_t fragmentsSent = 0;
    uint64_t fragmentRetransmits = 0;
    uint64_t nacksSent = 0;
    uint64_t pendingMessages = 0;
    uint64_t inboundBytes = 0;
  };
  Stats GetStats() const;

 private:
  struct Outbound {
    uint32_t seq = 0;
    std::vector<uint8_t> payload;
    uint16_t fragCount = 0;
    uint64_t lastSendUs = 0;
    uint32_t attempts = 0;
  };

  struct Inbound {
    uint32_t totalSize = 0;
    uint16_t fragCount = 0;
    std::vector<uint8_t> bytes;
    std::vector<bool> have;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    uint16_t haveCount = 0;
    uint64_t lastProgressUs = 0;
    uint64_t lastNackUs = 0;
    uint64_t createdUs = 0;
  };

  void SendFragments(const Outbound& msg, const std::vector<uint16_t>* only);
  void SendProbeChunk();  // caller holds mu_
  static uint64_t ProbeClockUs();  // the same clock the verdict is stamped with
  void SendAckOrNack(uint16_t kind, uint32_t seq, const std::vector<uint16_t>& missing);
  void HandleData(const UdpControlChunkHeader& head, const uint8_t* payload, size_t payloadLen);
  void HandleAck(const UdpControlAckPacket& packet);

  SendFn send_;
  uint32_t txStreamId_ = 0;
  uint32_t rxStreamId_ = 0;
  uint32_t fragBytes_ = 1100;
  Timings timings_;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::atomic<bool> closed_{false};
  std::atomic<ControlCloseReason> closeReason_{ControlCloseReason::None};
  std::atomic<bool> shutdown_{false};  // Close(Shutdown) was called, whatever reason came first

  uint32_t nextTxSeq_ = 1;
  std::deque<Outbound> txQueue_;  // front is the message awaiting acknowledgement
  // The highest message this side has had acknowledged -- which is therefore one the peer has
  // delivered, and so one it will acknowledge again without delivering. That is the probe.
  uint32_t lastAckedTxSeq_ = 0;
  ControlProbeState probeState_ = ControlProbeState::Idle;
  uint32_t probeSeq_ = 0;
  uint32_t probeAttempts_ = 0;
  uint32_t probeMaxAttempts_ = 0;
  uint64_t probeIntervalUs_ = 0;
  uint64_t probeLastSendUs_ = 0;
  uint64_t probeDecidedUs_ = 0;

  std::map<uint32_t, Inbound> rxPending_;
  uint32_t rxDeliveredSeq_ = 0;
  std::deque<std::vector<uint8_t>> rxReady_;

  Stats stats_;
};

/**
 * Byte-oriented view over a control transport, so the existing request/response handlers work
 * against TCP and the tunnelled UDP channel without being rewritten.
 *
 * Reads are satisfied from the current inbound message and block for the next one when it is
 * exhausted. Writes accumulate until EndMessage(), which is what draws the message boundary
 * that UDP needs and TCP does not care about.
 */
class ControlLink {
 public:
  virtual ~ControlLink() = default;
  virtual bool Read(void* out, size_t len) = 0;
  virtual bool Write(const void* data, size_t len) = 0;
  virtual bool EndMessage() = 0;
  virtual bool Alive() const = 0;
  /** Discards len bytes of the current message. */
  bool Discard(size_t len);
};

class TcpControlLink : public ControlLink {
 public:
  explicit TcpControlLink(SocketHandle sock) : fixed_(sock) {}
  // Long-lived owners pass a getter instead: the socket is closed from another thread on
  // disconnect, and a captured handle would keep being used after the descriptor is reusable.
  explicit TcpControlLink(std::function<SocketHandle()> fetch) : fetch_(std::move(fetch)) {}
  bool Read(void* out, size_t len) override;
  bool Write(const void* data, size_t len) override;
  bool EndMessage() override { return Current() != kInvalidSocket; }
  bool Alive() const override { return Current() != kInvalidSocket; }
  /** Optional: bytes successfully written are added here (host traffic accounting). */
  void SetWriteCounter(std::atomic<uint64_t>* counter) { writeCounter_ = counter; }

 private:
  SocketHandle Current() const { return fetch_ ? fetch_() : fixed_; }
  std::atomic<uint64_t>* writeCounter_ = nullptr;

  SocketHandle fixed_ = kInvalidSocket;
  std::function<SocketHandle()> fetch_;
};

class UdpControlLink : public ControlLink {
 public:
  // readTimeoutMs bounds how long Read() waits for the next message before giving up, which is
  // what lets a stalled peer surface as a dead link instead of a hung thread. 0 waits forever.
  UdpControlLink(UdpControlChannel* channel, uint32_t readTimeoutMs)
      : channel_(channel), readTimeoutMs_(readTimeoutMs) {}
  bool Read(void* out, size_t len) override;
  bool Write(const void* data, size_t len) override;
  bool EndMessage() override;
  bool Alive() const override { return channel_ && !channel_->IsClosed(); }

 private:
  bool EnsureInbound();

  UdpControlChannel* channel_ = nullptr;
  uint32_t readTimeoutMs_ = 5000;
  std::vector<uint8_t> inbound_;
  size_t inboundRead_ = 0;
  std::vector<uint8_t> outbound_;
};

}  // namespace remote60::native_poc
