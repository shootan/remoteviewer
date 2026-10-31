CPP = r'C:\work\gnlink-wt\apk-inbox-c\apps\android_direct_client\app\src\main\cpp'


def load(p):
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    return raw.decode('utf-8').replace('\r\n', '\n'), crlf


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b, n=1):
    assert s.count(a) == n, (a[:90], s.count(a))
    return s.replace(a, b)


# ============================================================== hpp
p = CPP + r'\android_video_decoder.hpp'
s, crlf = load(p)
s = rep(s, '#include "client_decode_snapshot.hpp"\n', '#include "client_decode_snapshot.hpp"\n#include "decoder_chain_inbox.hpp"\n')
s = rep(s, '''  bool ConsumeDecoderKeyframeRequest() override { return ConsumeDecoderKeyframeRequestAs(kAnyBinding); }
''', '''  bool ConsumeDecoderKeyframeRequest() override { return ConsumeDecoderKeyframeRequestAs(kAnyBinding); }
  // apk-inbox C (design r3 §3-§4): the want with its descriptor, its validity at the session's held / accepted points,
  // and the send that starts the recovery episode's retry clock.
  bool TakeDecoderKeyRequest(remote60::native_poc::DecoderKeyRequest* out) override {
    return TakeDecoderKeyRequestAs(kAnyBinding, out);
  }
  uint32_t DecoderKeyRequestValidCauses(const remote60::native_poc::DecoderKeyRequest& request) override {
    return DecoderKeyRequestValidCausesAs(kAnyBinding, request);
  }
  void OnDecoderKeyRequestSent(const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs) override {
    OnDecoderKeyRequestSentAs(kAnyBinding, request, nowUs);
  }
''')
s = rep(s, '''  bool ConsumeDecoderKeyframeRequestAs(uint64_t token);
''', '''  bool ConsumeDecoderKeyframeRequestAs(uint64_t token);
  bool TakeDecoderKeyRequestAs(uint64_t token, remote60::native_poc::DecoderKeyRequest* out);
  uint32_t DecoderKeyRequestValidCausesAs(uint64_t token, const remote60::native_poc::DecoderKeyRequest& request);
  void OnDecoderKeyRequestSentAs(uint64_t token, const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs);
''')
# the inbox members
s = rep(s, '''  static constexpr size_t kInboxMaxFrames = 8;
  static constexpr size_t kInboxMaxBytes = 4u << 20;
  std::deque<InboxFrame> inbox_;
  size_t inboxBytes_ = 0;
  uint64_t inboxDrops_ = 0;
  uint64_t inboxGapDrops_ = 0;
  bool inboxGap_ = false;
''', '''  static constexpr size_t kInboxMaxFrames = 8;
  static constexpr size_t kInboxMaxBytes = 4u << 20;
  // apk-inbox C (design r3 §1-§2, decoder_chain_inbox.hpp): the same bounds, chain-aware -- on overflow the earliest
  // usable IDR + its suffix is kept (the older chain skipped, no loss) or, with none, the loss is taken as one and the
  // tail gap refuses the deltas that follow it until a usable IDR. A loss opens (or continues) recovery_.
  struct InboxInfo {
    remote60::native_poc::ChainAuInfo operator()(const InboxFrame& f) const {
      return remote60::native_poc::ChainAuInfo{f.frame.header.streamGeneration, f.frame.header.seq,
                                                (f.frame.header.flags & 1u) != 0, f.frame.payload.size()};
    }
  };
  remote60::native_poc::ChainInbox<InboxFrame, InboxInfo> inbox_{kInboxMaxFrames, kInboxMaxBytes, InboxInfo{}};
  // The recovery episode (design r3 §2-§3): at most 4 asks per episode at t0 / +1 / +3 / +7 s from real sends, a final
  // window, then exhausted (recorded; a later usable IDR still decodes and ends it). Under mu_.
  remote60::native_poc::KeyRecoveryEpisode recovery_;
  // Moves at every bind / unbind / pick / abort / stream reset / surface request: an ask made before is moot (D8).
  uint64_t keyEpoch_ = 1;
  // The IDR the worker is handing to the codec right now (mu_ released around the codec calls): a candidate too.
  bool inFlightKey_ = false;
  uint32_t inFlightKeySeq_ = 0;
  // One-shot causes outside the episode (r4 N3 codec up, mobile-abr r5 abort); each taken once, kept as its cause.
  bool wantCodecUp_ = false;
  bool wantSelectionAbort_ = false;
  uint64_t keyRequestsTaken_ = 0;      // wants handed to the session (any cause)
  uint64_t recoveryDroppedMoot_ = 0;   // recovery asks judged moot at the session's points (a candidate held)
  uint64_t recoveryExhaustedLogged_ = 0;
  void InvalidateKeyAsksLocked();                  // keyEpoch_++, the episode and the one-shot wants dropped
  bool RecoveryCandidateLocked() const;            // a usable IDR after the loss point is preserved somewhere
  void NoteChainLossLocked(int cause, uint32_t seq);  // a loss of the chain the decoder saw itself
  void NoteInboxOfferLocked(const remote60::native_poc::ChainOfferResult& r);
  void TickRecoveryLocked();
''')
s = rep(s, '''  // Set when a delta had to be discarded before reaching the codec; consumed by the session,
  // which turns it into a rate-limited IDR request.
  bool decoderKeyframeRequest_ = false;
''', '''  // apk-inbox C: the old single want flag (decoderKeyframeRequest_) is gone -- a chain loss goes to recovery_, the two
  // other causes are the one-shots above; the session asks TakeDecoderKeyRequest (a cause set + the sink state).
''')
s = rep(s, '''  void NoteKeyWantLocked(int cause) {
    decoderKeyframeRequest_ = true;
    ++keyWant_[cause];
  }
''', '''  // apk-inbox C: counted by cause as before; what it asks for is decided by the cause (NoteChainLossLocked for a chain
  // loss, the one-shot flags for codec up / abort).
  void NoteKeyWantLocked(int cause) { ++keyWant_[cause]; }
''')
save(p, s, crlf)

# ============================================================== cpp
p = CPP + r'\android_video_decoder.cpp'
s, crlf = load(p)

s = rep(s, '''void AndroidVideoDecoderSink::ClearInboxLocked() {
  inbox_.clear();
  inboxBytes_ = 0;
  inboxGap_ = false;
}
''', '''void AndroidVideoDecoderSink::ClearInboxLocked() {
  inbox_.Clear();
}

// apk-inbox C (D8): every ask made before this moment is moot -- the session drops it at its held / accepted point.
void AndroidVideoDecoderSink::InvalidateKeyAsksLocked() {
  ++keyEpoch_;
  recovery_.Invalidate();
  wantCodecUp_ = false;
  wantSelectionAbort_ = false;
}

// apk-inbox C (design r3 §3 보류): a usable IDR (after the loss point) is in the inbox, in the pending slot or going
// into the codec right now -- nothing is asked while one is.
bool AndroidVideoDecoderSink::RecoveryCandidateLocked() const {
  if (recovery_.state() == remote60::native_poc::KeyRecoveryEpisode::State::Idle) return false;
  const uint32_t loss = recovery_.lossSeq();
  if (inbox_.HoldsKeyAfter(loss)) return true;
  if (inFlightKey_ && remote60::native_poc::chain_seq_after(inFlightKeySeq_, loss)) return true;
  const auto* held = pendingFrame_.held();
  return held && (held->header.flags & 1u) != 0 && remote60::native_poc::chain_seq_after(held->header.seq, loss);
}

// A loss of the chain the decoder saw itself (await-key, its pending slot): the episode, and the tail gap.
void AndroidVideoDecoderSink::NoteChainLossLocked(int cause, uint32_t seq) {
  NoteKeyWantLocked(cause);
  inbox_.MarkLoss(seq);
  recovery_.OnLoss(remote60::native_poc::kKeyCauseRecovery, seq, steady_now_us());
}

void AndroidVideoDecoderSink::NoteInboxOfferLocked(const remote60::native_poc::ChainOfferResult& r) {
  if (r.keysLost) gate_.KeyLost(r.keyLostGen);  // r5 N1: an IDR of that generation is owed again
  if (!r.loss) return;
  NoteKeyWantLocked(r.admit == remote60::native_poc::ChainAdmit::Oversize ? kWantOversize : kWantInboxOverflow);
  recovery_.OnLoss(remote60::native_poc::kKeyCauseRecovery, r.lossSeq, steady_now_us());
}

void AndroidVideoDecoderSink::TickRecoveryLocked() {
  recovery_.Tick(steady_now_us(), RecoveryCandidateLocked());
  if (recovery_.exhausted() != recoveryExhaustedLogged_) {
    recoveryExhaustedLogged_ = recovery_.exhausted();
    char line[192];
    std::snprintf(line, sizeof(line),
                  "decoder recovery exhausted episode=%llu asks=%u lossSeq=%u -- no more asks; the next usable IDR "
                  "(the host's periodic one) still decodes",
                  static_cast<unsigned long long>(recovery_.id()), recovery_.attempts(), recovery_.lossSeq());
    log_error(line);
  }
}
''')

# the worker takes from the chain inbox
s = rep(s, '''    while (!inbox_.empty() && !workerStop_ && surfaceApplied_ == surfaceSeq_ && !CodecResetPendingLocked()) {
      InboxFrame item = std::move(inbox_.front());
      inbox_.pop_front();
      inboxBytes_ -= std::min(inboxBytes_, item.frame.payload.size());
      REMOTE60_LT({''', '''    while (!inbox_.empty() && !workerStop_ && surfaceApplied_ == surfaceSeq_ && !CodecResetPendingLocked()) {
      InboxFrame item;
      inbox_.Pop(&item);
      REMOTE60_LT({''')
s = rep(s, '''    if (codec_) PumpCodecLocked();
    PublishDecodeStateLocked();
    ++workerPasses_;''', '''    if (codec_) PumpCodecLocked();
    TickRecoveryLocked();  // apk-inbox C: the final window ends on the clock
    PublishDecodeStateLocked();
    ++workerPasses_;''')
s = rep(s, '''      ltInbox_.store(inbox_.size(), std::memory_order_relaxed);''',
        '''      ltInbox_.store(inbox_.frames(), std::memory_order_relaxed);''')

# RX: oversize and the trim go through the chain inbox
old_rx_start = s.index('  // r4 N2 (Codex src r1): the byte cap holds for every frame -- r1-r3 kept one frame of ANY size')
old_rx_end = s.index('  REMOTE60_LT({  // r2: what stays admitted after the trim (never over the caps)')
s = s[:old_rx_start] + '''  // apk-inbox C (design r3 §1-§2): the chain-aware inbox decides -- the order (a duplicate / older AU dropped), the
  // byte cap for every frame (r4 N2: a frame over the whole cap is refused: a loss), the tail gap (a delta after a loss
  // is refused until a usable IDR), and the trim (the earliest usable IDR + suffix kept; else the loss taken as one).
  // r5 (Codex src r4) still holds: the cap is an AVERAGE of the highest bitrate asked for, an encoder may make one IDR
  // over 4 MiB -- refused here, every time it comes. The assembler itself admits up to 16 MB.
  const uint32_t seq = frame.header.seq;
  const uint64_t gen = frame.header.streamGeneration;
  InboxFrame item;
  item.resetPts = latestInputStreamGeneration_ != 0 && latestInputStreamGeneration_ != gen;
  REMOTE60_LT(item.enqueueUs = LtNowUs());
  item.frame = std::move(frame);
  const auto offer = inbox_.Offer(std::move(item));
  if (offer.admit == remote60::native_poc::ChainAdmit::Duplicate ||
      offer.admit == remote60::native_poc::ChainAdmit::Stale) {
    return;  // counted by the inbox (chainDup / chainStale); not a loss
  }
  latestInputStreamGeneration_ = gen;
  if (offer.admit == remote60::native_poc::ChainAdmit::Oversize) {
    ++inboxOversizeDrops_;
    char line[192];
    std::snprintf(line, sizeof(line), "decoder inbox: frame over the cap refused seq=%u cap=%zu key=%u refused=%llu", seq,
                  kInboxMaxBytes, isKeyFrame ? 1u : 0u, static_cast<unsigned long long>(inboxOversizeDrops_));
    log_error(line);
  }
  NoteInboxOfferLocked(offer);
  if (offer.admit == remote60::native_poc::ChainAdmit::Admitted) {
    REMOTE60_LT({
      // apk-decoder-inbox r2 (Codex codex_apk_inbox_plan_src_r1.md N1): the AU's own enqueue time, by its identity
      // (stream generation + AU seq) -- looked up when THAT AU goes into the codec, wherever it waited.
      LtEnq& e = ltEnq_[seq % kLtEnqRing];
      e.gen = gen;
      e.seq = seq;
      e.us = LtNowUs();
    });
  }
  if (offer.loss && ((inbox_.lost() % 30u) == 1u || offer.lostFrames > 1)) {
    char line[192];
    std::snprintf(line, sizeof(line), "decoder inbox: chain loss at seq=%u lost=%zu (total %llu) skipped=%zu episode=%llu %s",
                  offer.lossSeq, offer.lostFrames, static_cast<unsigned long long>(inbox_.lost()), offer.skippedFrames,
                  static_cast<unsigned long long>(recovery_.id()), recovery_.StateName());
    log_info(line);
  }
''' + s[old_rx_end:]
s = rep(s, '''  REMOTE60_LT({  // r2: what stays admitted after the trim (never over the caps)
    LtMax(ltInboxHwFrames_, inbox_.size());
    LtMax(ltInboxHwBytes_, inboxBytes_);
  });''', '''  REMOTE60_LT({  // r2: what stays admitted after the trim (never over the caps)
    LtMax(ltInboxHwFrames_, inbox_.frames());
    LtMax(ltInboxHwBytes_, inbox_.bytes());
  });''')

# the worker's part: the old worker-side gap is the inbox's now; await-key and the slot are chain losses
s = rep(s, '''  if (item.resetPts) ResetPtsStateLocked();
  if (inboxGap_) {
    if (!isKeyFrame) {
      ++inboxGapDrops_;
      NoteKeyWantLocked(kWantGapDelta);
      return;
    }
    inboxGap_ = false;
  }
''', '''  if (item.resetPts) ResetPtsStateLocked();
  // apk-inbox C: no worker-side gap any more -- after a loss the inbox holds no delta before a usable IDR (the trim
  // and the tail gap). The IDR going in now is a recovery candidate while the codec calls run (mu_ released).
  struct InFlight {
    AndroidVideoDecoderSink* s;
    ~InFlight() { s->inFlightKey_ = false; }
  } inFlight{this};
  inFlightKey_ = isKeyFrame;
  inFlightKeySeq_ = frame.header.seq;
''')
s = rep(s, '''  if (codecAwaitsKey_ && !isKeyFrame) {
    ++deltaBeforeKeyDrops_;
    NoteKeyWantLocked(kWantAwaitKey);
''', '''  if (codecAwaitsKey_ && !isKeyFrame) {
    ++deltaBeforeKeyDrops_;
    NoteChainLossLocked(kWantAwaitKey, frame.header.seq);  // apk-inbox C: one episode, not one ask per delta
''')
s = rep(s, '''  const bool hadPending = pendingFrame_.has();
  bool keyRequest = false;
  pendingFrame_.Offer(std::move(frame), gate_, [this](remote60::native_poc::UdpH264AssembledFrame& f) {
    return TryQueueFrameLocked(f);
  }, &keyRequest);
  if (keyRequest) NoteKeyWantLocked(kWantPendingSlot);
''', '''  const bool hadPending = pendingFrame_.has();
  bool keyRequest = false;
  const uint32_t offeredSeq = frame.header.seq;
  pendingFrame_.Offer(std::move(frame), gate_, [this](remote60::native_poc::UdpH264AssembledFrame& f) {
    return TryQueueFrameLocked(f);
  }, &keyRequest);
  if (keyRequest) NoteChainLossLocked(kWantPendingSlot, offeredSeq);  // apk-inbox C: a chain loss, one episode
''')

# bind / unbind
s = rep(s, '''  presentScheduledCount_ = 0;
  presentImmediateCount_ = 0;
  decoderKeyframeRequest_ = false;
}
''', '''  presentScheduledCount_ = 0;
  presentImmediateCount_ = 0;
  InvalidateKeyAsksLocked();  // apk-inbox C (was: the single want flag cleared)
}
''')
# stream reset
s = rep(s, '''  if (!OwnerLocked(token)) return;
  RequestCodecResetLocked();
  ClearInboxLocked();
  ResetCodecRetryLocked();  // r4 N3
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  gate_.Reset();
  readySelectionGeneration_ = 0;
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  csd0_.clear();
  csd1_.clear();
}
''', '''  if (!OwnerLocked(token)) return;
  RequestCodecResetLocked();
  ClearInboxLocked();
  ResetCodecRetryLocked();  // r4 N3
  InvalidateKeyAsksLocked();  // apk-inbox C
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  gate_.Reset();
  readySelectionGeneration_ = 0;
  latestInputStreamGeneration_ = 0;
  latestOutputStreamGeneration_ = 0;
  lastOutputPresentationUs_ = 0;
  staleFrameDropCount_ = 0;
  csd0_.clear();
  csd1_.clear();
}
''')
# prepare
s = rep(s, '''  RequestCodecResetLocked();
  ClearInboxLocked();
  ResetCodecRetryLocked();  // r4 N3
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  gate_.Prepare(selectionGeneration);''', '''  RequestCodecResetLocked();
  ClearInboxLocked();
  ResetCodecRetryLocked();  // r4 N3
  InvalidateKeyAsksLocked();  // apk-inbox C: the old selection's asks are moot (inbox contrast [5])
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  outputWidth_ = 0;
  outputHeight_ = 0;
  inputFrameCount_ = 0;
  outputFrameCount_ = 0;
  gate_.Prepare(selectionGeneration);''')
# abort
s = rep(s, '''  StartGraceLocked(false);
  NoteKeyWantLocked(kWantSelectionAbort);
  log_info("selection aborted");''', '''  StartGraceLocked(false);
  InvalidateKeyAsksLocked();  // apk-inbox C: what was asked before is moot -- the abort's own ask is new
  NoteKeyWantLocked(kWantSelectionAbort);
  wantSelectionAbort_ = true;
  log_info("selection aborted");''')
# codec up after a failure
s = rep(s, '''    gate_.Rearm();
    NoteKeyWantLocked(kWantCodecUp);
''', '''    gate_.Rearm();
    NoteKeyWantLocked(kWantCodecUp);
    wantCodecUp_ = true;  // apk-inbox C: its own cause, taken once
''')
# a new surface / a detach: the asks made for the old one are moot
s = rep(s, '''  surfaceRequested_ = window != nullptr;
  const uint64_t seq = ++surfaceSeq_;''', '''  surfaceRequested_ = window != nullptr;
  InvalidateKeyAsksLocked();  // apk-inbox C: a new surface (or none) -- the codec starts over; the owed duty re-arms
  const uint64_t seq = ++surfaceSeq_;''')

# the commit point closes the episode
s = rep(s, '''  if (queueingReplay_) RememberReplayPtsLocked(ptsUs);
  lastInputQueueSteadyUs_ = steady_now_us();
  if (isKey) codecAwaitsKey_ = false;  // keepalive r5 D1: this instance has its IDR
''', '''  if (queueingReplay_) RememberReplayPtsLocked(ptsUs);
  lastInputQueueSteadyUs_ = steady_now_us();
  if (isKey) codecAwaitsKey_ = false;  // keepalive r5 D1: this instance has its IDR
  // apk-inbox C (design r3 §2): the episode ends only here -- an IDR after the loss point committed to the codec.
  if (isKey && recovery_.OnKeyCommitted(frame.header.seq)) {
    char cline[160];
    std::snprintf(cline, sizeof(cline), "decoder recovery: IDR seq=%u committed -- episode %llu closed after %u asks",
                  frame.header.seq, static_cast<unsigned long long>(recovery_.id()), recovery_.attempts());
    log_info(cline);
  }
''')

# the session's hooks
s = rep(s, '''bool AndroidVideoDecoderSink::ConsumeDecoderKeyframeRequestAs(uint64_t token) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  const bool requested = decoderKeyframeRequest_;
  decoderKeyframeRequest_ = false;
  return requested;
}
''', '''bool AndroidVideoDecoderSink::ConsumeDecoderKeyframeRequestAs(uint64_t token) {
  return TakeDecoderKeyRequestAs(token, nullptr);
}

// apk-inbox C (design r3 §3): the want now, with what it is for. The episode hands out its ask at t0 and then only
// when due (1 / 2 / 4 s after the previous SEND), never while a usable IDR is preserved; the one-shots once each.
bool AndroidVideoDecoderSink::TakeDecoderKeyRequestAs(uint64_t token, remote60::native_poc::DecoderKeyRequest* out) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return false;
  remote60::native_poc::DecoderKeyRequest r;
  r.sinkEpoch = keyEpoch_;
  if (wantCodecUp_) r.causes |= remote60::native_poc::kKeyCauseCodecUp;
  if (wantSelectionAbort_) r.causes |= remote60::native_poc::kKeyCauseSelectionAbort;
  wantCodecUp_ = false;
  wantSelectionAbort_ = false;
  uint64_t episode = 0;
  const bool candidate = RecoveryCandidateLocked();
  if (recovery_.TakeWant(steady_now_us(), candidate, &episode)) {
    r.causes |= remote60::native_poc::kKeyCauseRecovery;
    r.episodeId = episode;
  }
  TickRecoveryLocked();  // logs an exhaustion TakeWant's own tick reached
  if (r.empty()) return false;
  ++keyRequestsTaken_;
  if (out) *out = r;
  return true;
}

// apk-inbox C (D8): which causes of a held / accepted request still hold, asked by the session right before it goes
// on (to the limiter again / to the send). A short query under mu_ -- never across the network.
uint32_t AndroidVideoDecoderSink::DecoderKeyRequestValidCausesAs(uint64_t token,
                                                                 const remote60::native_poc::DecoderKeyRequest& r) {
  using namespace remote60::native_poc;
  const uint32_t sessionScoped = r.causes & (kKeyCausePipeline | kKeyCauseLegacy);
  LockedState lock(this);
  if (!OwnerLocked(token)) return sessionScoped;  // another PC on screen: none of this sink's state is that request's
  uint32_t valid = sessionScoped;
  const bool sameEpoch = r.sinkEpoch == keyEpoch_;
  if ((r.causes & kKeyCauseRecovery) != 0 && sameEpoch && recovery_.StillValid(r.episodeId)) {
    if (!RecoveryCandidateLocked()) {
      valid |= kKeyCauseRecovery;
    } else {
      recovery_.OnWantDropped(r.episodeId);  // moot for now: a usable IDR is preserved
      ++recoveryDroppedMoot_;
    }
  }
  if ((r.causes & kKeyCauseCodecUp) != 0 && sameEpoch && codecAwaitsKey_) valid |= kKeyCauseCodecUp;
  if ((r.causes & kKeyCauseSelectionAbort) != 0 && sameEpoch) valid |= kKeyCauseSelectionAbort;
  if ((r.causes & kKeyCauseOwed) != 0 && r.owedToken != 0 && gate_.OwedToken() == r.owedToken) valid |= kKeyCauseOwed;
  return valid;
}

// apk-inbox C: the control exchange for this request started -- the episode's attempt, on the sink's own clock.
void AndroidVideoDecoderSink::OnDecoderKeyRequestSentAs(uint64_t token, const remote60::native_poc::DecoderKeyRequest& r,
                                                        uint64_t /* nowUs: the session's clock */) {
  LockedState lock(this);
  if (!OwnerLocked(token)) return;
  if ((r.causes & remote60::native_poc::kKeyCauseRecovery) != 0 && r.sinkEpoch == keyEpoch_) {
    recovery_.OnSent(r.episodeId, steady_now_us());
  }
}
''')

# DebugStatus
s = rep(s, '''  status += " inbox=" + std::to_string(inbox_.size());
  status += " inboxBytes=" + std::to_string(inboxBytes_);  // r4 N2
  status += " inboxDrops=" + std::to_string(inboxDrops_);
  status += " gapDrops=" + std::to_string(inboxGapDrops_);''', '''  status += " inbox=" + std::to_string(inbox_.frames());
  status += " inboxBytes=" + std::to_string(inbox_.bytes());  // r4 N2
  status += " inboxDrops=" + std::to_string(inbox_.lost());   // apk-inbox C: frames lost to the chain (trim / gap / oversize)
  status += " gapDrops=" + std::to_string(inbox_.gapDeltas());
  // apk-inbox C: the chain inbox and the recovery episode
  status += " chainSkipped=" + std::to_string(inbox_.skipped()) + " chainDup=" + std::to_string(inbox_.duplicates()) +
            " chainStale=" + std::to_string(inbox_.stale()) + " tailGap=" + std::to_string(inbox_.gapOpen() ? 1 : 0);
  status += " recovery=" + std::string(recovery_.StateName()) + " episode=" + std::to_string(recovery_.id()) +
            " asks=" + std::to_string(recovery_.attempts()) + " episodes=" + std::to_string(recovery_.opened()) +
            " recClosed=" + std::to_string(recovery_.closed()) + " recExhausted=" + std::to_string(recovery_.exhausted()) +
            " recWants=" + std::to_string(recovery_.wants()) + " recSent=" + std::to_string(recovery_.sent()) +
            " recMoot=" + std::to_string(recoveryDroppedMoot_) + " keyTaken=" + std::to_string(keyRequestsTaken_) +
            " keyEpoch=" + std::to_string(keyEpoch_);''')
save(p, s, crlf)

# ============================================================== adapter
p = CPP + r'\entry_sink_adapter.hpp'
s, crlf = load(p)
s = rep(s, '''  bool DrainPresentationStats(remote60::native_poc::ClientPresentationStats* out) override {''', '''  // apk-inbox C: the want with its descriptor, its validity and its send -- for this PC's binding only (a PC not on
  // screen asks nothing; its held / accepted requests keep only their session-scoped causes).
  bool TakeDecoderKeyRequest(remote60::native_poc::DecoderKeyRequest* out) override {
    const uint64_t t = Token();
    return t != 0 && decoder_->TakeDecoderKeyRequestAs(t, out);
  }
  uint32_t DecoderKeyRequestValidCauses(const remote60::native_poc::DecoderKeyRequest& request) override {
    const uint64_t t = Token();
    return t != 0 ? decoder_->DecoderKeyRequestValidCausesAs(t, request)
                  : request.causes & (remote60::native_poc::kKeyCausePipeline | remote60::native_poc::kKeyCauseLegacy);
  }
  void OnDecoderKeyRequestSent(const remote60::native_poc::DecoderKeyRequest& request, uint64_t nowUs) override {
    if (const uint64_t t = Token()) decoder_->OnDecoderKeyRequestSentAs(t, request, nowUs);
  }
  bool DrainPresentationStats(remote60::native_poc::ClientPresentationStats* out) override {''')
save(p, s, crlf)
print('ok')
