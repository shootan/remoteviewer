"""old-ref r1 DIAGNOSTIC (Codex codex_old_apk_lane_peer_lost_decision_r1.md): the same header-only, test-only record in
the 0.2.163 source host and the 0.2.164 candidate host -- udp_control_channel.cpp/hpp are the same blob in both
(71be7daf / cf7c173c). Compiled ONLY with REMOTE60_CONTROL_CLOSE_DIAG (the PinnedSelectTest host and its unit test);
the product GNLinkStream is unchanged. What it keeps: the control ACKs this host saw (on its stream, another stream,
after it had closed), which of them it ACCEPTED (popped the head), stale / probe / NACK; the head message waiting for its
ACK (seq, attempts, fragments, queue depth, first / last send, its control message TYPE from the payload's
MessageHeader -- the header's type field only, nothing else of the payload); the same head as it stood when the channel
declared peer-lost; the receive side's last delivered seq. One line, appended to "udp control session ended".
  python oldref_diag_edit.py <worktree>"""
import sys

WT = sys.argv[1]
SRC = WT + r'\apps\native_poc'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b):
    assert s.count(a) == 1, (a[:100], s.count(a))
    return s.replace(a, b)


# ---- the header
p = SRC + r'\src\udp_control_channel.hpp'
s, crlf = load(p)
s = rep(s, '''#include <map>
#include <mutex>
#include <vector>
''', '''#include <map>
#include <mutex>
#include <vector>
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
#include <string>
#endif
''')
s = rep(s, '''  struct Stats {
    uint64_t messagesSent = 0;''', '''#ifdef REMOTE60_CONTROL_CLOSE_DIAG
  // TEST BUILD ONLY (old-ref r1): the channel's ACK / head record, one line (header fields only, no payload).
  std::string CloseDiagLine() const;
#endif

  struct Stats {
    uint64_t messagesSent = 0;''')
s = rep(s, '''    uint64_t lastSendUs = 0;
    uint32_t attempts = 0;
  };''', '''    uint64_t lastSendUs = 0;
    uint32_t attempts = 0;
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
    uint64_t firstSendUs = 0;  // when it first went out as the head
#endif
  };''')
s = rep(s, '''  Stats stats_;
};''', '''  Stats stats_;
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
  struct CloseDiag {
    uint64_t acksSeen = 0;          // ControlAck / ControlNack datagrams that reached OnPacket while open
    uint64_t acksWrongStream = 0;   //   of them: for another stream (ignored)
    uint64_t acksAfterClose = 0;    // ... and those that came after this channel had closed (dropped)
    uint64_t acksAccepted = 0;      // the head's ACK: the head popped
    uint64_t acksStale = 0;         // an ACK for no head / not the head's seq
    uint64_t probeAcks = 0;         // the liveness probe's answer
    uint64_t nacks = 0;             // a NACK for the head (fragments resent)
    uint32_t lastSeenSeq = 0;       // the seq of the last ACK / NACK seen
    uint64_t lastSeenUs = 0;
    uint64_t lastAcceptedUs = 0;
    bool lost = false;              // the head as it stood when retransmits ran out (peer-lost)
    uint32_t lostSeq = 0, lostAttempts = 0, lostFrags = 0;
    uint64_t lostQueue = 0, lostFirstSendUs = 0, lostLastSendUs = 0, lostAtUs = 0;
    int lostType = -1;
  };
  CloseDiag diag_;
  static int HeaderType(const std::vector<uint8_t>& payload);  // the MessageHeader type field, or -1
#endif
};''')
save(p, s, crlf)

# ---- the implementation
p = SRC + r'\src\udp_control_channel.cpp'
s, crlf = load(p)
# Send: the first send of a message that goes out at once
s = rep(s, '''  msg.lastSendUs = now_us();
  msg.attempts = 1;
  txQueue_.push_back(std::move(msg));''', '''  msg.lastSendUs = now_us();
  msg.attempts = 1;
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
  msg.firstSendUs = txQueue_.empty() ? msg.lastSendUs : 0;  // a queued one starts when it becomes the head
#endif
  txQueue_.push_back(std::move(msg));''')
# OnPacket: ACKs after close, ACKs on another stream
s = rep(s, '''  std::lock_guard<std::mutex> lock(mu_);
  if (closed_.load(std::memory_order_relaxed)) return true;
  if (kind == static_cast<uint16_t>(UdpPacketKind::ControlData)) {''', '''  std::lock_guard<std::mutex> lock(mu_);
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
  if (closed_.load(std::memory_order_relaxed) && kind != static_cast<uint16_t>(UdpPacketKind::ControlData)) {
    ++diag_.acksAfterClose;
  }
#endif
  if (closed_.load(std::memory_order_relaxed)) return true;
  if (kind == static_cast<uint16_t>(UdpPacketKind::ControlData)) {''')
s = rep(s, '''    // Acks refer to the stream we transmit on.
    if (packet.streamId != txStreamId_) return true;''', '''#ifdef REMOTE60_CONTROL_CLOSE_DIAG
    ++diag_.acksSeen;
    diag_.lastSeenSeq = packet.messageSeq;
    diag_.lastSeenUs = now_us();
    if (packet.streamId != txStreamId_) ++diag_.acksWrongStream;
#endif
    // Acks refer to the stream we transmit on.
    if (packet.streamId != txStreamId_) return true;''')
# HandleAck: probe / stale / NACK / accepted
s = rep(s, '''    probeState_ = ControlProbeState::Alive;
    probeDecidedUs_ = now_us();
    return;
  }
  if (txQueue_.empty() || txQueue_.front().seq != packet.messageSeq) return;
''', '''    probeState_ = ControlProbeState::Alive;
    probeDecidedUs_ = now_us();
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
    ++diag_.probeAcks;
#endif
    return;
  }
  if (txQueue_.empty() || txQueue_.front().seq != packet.messageSeq) {
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
    ++diag_.acksStale;
#endif
    return;
  }
''')
s = rep(s, '''    std::vector<uint16_t> missing(packet.missing, packet.missing + packet.missingCount);''', '''#ifdef REMOTE60_CONTROL_CLOSE_DIAG
    ++diag_.nacks;
#endif
    std::vector<uint16_t> missing(packet.missing, packet.missing + packet.missingCount);''')
s = rep(s, '''  lastAckedTxSeq_ = packet.messageSeq;
  txQueue_.pop_front();
  if (!txQueue_.empty()) {
    txQueue_.front().lastSendUs = now_us();
    txQueue_.front().attempts = 1;''', '''  lastAckedTxSeq_ = packet.messageSeq;
  txQueue_.pop_front();
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
  ++diag_.acksAccepted;
  diag_.lastAcceptedUs = now_us();
  if (!txQueue_.empty()) txQueue_.front().firstSendUs = diag_.lastAcceptedUs;
#endif
  if (!txQueue_.empty()) {
    txQueue_.front().lastSendUs = now_us();
    txQueue_.front().attempts = 1;''')
# Tick: the head at peer-lost
s = rep(s, '''      if (++head.attempts > timings_.maxAttempts) {
        auto expected = ControlCloseReason::None;''', '''      if (++head.attempts > timings_.maxAttempts) {
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
        diag_.lost = true;
        diag_.lostSeq = head.seq;
        diag_.lostAttempts = head.attempts;
        diag_.lostFrags = head.fragCount;
        diag_.lostQueue = txQueue_.size();
        diag_.lostFirstSendUs = head.firstSendUs;
        diag_.lostLastSendUs = head.lastSendUs;
        diag_.lostAtUs = now;
        diag_.lostType = HeaderType(head.payload);
#endif
        auto expected = ControlCloseReason::None;''')
# Reset: a new session starts its own record
s = rep(s, '''  // HandleData answers it with an ack while dropping the data -- alive on the wire, deaf above.
  rxDeliveredSeq_ = 0;
  closeReason_.store(ControlCloseReason::None, std::memory_order_relaxed);''', '''  // HandleData answers it with an ack while dropping the data -- alive on the wire, deaf above.
  rxDeliveredSeq_ = 0;
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
  diag_ = CloseDiag{};
#endif
  closeReason_.store(ControlCloseReason::None, std::memory_order_relaxed);''')
# the line
s = rep(s, '''const char* to_string(ControlCloseReason reason) {''', '''#ifdef REMOTE60_CONTROL_CLOSE_DIAG
int UdpControlChannel::HeaderType(const std::vector<uint8_t>& payload) {
  if (payload.size() < sizeof(MessageHeader)) return -1;
  MessageHeader h{};
  std::memcpy(&h, payload.data(), sizeof(h));  // the header only: magic, type, size
  return h.magic == kMagic ? static_cast<int>(h.type) : -1;
}

std::string UdpControlChannel::CloseDiagLine() const {
  std::lock_guard<std::mutex> lock(mu_);
  const uint64_t now = now_us();
  auto ago = [now](uint64_t t) { return t == 0 ? std::string("-") : std::to_string((now - t) / 1000); };
  std::string line = "diag{ack seen=" + std::to_string(diag_.acksSeen) +
                     " wrongStream=" + std::to_string(diag_.acksWrongStream) +
                     " accepted=" + std::to_string(diag_.acksAccepted) + " stale=" + std::to_string(diag_.acksStale) +
                     " probe=" + std::to_string(diag_.probeAcks) + " nack=" + std::to_string(diag_.nacks) +
                     " afterClose=" + std::to_string(diag_.acksAfterClose) +
                     " lastSeenSeq=" + std::to_string(diag_.lastSeenSeq) + " lastSeenAgoMs=" + ago(diag_.lastSeenUs) +
                     " lastAcceptedAgoMs=" + ago(diag_.lastAcceptedUs) +
                     " lastAckedTxSeq=" + std::to_string(lastAckedTxSeq_) + "}";
  if (txQueue_.empty()) {
    line += " head{none}";
  } else {
    const Outbound& h = txQueue_.front();
    line += " head{seq=" + std::to_string(h.seq) + " type=" + std::to_string(HeaderType(h.payload)) +
            " attempts=" + std::to_string(h.attempts) + " frags=" + std::to_string(h.fragCount) +
            " queue=" + std::to_string(txQueue_.size()) + " firstSendAgoMs=" + ago(h.firstSendUs) +
            " lastSendAgoMs=" + ago(h.lastSendUs) + "}";
  }
  if (diag_.lost) {
    line += " lost{seq=" + std::to_string(diag_.lostSeq) + " type=" + std::to_string(diag_.lostType) +
            " attempts=" + std::to_string(diag_.lostAttempts) + " frags=" + std::to_string(diag_.lostFrags) +
            " queue=" + std::to_string(diag_.lostQueue) + " waitedMs=" +
            (diag_.lostFirstSendUs ? std::to_string((diag_.lostAtUs - diag_.lostFirstSendUs) / 1000) : std::string("-")) +
            " agoMs=" + ago(diag_.lostAtUs) + "}";
  } else {
    line += " lost{none}";
  }
  line += " rx{deliveredSeq=" + std::to_string(rxDeliveredSeq_) + " pending=" + std::to_string(rxPending_.size()) +
          " ready=" + std::to_string(rxReady_.size()) + "}";
  return line;
}
#endif

const char* to_string(ControlCloseReason reason) {''')
save(p, s, crlf)

# ---- the host's session-ended line
p = SRC + r'\src\host_startup_control.cpp'
s, crlf = load(p)
s = rep(s, '''                  << " reason=" << remote60::native_poc::to_string(clientSession.udpControlChannel.CloseReason())
                  << "\\n";''', '''                  << " reason=" << remote60::native_poc::to_string(clientSession.udpControlChannel.CloseReason())
#ifdef REMOTE60_CONTROL_CLOSE_DIAG
                  << " " << clientSession.udpControlChannel.CloseDiagLine()  // old-ref r1 (test build only)
#endif
                  << "\\n";''')
save(p, s, crlf)

# ---- CMake: the test host gets the define; the unit test
p = SRC + r'\CMakeLists.txt'
s, crlf = load(p)
s = rep(s, '''target_compile_definitions(remote60_native_video_host_pinned_select PRIVATE
  REMOTE60_PINNED_SELECT_TEST_SEAM=1''', '''target_compile_definitions(remote60_native_video_host_pinned_select PRIVATE
  REMOTE60_PINNED_SELECT_TEST_SEAM=1
  REMOTE60_CONTROL_CLOSE_DIAG=1''')
s = rep(s, '''add_executable(remote60_recovery_process_test src/recovery_process_test.cpp src/udp_control_channel.cpp)''',
        '''add_executable(remote60_recovery_process_test src/recovery_process_test.cpp src/udp_control_channel.cpp)
# old-ref r1: the control channel's test-only ACK / head record (REMOTE60_CONTROL_CLOSE_DIAG) -- peer-lost and normal
add_executable(remote60_udp_control_close_diag_test src/udp_control_close_diag_test.cpp src/udp_control_channel.cpp)
target_include_directories(remote60_udp_control_close_diag_test PRIVATE src)
target_compile_definitions(remote60_udp_control_close_diag_test PRIVATE REMOTE60_CONTROL_CLOSE_DIAG=1)
target_link_libraries(remote60_udp_control_close_diag_test PRIVATE ws2_32)''')
save(p, s, crlf)
print('ok', WT)
