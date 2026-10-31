src = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\'
NL = chr(92) + 'n'

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

# ------------------------------------------------------------------ gate: the frame commit and the ack+panel boundary
edit('viewer_selection_gate.hpp', [
    ('''  bool IsLiveLocal(uint64_t opEpoch) const;                // recv: the frame's op is the live local op''',
     '''  bool IsLiveLocal(uint64_t opEpoch) const;                // recv: the frame's op is the live local op
  // rel-0163 r5 (Codex codex_rel_0163_src_r4.md R3): the FINAL adoption of a decoded picture, called inside the
  // FrameBuffer's own lock right before it is written. `frameLocal` is what the frame was processed as (taken when
  // its op token was taken -- a frame of a local op is never re-read as an ordinary one). For a local frame: only
  // while its op is still the live local op; an unbound op is bound HERE to `candidateGen` (a valid output exists
  // by now). *revealGen = the generation this adoption stands for. Ordinary frames: permitted as before.
  bool CommitFrame(uint64_t opEpoch, bool frameLocal, uint64_t candidateGen, uint64_t inputGeneration,
                   uint64_t* revealGen);
  // rel-0163 r5 (R4): a host WindowSelected, applied to the panel AND the gate under one boundary. `applyPanel`
  // runs only when the in-flight op asked for an ack (never for a local op) and returns {ok, streamGeneration}.
  SelectionAck ApplyHostAck(const std::function<std::pair<bool, uint64_t>()>& applyPanel);'''),
    ('''  std::function<void()> testAfterRoleRead;''',
     '''  std::function<void()> testAfterRoleRead;
  // Tests only: right before a decoded picture's final adoption (CommitFrame) -- a cancel / new op lands here.
  std::function<void()> testBeforeFrameCommit;
  std::atomic<uint64_t> localFramesRefused{0};  // pictures of a no-longer-live local op not adopted (telemetry)'''),
    ('''#include <functional>
#include <mutex>''', '''#include <functional>
#include <mutex>
#include <utility>'''),
])

edit('viewer_selection_gate.cpp', [
    ('''SelectionAck SelectionGateState::ApplyAck(bool ok, uint64_t streamGeneration) {''',
     '''bool SelectionGateState::CommitFrame(uint64_t opEpoch, bool frameLocal, uint64_t candidateGen, uint64_t inputGeneration,
                                     uint64_t* revealGen) {
  std::lock_guard<std::mutex> lk(mu);
  if (revealGen) *revealGen = inputGeneration;
  if (!frameLocal) return true;  // an ordinary frame: as before (the generation gate decided it)
  if (!pending.load(std::memory_order_acquire) || !localAckless.load(std::memory_order_acquire) ||
      epoch.load(std::memory_order_acquire) != opEpoch) {
    // Its op was cancelled / replaced / revealed-and-closed meanwhile. A revealed local op's later pictures are
    // ordinary from then on (pending is over) -- but this frame was taken for the op, and only the op it was
    // taken for may own it while that op is pending; after the reveal the active-generation filter applies.
    if (!pending.load(std::memory_order_acquire) && activeStreamGeneration.load(std::memory_order_acquire) != 0 &&
        epoch.load(std::memory_order_acquire) == opEpoch) {
      if (revealGen) *revealGen = activeStreamGeneration.load(std::memory_order_acquire);
      return true;  // the same op, already revealed: its stream goes on
    }
    localFramesRefused.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  if (expectedGeneration.load(std::memory_order_acquire) == 0) {
    if (candidateGen == 0) return false;  // no decoded keyframe of this op yet: nothing to stand for
    expectedGeneration.store(candidateGen, std::memory_order_release);
  }
  if (revealGen) *revealGen = expectedGeneration.load(std::memory_order_acquire);
  return true;
}

SelectionAck SelectionGateState::ApplyHostAck(const std::function<std::pair<bool, uint64_t>()>& applyPanel) {
  std::lock_guard<std::mutex> lk(mu);
  if (pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire)) {
    return SelectionAck::IgnoredLocal;  // the panel is not touched either
  }
  const std::pair<bool, uint64_t> r = applyPanel();
  if (!pending.load(std::memory_order_acquire)) return SelectionAck::NotPending;
  if (r.first) {
    expectedGeneration.store(r.second, std::memory_order_release);
    awaitingAck.store(false, std::memory_order_release);
    return SelectionAck::Acked;
  }
  return SelectionAck::Failed;
}

SelectionAck SelectionGateState::ApplyAck(bool ok, uint64_t streamGeneration) {'''),
])

# ------------------------------------------------------------------ receive path: the op and its mode are the frame's, committed at the FrameBuffer
edit('viewer_video_receiver_frame.cpp', [
    ('''    const uint64_t frameOp = dec.recvSelectionEpoch;''',
     '''    const uint64_t frameOp = dec.recvSelectionEpoch;
    // rel-0163 r5: ...and whether it was taken for a LOCAL op -- fixed now, never re-read later as ordinary.
    const bool frameLocal = ctx.sel.IsLiveLocal(frameOp);'''),
    ('''    // rel-0163 r4 (Codex R3): output came -- the decoder (reset for this op, started on a keyframe) produced a
    // picture: the local op takes its candidate keyframe's generation, for THIS op only.
    if (dec.localCandidateOp == frameOp && dec.localCandidateGen != 0 &&
        ctx.sel.BindLocalGeneration(frameOp, dec.localCandidateGen)) {
      std::cout << "[native-video-client][picker] local selection bound to streamGeneration=" << dec.localCandidateGen
                << " epoch=" << frameOp << " (its first decoded output)''' + NL + '''";
    }
    // A picture of an op that is no longer the live one is not adopted for a local op (a newer pick began).
    if (ctx.sel.localAckless.load(std::memory_order_acquire) && ctx.sel.pending.load(std::memory_order_acquire) &&
        !ctx.sel.IsLiveLocal(frameOp)) {
      ++st.skippedQueued;
      return true;
    }
''',
     '''    // rel-0163 r5 (Codex R3): a local op's generation is bound and its picture adopted at ONE point -- the final
    // FrameBuffer commit below (CommitFrame inside the FrameBuffer lock), after the output is known to be valid.
'''),
    ('''    ctx.recvLive.Enter(RecvStage::Publish, nowUs);
    {
      std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);''',
     '''    ctx.recvLive.Enter(RecvStage::Publish, nowUs);
    if (ctx.sel.testBeforeFrameCommit) ctx.sel.testBeforeFrameCommit();  // tests only
    uint64_t revealGen = h.streamGeneration;
    {
      std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
      // rel-0163 r5 (Codex R3): the final adoption -- this frame's op (and, for a local op, its generation bind)
      // checked in the same step that writes the FrameBuffer. A picture of an op no longer live is not written.
      const uint64_t candidate = dec.localCandidateOp == frameOp ? dec.localCandidateGen : 0;
      const bool wasUnbound = frameLocal && ctx.sel.expectedGeneration.load(std::memory_order_acquire) == 0;
      if (!ctx.sel.CommitFrame(frameOp, frameLocal, candidate, h.streamGeneration, &revealGen)) {
        ++st.skippedQueued;
        return true;
      }
      if (wasUnbound && frameLocal) {
        std::cout << "[native-video-client][picker] local selection bound to streamGeneration=" << revealGen
                  << " epoch=" << frameOp << " (its first adopted output)''' + NL + '''";
      }'''),
    ('''    if (ctx.sel.AckedSelectionPending()) {
      // rel-0163 r4 (Codex R3): the candidate is this frame's op -- never the live epoch read now. For a local op its
      // generation is the bound one (the decoded keyframe's), not the current input header's.
      const uint64_t revealGen = ctx.sel.localAckless.load(std::memory_order_acquire)
                                     ? ctx.sel.expectedGeneration.load(std::memory_order_acquire)
                                     : h.streamGeneration;
      post_pc_selection_reveal(ctx, revealGen, frameOp);
    }''',
     '''    if (ctx.sel.AckedSelectionPending()) {
      // rel-0163 r4/r5 (Codex R3): the candidate is this frame's op and the generation its adoption stood for (for a
      // local op the bound one) -- never the live epoch or the current input header read now.
      post_pc_selection_reveal(ctx, revealGen, frameOp);
    }'''),
])

# ------------------------------------------------------------------ picker: R1 run captured at the click; R4 one boundary
edit('viewer_picker.cpp', [
    ('''bool begin_pc_target_selection(ViewerState& ctx, uint64_t windowId, const char* statusText) {
  if (ctx.sel.pending.load(std::memory_order_acquire)) return false;''',
     '''bool begin_pc_target_selection(ViewerState& ctx, uint64_t windowId, const char* statusText) {
  // rel-0163 r5 (Codex R1-run): the control run this pick was made on, taken FIRST -- a held pick stays that run's.
  const uint64_t runAtPick = ctx.session.controlRunGen.load(std::memory_order_seq_cst);
  if (ctx.sel.pending.load(std::memory_order_acquire)) return false;'''),
    ('''    ctx.sel.deferredRun.store(ctx.session.controlRunGen.load(std::memory_order_acquire), std::memory_order_seq_cst);''',
     '''    ctx.sel.deferredRun.store(runAtPick, std::memory_order_seq_cst);'''),
    ('''    if (ctx.session.hostRoleKnown.load(std::memory_order_seq_cst)) run_deferred_pc_selection(ctx);
    return true;''',
     '''    if (ctx.session.hostRoleKnown.load(std::memory_order_seq_cst) &&
        ctx.session.controlRunGen.load(std::memory_order_seq_cst) == runAtPick) {
      run_deferred_pc_selection(ctx);
    }
    return true;'''),
    ('''void apply_window_selected_result(ViewerState& ctx, const ControlWindowSelectedMessage& msg) {
  // rel-0163 r4 (Codex R4): a local op asked for no ack -- one arriving now is an older op's: neither the panel nor
  // the gate, the generation, the stream or the pending state changes.
  if (ctx.sel.pending.load(std::memory_order_acquire) && ctx.sel.localAckless.load(std::memory_order_acquire)) {
    log_client_line(ctx, "[native-video-client][picker] window-selected ignored: the pick in flight is local (no ack asked) seq=" +
                             std::to_string(msg.seq));
    return;
  }
  const auto result = ctx.picker.windowPanel.ApplyWindowSelected(msg);
  log_client_line(ctx, result.logLine);
  switch (ctx.sel.ApplyAck(result.ok, msg.streamGeneration)) {''',
     '''void apply_window_selected_result(ViewerState& ctx, const ControlWindowSelectedMessage& msg) {
  // rel-0163 r4/r5 (Codex R4): whether the in-flight op takes an ack, and the panel's and the gate's application,
  // are ONE boundary (ApplyHostAck): a local op asked for no ack -- one arriving now is an older op's, and neither
  // the panel nor the gate, the generation, the stream or the pending state changes.
  if (ctx.sel.testBeforeAckBoundary) ctx.sel.testBeforeAckBoundary();  // tests only
  WindowSelectApplyResult result{};
  const SelectionAck ack = ctx.sel.ApplyHostAck([&] {
    result = ctx.picker.windowPanel.ApplyWindowSelected(msg);
    return std::make_pair(result.ok, msg.streamGeneration);
  });
  if (ack == SelectionAck::IgnoredLocal) {
    log_client_line(ctx, "[native-video-client][picker] window-selected ignored: the pick in flight is local (no ack asked) seq=" +
                             std::to_string(msg.seq));
    return;
  }
  log_client_line(ctx, result.logLine);
  switch (ack) {'''),
    ('''    case SelectionAck::IgnoredLocal:  // (raced to local after the check above) -- nothing
      return;''',
     '''    case SelectionAck::IgnoredLocal:  // handled above
      return;'''),
])

edit('viewer_selection_gate.hpp', [
    ('''  std::function<void()> testBeforeFrameCommit;''',
     '''  std::function<void()> testBeforeFrameCommit;
  // Tests only: right before a host ack's boundary (apply_window_selected_result) -- a local op begins here.
  std::function<void()> testBeforeAckBoundary;'''),
])
print('ok')
