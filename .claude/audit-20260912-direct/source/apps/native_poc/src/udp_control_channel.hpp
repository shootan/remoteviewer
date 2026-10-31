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
};

const char* to_string(ControlCloseReason reason);

class UdpControlChannel {
 public:
  // Transmits one datagram to the peer. Called from whichever thread is sending; must be safe
  // to call concurrently with the socket's receive path (sendto is).
  using SendFn = std::function<bool(const void* data, size_t len)>;

  void Configure(SendFn send, uint32_t txStreamId, uint32_t rxStreamId, uint32_t mtuBytes);

  /** Feed a datagram that the media protocol did not recognise. True when it was ours. */
  bool OnPacket(const void* data, size_t len);

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
    uint16_t haveCount = 0;
    uint64_t lastProgressUs = 0;
    uint64_t lastNackUs = 0;
  };

  void SendFragments(const Outbound& msg, const std::vector<uint16_t>* only);
  void SendAckOrNack(uint16_t kind, uint32_t seq, const std::vector<uint16_t>& missing);
  void HandleData(const UdpControlChunkHeader& head, const uint8_t* payload, size_t payloadLen);
  void HandleAck(const UdpControlAckPacket& packet);

  SendFn send_;
  uint32_t txStreamId_ = 0;
  uint32_t rxStreamId_ = 0;
  uint32_t fragBytes_ = 1100;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::atomic<bool> closed_{false};
  std::atomic<ControlCloseReason> closeReason_{ControlCloseReason::None};

  uint32_t nextTxSeq_ = 1;
  std::deque<Outbound> txQueue_;  // front is the message awaiting acknowledgement

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

 private:
  SocketHandle Current() const { return fetch_ ? fetch_() : fixed_; }

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
