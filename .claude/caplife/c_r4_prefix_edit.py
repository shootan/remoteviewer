"""C r4 prototype (Codex codex_apk_inbox_prefix_design_go_r1.md): the keep-prefix overflow case, freshness and slot
backpressure -- behind ONE runtime switch (default OFF = today's C). Branch exp/apk-inbox-prefix from 2b492ef."""
WT = r'C:\work\gnlink-wt\apk-inbox-c'
SRC = WT + r'\apps\native_poc\src'
CPP = WT + r'\apps\android_direct_client\app\src\main\cpp'


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


# ================= the inbox
p = SRC + r'\decoder_chain_inbox.hpp'
s, crlf = load(p)
s = rep(s, '''//   3. no IDR in the inbox: everything goes (all of it continues a chain whose next reference is gone) -- a loss.''',
        '''//   3. no IDR in the inbox: everything goes (all of it continues a chain whose next reference is gone) -- a loss.
//   3'. C r4 KEEP-PREFIX (off unless SetPrefixPolicy(true); Codex codex_apk_inbox_prefix_design_go_r1.md): no IDR in
//      the inbox, and the owner PROVES the queue continues what its codec holds -- SetFrontier(valid, seq, now,
//      budget) before the Offer: the codec's chain intact since a committed IDR, the queue's head the very next AU
//      after the newest one the codec took (frontier + 1), every queued AU the next of the one before it, one
//      generation, the head no older than the budget. Then the PREFIX is kept and the NEWEST frames go until it fits
//      -- a tail loss (the gap opens after them; the recovery asks as for any loss). Not proven: case 3.''')
s = rep(s, '''struct ChainAuInfo {
  uint64_t gen = 0;
  uint32_t seq = 0;
  bool key = false;
  size_t bytes = 0;
};''', '''struct ChainAuInfo {
  uint64_t gen = 0;
  uint32_t seq = 0;
  bool key = false;
  size_t bytes = 0;
  uint64_t enqueueUs = 0;  // C r4: when the owner queued it (its local residence starts here); 0 = unknown
};''')
s = rep(s, '''  bool Pop(T* out) {
    if (items_.empty()) return false;''', '''  // ---- C r4 keep-prefix (Codex codex_apk_inbox_prefix_design_go_r1.md) ----
  void SetPrefixPolicy(bool on) { prefixPolicy_ = on; }
  bool prefixPolicy() const { return prefixPolicy_; }
  // What the owner has proven at this moment (read only by case 3'): the codec's chain is intact and `frontierSeq` is
  // the newest AU it took to feed; `nowUs` / `ageBudgetUs` for the head's residence.
  void SetFrontier(bool valid, uint32_t frontierSeq, uint64_t nowUs, uint64_t ageBudgetUs) {
    frontierValid_ = valid;
    frontierSeq_ = frontierSeq;
    frontierNowUs_ = nowUs;
    ageBudgetUs_ = ageBudgetUs;
  }
  // The head is a delta whose residence is over the budget (an IDR never expires here: it starts a chain).
  bool HeadExpired(uint64_t nowUs, uint64_t budgetUs) const {
    if (items_.empty()) return false;
    const ChainAuInfo a = info_(items_.front());
    return !a.key && a.enqueueUs != 0 && nowUs > a.enqueueUs && nowUs - a.enqueueUs > budgetUs;
  }
  uint64_t prefixKeptEvents() const { return prefixKeptEvents_; }
  uint64_t prefixKeptFrames() const { return prefixKeptFrames_; }
  uint64_t tailLost() const { return tailLost_; }
  uint64_t prefixRefused() const { return prefixRefused_; }

  bool Pop(T* out) {
    if (items_.empty()) return false;''')
s = rep(s, '''    // case 3: no IDR -- the whole chain in here is undecodable once its head goes
    while (!items_.empty()) DropFront(true, r);
  }''', '''    // case 3': C r4 -- the queue PROVABLY continues the codec's chain: keep it, cut the newest (a tail loss)
    if (prefixPolicy_) {
      if (PrefixProvable()) {
        while (!items_.empty() && (items_.size() > maxFrames_ || bytes_ > maxBytes_)) {
          DropBack(r);
          ++tailLost_;
        }
        ++prefixKeptEvents_;
        prefixKeptFrames_ += items_.size();
        return;
      }
      ++prefixRefused_;
    }
    // case 3: no IDR -- the whole chain in here is undecodable once its head goes
    while (!items_.empty()) DropFront(true, r);
  }
  // C r4: the proof case 3' needs, from the owner's frontier and the queue itself.
  bool PrefixProvable() const {
    if (!frontierValid_ || items_.empty()) return false;
    const ChainAuInfo head = info_(items_.front());
    if (head.key || head.seq != frontierSeq_ + 1u) return false;
    if (head.enqueueUs == 0 || frontierNowUs_ < head.enqueueUs || frontierNowUs_ - head.enqueueUs > ageBudgetUs_) return false;
    uint32_t prev = head.seq;
    for (size_t i = 1; i < items_.size(); ++i) {
      const ChainAuInfo a = info_(items_[i]);
      if (a.gen != head.gen || a.seq != prev + 1u) return false;
      prev = a.seq;
    }
    return true;
  }''')
s = rep(s, '''  uint64_t duplicates_ = 0, stale_ = 0, oversize_ = 0, gapDeltas_ = 0, skipped_ = 0, lost_ = 0, trimmed_ = 0;
};''', '''  uint64_t duplicates_ = 0, stale_ = 0, oversize_ = 0, gapDeltas_ = 0, skipped_ = 0, lost_ = 0, trimmed_ = 0;
  // C r4 keep-prefix
  bool prefixPolicy_ = false;
  bool frontierValid_ = false;
  uint32_t frontierSeq_ = 0;
  uint64_t frontierNowUs_ = 0, ageBudgetUs_ = 0;
  uint64_t prefixKeptEvents_ = 0, prefixKeptFrames_ = 0, tailLost_ = 0, prefixRefused_ = 0;
};''')
save(p, s, crlf)

# ================= the sink header
p = CPP + r'\android_video_decoder.hpp'
s, crlf = load(p)
s = rep(s, '''    uint64_t enqueueUs = 0;  // apk-decoder-inbox r1: set only in test builds (GNLINK_LIVENESS_TRACE)''',
        '''    uint64_t enqueueUs = 0;  // C r4: set always -- the frame's local residence starts here (keep-prefix, age)''')
s = rep(s, '''      return remote60::native_poc::ChainAuInfo{f.frame.header.streamGeneration, f.frame.header.seq,
                                                (f.frame.header.flags & 1u) != 0, f.frame.payload.size()};''',
        '''      return remote60::native_poc::ChainAuInfo{f.frame.header.streamGeneration, f.frame.header.seq,
                                                (f.frame.header.flags & 1u) != 0, f.frame.payload.size(), f.enqueueUs};''')
s = rep(s, '''  uint64_t coveredToken_ = 0;''', '''  // C r4 keep-prefix prototype (Codex codex_apk_inbox_prefix_design_go_r1.md) -- OFF unless SetPrefixPolicyForTest.
  bool prefixPolicy_ = false;
  bool chainValid_ = false;        // the codec's chain intact since a committed IDR (no loss / reset / drop since)
  bool haveFrontier_ = false;
  uint32_t frontierSeq_ = 0;       // the newest AU the worker took to feed (popped, under mu_)
  uint32_t targetFps_ = 30;        // SetTargetFps (the age budget's base)
  uint64_t pendingEnqueueUs_ = 0;  // the held frame's residence start
  uint64_t ageExpiredPop_ = 0, ageExpiredSlot_ = 0, backpressurePasses_ = 0;
  uint64_t PrefixAgeBudgetUsLocked() const;
  void InvalidateChainLocked() { chainValid_ = false; }
  uint64_t coveredToken_ = 0;''')
s = rep(s, '''  void OnDecoderKeyOwedExhaustedAs(uint64_t token, uint64_t owedToken, uint64_t nowUs);
''', '''  void OnDecoderKeyOwedExhaustedAs(uint64_t token, uint64_t owedToken, uint64_t nowUs);
  // C r4 prototype: the keep-prefix overflow case + age + slot backpressure (default OFF = today's C). Tests only.
  void SetPrefixPolicyForTest(bool on);
''')
save(p, s, crlf)

# ================= the sink
p = CPP + r'\android_video_decoder.cpp'
s, crlf = load(p)
# the chain's validity: lost on any loss / reset / clear / invalidation
s = rep(s, '''void AndroidVideoDecoderSink::NoteChainLossLocked(int cause, uint32_t seq) {
  NoteKeyWantLocked(cause);''', '''void AndroidVideoDecoderSink::NoteChainLossLocked(int cause, uint32_t seq) {
  InvalidateChainLocked();  // C r4: a loss the decoder saw -- the codec's chain is not provably intact any more
  NoteKeyWantLocked(cause);''')
s = rep(s, '''void AndroidVideoDecoderSink::RequestCodecResetLocked() {''', '''void AndroidVideoDecoderSink::SetPrefixPolicyForTest(bool on) {
  LockedState lock(this);
  prefixPolicy_ = on;
  inbox_.SetPrefixPolicy(on);
}

// C r4: the residence budget -- the time the whole inbox (kInboxMaxFrames) represents at the session's target fps,
// floored at 250 ms (high fps: one jitter spike must not expire a valid prefix) and capped at 500 ms (low fps: the
// picture is not allowed to trail by more). 30 fps -> 267 ms, 20 -> 400 ms, <= 16 -> 500 ms.
uint64_t AndroidVideoDecoderSink::PrefixAgeBudgetUsLocked() const {
  const uint64_t fps = targetFps_ == 0 ? 30u : targetFps_;
  return std::clamp<uint64_t>(static_cast<uint64_t>(kInboxMaxFrames) * 1000000ULL / fps, 250000ULL, 500000ULL);
}

void AndroidVideoDecoderSink::RequestCodecResetLocked() {
  InvalidateChainLocked();  // C r4''')
s = rep(s, '''void AndroidVideoDecoderSink::ClearInboxLocked() {''', '''void AndroidVideoDecoderSink::ClearInboxLocked() {
  InvalidateChainLocked();  // C r4
  haveFrontier_ = false;''')
s = rep(s, '''void AndroidVideoDecoderSink::InvalidateKeyAsksLocked() {''', '''void AndroidVideoDecoderSink::InvalidateKeyAsksLocked() {
  InvalidateChainLocked();  // C r4''')
# the worker: backpressure + the Pop-time expiry + the frontier
s = rep(s, '''    while (!inbox_.empty() && !workerStop_ && surfaceApplied_ == surfaceSeq_ && !CodecResetPendingLocked()) {
      InboxFrame item;
      inbox_.Pop(&item);''', '''    while (!inbox_.empty() && !workerStop_ && surfaceApplied_ == surfaceSeq_ && !CodecResetPendingLocked()) {
      // C r4 backpressure: while a frame waits in the slot for an input buffer, nothing more is taken -- the next one
      // would only replace (lose) it; the queue keeps the chain and the overflow decides (case 3'), and the held
      // frame is retried on this pass's pump. A held DELTA past the age budget is dropped there (finite: an IDR
      // queued behind it waits at most that long).
      if (prefixPolicy_ && pendingFrame_.has()) {
        ++backpressurePasses_;
        break;
      }
      // C r4 age, at the Pop: a delta past the budget is not fed -- a loss at its seq (MarkLoss then drops the deltas
      // that reference it, up to the next IDR); never a one-frame skip.
      if (prefixPolicy_ && inbox_.HeadExpired(steady_now_us(), PrefixAgeBudgetUsLocked())) {
        InboxFrame expired;
        inbox_.Pop(&expired);
        ++ageExpiredPop_;
        NoteChainLossLocked(kWantInboxOverflow, expired.frame.header.seq);
        continue;
      }
      InboxFrame item;
      inbox_.Pop(&item);
      frontierSeq_ = item.frame.header.seq;  // C r4: the newest AU the codec path took
      haveFrontier_ = true;''')
s = rep(s, '''    workCv_.wait_for(lk, kPumpInterval, [this] {
      return workerStop_ || !inbox_.empty() || surfaceApplied_ != surfaceSeq_ || CodecResetPendingLocked();
    });''', '''    workCv_.wait_for(lk, kPumpInterval, [this] {
      // C r4: a queue behind a held frame does not wake the worker (no hot loop); the pump interval retries the slot
      return workerStop_ || (!inbox_.empty() && !(prefixPolicy_ && pendingFrame_.has())) ||
             surfaceApplied_ != surfaceSeq_ || CodecResetPendingLocked();
    });''')
# the slot: residence start of the held frame; held delta past the budget dropped
s = rep(s, '''  const uint32_t heldSeq = pendingFrame_.held() ? pendingFrame_.held()->header.seq : offeredSeq;''',
        '''  const uint32_t heldSeq = pendingFrame_.held() ? pendingFrame_.held()->header.seq : offeredSeq;
  const uint64_t offeredEnqueueUs = item.enqueueUs;  // C r4''')
s = rep(s, '''  if (pendingFrame_.has()) {
    pendingFrameCount_ = 1;
    if (!hadPending) pendingFrameQueueRetryCount_ = 0;
  }
  PumpCodecLocked();
}''', '''  if (pendingFrame_.has()) {
    pendingFrameCount_ = 1;
    if (!hadPending) pendingFrameQueueRetryCount_ = 0;
    if (!hadPending || offeredKey) pendingEnqueueUs_ = offeredEnqueueUs;  // C r4: the held frame's residence start
  }
  PumpCodecLocked();
}''')
s = rep(s, '''  if (pendingFrame_.has() && codec_) {
    // apk-0232 r2 (N2): the held frame goes in only if the gate still admits its generation; else it is dropped.''',
        '''  // C r4 age, at the slot: a held DELTA past the budget is dropped (a loss at its seq) -- an IDR never is.
  if (prefixPolicy_ && pendingFrame_.has() && (pendingFrame_.held()->header.flags & 1u) == 0 && pendingEnqueueUs_ != 0 &&
      nowUs > pendingEnqueueUs_ && nowUs - pendingEnqueueUs_ > PrefixAgeBudgetUsLocked()) {
    const uint32_t seq = pendingFrame_.held()->header.seq;
    pendingFrame_.Reset();
    pendingFrameCount_ = 0;
    ++ageExpiredSlot_;
    NoteChainLossLocked(kWantPendingSlot, seq);
  }
  if (pendingFrame_.has() && codec_) {
    // apk-0232 r2 (N2): the held frame goes in only if the gate still admits its generation; else it is dropped.''')
# the IDR commit: the chain starts (valid) here
s = rep(s, '''    committedKeyValid_ = true;
    committedKeySeq_ = frame.header.seq;
    inbox_.OnKeyCommitted(frame.header.seq);''', '''    committedKeyValid_ = true;
    committedKeySeq_ = frame.header.seq;
    inbox_.OnKeyCommitted(frame.header.seq);
    chainValid_ = true;  // C r4: the codec's chain is intact from here (until a loss / reset / clear)''')
# the RX side: residence start always; the proof handed to the inbox before the Offer
s = rep(s, '''  REMOTE60_LT(item.enqueueUs = LtNowUs());
  item.frame = std::move(frame);
  const auto offer = inbox_.Offer(std::move(item));''', '''  item.enqueueUs = steady_now_us();  // C r4: always (the residence start)
  item.frame = std::move(frame);
  if (prefixPolicy_) {
    inbox_.SetFrontier(chainValid_ && haveFrontier_, frontierSeq_, item.enqueueUs,
                       PrefixAgeBudgetUsLocked());
  }
  const auto offer = inbox_.Offer(std::move(item));''')
s = rep(s, '''  playoutClock_.SetTargetFrameIntervalUs(
      std::max<uint64_t>(1ULL, 1000000ULL / static_cast<uint64_t>(clampedFps)));''', '''  playoutClock_.SetTargetFrameIntervalUs(
      std::max<uint64_t>(1ULL, 1000000ULL / static_cast<uint64_t>(clampedFps)));
  targetFps_ = clampedFps;  // C r4: the age budget's base''')
# DebugStatus
s = rep(s, '''            " covered=" + std::to_string(coveredToken_ != 0 ? 1 : 0) + " coveredExh=" + std::to_string(coveredExhausted_ ? 1 : 0) +''',
        '''            " covered=" + std::to_string(coveredToken_ != 0 ? 1 : 0) + " coveredExh=" + std::to_string(coveredExhausted_ ? 1 : 0) +
            " prefixPolicy=" + std::to_string(prefixPolicy_ ? 1 : 0) + " prefixKept=" + std::to_string(inbox_.prefixKeptEvents()) +
            " prefixKeptFrames=" + std::to_string(inbox_.prefixKeptFrames()) + " tailLost=" + std::to_string(inbox_.tailLost()) +
            " prefixRefused=" + std::to_string(inbox_.prefixRefused()) + " ageExpPop=" + std::to_string(ageExpiredPop_) +
            " ageExpSlot=" + std::to_string(ageExpiredSlot_) + " backpressure=" + std::to_string(backpressurePasses_) +''')
save(p, s, crlf)
print('ok')
