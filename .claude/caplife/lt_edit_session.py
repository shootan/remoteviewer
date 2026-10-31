S = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/'


def edit(path, reps):
    s = open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (path, a[:80], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print('ok', path)


edit(S + 'client_liveness_trace.hpp', [
    ('#include <cstdio>\n#include <string>\n', '#include <cstdio>\n#include <functional>\n#include <string>\n'),
    ('    case LtPhase::WkWrite: return "write";', '    case LtPhase::WkWrite: return "exchange";'),
    ('  WkWrite = 12,      // writing an action to the control link',
     '  WkWrite = 12,      // an action written to the control link and its answer read (execute_control_action)'),
])

edit(S + 'native_video_client_session.hpp', [
    ('#include "clipboard_sync.hpp"\n', '#include "client_liveness_trace.hpp"\n#include "clipboard_sync.hpp"\n'),
    ('  ClientSessionSnapshot Snapshot() const;\n  WindowPanelSnapshot WindowPanelSnapshotCopy() const;\n',
     '  ClientSessionSnapshot Snapshot() const;\n  WindowPanelSnapshot WindowPanelSnapshotCopy() const;\n'
     '  // apk-decoder-liveness r2: the progress trace (client_liveness_trace.hpp) plus the control transport\'s counters, as\n'
     '  // one line. Empty unless the build records it (GNLINK_LIVENESS_TRACE). Lock-free but for the channel\'s own short lock.\n'
     '  std::string LivenessTraceLine();\n'),
    ('  UdpControlChannel udpControl_;\n  std::atomic<bool> controlOverUdp_{false};\n',
     '  UdpControlChannel udpControl_;\n  std::atomic<bool> controlOverUdp_{false};\n'
     '  ClientLivenessTrace livenessTrace_;  // apk-decoder-liveness r2: written only with GNLINK_LIVENESS_TRACE\n'),
])

edit(S + 'native_video_client_session.cpp', [
    # the line
    ('void ClientSessionController::RunVideoReceiveForTest(SocketHandle udpSocket, ClientEncodedFrameSink* sink) {',
     '''std::string ClientSessionController::LivenessTraceLine() {
#ifdef GNLINK_LIVENESS_TRACE
  std::string line = livenessTrace_.Format(now_us());
  if (controlOverUdp_.load(std::memory_order_acquire)) {
    const UdpControlChannel::Stats st = udpControl_.GetStats();
    char ch[200];
    std::snprintf(ch, sizeof(ch), " | ch sent=%llu recv=%llu retx=%llu nack=%llu pend=%llu closed=%d reason=%s",
                  static_cast<unsigned long long>(st.messagesSent), static_cast<unsigned long long>(st.messagesReceived),
                  static_cast<unsigned long long>(st.fragmentRetransmits), static_cast<unsigned long long>(st.nacksSent),
                  static_cast<unsigned long long>(st.pendingMessages), udpControl_.IsClosed() ? 1 : 0,
                  to_string(udpControl_.CloseReason()));
    line += ch;
  } else {
    line += " | ch tcp";
  }
  return line;
#else
  return std::string();
#endif
}

void ClientSessionController::RunVideoReceiveForTest(SocketHandle udpSocket, ClientEncodedFrameSink* sink) {'''),
    # worker: the pass
    ('''  while (!stopRequested_.load(std::memory_order_acquire)) {
    bool didWork = false;
    const uint64_t loopNowUs = now_us();
    ControlOutboundAction action{};''', '''  REMOTE60_LT(livenessTrace_.wkTid.store(liveness_tid(), std::memory_order_relaxed));
  while (!stopRequested_.load(std::memory_order_acquire)) {
    bool didWork = false;
    const uint64_t loopNowUs = now_us();
    REMOTE60_LT(livenessTrace_.wkLastPassUs.store(loopNowUs, std::memory_order_relaxed));
    REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkPass, loopNowUs));
    ControlOutboundAction action{};'''),
    # worker: owed
    ('''    if (!parking) {
      const uint64_t owed = encodedFrameSink_ ? encodedFrameSink_->KeyframeOwedFor() : 0;''', '''    if (!parking) {
      REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));
      const uint64_t owed = encodedFrameSink_ ? encodedFrameSink_->KeyframeOwedFor() : 0;
      REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkPass, now_us()));'''),
    ('''      if (owed != 0 && owedAttempts_ < kOwedKeyMaxAttempts && loopNowUs >= owedNextUs_ &&
          keyframeRequests_.Request(2, loopNowUs).queued) {
        ++owedAttempts_;
        owedNextUs_ = loopNowUs + kOwedKeyRetryUs;''', '''      if (owed != 0 && owedAttempts_ < kOwedKeyMaxAttempts && loopNowUs >= owedNextUs_ &&
          keyframeRequests_.Request(2, loopNowUs).queued) {
        ++owedAttempts_;
        owedNextUs_ = loopNowUs + kOwedKeyRetryUs;
        REMOTE60_LT(livenessTrace_.keyWantQueued.fetch_add(1, std::memory_order_relaxed));'''),
    ('''    if (!parking && decoderRekeyPending_.load(std::memory_order_acquire) &&
        keyframeRequests_.Request(2, loopNowUs).queued) {''', '''    REMOTE60_LT({
      livenessTrace_.owedToken.store(owedSelection_, std::memory_order_relaxed);
      livenessTrace_.owedAttempts.store(owedAttempts_, std::memory_order_relaxed);
      livenessTrace_.owedNextUs.store(owedNextUs_, std::memory_order_relaxed);
    });
    if (!parking && decoderRekeyPending_.load(std::memory_order_acquire) &&
        keyframeRequests_.Request(2, loopNowUs).queued) {'''),
    # worker: the exchange
    ('''      TcpControlResponse response{};
      if (!execute_control_action(*controlLink, action, &response)) {''', '''      TcpControlResponse response{};
      REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkWrite, now_us()));
      const bool exchanged = execute_control_action(*controlLink, action, &response);
      REMOTE60_LT({
        const uint64_t t = now_us();
        livenessTrace_.Wk(LtPhase::WkPass, t);
        if (action.kind == ControlOutboundActionKind::KeyframeRequest) {
          livenessTrace_.keySentSeq.store(action.keyframe.seq, std::memory_order_relaxed);
          livenessTrace_.keySentReason.store(action.keyframe.reason, std::memory_order_relaxed);
          livenessTrace_.keySentOk.store(exchanged ? 1u : 0u, std::memory_order_relaxed);
          livenessTrace_.keySentUs.store(t, std::memory_order_relaxed);
          if (controlOverUdp_.load(std::memory_order_acquire)) {
            livenessTrace_.keySentTransportMsg.store(udpControl_.GetStats().messagesSent, std::memory_order_relaxed);
          }
        }
      });
      if (!exchanged) {'''),
    # worker: the selection result hook
    ('''          if (encodedFrameSink_) {
            encodedFrameSink_->OnWindowSelectionControlResultFor(answer, action.selectionTag);''', '''          if (encodedFrameSink_) {
            REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));
            encodedFrameSink_->OnWindowSelectionControlResultFor(answer, action.selectionTag);'''),
    # RX: deliver + key consume
    ('''    sinkPayloadBytes_.fetch_add(frame.payload.size(), std::memory_order_relaxed);
    currentSink->OnEncodedH264Frame(std::move(frame));
    // The decoder may have had to discard what it was just handed. Ask for an IDR now
    // rather than letting every later delta decode against a reference that never arrived.
    if (currentSink->ConsumeDecoderKeyframeRequest()) {
      // r3 M1: refused by the limiter is not forgotten -- the control loop retries it.
      if (!keyframeRequests_.Request(2, now_us()).queued) decoderRekeyPending_.store(true, std::memory_order_release);
    }''', '''    sinkPayloadBytes_.fetch_add(frame.payload.size(), std::memory_order_relaxed);
    REMOTE60_LT(livenessTrace_.Rx(LtPhase::RxDeliver, now_us()));
#ifdef GNLINK_LIVENESS_TRACE
    const uint64_t deliverStartUs = now_us();
#endif
    currentSink->OnEncodedH264Frame(std::move(frame));
    REMOTE60_LT({
      const uint64_t t = now_us();
      livenessTrace_.MaxDeliver(t - deliverStartUs);
      livenessTrace_.Rx(LtPhase::RxKeyConsume, t);
    });
    // The decoder may have had to discard what it was just handed. Ask for an IDR now
    // rather than letting every later delta decode against a reference that never arrived.
    if (currentSink->ConsumeDecoderKeyframeRequest()) {
      // r3 M1: refused by the limiter is not forgotten -- the control loop retries it.
      if (!keyframeRequests_.Request(2, now_us()).queued) {
        decoderRekeyPending_.store(true, std::memory_order_release);
        REMOTE60_LT(livenessTrace_.keyWantRefused.fetch_add(1, std::memory_order_relaxed));
      } else {
        REMOTE60_LT(livenessTrace_.keyWantQueued.fetch_add(1, std::memory_order_relaxed));
      }
    }
    REMOTE60_LT(livenessTrace_.Rx(LtPhase::RxAssemble, now_us()));'''),
    # RX: the loop top (wait)
    ('''    int recvErr = 0;
    int n;
    if (udpRecvHookForTest_) {''', '''    REMOTE60_LT({
      if (livenessTrace_.rxTid.load(std::memory_order_relaxed) == 0) {
        livenessTrace_.rxTid.store(liveness_tid(), std::memory_order_relaxed);
      }
      livenessTrace_.Rx(LtPhase::RxWait, now_us());
    });
    int recvErr = 0;
    int n;
    if (udpRecvHookForTest_) {'''),
    # RX: the tick on a quiet socket
    ('''      if (controlOverUdp_.load(std::memory_order_acquire)) udpControl_.Tick();
      pipeline.OnTick(now_us());
      // r2 U2 anti-spin''', '''      REMOTE60_LT({
        const uint64_t t = now_us();
        livenessTrace_.Rx(LtPhase::RxTick, t);
        livenessTrace_.rxLastTickUs.store(t, std::memory_order_relaxed);
      });
      if (controlOverUdp_.load(std::memory_order_acquire)) udpControl_.Tick();
      pipeline.OnTick(now_us());
      // r2 U2 anti-spin'''),
    # RX: a datagram, control or video
    ('''    lastDatagramUs = now_us();
    lastDropLoopUs = 0;  // a real datagram breaks any immediate-drop streak

    if (controlOverUdp_.load(std::memory_order_acquire) &&
        udpControl_.OnPacket(datagram.data(), static_cast<size_t>(n))) {
      sessionBytesReceived_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);''', '''    lastDatagramUs = now_us();
    lastDropLoopUs = 0;  // a real datagram breaks any immediate-drop streak
    REMOTE60_LT({
      livenessTrace_.rxLastDatagramUs.store(lastDatagramUs, std::memory_order_relaxed);
      livenessTrace_.rxDatagrams.fetch_add(1, std::memory_order_relaxed);
      livenessTrace_.Rx(LtPhase::RxControl, lastDatagramUs);
    });

    if (controlOverUdp_.load(std::memory_order_acquire) &&
        udpControl_.OnPacket(datagram.data(), static_cast<size_t>(n))) {
      REMOTE60_LT({
        livenessTrace_.rxLastControlUs.store(now_us(), std::memory_order_relaxed);
        livenessTrace_.rxControlPackets.fetch_add(1, std::memory_order_relaxed);
      });
      sessionBytesReceived_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);'''),
    ('''    sessionBytesReceived_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
    const uint64_t malformedBefore = pipeline.stats().malformed;''', '''    sessionBytesReceived_.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
    REMOTE60_LT(livenessTrace_.Rx(LtPhase::RxAssemble, now_us()));
    const uint64_t malformedBefore = pipeline.stats().malformed;'''),
])
