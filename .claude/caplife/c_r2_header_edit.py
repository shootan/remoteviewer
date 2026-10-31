p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\decoder_chain_inbox.hpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


# N3: the 30-in-a-row resync is gone -- order changes only with an explicit reset (Clear)
rep('''// A safety valve: kOrderResyncAfter such drops in a row are taken as a restarted sequence, not reordering -- the order
// starts over from the next AU (counted), so no unforeseen restart can blank the picture for good.''',
    '''// C r2 (Codex codex_apk_inbox_c_src_r1.md N3): no automatic re-ordering -- however many duplicate / older AUs come in a
// row, they stay dropped; the order starts over only at an explicit reset (Clear: a new pick / binding / stream).''')
rep('''    if (haveLast_ && a.gen == lastGen_ && orderDropsInRow_ < kOrderResyncAfter) {
      if (a.seq == lastSeq_) {
        r.admit = ChainAdmit::Duplicate;
        ++duplicates_;
        ++orderDropsInRow_;
        return r;
      }
      if (!chain_seq_after(a.seq, lastSeq_)) {
        r.admit = ChainAdmit::Stale;
        ++stale_;
        ++orderDropsInRow_;
        return r;
      }
    }
    if (haveLast_ && a.gen == lastGen_ && orderDropsInRow_ >= kOrderResyncAfter) {
      // the safety valve: so many in a row is no reordering -- the sequence restarted (not expected: the assembler
      // ignores anything not newer, and a stream reset clears this inbox). The order starts over from this AU.
      ++orderResyncs_;
    }
    orderDropsInRow_ = 0;
''', '''    if (haveLast_ && a.gen == lastGen_) {
      if (a.seq == lastSeq_) {
        r.admit = ChainAdmit::Duplicate;
        ++duplicates_;
        return r;
      }
      if (!chain_seq_after(a.seq, lastSeq_)) {
        r.admit = ChainAdmit::Stale;
        ++stale_;
        return r;
      }
    }
''')
rep('''    haveLast_ = false;
    orderDropsInRow_ = 0;
  }

  // Duplicate / older AUs in a row after which the order is taken to have restarted (about a second at 30 fps).
  static constexpr uint32_t kOrderResyncAfter = 30;
''', '''    haveLast_ = false;
  }
''')
rep('''  uint64_t orderResyncs() const { return orderResyncs_; }
''', '')
rep('''  uint32_t orderDropsInRow_ = 0;
  uint64_t orderResyncs_ = 0;
''', '')

# N1-a / N1-b: a loss seen outside the inbox trims what it broke; an IDR committed after the gap closes it
rep('''  // A loss seen outside the inbox (the decoder's own: a codec that lost its references, a delta it could not take, a
  // frame its pending slot gave up): the tail gap opens after `seq` -- deltas are refused from here until a usable IDR.
  // Not if the inbox already holds an IDR after `seq`: the chain restarts there, and what follows it is that chain's.
  void MarkLoss(uint32_t seq) {
    if (HoldsKeyAfter(seq)) return;
    if (!gapOpen_ || chain_seq_after(seq, gapAfterSeq_)) gapAfterSeq_ = seq;
    gapOpen_ = true;
  }
''', '''  // A loss seen outside the inbox (the decoder's own: a codec that lost its references, a delta it could not take, a
  // frame its pending slot gave up) at `seq`. Everything queued is after `seq` (the worker takes in order).
  // C r2 (Codex N1-a): the queued deltas up to the first IDR after `seq` reference what was lost -- they are DROPPED here
  // (counted as lost), never handed out; the chain restarts at that IDR and its suffix is kept, no gap. With no IDR
  // queued everything goes and the tail gap opens after the newest lost. C r2 (N1-b): `usableKeyOutside` -- an IDR after
  // `seq` is held outside the inbox (the pending slot, going into the codec): the chain restarts there and what is
  // queued is its suffix -- nothing dropped, no gap. -> the frames dropped.
  size_t MarkLoss(uint32_t seq, bool usableKeyOutside = false) {
    if (usableKeyOutside) return 0;
    size_t dropped = 0;
    uint32_t newest = seq;
    while (!items_.empty()) {
      const ChainAuInfo a = info_(items_.front());
      if (a.key && chain_seq_after(a.seq, seq)) return dropped;
      bytes_ -= (a.bytes <= bytes_ ? a.bytes : bytes_);
      items_.pop_front();
      ++dropped;
      ++lost_;
      ++trimmed_;
      if (chain_seq_after(a.seq, newest)) newest = a.seq;
    }
    if (!gapOpen_ || chain_seq_after(newest, gapAfterSeq_)) gapAfterSeq_ = newest;
    gapOpen_ = true;
    return dropped;
  }

  // C r2 (Codex N1-b): an IDR went into the codec (CommitQueue). If it is after the loss the gap is open for, the chain
  // restarted there -- the gap closes and the deltas after it are taken again. A loss after that IDR keeps its gap.
  void OnKeyCommitted(uint32_t seq) {
    if (gapOpen_ && chain_seq_after(seq, gapAfterSeq_)) gapOpen_ = false;
  }

  // C r2: a delta the worker took is newer than the open gap's loss point -- defensive (the trim and the door keep
  // such a delta out of the inbox); the worker drops it instead of feeding it.
  bool BeyondGap(uint32_t seq, bool key) const { return gapOpen_ && !key && chain_seq_after(seq, gapAfterSeq_); }
  uint64_t trimmed() const { return trimmed_; }
''')
rep('''  uint64_t duplicates_ = 0, stale_ = 0, oversize_ = 0, gapDeltas_ = 0, skipped_ = 0, lost_ = 0;
''', '''  uint64_t duplicates_ = 0, stale_ = 0, oversize_ = 0, gapDeltas_ = 0, skipped_ = 0, lost_ = 0, trimmed_ = 0;
''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
