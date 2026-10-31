CPP = r'C:\work\gnlink-wt\apk-inbox-c\apps\android_direct_client\app\src\main\cpp'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


p = CPP + r'\android_video_decoder.hpp'
s, crlf = load(p)
s = rep(s, '''  bool inFlightKey_ = false;
  uint32_t inFlightKeySeq_ = 0;
''', '''  bool inFlightKey_ = false;
  uint32_t inFlightKeySeq_ = 0;
  // C r2 (Codex N1-b): the last IDR this codec instance took (CommitQueue) -- a loss at a seq BEFORE it is already
  // recovered (the pending slot's replaced delta, a late await-key note). Forgotten with the codec, the inbox, the asks.
  bool committedKeyValid_ = false;
  uint32_t committedKeySeq_ = 0;
  uint64_t lossAlreadyRecovered_ = 0;  // losses noted behind a committed IDR (no episode, no gap)
  uint64_t beyondGapDrops_ = 0;        // the worker's defensive drop of a delta past the open gap (expected 0)
''')
save(p, s, crlf)

p = CPP + r'\android_video_decoder.cpp'
s, crlf = load(p)
# forget the committed key with the inbox / codec / asks
s = rep(s, '''void AndroidVideoDecoderSink::ClearInboxLocked() {
  inbox_.Clear();
}
''', '''void AndroidVideoDecoderSink::ClearInboxLocked() {
  inbox_.Clear();
  committedKeyValid_ = false;  // C r2
}
''')
s = rep(s, '''void AndroidVideoDecoderSink::InvalidateKeyAsksLocked() {
  ++keyEpoch_;
  recovery_.Invalidate();''', '''void AndroidVideoDecoderSink::InvalidateKeyAsksLocked() {
  ++keyEpoch_;
  committedKeyValid_ = false;  // C r2
  recovery_.Invalidate();''')
s = rep(s, '''  pendingFrame_.Reset();
  pendingFrameCount_ = 0;
  pendingFrameQueueRetryCount_ = 0;
  bootstrapFrame_.reset();''', '''  pendingFrame_.Reset();
  pendingFrameCount_ = 0;
  pendingFrameQueueRetryCount_ = 0;
  committedKeyValid_ = false;  // C r2: what this codec took goes with it
  bootstrapFrame_.reset();''')
# the loss note: already recovered / an outside candidate / the trim
s = rep(s, '''// A loss of the chain the decoder saw itself (await-key, its pending slot): the episode, and the tail gap.
void AndroidVideoDecoderSink::NoteChainLossLocked(int cause, uint32_t seq) {
  NoteKeyWantLocked(cause);
  inbox_.MarkLoss(seq);
  recovery_.OnLoss(remote60::native_poc::kKeyCauseRecovery, seq, steady_now_us());
}
''', '''// A loss of the chain the decoder saw itself (await-key, its pending slot) at `seq`.
// C r2 (Codex N1): (b) an IDR after `seq` already went into this codec -- recovered, nothing opens; an IDR after it held
// outside the inbox (the pending slot, going in now) -- the queue is its suffix: no trim, no gap, and the episode sees
// it as the candidate; (a) otherwise the queued deltas before the next usable IDR are dropped (never handed out), or
// everything and the tail gap when none is queued.
void AndroidVideoDecoderSink::NoteChainLossLocked(int cause, uint32_t seq) {
  NoteKeyWantLocked(cause);
  if (committedKeyValid_ && remote60::native_poc::chain_seq_after(committedKeySeq_, seq)) {
    ++lossAlreadyRecovered_;
    return;
  }
  const auto* held = pendingFrame_.held();
  const bool keyOutside =
      (held && (held->header.flags & 1u) != 0 && remote60::native_poc::chain_seq_after(held->header.seq, seq)) ||
      (inFlightKey_ && remote60::native_poc::chain_seq_after(inFlightKeySeq_, seq));
  inbox_.MarkLoss(seq, keyOutside);
  recovery_.OnLoss(remote60::native_poc::kKeyCauseRecovery, seq, steady_now_us());
}
''')
# the dequeue guard
s = rep(s, '''      InboxFrame item;
      inbox_.Pop(&item);
''', '''      InboxFrame item;
      inbox_.Pop(&item);
      // C r2: defensive -- a delta past the open gap's loss point is not fed (the trim and the door keep it out)
      if (inbox_.BeyondGap(item.frame.header.seq, (item.frame.header.flags & 1u) != 0)) {
        ++beyondGapDrops_;
        continue;
      }
''')
# the commit: remember it, close the gap it closes
s = rep(s, '''  // apk-inbox C (design r3 §2): the episode ends only here -- an IDR after the loss point committed to the codec.
  if (isKey && recovery_.OnKeyCommitted(frame.header.seq)) {''', '''  if (isKey) {  // C r2 (Codex N1-b): this codec's chain restarts here; a gap opened for a loss before it closes
    committedKeyValid_ = true;
    committedKeySeq_ = frame.header.seq;
    inbox_.OnKeyCommitted(frame.header.seq);
  }
  // apk-inbox C (design r3 §2): the episode ends only here -- an IDR after the loss point committed to the codec.
  if (isKey && recovery_.OnKeyCommitted(frame.header.seq)) {''')
# DebugStatus
s = rep(s, '''            " chainStale=" + std::to_string(inbox_.stale()) + " tailGap=" + std::to_string(inbox_.gapOpen() ? 1 : 0);''',
        '''            " chainStale=" + std::to_string(inbox_.stale()) + " tailGap=" + std::to_string(inbox_.gapOpen() ? 1 : 0) +
            " chainTrim=" + std::to_string(inbox_.trimmed()) + " lossRecovered=" + std::to_string(lossAlreadyRecovered_) +
            " beyondGap=" + std::to_string(beyondGapDrops_);''')
save(p, s, crlf)
print('ok')
