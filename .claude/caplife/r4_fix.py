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

# ============================================================== gate: one lock over the transaction's state; op tokens
edit('viewer_selection_gate.hpp', [
    ('''//   deferredPick           : a pick made before the host's role was known (its first pong) -- id + 1, 0 =
//                            none. Run once when the role is known (kMsgRunDeferredPick), never sent blind.''',
     '''//   deferredPick           : a pick made before the host's role was known (its first pong) -- id + 1, 0 =
//                            none, with deferredRun (the control run it belongs to). Run once when the role is
//                            known (kMsgRunDeferredPick, or the UI's own re-check right after holding it).
// rel-0163 r4 (Codex codex_rel_0163_src_r3.md R3/R4): every transition and every read that decides a frame or a
// reveal takes `mu`, so Begin/BeginLocal/Clear/Convert/bind/commit are seen whole. A local op's generation is
// bound only for THAT op (its epoch, the token the receive path took when it began the frame), so an old frame
// can never bind a newer op; a host ack never touches a local op (IgnoredLocal).'''),
    ('''  NotPending = 0,  // no PC-side selection tracked (legacy stream-view session): caller's old path
  Acked = 1,       // the transaction now waits for the first frame of its generation
  Failed = 2,      // the caller stops the speculative stream and clears the transaction
};''',
     '''  NotPending = 0,  // no PC-side selection tracked (legacy stream-view session): caller's old path
  Acked = 1,       // the transaction now waits for the first frame of its generation
  Failed = 2,      // the caller stops the speculative stream and clears the transaction
  IgnoredLocal = 3,  // rel-0163 r4: the in-flight op is local (no ack asked) -- an ack changes nothing
};'''),
    ('''  std::atomic<bool> localAckless{false};     // rel-0163 r3: see above
  std::atomic<uint64_t> deferredPick{0};     // rel-0163 r3: UI writes, control reads (posts), UI runs''',
     '''  std::atomic<bool> localAckless{false};     // rel-0163 r3: see above
  std::atomic<uint64_t> deferredPick{0};     // rel-0163 r3: UI writes, control reads (posts), UI runs
  std::atomic<uint64_t> deferredRun{0};      // rel-0163 r4: the control run the held pick belongs to
  mutable std::mutex mu;                     // rel-0163 r4: the transitions and the deciding reads'''),
    ('''  bool LocalUnbound() const;                               // recv: a local transaction still waiting for its keyframe
  bool BindLocalGeneration(uint64_t streamGeneration);     // recv: bind it to the first admitted keyframe's generation''',
     '''  bool LocalUnbound(uint64_t opEpoch) const;               // recv: THAT local op still waits for its keyframe
  // recv: bind THAT local op to the generation of the keyframe whose decode produced its first output.
  bool BindLocalGeneration(uint64_t opEpoch, uint64_t streamGeneration);
  bool IsLiveLocal(uint64_t opEpoch) const;                // recv: the frame's op is the live local op'''),
])

p = src + 'viewer_selection_gate.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
start = s.index('void SelectionGateState::Begin() {')
end = s.index('bool SelectionGateState::EpochChanged(uint64_t& seenEpoch) {')
s = s[:start] + '''void SelectionGateState::Begin() {
  std::lock_guard<std::mutex> lk(mu);
  localAckless.store(false, std::memory_order_release);
  expectedGeneration.store(0, std::memory_order_release);
  awaitingAck.store(true, std::memory_order_release);
  pending.store(true, std::memory_order_release);
  // Bumped so the receive loop resets the decoder and holds for the new generation's keyframe.
  epoch.fetch_add(1, std::memory_order_acq_rel);
}

void SelectionGateState::Clear() {
  std::lock_guard<std::mutex> lk(mu);
  pending.store(false, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  expectedGeneration.store(0, std::memory_order_release);
  localAckless.store(false, std::memory_order_release);
}
// rel-0163 r3: the same transaction as Begin -- pending, a new epoch (the receive loop resets the decoder and
// waits for a keyframe) -- without an ack to wait for. Its generation is bound by its first decoded keyframe.
void SelectionGateState::BeginLocal() {
  std::lock_guard<std::mutex> lk(mu);
  expectedGeneration.store(0, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  localAckless.store(true, std::memory_order_release);
  pending.store(true, std::memory_order_release);
  epoch.fetch_add(1, std::memory_order_acq_rel);
}
// A transaction begun the ordinary way whose WindowSelect(0) turned out unsendable (the host's role became
// known as the logon screen before it went out): the SAME op -- epoch unchanged, its decoder reset already done --
// continues as a local one. Only that op (its own tag), only while it still waits for its ack.
bool SelectionGateState::ConvertToLocal(uint64_t opEpoch) {
  std::lock_guard<std::mutex> lk(mu);
  if (!pending.load(std::memory_order_acquire) || !awaitingAck.load(std::memory_order_acquire) ||
      localAckless.load(std::memory_order_acquire) || epoch.load(std::memory_order_acquire) != opEpoch) {
    return false;
  }
  expectedGeneration.store(0, std::memory_order_release);
  localAckless.store(true, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  return true;
}
bool SelectionGateState::LocalUnbound(uint64_t opEpoch) const {
  std::lock_guard<std::mutex> lk(mu);
  return pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire) &&
         epoch.load(std::memory_order_acquire) == opEpoch && expectedGeneration.load(std::memory_order_acquire) == 0;
}
bool SelectionGateState::IsLiveLocal(uint64_t opEpoch) const {
  std::lock_guard<std::mutex> lk(mu);
  return pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire) &&
         epoch.load(std::memory_order_acquire) == opEpoch;
}
bool SelectionGateState::BindLocalGeneration(uint64_t opEpoch, uint64_t streamGeneration) {
  std::lock_guard<std::mutex> lk(mu);
  if (streamGeneration == 0 || !pending.load(std::memory_order_acquire) || !localAckless.load(std::memory_order_acquire) ||
      epoch.load(std::memory_order_acquire) != opEpoch || expectedGeneration.load(std::memory_order_acquire) != 0) {
    return false;
  }
  expectedGeneration.store(streamGeneration, std::memory_order_release);
  return true;
}

SelectionAck SelectionGateState::ApplyAck(bool ok, uint64_t streamGeneration) {
  std::lock_guard<std::mutex> lk(mu);
  if (!pending.load(std::memory_order_acquire)) return SelectionAck::NotPending;
  // rel-0163 r4 (Codex R4): a local op asked for no ack; an ack arriving now is an older op's -- success or
  // failure, bound or not, it changes nothing here.
  if (localAckless.load(std::memory_order_acquire)) return SelectionAck::IgnoredLocal;
  if (ok) {
    // Ack received: hold the picker up until the first frame of this generation is presented.
    // Do NOT hide the picker here -- that is what the first-frame gate is for.
    expectedGeneration.store(streamGeneration, std::memory_order_release);
    awaitingAck.store(false, std::memory_order_release);
    return SelectionAck::Acked;
  }
  return SelectionAck::Failed;
}

''' + s[end:]
s = s.replace('''SelectionAdmit SelectionGateState::AdmitGeneration(uint64_t streamGeneration) const {
  if (pending.load(std::memory_order_acquire)) {''', '''SelectionAdmit SelectionGateState::AdmitGeneration(uint64_t streamGeneration) const {
  std::lock_guard<std::mutex> lk(mu);
  if (pending.load(std::memory_order_acquire)) {''')
s = s.replace('''bool SelectionGateState::AckedSelectionPending() const {
  // rel-0163 r3''', '''bool SelectionGateState::AckedSelectionPending() const {
  std::lock_guard<std::mutex> lk(mu);
  // rel-0163 r3''')
s = s.replace('''bool SelectionGateState::CommitReveal() {''', '''bool SelectionGateState::CommitReveal() {
  std::lock_guard<std::mutex> lk(mu);''')
assert s.count('std::lock_guard<std::mutex> lk(mu);') == 11, s.count('std::lock_guard<std::mutex> lk(mu);')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

edit('viewer_selection_gate.hpp', [
    ('''#include "viewer_common.hpp"''', '''#include "viewer_common.hpp"

#include <mutex>'''),
])

# ============================================================== decoder state: the local candidate
edit('viewer_decoder_state.hpp', [
    ('''  uint64_t recvSelectionEpoch = 0;  // set from ctx.sel.epoch at thread start''',
     '''  uint64_t recvSelectionEpoch = 0;  // set from ctx.sel.epoch at thread start
  // rel-0163 r4 (Codex R3): a local op's candidate -- the generation of the last keyframe the frame gate sent to
  // the decoder for that op (recv thread only). Bound only when its decode produces output.
  uint64_t localCandidateOp = 0;
  uint64_t localCandidateGen = 0;'''),
])

# ============================================================== receive path: bind on a decoded output, under the frame's op
edit('viewer_video_receiver_frame.cpp', [
    ('''    // rel-0163 r3: a local (logon-screen desktop) selection takes the generation of the first KEYFRAME admitted
    // after it began (the decoder was reset for it and waits for a keyframe); from here frames of another
    // generation drop as after an ack, and the reveal below can only come from that generation.
    if ((h.flags & kEncodedFrameFlagKeyFrame) != 0 && ctx.sel.LocalUnbound() &&
        ctx.sel.BindLocalGeneration(h.streamGeneration)) {
      std::cout << "[native-video-client][picker] local selection bound to streamGeneration=" << h.streamGeneration
                << " epoch=" << ctx.sel.epoch.load(std::memory_order_acquire) << "''' + NL + '''";
    }
''',
     '''    // rel-0163 r4 (Codex R3): the op this frame is processed for -- the token taken when the receive loop last saw
    // the selection epoch (it reset the decoder for it). Everything this frame binds or reveals is that op's.
    const uint64_t frameOp = dec.recvSelectionEpoch;
'''),
    ('''    if (keyFrame && activeAssembler_ != nullptr) {''',
     '''    // rel-0163 r4: a keyframe the frame gate sends to the decoder for a local op that has no generation yet is its
    // candidate (bound below only if its decode produces output -- a rejected or failed key never binds).
    if (keyFrame && ctx.sel.LocalUnbound(frameOp)) {
      dec.localCandidateOp = frameOp;
      dec.localCandidateGen = h.streamGeneration;
    }
    if (keyFrame && activeAssembler_ != nullptr) {'''),
    ('''    fg.clear_empty_streak();
''',
     '''    fg.clear_empty_streak();
    // rel-0163 r4 (Codex R3): output came -- the decoder (reset for this op, started on a keyframe) produced a
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
'''),
    ('''    if (ctx.sel.AckedSelectionPending()) {
      post_pc_selection_reveal(ctx, h.streamGeneration,
                               ctx.sel.epoch.load(std::memory_order_acquire));
    }''',
     '''    if (ctx.sel.AckedSelectionPending()) {
      // rel-0163 r4 (Codex R3): the candidate is this frame's op -- never the live epoch read now. For a local op its
      // generation is the bound one (the decoded keyframe's), not the current input header's.
      const uint64_t revealGen = ctx.sel.localAckless.load(std::memory_order_acquire)
                                     ? ctx.sel.expectedGeneration.load(std::memory_order_acquire)
                                     : h.streamGeneration;
      post_pc_selection_reveal(ctx, revealGen, frameOp);
    }'''),
])

# ============================================================== picker: R1 handshake, R2 op tag, R4 ack
edit('viewer_picker.cpp', [
    ('''  if (!ctx.session.hostRoleKnown.load(std::memory_order_acquire)) {
    ctx.sel.deferredPick.store(windowId + 1, std::memory_order_release);
    ctx.picker.windowPanel.SetStatus("waiting_host_role");
    std::cout << "[native-video-client][picker] pick held until the host's role is known id=" << windowId << "''' + NL + '''";
    if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
    return true;
  }
  ctx.sel.deferredPick.store(0, std::memory_order_release);''',
     '''  if (!ctx.session.hostRoleKnown.load(std::memory_order_seq_cst)) {
    if (ctx.sel.testAfterRoleRead) ctx.sel.testAfterRoleRead();  // tests only: a pong lands here
    ctx.sel.deferredRun.store(ctx.session.controlRunGen.load(std::memory_order_acquire), std::memory_order_seq_cst);
    ctx.sel.deferredPick.store(windowId + 1, std::memory_order_seq_cst);
    ctx.picker.windowPanel.SetStatus("waiting_host_role");
    std::cout << "[native-video-client][picker] pick held until the host's role is known id=" << windowId << "''' + NL + '''";
    if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
    // rel-0163 r4 (Codex R1): the handshake with the first pong. The control thread sets the role and THEN looks
    // for a held pick; this side holds the pick and THEN looks at the role again (both sequentially consistent):
    // at least one of them sees the other, so the pick is never lost; run_deferred_pc_selection takes it once.
    if (ctx.session.hostRoleKnown.load(std::memory_order_seq_cst)) run_deferred_pc_selection(ctx);
    return true;
  }
  ctx.sel.deferredPick.store(0, std::memory_order_release);'''),
    ('''  ctx.sel.deferredPick.store(0, std::memory_order_release);  // rel-0163 r3: a held pick ends with it
}''',
     '''  ctx.sel.deferredPick.store(0, std::memory_order_release);  // rel-0163 r3: a held pick ends with it
}

// rel-0163 r4: the control thread's half of the R1 handshake -- the role just became known.
void note_host_role_known(ViewerState& ctx, bool firstTime) {
  if (firstTime && ctx.sel.deferredPick.load(std::memory_order_seq_cst) != 0 && ctx.session.hwnd) {
    PostMessageW(ctx.session.hwnd, kMsgRunDeferredPick, 0, 0);
  }
}'''),
    ('''void run_deferred_pc_selection(ViewerState& ctx) {
  const uint64_t held = ctx.sel.deferredPick.exchange(0, std::memory_order_acq_rel);
  if (held == 0) return;''',
     '''void run_deferred_pc_selection(ViewerState& ctx) {
  const uint64_t held = ctx.sel.deferredPick.exchange(0, std::memory_order_seq_cst);
  if (held == 0) return;  // already run (the UI's own re-check and the pong's message may both come): once
  // A pick held on an earlier connection is that connection's.
  if (ctx.sel.deferredRun.load(std::memory_order_acquire) != ctx.session.controlRunGen.load(std::memory_order_acquire)) return;'''),
    # R2: the op tag rides the request
    ('''  // RequestSelect refuses when the target is locked by host config; do not touch the stream then.
  if (!ctx.picker.windowPanel.RequestSelect(windowId, statusText)) return false;
  ctx.sel.Begin();''',
     '''  // RequestSelect refuses when the target is locked by host config; do not touch the stream then.
  // rel-0163 r4 (Codex R2): the request carries its op's tag (the epoch Begin is about to give it -- the UI thread
  // is the only one that begins a selection), so what becomes of the request is that op's alone.
  const uint64_t opTag = ctx.sel.epoch.load(std::memory_order_acquire) + 1;
  if (!ctx.picker.windowPanel.RequestSelect(windowId, statusText, opTag)) return false;
  ctx.sel.Begin();'''),
    # R4: the ack is applied to the panel only when the gate takes it
    ('''void apply_window_selected_result(ViewerState& ctx, const ControlWindowSelectedMessage& msg) {
  const auto result = ctx.picker.windowPanel.ApplyWindowSelected(msg);''',
     '''void apply_window_selected_result(ViewerState& ctx, const ControlWindowSelectedMessage& msg) {
  // rel-0163 r4 (Codex R4): a local op asked for no ack -- one arriving now is an older op's: neither the panel nor
  // the gate, the generation, the stream or the pending state changes.
  if (ctx.sel.pending.load(std::memory_order_acquire) && ctx.sel.localAckless.load(std::memory_order_acquire)) {
    log_client_line(ctx, "[native-video-client][picker] window-selected ignored: the pick in flight is local (no ack asked) seq=" +
                             std::to_string(msg.seq));
    return;
  }
  const auto result = ctx.picker.windowPanel.ApplyWindowSelected(msg);'''),
    ('''    case SelectionAck::NotPending:
      break;''',
     '''    case SelectionAck::IgnoredLocal:  // (raced to local after the check above) -- nothing
      return;
    case SelectionAck::NotPending:
      break;'''),
])

edit('viewer_selection_gate.hpp', [
    ('''  mutable std::mutex mu;                     // rel-0163 r4: the transitions and the deciding reads''',
     '''  mutable std::mutex mu;                     // rel-0163 r4: the transitions and the deciding reads
  // Tests only (empty in the product): runs between the UI's role read and the held pick's store -- a
  // pong landing exactly there (Codex R1's order).
  std::function<void()> testAfterRoleRead;'''),
    ('''#include <mutex>''', '''#include <functional>
#include <mutex>'''),
])

edit('viewer_picker.hpp', [
    ('''void run_deferred_pc_selection(ViewerState& ctx);''',
     '''void run_deferred_pc_selection(ViewerState& ctx);
// rel-0163 r4: the role just became known (control thread) -- posts the held pick's run (R1 handshake).
void note_host_role_known(ViewerState& ctx, bool firstTime);'''),
])

edit('viewer_control_client.cpp', [
    ('''    const bool roleWasKnown = ctx.session.hostRoleKnown.exchange(true, std::memory_order_acq_rel);  // r7 H6
    // rel-0163 r3: a pick held until now runs once, on the UI thread.
    if (!roleWasKnown && ctx.sel.deferredPick.load(std::memory_order_acquire) != 0 && ctx.session.hwnd) {
      PostMessageW(ctx.session.hwnd, kMsgRunDeferredPick, 0, 0);
    }''',
     '''    const bool roleWasKnown = ctx.session.hostRoleKnown.exchange(true, std::memory_order_seq_cst);  // r7 H6
    // rel-0163 r3/r4: a pick held until now runs once, on the UI thread (the R1 handshake's control half).
    note_host_role_known(ctx, !roleWasKnown);'''),
    ('''          PostMessageW(ctx.session.hwnd, kMsgUnsendableSelect, static_cast<WPARAM>(action.windowSelect.windowId),
                       static_cast<LPARAM>(ctx.sel.epoch.load(std::memory_order_acquire)));''',
     '''          // rel-0163 r4 (Codex R2): the op tag the request was made with -- never the live epoch read now.
          PostMessageW(ctx.session.hwnd, kMsgUnsendableSelect, static_cast<WPARAM>(action.windowSelect.windowId),
                       static_cast<LPARAM>(action.selectionTag));'''),
])
print('ok')
