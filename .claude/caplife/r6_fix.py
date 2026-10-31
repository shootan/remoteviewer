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
    ('''  bool CommitFrame(uint64_t opEpoch, bool frameLocal, uint64_t candidateGen, uint64_t inputGeneration,
                   uint64_t* revealGen);''',
     '''  // rel-0163 r6 (Codex codex_rel_0163_src_r5.md R3 1): when it permits, the gate's lock is handed to the caller
  // in *held and must be kept until the FrameBuffer write is done -- a Begin/Clear (which take only this lock)
  // can then never fall between the permission and the write. Lock order: FrameBuffer, then this.
  bool CommitFrame(uint64_t opEpoch, bool frameLocal, uint64_t candidateGen, uint64_t inputGeneration,
                   uint64_t* revealGen, std::unique_lock<std::mutex>* held);'''),
    ('''  std::atomic<uint64_t> activeStreamGeneration{0};''',
     '''  std::atomic<uint64_t> activeStreamGeneration{0};
  // rel-0163 r6 (R3 2): the op whose reveal was committed (with activeStreamGeneration, its generation) -- only
  // THAT op's later pictures go on after its pending is over; a cancelled op of the same epoch is not it.
  std::atomic<uint64_t> revealedOp{0};'''),
])

edit('viewer_selection_gate.cpp', [
    ('''bool SelectionGateState::CommitFrame(uint64_t opEpoch, bool frameLocal, uint64_t candidateGen, uint64_t inputGeneration,
                                     uint64_t* revealGen) {
  std::lock_guard<std::mutex> lk(mu);
  if (revealGen) *revealGen = inputGeneration;
  if (!frameLocal) return true;  // an ordinary frame: as before (the generation gate decided it)''',
     '''bool SelectionGateState::CommitFrame(uint64_t opEpoch, bool frameLocal, uint64_t candidateGen, uint64_t inputGeneration,
                                     uint64_t* revealGen, std::unique_lock<std::mutex>* held) {
  std::unique_lock<std::mutex> lk(mu);
  if (revealGen) *revealGen = inputGeneration;
  const auto permit = [&] {
    if (held) *held = std::move(lk);  // kept by the caller until its write is done
    return true;
  };
  if (!frameLocal) return permit();  // an ordinary frame: as before (the generation gate decided it)'''),
    ('''    if (!pending.load(std::memory_order_acquire) && activeStreamGeneration.load(std::memory_order_acquire) != 0 &&
        epoch.load(std::memory_order_acquire) == opEpoch) {
      if (revealGen) *revealGen = activeStreamGeneration.load(std::memory_order_acquire);
      return true;  // the same op, already revealed: its stream goes on
    }''',
     '''    // rel-0163 r6 (R3 2): only the op that WAS revealed (its reveal committed with this op's epoch) -- a later op
    // cancelled before its own reveal has the same epoch but is not it, and its pictures are refused.
    if (!pending.load(std::memory_order_acquire) && revealedOp.load(std::memory_order_acquire) == opEpoch &&
        epoch.load(std::memory_order_acquire) == opEpoch && activeStreamGeneration.load(std::memory_order_acquire) != 0) {
      if (revealGen) *revealGen = activeStreamGeneration.load(std::memory_order_acquire);
      return permit();  // the same op, already revealed: its stream goes on
    }'''),
    ('''  if (revealGen) *revealGen = expectedGeneration.load(std::memory_order_acquire);
  return true;
}

SelectionAck SelectionGateState::ApplyHostAck(''',
     '''  if (revealGen) *revealGen = expectedGeneration.load(std::memory_order_acquire);
  return permit();
}

SelectionAck SelectionGateState::ApplyHostAck('''),
    ('''  if (commit) {
    // Persistent filter for late stragglers from the previous target (see the recv gate).
    activeStreamGeneration.store(readyGeneration.load(std::memory_order_acquire),
                                 std::memory_order_release);
  }''',
     '''  if (commit) {
    // Persistent filter for late stragglers from the previous target (see the recv gate).
    activeStreamGeneration.store(readyGeneration.load(std::memory_order_acquire),
                                 std::memory_order_release);
    revealedOp.store(readyEpoch.load(std::memory_order_acquire), std::memory_order_release);  // rel-0163 r6
  }'''),
])

edit('viewer_video_receiver_frame.cpp', [
    ('''      const bool wasUnbound = frameLocal && ctx.sel.expectedGeneration.load(std::memory_order_acquire) == 0;
      if (!ctx.sel.CommitFrame(frameOp, frameLocal, candidate, h.streamGeneration, &revealGen)) {
        ++st.skippedQueued;
        return true;
      }''',
     '''      const bool wasUnbound = frameLocal && ctx.sel.expectedGeneration.load(std::memory_order_acquire) == 0;
      // rel-0163 r6 (R3 1): the permission holds the gate's lock to the end of this write (selHeld, released with
      // this scope) -- no Begin/Clear can fall between "this op is live" and the picture being written.
      std::unique_lock<std::mutex> selHeld;
      if (!ctx.sel.CommitFrame(frameOp, frameLocal, candidate, h.streamGeneration, &revealGen, &selHeld)) {
        ++st.skippedQueued;
        return true;
      }
      if (ctx.sel.testAfterFramePermit) ctx.sel.testAfterFramePermit();  // tests only (the lock is held here)'''),
])

edit('viewer_selection_gate.hpp', [
    ('''  std::function<void()> testBeforeFrameCommit;''',
     '''  std::function<void()> testBeforeFrameCommit;
  // Tests only: between a picture's permission and its write (the gate's lock held) -- a Begin/Clear tried here
  // must wait until the write is done.
  std::function<void()> testAfterFramePermit;'''),
])
print('ok')
