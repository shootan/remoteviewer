import io

ROOT = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src'


def load(p):
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    return raw.decode('utf-8').replace('\r\n', '\n'), crlf


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b, n=1):
    assert s.count(a) == n, (a[:80], s.count(a))
    return s.replace(a, b)


# ---------------------------------------------------------------- header
p = ROOT + r'\native_video_client_session.hpp'
s, crlf = load(p)
s = rep(s, '#include "client_host_role_gate.hpp"\n', '#include "client_host_role_gate.hpp"\n#include "decoder_key_request.hpp"\n')
s = rep(s, '''  virtual bool ConsumeDecoderKeyframeRequest() { return false; }
''', '''  virtual bool ConsumeDecoderKeyframeRequest() { return false; }
  // apk-inbox C (design r3 §3-§4 D8): the same want with its descriptor -- why it is asked (a cause set) and the sink
  // state it was asked in. The session keeps the descriptor with the request while it is HELD (the limiter refused)
  // or ACCEPTED (pending a send), asks DecoderKeyRequestValidCauses before it goes on at either point and does not send
  // a request none of whose causes are valid any more; OnDecoderKeyRequestSent when the control exchange for it
  // started (the sink's retry clock runs from real sends, not from wants). The defaults keep a sink that only answers
  // ConsumeDecoderKeyframeRequest exactly as before (a legacy cause, always valid).
  virtual bool TakeDecoderKeyRequest(DecoderKeyRequest* out) {
    if (!ConsumeDecoderKeyframeRequest()) return false;
    if (out) {
      out->Clear();
      out->causes = kKeyCauseLegacy;
    }
    return true;
  }
  virtual uint32_t DecoderKeyRequestValidCauses(const DecoderKeyRequest& request) { return request.causes; }
  virtual void OnDecoderKeyRequestSent(const DecoderKeyRequest& /* request */, uint64_t /* nowUs */) {}
''')
s = rep(s, '''  void ClearPendingDecoderKeyframe() { decoderRekeyPending_.store(false, std::memory_order_release); }
''', '''  void ClearPendingDecoderKeyframe() {
    decoderRekeyPending_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lk(keyLedgerMu_);
    keyHeld_.Clear();
  }
  // apk-inbox C (D8): requests not sent because no cause of theirs was valid any more (held / at the send point).
  uint64_t KeyRequestsDroppedStale() const { return keyDroppedStale_.load(std::memory_order_relaxed); }
''')
s = rep(s, '''  std::atomic<bool> decoderRekeyPending_{false};
''', '''  std::atomic<bool> decoderRekeyPending_{false};
  // apk-inbox C (design r3 §4 D8): the descriptors of the key request HELD (refused by the limiter, decoderRekeyPending_)
  // and of the one ACCEPTED (the limiter took it; the scheduler's next KeyframeRequest action sends it). Coalesced
  // (DecoderKeyRequest::Merge); under keyLedgerMu_ (RX and control threads), never held across a sink call or a send.
  std::mutex keyLedgerMu_;
  DecoderKeyRequest keyHeld_;
  DecoderKeyRequest keyAccepted_;
  std::atomic<uint64_t> keyDroppedStale_{0};
  void NoteKeyRequest(const DecoderKeyRequest& request, bool queued);
  void ClearKeyLedger();
''')
save(p, s, crlf)

# ---------------------------------------------------------------- session
p = ROOT + r'\native_video_client_session.cpp'
s, crlf = load(p)

# 1) the resets that cleared decoderRekeyPending_ also clear the ledger
s = rep(s, '''  decoderRekeyPending_.store(false, std::memory_order_release);  // a new pick: the old one's IDR is moot
''', '''  decoderRekeyPending_.store(false, std::memory_order_release);  // a new pick: the old one's IDR is moot
  ClearKeyLedger();  // apk-inbox C: and the descriptors with it (the sink judges a later one by its own new epoch)
''')
s = rep(s, '''  keyframeRequests_.Reset();
  decoderRekeyPending_.store(false, std::memory_order_release);
  owedSelection_ = 0;
''', '''  keyframeRequests_.Reset();
  decoderRekeyPending_.store(false, std::memory_order_release);
  ClearKeyLedger();  // apk-inbox C
  owedSelection_ = 0;
''')

s = rep(s, '''  if (classify_select_target(windowId).kind != SelectTargetKind::Window) return false;
  decoderRekeyPending_.store(false, std::memory_order_release);
''', '''  if (classify_select_target(windowId).kind != SelectTargetKind::Window) return false;
  decoderRekeyPending_.store(false, std::memory_order_release);
  ClearKeyLedger();  // apk-inbox C
''')
s = rep(s, '''  windowPanel_.Reset();
  decoderRekeyPending_.store(false, std::memory_order_release);
  owedSelection_ = 0;
''', '''  windowPanel_.Reset();
  decoderRekeyPending_.store(false, std::memory_order_release);
  ClearKeyLedger();  // apk-inbox C
  owedSelection_ = 0;
''')

# 2) owed path: the queued ask is recorded as accepted with its token
s = rep(s, '''        const bool queued = keyframeRequests_.Request(2, loopNowUs).queued;
        REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Owed, queued));  // apk-decoder-inbox r1
        if (queued) {
''', '''        const bool queued = keyframeRequests_.Request(2, loopNowUs).queued;
        REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Owed, queued));  // apk-decoder-inbox r1
        if (queued) {
          DecoderKeyRequest owedReq;  // apk-inbox C: rides with whatever else is pending (one send, both causes)
          owedReq.causes = kKeyCauseOwed;
          owedReq.owedToken = owed;
          NoteKeyRequest(owedReq, true);
''')

# 3) the held path: judged first, then the limiter; plus the sink's own due wants (its retries need no frame)
s = rep(s, '''    if (!parking && decoderRekeyPending_.load(std::memory_order_acquire)) {
      const bool queued = keyframeRequests_.Request(2, loopNowUs).queued;
      REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Held, queued));  // apk-decoder-inbox r1
      if (queued) {
        decoderRekeyPending_.store(false, std::memory_order_release);
        std::fprintf(stderr, "[native-video-client-session] held keyframe request queued\\n");
      }
    }
''', '''    if (!parking && decoderRekeyPending_.load(std::memory_order_acquire)) {
      // apk-inbox C (D8, the HELD point): before the held request goes to the limiter again, the sink says which of its
      // causes still hold. None: it is dropped here (a newer pick / binding / committed IDR made it moot).
      DecoderKeyRequest held;
      {
        std::lock_guard<std::mutex> lk(keyLedgerMu_);
        held = keyHeld_;
      }
      uint32_t valid = held.causes;
      if (!held.empty() && encodedFrameSink_) {
        REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));
        valid = encodedFrameSink_->DecoderKeyRequestValidCauses(held);
        REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkPass, now_us()));
      }
      if (!held.empty() && valid == 0) {
        {
          std::lock_guard<std::mutex> lk(keyLedgerMu_);
          keyHeld_.Clear();
        }
        decoderRekeyPending_.store(false, std::memory_order_release);
        keyDroppedStale_.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[native-video-client-session] held keyframe request dropped: no cause still valid\\n");
      } else {
        const bool queued = keyframeRequests_.Request(2, loopNowUs).queued;
        REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Held, queued));  // apk-decoder-inbox r1
        if (queued) {
          held.causes = valid;
          {
            std::lock_guard<std::mutex> lk(keyLedgerMu_);
            keyHeld_.Clear();
            if (!held.empty()) keyAccepted_.Merge(held);
          }
          decoderRekeyPending_.store(false, std::memory_order_release);
          std::fprintf(stderr, "[native-video-client-session] held keyframe request queued\\n");
        }
      }
    }
    // apk-inbox C: the sink's own wants on every pass -- a recovery retry falls due on the clock, not on a frame (the
    // tail gap refuses the deltas that would otherwise have carried the ask).
    if (!parking && encodedFrameSink_) {
      DecoderKeyRequest want;
      REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));
      const bool wanted = encodedFrameSink_->TakeDecoderKeyRequest(&want);
      REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkPass, now_us()));
      if (wanted) {
        const bool queued = keyframeRequests_.Request(2, loopNowUs).queued;
        REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Held, queued));
        NoteKeyRequest(want, queued);
      }
    }
''')

# 4) the send point: an accepted request judged before it leaves; the sink told when it left
s = rep(s, '''      TcpControlResponse response{};
      REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkWrite, now_us()));
      const bool exchanged = execute_control_action(*controlLink, action, &response);
''', '''      // apk-inbox C (D8, the ACCEPTED point): a KeyframeRequest action carries the descriptor the limiter took; the
      // sink is asked (a short query, no lock held here) which causes still hold, right before the exchange starts.
      // None: not sent (the limiter's token is spent; nothing is queued again for it). Legacy / pipeline causes and a
      // request with no descriptor go as before.
      DecoderKeyRequest sentKey;
      if (action.kind == ControlOutboundActionKind::KeyframeRequest) {
        {
          std::lock_guard<std::mutex> lk(keyLedgerMu_);
          sentKey = keyAccepted_;
          keyAccepted_.Clear();
        }
        if (!sentKey.empty() && encodedFrameSink_) {
          REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));
          sentKey.causes = encodedFrameSink_->DecoderKeyRequestValidCauses(sentKey);
          REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkPass, now_us()));
          if (sentKey.empty()) {
            keyDroppedStale_.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[native-video-client-session] keyframe request not sent: no cause still valid\\n");
            didWork = true;
            continue;
          }
        }
      }
      TcpControlResponse response{};
      REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkWrite, now_us()));
      const bool exchanged = execute_control_action(*controlLink, action, &response);
      if (exchanged && action.kind == ControlOutboundActionKind::KeyframeRequest && !sentKey.empty() &&
          encodedFrameSink_) {
        encodedFrameSink_->OnDecoderKeyRequestSent(sentKey, now_us());  // the retry clock starts at the send
      }
''')

# 5) after a select answer: the want with its descriptor
s = rep(s, '''            if (encodedFrameSink_->ConsumeDecoderKeyframeRequest()) {
              answerQueued = keyframeRequests_.Request(2, now_us()).queued;
''', '''            DecoderKeyRequest answerWant;
            if (encodedFrameSink_->TakeDecoderKeyRequest(&answerWant)) {
              answerQueued = keyframeRequests_.Request(2, now_us()).queued;
              NoteKeyRequest(answerWant, answerQueued);
''')

# 6) the receive thread: the want with its descriptor
s = rep(s, '''    if (currentSink->ConsumeDecoderKeyframeRequest()) {
      // r3 M1: refused by the limiter is not forgotten -- the control loop retries it.
      const bool frameQueued = keyframeRequests_.Request(2, now_us()).queued;
      REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Frame, frameQueued));  // apk-decoder-inbox r1
      if (!frameQueued) {
        decoderRekeyPending_.store(true, std::memory_order_release);
''', '''    DecoderKeyRequest frameWant;
    if (currentSink->TakeDecoderKeyRequest(&frameWant)) {
      // r3 M1: refused by the limiter is not forgotten -- the control loop retries it.
      const bool frameQueued = keyframeRequests_.Request(2, now_us()).queued;
      REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Frame, frameQueued));  // apk-decoder-inbox r1
      NoteKeyRequest(frameWant, frameQueued);  // apk-inbox C: held or accepted, with its causes
      if (!frameQueued) {
        decoderRekeyPending_.store(true, std::memory_order_release);
''')

# 7) the assembler's own ask: accepted with the pipeline cause (no sink state)
s = rep(s, '''    const bool pipeQueued = keyframeRequests_.Request(2, now_us()).queued;
    REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Pipeline, pipeQueued));  // apk-decoder-inbox r1
    (void)pipeQueued;
''', '''    const bool pipeQueued = keyframeRequests_.Request(2, now_us()).queued;
    REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Pipeline, pipeQueued));  // apk-decoder-inbox r1
    if (pipeQueued) {
      DecoderKeyRequest pipe;  // apk-inbox C: rides with a pending sink request, never judged stale
      pipe.causes = kKeyCausePipeline;
      NoteKeyRequest(pipe, true);
    }
''')

# 8) the two helpers, before VideoReceiveMain
s = rep(s, '''void ClientSessionController::VideoReceiveMain() {''', '''// apk-inbox C (design r3 §4 D8): a key request the limiter took (accepted: it goes with the next KeyframeRequest
// action) or refused (held: the control loop asks the limiter again). Coalesced with what is already there.
void ClientSessionController::NoteKeyRequest(const DecoderKeyRequest& request, bool queued) {
  if (request.empty()) return;
  std::lock_guard<std::mutex> lk(keyLedgerMu_);
  if (queued) {
    keyAccepted_.Merge(request);
  } else {
    keyHeld_.Merge(request);
    decoderRekeyPending_.store(true, std::memory_order_release);
  }
}

void ClientSessionController::ClearKeyLedger() {
  std::lock_guard<std::mutex> lk(keyLedgerMu_);
  keyHeld_.Clear();
  keyAccepted_.Clear();
}

void ClientSessionController::VideoReceiveMain() {''')
save(p, s, crlf)
print('ok')
