ROOT = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b, n=1):
    assert s.count(a) == n, (a[:90], s.count(a))
    return s.replace(a, b)


# ------------------------------------------------------------ header
p = ROOT + r'\native_video_client_session.hpp'
s, crlf = load(p)
s = rep(s, '''  virtual void OnDecoderKeyRequestSent(const DecoderKeyRequest& /* request */, uint64_t /* nowUs */) {}
''', '''  virtual void OnDecoderKeyRequestSent(const DecoderKeyRequest& /* request */, uint64_t /* nowUs */) {}
  // C r2 (Codex codex_apk_inbox_c_src_r1.md §8 (a)): the session's asks for the owed IDR `owedToken` are used up (8 taken
  // by the limiter -- queued, not "8 on the wire") and the last of them has left or been dropped. The sink records the
  // duty as covered-and-spent: no recovery asks of its own on top, a final answer window, then exhausted -- a late IDR
  // still decodes and ends it. A stale token (the duty re-armed or paid since) changes nothing.
  virtual void OnDecoderKeyOwedExhausted(uint64_t /* owedToken */, uint64_t /* nowUs */) {}
''')
s = rep(s, '''  void ClearPendingDecoderKeyframe() {
    decoderRekeyPending_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lk(keyLedgerMu_);
    keyHeld_.Clear();
  }
''', '''  void ClearPendingDecoderKeyframe() { ClearKeyLedger(); }  // C r2: the ledger and the limiter's pending together
''')
s = rep(s, '''  std::mutex keyLedgerMu_;
  DecoderKeyRequest keyHeld_;
  DecoderKeyRequest keyAccepted_;
  std::atomic<uint64_t> keyDroppedStale_{0};
  void NoteKeyRequest(const DecoderKeyRequest& request, bool queued);
  void ClearKeyLedger();
''', '''  // C r2 (Codex N2): the limiter's pending flag and the accepted descriptor are published together (RequestKey: the
  // limiter's Request and the merge in ONE section of keyLedgerMu_) and consumed together (the scheduler's NextAction
  // and the take, in one section). A KeyframeRequest action with no descriptor was cancelled (ClearKeyLedger consumes
  // the pending flag too) -- it is not sent. keyHeldVersion_ moves with every change of keyHeld_: the held check returns
  // only its own snapshot. Lock order: parkMu_ before keyLedgerMu_; the limiter's own lock inside; no sink call and no
  // network under it.
  std::mutex keyLedgerMu_;
  DecoderKeyRequest keyHeld_;
  DecoderKeyRequest keyAccepted_;
  uint64_t keyHeldVersion_ = 0;
  std::atomic<uint64_t> keyDroppedStale_{0};
  std::atomic<uint64_t> keyDroppedCancelled_{0};
  bool RequestKey(const DecoderKeyRequest& request, uint64_t nowUs);
  void ClearKeyLedger();
  // C r2 (§8 (a)): the owed duty whose 8th ask was taken; told to the sink once that ask has left or been dropped.
  uint64_t owedExhaustToken_ = 0;
''')
s = rep(s, '''  // TEST BUILD ONLY (apk-inbox C): the keyframe limiter's parameters, so a test can make it refuse (a HELD request).''',
        '''  // ("publish", C r2: INSIDE the request's publish section, between the limiter taking it and its descriptor -- the
  // hook may only call the sink, never this session.)
  // TEST BUILD ONLY (apk-inbox C): the keyframe limiter's parameters, so a test can make it refuse (a HELD request).''')
s = rep(s, '''  uint64_t KeyRequestsDroppedStale() const { return keyDroppedStale_.load(std::memory_order_relaxed); }
''', '''  uint64_t KeyRequestsDroppedStale() const { return keyDroppedStale_.load(std::memory_order_relaxed); }
  // C r2: KeyframeRequest actions whose descriptor was cancelled (a new pick / reset) -- not sent.
  uint64_t KeyRequestsDroppedCancelled() const { return keyDroppedCancelled_.load(std::memory_order_relaxed); }
''')
save(p, s, crlf)

# ------------------------------------------------------------ session
p = ROOT + r'\native_video_client_session.cpp'
s, crlf = load(p)

# owed: through RequestKey; the 8th taken -> the exhaustion to tell
s = rep(s, '''      if (owed != owedSelection_) {
        owedSelection_ = owed;
        owedAttempts_ = 0;
        owedNextUs_ = 0;
      }''', '''      if (owed != owedSelection_) {
        owedSelection_ = owed;
        owedAttempts_ = 0;
        owedNextUs_ = 0;
        owedExhaustToken_ = 0;  // C r2: another duty (or none)
      }''')
s = rep(s, '''        const bool queued = keyframeRequests_.Request(2, loopNowUs).queued;
        REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Owed, queued));  // apk-decoder-inbox r1
        if (queued) {
          DecoderKeyRequest owedReq;  // apk-inbox C: rides with whatever else is pending (one send, both causes)
          owedReq.causes = kKeyCauseOwed;
          owedReq.owedToken = owed;
          NoteKeyRequest(owedReq, true);
          ++owedAttempts_;''', '''        DecoderKeyRequest owedReq;  // apk-inbox C: rides with whatever else is pending (one send, both causes)
        owedReq.causes = kKeyCauseOwed;
        owedReq.owedToken = owed;
        const bool queued = RequestKey(owedReq, loopNowUs);
        REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Owed, queued));  // apk-decoder-inbox r1
        if (queued) {
          ++owedAttempts_;
          if (owedAttempts_ == kOwedKeyMaxAttempts) owedExhaustToken_ = owed;  // C r2 §8: told once that ask is done''')
# the held check: its own snapshot only; the limiter and the merge in one section
s = rep(s, '''      DecoderKeyRequest held;
      {
        std::lock_guard<std::mutex> lk(keyLedgerMu_);
        held = keyHeld_;
      }
#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
      if (!held.empty()) RunTestKeyHook("held");
#endif
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
      }''', '''      // C r2 (Codex N2): the snapshot and its version; the sink is asked with no lock held; then, under the lock, only
      // that same snapshot is acted on -- a request held meanwhile (version moved) stays for the next pass, whole.
      DecoderKeyRequest held;
      uint64_t heldVersion = 0;
      {
        std::lock_guard<std::mutex> lk(keyLedgerMu_);
        held = keyHeld_;
        heldVersion = keyHeldVersion_;
      }
#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
      if (!held.empty()) RunTestKeyHook("held");
#endif
      uint32_t valid = held.causes;
      if (!held.empty() && encodedFrameSink_) {
        REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));
        valid = encodedFrameSink_->DecoderKeyRequestValidCauses(held);
        REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkPass, now_us()));
      }
      bool droppedNow = false, queuedNow = false, queuedTried = false;
      {
        std::lock_guard<std::mutex> lk(keyLedgerMu_);
        if (keyHeldVersion_ == heldVersion) {
          if (held.empty() || valid == 0) {
            droppedNow = !held.empty();
            keyHeld_.Clear();
            ++keyHeldVersion_;
            decoderRekeyPending_.store(false, std::memory_order_release);
          } else {
            queuedTried = true;
            queuedNow = keyframeRequests_.Request(2, loopNowUs).queued;
            if (queuedNow) {
              held.causes = valid;
              keyAccepted_.Merge(held);
              keyHeld_.Clear();
              ++keyHeldVersion_;
              decoderRekeyPending_.store(false, std::memory_order_release);
            }
          }
        }
      }
      if (queuedTried) REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Held, queuedNow));  // apk-decoder-inbox r1
      if (droppedNow) {
        keyDroppedStale_.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[native-video-client-session] held keyframe request dropped: no cause still valid\\n");
      } else if (queuedNow) {
        std::fprintf(stderr, "[native-video-client-session] held keyframe request queued\\n");
      }''')
# the sink's own wants
s = rep(s, '''      if (wanted) {
        const bool queued = keyframeRequests_.Request(2, loopNowUs).queued;
        REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Held, queued));
        NoteKeyRequest(want, queued);
      }''', '''      if (wanted) {
        const bool queued = RequestKey(want, loopNowUs);
        REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Held, queued));
      }''')
# the scheduler: consume and take in one section
s = rep(s, '''    bool haveAction = false;
    {
      // r3 R1: the next action is taken and judged against the park in one step, under parkMu_ -- the lock
      // every request path, BeginPark and ResumeFromPark take. A resume cannot fall between the loop's read
      // of the park and its use: the request a resumed session makes is never dropped by the park before
      // it. (Local work only: no network under this lock.)
      std::lock_guard<std::mutex> lk(parkMu_);
      haveAction = controlScheduler_.NextAction(loopNowUs, metrics, &windowPanel_, &streamState_, &captureMode_,
                                                &keyframeRequests_, &runtimeTune_, &inputQueue_, &action,
                                                &desktopBackend_);''', '''    bool haveAction = false;
    DecoderKeyRequest sentKey;  // C r2: taken with its KeyframeRequest action, in the same section
    {
      // r3 R1: the next action is taken and judged against the park in one step, under parkMu_ -- the lock
      // every request path, BeginPark and ResumeFromPark take. A resume cannot fall between the loop's read
      // of the park and its use: the request a resumed session makes is never dropped by the park before
      // it. (Local work only: no network under this lock.)
      std::lock_guard<std::mutex> lk(parkMu_);
      {
        // C r2 (Codex N2): the limiter's pending flag is consumed (NextAction) and the accepted descriptor taken in ONE
        // section of keyLedgerMu_ -- the pair RequestKey publishes together. Local work only.
        std::lock_guard<std::mutex> lk2(keyLedgerMu_);
        haveAction = controlScheduler_.NextAction(loopNowUs, metrics, &windowPanel_, &streamState_, &captureMode_,
                                                  &keyframeRequests_, &runtimeTune_, &inputQueue_, &action,
                                                  &desktopBackend_);
        if (haveAction && action.kind == ControlOutboundActionKind::KeyframeRequest) {
          sentKey = keyAccepted_;
          keyAccepted_.Clear();
        }
      }''')
s = rep(s, '''      DecoderKeyRequest sentKey;
      if (action.kind == ControlOutboundActionKind::KeyframeRequest) {
        {
          std::lock_guard<std::mutex> lk(keyLedgerMu_);
          sentKey = keyAccepted_;
          keyAccepted_.Clear();
        }
#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
        if (!sentKey.empty()) RunTestKeyHook("accepted");
#endif
        if (!sentKey.empty() && encodedFrameSink_) {''', '''      const DecoderKeyRequest takenKey = sentKey;  // C r2: as taken (the owed exhaustion is judged on it)
      if (action.kind == ControlOutboundActionKind::KeyframeRequest) {
        if (sentKey.empty()) {
          // C r2 (Codex N2): a KeyframeRequest with no descriptor was cancelled (a new pick / reset cleared the ledger
          // between the limiter and here) -- never read as a legacy request, never sent.
          keyDroppedCancelled_.fetch_add(1, std::memory_order_relaxed);
          std::fprintf(stderr, "[native-video-client-session] keyframe request not sent: cancelled before the send\\n");
          didWork = true;
          continue;
        }
#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
        RunTestKeyHook("accepted");
#endif
        if (encodedFrameSink_) {''')
s = rep(s, '''          if (sentKey.empty()) {
            keyDroppedStale_.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[native-video-client-session] keyframe request not sent: no cause still valid\\n");
            didWork = true;
            continue;
          }''', '''          if (sentKey.empty()) {
            keyDroppedStale_.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[native-video-client-session] keyframe request not sent: no cause still valid\\n");
            if ((takenKey.causes & kKeyCauseOwed) != 0 && owedExhaustToken_ != 0 &&
                takenKey.owedToken == owedExhaustToken_) {
              encodedFrameSink_->OnDecoderKeyOwedExhausted(owedExhaustToken_, now_us());  // C r2 §8: the last, dropped
              owedExhaustToken_ = 0;
            }
            didWork = true;
            continue;
          }''')
s = rep(s, '''        encodedFrameSink_->OnDecoderKeyRequestSent(sentKey, now_us());  // the retry clock starts at the send''',
        '''        encodedFrameSink_->OnDecoderKeyRequestSent(sentKey, now_us());  // the retry clock starts at the send
        if ((takenKey.causes & kKeyCauseOwed) != 0 && owedExhaustToken_ != 0 && takenKey.owedToken == owedExhaustToken_) {
          encodedFrameSink_->OnDecoderKeyOwedExhausted(owedExhaustToken_, now_us());  // C r2 §8: the last, sent
          owedExhaustToken_ = 0;
        }''')
# the answer path, RX, the pipeline -> RequestKey
s = rep(s, '''              answerQueued = keyframeRequests_.Request(2, now_us()).queued;
              NoteKeyRequest(answerWant, answerQueued);''', '''              answerQueued = RequestKey(answerWant, now_us());''')
s = rep(s, '''      const bool frameQueued = keyframeRequests_.Request(2, now_us()).queued;
      REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Frame, frameQueued));  // apk-decoder-inbox r1
      NoteKeyRequest(frameWant, frameQueued);  // apk-inbox C: held or accepted, with its causes''', '''      const bool frameQueued = RequestKey(frameWant, now_us());  // C r2: published with its causes, in one section
      REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Frame, frameQueued));  // apk-decoder-inbox r1''')
s = rep(s, '''    const bool pipeQueued = keyframeRequests_.Request(2, now_us()).queued;
    REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Pipeline, pipeQueued));  // apk-decoder-inbox r1
    if (pipeQueued) {
      DecoderKeyRequest pipe;  // apk-inbox C: rides with a pending sink request, never judged stale
      pipe.causes = kKeyCausePipeline;
      NoteKeyRequest(pipe, true);
    }''', '''    DecoderKeyRequest pipe;  // apk-inbox C: rides with a pending sink request, never judged stale
    pipe.causes = kKeyCausePipeline;
    const bool pipeQueued = RequestKey(pipe, now_us());
    REMOTE60_LT(livenessTrace_.KeyReq(LtKeyOrigin::Pipeline, pipeQueued));  // apk-decoder-inbox r1''')
# the helpers
s = rep(s, '''// apk-inbox C (design r3 §4 D8): a key request the limiter took (accepted: it goes with the next KeyframeRequest
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
}''', '''// apk-inbox C (design r3 §4 D8) / C r2 (Codex N2): one key request through the limiter and into the ledger in ONE
// section -- taken (accepted: it goes with the next KeyframeRequest action) or refused (held: the control loop asks the
// limiter again). Coalesced with what is already there. A request with no cause is not one: nothing is asked.
bool ClientSessionController::RequestKey(const DecoderKeyRequest& request, uint64_t nowUs) {
  if (request.empty()) return false;
  std::lock_guard<std::mutex> lk(keyLedgerMu_);
  const bool queued = keyframeRequests_.Request(2, nowUs).queued;
#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
  RunTestKeyHook("publish");  // inside the section: a test may change the SINK here (never this session)
#endif
  if (queued) {
    keyAccepted_.Merge(request);
  } else {
    keyHeld_.Merge(request);
    ++keyHeldVersion_;
    decoderRekeyPending_.store(true, std::memory_order_release);
  }
  return queued;
}

// C r2 (Codex N2): a new pick / reset -- the descriptors AND the limiter's pending flag go together, so no
// KeyframeRequest action is left to be read as a request without a cause.
void ClientSessionController::ClearKeyLedger() {
  std::lock_guard<std::mutex> lk(keyLedgerMu_);
  keyHeld_.Clear();
  keyAccepted_.Clear();
  ++keyHeldVersion_;
  (void)keyframeRequests_.ConsumePending(nullptr);
  decoderRekeyPending_.store(false, std::memory_order_release);
}''')
save(p, s, crlf)
assert 'keyframeRequests_.Request(' in s
print('ok', s.count('keyframeRequests_.Request('))
