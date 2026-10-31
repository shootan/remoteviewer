src = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = src + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('viewer_selection_gate.hpp', [
    ('''  bool IsLiveLocal(uint64_t opEpoch) const;                // recv: the frame's op is the live local op''',
     '''  bool IsLiveLocal(uint64_t opEpoch) const;                // recv: the frame's op is the live local op
  // rel-0163 r7 (Codex codex_rel_0163_src_r6.md R3): the op a frame is processed for -- its epoch AND whether it is
  // a local op -- taken in ONE snapshot under the gate's lock, before the frame does anything else. The frame keeps
  // this ticket to its final commit and reveal (a cancel / new op in between is caught there, never by a second
  // read that could see a different op).
  struct FrameTicket {
    uint64_t epoch = 0;
    bool local = false;
  };
  FrameTicket TakeFrameTicket() const;'''),
    ('''  std::function<void()> testAfterFramePermit;''',
     '''  std::function<void()> testAfterFramePermit;
  // Tests only: right after a frame took its ticket (no gate lock held) -- a cancel / new op lands here.
  std::function<void(uint64_t epoch, bool local)> testAfterFrameTicket;'''),
])

edit('viewer_selection_gate.cpp', [
    ('''bool SelectionGateState::IsLiveLocal(uint64_t opEpoch) const {''',
     '''SelectionGateState::FrameTicket SelectionGateState::TakeFrameTicket() const {
  std::lock_guard<std::mutex> lk(mu);
  FrameTicket t;
  t.epoch = epoch.load(std::memory_order_acquire);
  t.local = pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire);
  return t;
}

bool SelectionGateState::IsLiveLocal(uint64_t opEpoch) const {'''),
    ('''  if (!frameLocal) return permit();  // an ordinary frame: as before (the generation gate decided it)''',
     '''  if (!frameLocal) {
    // rel-0163 r7: an ordinary frame is not mixed into a local op that began after its ticket was taken -- that op
    // shows only what it took a ticket for itself.
    if (pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire) &&
        epoch.load(std::memory_order_acquire) != opEpoch) {
      localFramesRefused.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    return permit();  // an ordinary frame: as before (the generation gate decided it)
  }'''),
])

edit('viewer_video_receiver_frame.cpp', [
    ('''    if (ctx.sel.EpochChanged(dec.recvSelectionEpoch)) {
      dec.lastRealDecodedCaptureUs = 0;
      // A fresh pick: drop stale reference frames and hold for the new generation's keyframe.
      dec.decoder.reset();
      gate.waitForKeyFrame = true;
    }''',
     '''    // rel-0163 r7 (Codex R3): this frame's op -- epoch and local mode in ONE snapshot (TakeFrameTicket), taken first;
    // the decoder is reset for that epoch, and the same ticket goes to the final commit and the reveal below.
    const SelectionGateState::FrameTicket ticket = ctx.sel.TakeFrameTicket();
    if (ctx.sel.testAfterFrameTicket) ctx.sel.testAfterFrameTicket(ticket.epoch, ticket.local);  // tests only
    if (ticket.epoch != dec.recvSelectionEpoch) {
      dec.recvSelectionEpoch = ticket.epoch;
      dec.lastRealDecodedCaptureUs = 0;
      // A fresh pick: drop stale reference frames and hold for the new generation's keyframe.
      dec.decoder.reset();
      gate.waitForKeyFrame = true;
    }'''),
    ('''    // rel-0163 r4 (Codex R3): the op this frame is processed for -- the token taken when the receive loop last saw
    // the selection epoch (it reset the decoder for it). Everything this frame binds or reveals is that op's.
    const uint64_t frameOp = dec.recvSelectionEpoch;
    // rel-0163 r5: ...and whether it was taken for a LOCAL op -- fixed now, never re-read later as ordinary.
    const bool frameLocal = ctx.sel.IsLiveLocal(frameOp);''',
     '''    // rel-0163 r4/r5/r7 (Codex R3): the op this frame is processed for and whether it is local -- the ticket's, never
    // re-read. Everything this frame binds, writes or reveals is that op's.
    const uint64_t frameOp = ticket.epoch;
    const bool frameLocal = ticket.local;'''),
])
print('ok')
