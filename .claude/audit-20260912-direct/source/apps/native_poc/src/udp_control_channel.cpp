#include "udp_control_channel.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace remote60::native_poc {
namespace {

// One unacknowledged message at a time matches the request/response protocol above, so these
// only have to cover loss, not congestion.
constexpr uint64_t kRetransmitIntervalUs = 250000;   // whole-message resend while unacked
constexpr uint32_t kMaxAttempts = 24;                // ~6 s before the link is declared dead
constexpr uint64_t kNackDelayUs = 90000;             // wait this long for stragglers first
constexpr uint64_t kNackIntervalUs = 90000;
// Long messages (window thumbnails) run to hundreds of fragments; a short pause every burst
// keeps them from overrunning the receiver's socket buffer in one go.
constexpr size_t kBurstFragments = 24;
// Nothing in the control protocol comes close to this; it only stops a malformed or hostile
// header from making us allocate an arbitrary buffer.
constexpr uint32_t kMaxMessageBytes = 8u * 1024u * 1024u;

uint64_t now_us() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

}  // namespace

void UdpControlChannel::Configure(SendFn send, uint32_t txStreamId, uint32_t rxStreamId,
                                  uint32_t mtuBytes) {
  std::lock_guard<std::mutex> lock(mu_);
  send_ = std::move(send);
  txStreamId_ = txStreamId;
  rxStreamId_ = rxStreamId;
  const uint32_t headerBytes = static_cast<uint32_t>(sizeof(UdpControlChunkHeader));
  const uint32_t mtu = std::max<uint32_t>(mtuBytes, headerBytes + 64u);
  fragBytes_ = mtu - headerBytes;
  closed_.store(false, std::memory_order_relaxed);
}

void UdpControlChannel::Close(ControlCloseReason reason) {
  // First reason wins. A rollover that races with the departing peer's retransmits running out
  // would otherwise be reported as the peer's death, which reads like a network fault.
  auto expected = ControlCloseReason::None;
  closeReason_.compare_exchange_strong(expected, reason, std::memory_order_relaxed);
  closed_.store(true, std::memory_order_relaxed);
  cv_.notify_all();
}

void UdpControlChannel::Reset() {
  std::lock_guard<std::mutex> lock(mu_);
  nextTxSeq_ = 1;
  txQueue_.clear();
  rxPending_.clear();
  rxReady_.clear();
  // Without this the next client's first message looks like one already delivered, and
  // HandleData answers it with an ack while dropping the data -- alive on the wire, deaf above.
  rxDeliveredSeq_ = 0;
  closeReason_.store(ControlCloseReason::None, std::memory_order_relaxed);
  closed_.store(false, std::memory_order_relaxed);
}

const char* to_string(ControlCloseReason reason) {
  switch (reason) {
    case ControlCloseReason::PeerLost: return "peer-lost";
    case ControlCloseReason::SessionRollover: return "session-rollover";
    case ControlCloseReason::Shutdown: return "shutdown";
    default: return "none";
  }
}

UdpControlChannel::Stats UdpControlChannel::GetStats() const {
  std::lock_guard<std::mutex> lock(mu_);
  return stats_;
}

void UdpControlChannel::SendFragments(const Outbound& msg, const std::vector<uint16_t>* only) {
  if (!send_) return;
  std::vector<uint8_t> packet(sizeof(UdpControlChunkHeader) + fragBytes_);
  const size_t count = only ? only->size() : msg.fragCount;
  for (size_t i = 0; i < count; ++i) {
    const uint16_t index = only ? (*only)[i] : static_cast<uint16_t>(i);
    if (index >= msg.fragCount) continue;
    const uint32_t offset = static_cast<uint32_t>(index) * fragBytes_;
    if (offset >= msg.payload.size()) continue;
    const uint32_t size = static_cast<uint32_t>(
        std::min<size_t>(fragBytes_, msg.payload.size() - offset));

    UdpControlChunkHeader head{};
    head.streamId = txStreamId_;
    head.messageSeq = msg.seq;
    head.totalSize = static_cast<uint32_t>(msg.payload.size());
    head.fragIndex = index;
    head.fragCount = msg.fragCount;
    head.fragOffset = offset;
    head.fragSize = size;
    std::memcpy(packet.data(), &head, sizeof(head));
    std::memcpy(packet.data() + sizeof(head), msg.payload.data() + offset, size);
    (void)send_(packet.data(), sizeof(head) + size);
    ++stats_.fragmentsSent;
    if (only) ++stats_.fragmentRetransmits;

    if ((i + 1) % kBurstFragments == 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(700));
    }
  }
}

void UdpControlChannel::SendAckOrNack(uint16_t kind, uint32_t seq,
                                      const std::vector<uint16_t>& missing) {
  if (!send_) return;
  size_t sent = 0;
  do {
    UdpControlAckPacket packet{};
    packet.kind = kind;
    packet.streamId = rxStreamId_;
    packet.messageSeq = seq;
    const size_t take = std::min<size_t>(missing.size() - sent, kUdpControlMaxMissingPerNack);
    for (size_t i = 0; i < take; ++i) packet.missing[i] = missing[sent + i];
    packet.missingCount = static_cast<uint16_t>(take);
    (void)send_(&packet, sizeof(packet));
    sent += take;
  } while (sent < missing.size());
}

bool UdpControlChannel::Send(const void* data, size_t len) {
  if (closed_.load(std::memory_order_relaxed) || len == 0) return false;
  std::unique_lock<std::mutex> lock(mu_);
  if (!send_) return false;

  Outbound msg;
  msg.seq = nextTxSeq_++;
  msg.payload.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + len);
  msg.fragCount = static_cast<uint16_t>((len + fragBytes_ - 1) / fragBytes_);
  msg.lastSendUs = now_us();
  msg.attempts = 1;
  txQueue_.push_back(std::move(msg));
  ++stats_.messagesSent;

  // Only the message at the head is in flight; the rest go out as it is acknowledged, which
  // keeps ordering without any windowing logic.
  if (txQueue_.size() == 1) SendFragments(txQueue_.front(), nullptr);
  return true;
}

bool UdpControlChannel::Receive(std::vector<uint8_t>* out, uint32_t timeoutMs) {
  if (!out) return false;
  std::unique_lock<std::mutex> lock(mu_);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (rxReady_.empty()) {
    if (closed_.load(std::memory_order_relaxed)) return false;
    if (cv_.wait_until(lock, deadline) == std::cv_status::timeout && rxReady_.empty()) {
      return false;
    }
  }
  *out = std::move(rxReady_.front());
  rxReady_.pop_front();
  return true;
}

void UdpControlChannel::HandleData(const UdpControlChunkHeader& head, const uint8_t* payload,
                                   size_t payloadLen) {
  // Already delivered: the peer did not see our ack, so repeat it and drop the data.
  if (head.messageSeq <= rxDeliveredSeq_) {
    SendAckOrNack(static_cast<uint16_t>(UdpPacketKind::ControlAck), head.messageSeq, {});
    return;
  }
  if (head.fragCount == 0 || head.totalSize == 0 || head.totalSize > kMaxMessageBytes) return;
  if (head.fragOffset > head.totalSize || head.fragOffset + payloadLen > head.totalSize) return;

  auto& slot = rxPending_[head.messageSeq];
  if (slot.fragCount == 0) {
    slot.fragCount = head.fragCount;
    slot.totalSize = head.totalSize;
    slot.bytes.assign(head.totalSize, 0);
    slot.have.assign(head.fragCount, false);
    slot.lastProgressUs = now_us();
  }
  if (slot.fragCount != head.fragCount || slot.totalSize != head.totalSize) return;
  if (head.fragIndex >= slot.fragCount || slot.have[head.fragIndex]) return;

  std::memcpy(slot.bytes.data() + head.fragOffset, payload, payloadLen);
  slot.have[head.fragIndex] = true;
  ++slot.haveCount;
  slot.lastProgressUs = now_us();

  if (slot.haveCount < slot.fragCount) return;

  rxDeliveredSeq_ = head.messageSeq;
  rxReady_.push_back(std::move(slot.bytes));
  ++stats_.messagesReceived;
  // Drop this and anything older; sequence numbers only move forward.
  rxPending_.erase(rxPending_.begin(), rxPending_.upper_bound(head.messageSeq));
  SendAckOrNack(static_cast<uint16_t>(UdpPacketKind::ControlAck), head.messageSeq, {});
  cv_.notify_all();
}

void UdpControlChannel::HandleAck(const UdpControlAckPacket& packet) {
  if (txQueue_.empty() || txQueue_.front().seq != packet.messageSeq) return;

  if (packet.kind == static_cast<uint16_t>(UdpPacketKind::ControlNack)) {
    std::vector<uint16_t> missing(packet.missing, packet.missing + packet.missingCount);
    if (!missing.empty()) SendFragments(txQueue_.front(), &missing);
    txQueue_.front().lastSendUs = now_us();
    return;
  }

  txQueue_.pop_front();
  if (!txQueue_.empty()) {
    txQueue_.front().lastSendUs = now_us();
    txQueue_.front().attempts = 1;
    SendFragments(txQueue_.front(), nullptr);
  }
}

bool UdpControlChannel::OnPacket(const void* data, size_t len) {
  if (!data || len < sizeof(uint32_t) + sizeof(uint16_t)) return false;
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t magic = 0;
  uint16_t kind = 0;
  std::memcpy(&magic, bytes, sizeof(magic));
  std::memcpy(&kind, bytes + sizeof(magic), sizeof(kind));
  if (magic != kMagic) return false;

  std::lock_guard<std::mutex> lock(mu_);
  if (kind == static_cast<uint16_t>(UdpPacketKind::ControlData)) {
    if (len < sizeof(UdpControlChunkHeader)) return true;
    UdpControlChunkHeader head{};
    std::memcpy(&head, bytes, sizeof(head));
    if (head.streamId != rxStreamId_) return true;
    const size_t payloadLen = len - sizeof(UdpControlChunkHeader);
    if (payloadLen < head.fragSize) return true;
    HandleData(head, bytes + sizeof(UdpControlChunkHeader), head.fragSize);
    return true;
  }
  if (kind == static_cast<uint16_t>(UdpPacketKind::ControlAck) ||
      kind == static_cast<uint16_t>(UdpPacketKind::ControlNack)) {
    if (len < sizeof(UdpControlAckPacket)) return true;
    UdpControlAckPacket packet{};
    std::memcpy(&packet, bytes, sizeof(packet));
    // Acks refer to the stream we transmit on.
    if (packet.streamId != txStreamId_) return true;
    if (packet.missingCount > kUdpControlMaxMissingPerNack) return true;
    HandleAck(packet);
    return true;
  }
  return false;
}

void UdpControlChannel::Tick() {
  std::lock_guard<std::mutex> lock(mu_);
  const uint64_t now = now_us();

  if (!txQueue_.empty()) {
    Outbound& head = txQueue_.front();
    if (now - head.lastSendUs >= kRetransmitIntervalUs) {
      if (++head.attempts > kMaxAttempts) {
        auto expected = ControlCloseReason::None;
        closeReason_.compare_exchange_strong(expected, ControlCloseReason::PeerLost,
                                             std::memory_order_relaxed);
        closed_.store(true, std::memory_order_relaxed);
        cv_.notify_all();
        return;
      }
      head.lastSendUs = now;
      SendFragments(head, nullptr);
    }
  }

  for (auto& [seq, slot] : rxPending_) {
    if (slot.fragCount == 0 || slot.haveCount >= slot.fragCount) continue;
    if (now - slot.lastProgressUs < kNackDelayUs) continue;
    if (slot.lastNackUs != 0 && now - slot.lastNackUs < kNackIntervalUs) continue;
    std::vector<uint16_t> missing;
    for (uint16_t i = 0; i < slot.fragCount; ++i) {
      if (!slot.have[i]) missing.push_back(i);
    }
    if (missing.empty()) continue;
    SendAckOrNack(static_cast<uint16_t>(UdpPacketKind::ControlNack), seq, missing);
    slot.lastNackUs = now;
    ++stats_.nacksSent;
  }
}

// ---------------------------------------------------------------- links

bool ControlLink::Discard(size_t len) {
  uint8_t scratch[1024];
  while (len > 0) {
    const size_t take = std::min<size_t>(len, sizeof(scratch));
    if (!Read(scratch, take)) return false;
    len -= take;
  }
  return true;
}

bool TcpControlLink::Read(void* out, size_t len) {
  const SocketHandle sock = Current();
  if (sock == kInvalidSocket) return false;
  return recv_all(sock, out, len);
}

bool TcpControlLink::Write(const void* data, size_t len) {
  const SocketHandle sock = Current();
  if (sock == kInvalidSocket) return false;
  return send_all(sock, data, len);
}

bool UdpControlLink::EnsureInbound() {
  if (inboundRead_ < inbound_.size()) return true;
  if (!channel_) return false;
  inbound_.clear();
  inboundRead_ = 0;
  // Tick while waiting: nothing else drives retransmission on this thread.
  const uint32_t sliceMs = 25;
  uint32_t waited = 0;
  for (;;) {
    if (channel_->Receive(&inbound_, sliceMs)) {
      inboundRead_ = 0;
      return !inbound_.empty();
    }
    if (channel_->IsClosed()) return false;
    channel_->Tick();
    waited += sliceMs;
    // 0 means wait indefinitely, which is what the host wants: an idle user is not a dead
    // link, and the channel itself reports death when its retransmits run out.
    if (readTimeoutMs_ != 0 && waited >= readTimeoutMs_) return false;
  }
}

bool UdpControlLink::Read(void* out, size_t len) {
  auto* dst = static_cast<uint8_t*>(out);
  size_t done = 0;
  while (done < len) {
    if (!EnsureInbound()) return false;
    const size_t take = std::min(len - done, inbound_.size() - inboundRead_);
    if (take == 0) return false;
    std::memcpy(dst + done, inbound_.data() + inboundRead_, take);
    inboundRead_ += take;
    done += take;
  }
  return true;
}

bool UdpControlLink::Write(const void* data, size_t len) {
  if (!channel_ || channel_->IsClosed()) return false;
  const auto* src = static_cast<const uint8_t*>(data);
  outbound_.insert(outbound_.end(), src, src + len);
  return true;
}

bool UdpControlLink::EndMessage() {
  if (!channel_ || channel_->IsClosed()) return false;
  if (outbound_.empty()) return true;
  const bool ok = channel_->Send(outbound_.data(), outbound_.size());
  outbound_.clear();
  return ok;
}

}  // namespace remote60::native_poc
