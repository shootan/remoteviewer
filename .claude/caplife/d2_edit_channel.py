S = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/'
CR = chr(13)


def edit(path, reps):
    s = open(path, encoding='utf-8', newline='').read()
    crlf = (CR + '\n') in s
    if crlf:
        s = s.replace(CR + '\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (path, a[:80], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', CR + '\n')
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print('ok', path)


edit(S + 'udp_control_channel.hpp', [
    ('''    uint64_t pendingMessages = 0;
    uint64_t inboundBytes = 0;
  };''', '''    uint64_t pendingMessages = 0;
    uint64_t inboundBytes = 0;
    // apk-decoder-liveness r3 (Codex D2): the reliable head as it stands -- read by the test-build trace only. The
    // counters below are kept only with GNLINK_LIVENESS_TRACE (0 otherwise); the head fields are read from the queue.
    uint64_t txQueued = 0;          // messages not yet acknowledged (the head is in flight)
    uint32_t headSeq = 0;           // 0: none
    uint32_t headAttempts = 0;
    uint64_t headFirstSendUs = 0;   // when the head was first sent (trace builds)
    uint64_t headLastSendUs = 0;
    uint32_t lastAckedSeq = 0;
    uint64_t lastAckRxUs = 0;       // the last ACK that cleared the head (trace builds)
    uint64_t acksAccepted = 0;      // ACKs that cleared the head (trace builds)
    uint64_t acksIgnored = 0;       // ACK / NACK datagrams for another seq (trace builds)
    uint64_t nacksReceived = 0;     // NACKs for the head (trace builds)
  };'''),
    ('''  struct Outbound {
    uint32_t seq = 0;
    std::vector<uint8_t> payload;
    uint16_t fragCount = 0;
    uint64_t lastSendUs = 0;
    uint32_t attempts = 0;
  };''', '''  struct Outbound {
    uint32_t seq = 0;
    std::vector<uint8_t> payload;
    uint16_t fragCount = 0;
    uint64_t lastSendUs = 0;
    uint32_t attempts = 0;
    uint64_t firstSendUs = 0;  // apk-decoder-liveness r3 trace (set only with GNLINK_LIVENESS_TRACE)
  };'''),
])

edit(S + 'udp_control_channel.cpp', [
    ('''#include "udp_control_channel.hpp"
''', '''#include "udp_control_channel.hpp"
#include "client_liveness_trace.hpp"
'''),
    ('''  Stats result = stats_;
  result.pendingMessages = rxPending_.size();''', '''  Stats result = stats_;
  result.txQueued = txQueue_.size();
  if (!txQueue_.empty()) {
    result.headSeq = txQueue_.front().seq;
    result.headAttempts = txQueue_.front().attempts;
    result.headFirstSendUs = txQueue_.front().firstSendUs;
    result.headLastSendUs = txQueue_.front().lastSendUs;
  }
  result.lastAckedSeq = lastAckedTxSeq_;
  result.pendingMessages = rxPending_.size();'''),
    ('''  msg.lastSendUs = now_us();
  msg.attempts = 1;
  txQueue_.push_back(std::move(msg));''', '''  msg.lastSendUs = now_us();
  msg.attempts = 1;
  REMOTE60_LT(if (txQueue_.empty()) msg.firstSendUs = msg.lastSendUs);
  txQueue_.push_back(std::move(msg));'''),
    ('''  if (txQueue_.empty() || txQueue_.front().seq != packet.messageSeq) return;

  if (packet.kind == static_cast<uint16_t>(UdpPacketKind::ControlNack)) {''', '''  if (txQueue_.empty() || txQueue_.front().seq != packet.messageSeq) {
    REMOTE60_LT(++stats_.acksIgnored);
    return;
  }

  if (packet.kind == static_cast<uint16_t>(UdpPacketKind::ControlNack)) {
    REMOTE60_LT(++stats_.nacksReceived);'''),
    ('''  lastAckedTxSeq_ = packet.messageSeq;
  txQueue_.pop_front();
  if (!txQueue_.empty()) {
    txQueue_.front().lastSendUs = now_us();
    txQueue_.front().attempts = 1;''', '''  lastAckedTxSeq_ = packet.messageSeq;
  REMOTE60_LT({
    ++stats_.acksAccepted;
    stats_.lastAckRxUs = now_us();
  });
  txQueue_.pop_front();
  if (!txQueue_.empty()) {
    txQueue_.front().lastSendUs = now_us();
    txQueue_.front().attempts = 1;
    REMOTE60_LT(txQueue_.front().firstSendUs = txQueue_.front().lastSendUs);'''),
])

edit(S + 'native_video_client_session.cpp', [
    ('''    char ch[200];
    std::snprintf(ch, sizeof(ch), " | ch sent=%llu recv=%llu retx=%llu nack=%llu pend=%llu closed=%d reason=%s",
                  static_cast<unsigned long long>(st.messagesSent), static_cast<unsigned long long>(st.messagesReceived),
                  static_cast<unsigned long long>(st.fragmentRetransmits), static_cast<unsigned long long>(st.nacksSent),
                  static_cast<unsigned long long>(st.pendingMessages), udpControl_.IsClosed() ? 1 : 0,
                  to_string(udpControl_.CloseReason()));
    line += ch;''', '''    const uint64_t now = now_us();
    auto age = [now](uint64_t t) -> long long {
      return t == 0 ? -1 : static_cast<long long>((now >= t ? now - t : 0) / 1000);
    };
    char ch[400];
    std::snprintf(ch, sizeof(ch),
                  " | ch sent=%llu recv=%llu retx=%llu nack=%llu pend=%llu txq=%llu head=%u/%u first=%lldms last=%lldms "
                  "acked=%u ack=%lldms ackOk=%llu ackIgn=%llu nackRx=%llu closed=%d reason=%s",
                  static_cast<unsigned long long>(st.messagesSent), static_cast<unsigned long long>(st.messagesReceived),
                  static_cast<unsigned long long>(st.fragmentRetransmits), static_cast<unsigned long long>(st.nacksSent),
                  static_cast<unsigned long long>(st.pendingMessages), static_cast<unsigned long long>(st.txQueued),
                  st.headSeq, st.headAttempts, age(st.headFirstSendUs), age(st.headLastSendUs), st.lastAckedSeq,
                  age(st.lastAckRxUs), static_cast<unsigned long long>(st.acksAccepted),
                  static_cast<unsigned long long>(st.acksIgnored), static_cast<unsigned long long>(st.nacksReceived),
                  udpControl_.IsClosed() ? 1 : 0, to_string(udpControl_.CloseReason()));
    line += ch;'''),
])
