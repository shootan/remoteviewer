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

# ---------------------------------------------------------------- the gate
edit('viewer_selection_gate.hpp', [
    ('''//   activeStreamGeneration : generation of the last successfully revealed selection; after
//                            reveal this is the persistent filter (0 = accept anything, which
//                            covers the legacy stream-view start and the window before any pick).''',
     '''//   activeStreamGeneration : generation of the last successfully revealed selection; after
//                            reveal this is the persistent filter (0 = accept anything, which
//                            covers the legacy stream-view start and the window before any pick).
//   localAckless           : rel-0163 r3 (Codex codex_rel_0163_e1_decision.md): a DESKTOP pick at a host
//                            known to be at the logon screen (kCaptureFlagPreloginV1). Such a host takes no
//                            WindowSelect (its allowlist), so no ack will ever come: the transaction is the
//                            same -- pending, its own epoch (decoder reset, wait for a keyframe) -- but its
//                            generation is the one of the FIRST KEYFRAME admitted after it began (bound
//                            once, BindLocalGeneration); until then nothing can reveal, after it frames of
//                            another generation drop exactly as after an ack.
//   deferredPick           : a pick made before the host's role was known (its first pong) -- id + 1, 0 =
//                            none. Run once when the role is known (kMsgRunDeferredPick), never sent blind.''',
     ),
    ('''  std::atomic<uint64_t> activeStreamGeneration{0};''',
     '''  std::atomic<uint64_t> activeStreamGeneration{0};
  std::atomic<bool> localAckless{false};     // rel-0163 r3: see above
  std::atomic<uint64_t> deferredPick{0};     // rel-0163 r3: UI writes, control reads (posts), UI runs'''),
    ('''  void Begin();                                            // UI: a pick was accepted for sending''',
     '''  void Begin();                                            // UI: a pick was accepted for sending
  void BeginLocal();                                       // UI: rel-0163 r3 -- a desktop pick at a logon-screen host
  bool ConvertToLocal(uint64_t opEpoch);                   // UI: that op's WindowSelect(0) was not sendable after all
  bool LocalUnbound() const;                               // recv: a local transaction still waiting for its keyframe
  bool BindLocalGeneration(uint64_t streamGeneration);     // recv: bind it to the first admitted keyframe's generation'''),
])

edit('viewer_selection_gate.cpp', [
    ('''void SelectionGateState::Begin() {
  expectedGeneration.store(0, std::memory_order_release);
  awaitingAck.store(true, std::memory_order_release);''',
     '''void SelectionGateState::Begin() {
  localAckless.store(false, std::memory_order_release);
  expectedGeneration.store(0, std::memory_order_release);
  awaitingAck.store(true, std::memory_order_release);'''),
    ('''void SelectionGateState::Clear() {
  pending.store(false, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  expectedGeneration.store(0, std::memory_order_release);
}''',
     '''void SelectionGateState::Clear() {
  pending.store(false, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  expectedGeneration.store(0, std::memory_order_release);
  localAckless.store(false, std::memory_order_release);
}
// rel-0163 r3: the same transaction as Begin -- pending, a new epoch (the receive loop resets the decoder and
// waits for a keyframe) -- without an ack to wait for. Its generation is bound by the first admitted keyframe.
void SelectionGateState::BeginLocal() {
  expectedGeneration.store(0, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  localAckless.store(true, std::memory_order_release);
  pending.store(true, std::memory_order_release);
  epoch.fetch_add(1, std::memory_order_acq_rel);
}
// A transaction begun the ordinary way whose WindowSelect(0) turned out unsendable (the host's role became
// known as the logon screen before it went out): the SAME op -- epoch unchanged, its decoder reset already done --
// continues as a local one. Only that op, only while it still waits for its ack.
bool SelectionGateState::ConvertToLocal(uint64_t opEpoch) {
  if (!pending.load(std::memory_order_acquire) || !awaitingAck.load(std::memory_order_acquire) ||
      epoch.load(std::memory_order_acquire) != opEpoch) {
    return false;
  }
  expectedGeneration.store(0, std::memory_order_release);
  localAckless.store(true, std::memory_order_release);
  awaitingAck.store(false, std::memory_order_release);
  return true;
}
bool SelectionGateState::LocalUnbound() const {
  return pending.load(std::memory_order_acquire) && localAckless.load(std::memory_order_acquire) &&
         expectedGeneration.load(std::memory_order_acquire) == 0;
}
bool SelectionGateState::BindLocalGeneration(uint64_t streamGeneration) {
  if (streamGeneration == 0 || !LocalUnbound()) return false;
  uint64_t unbound = 0;
  return expectedGeneration.compare_exchange_strong(unbound, streamGeneration, std::memory_order_acq_rel);
}'''),
    ('''bool SelectionGateState::AckedSelectionPending() const {
  return pending.load(std::memory_order_acquire) && !awaitingAck.load(std::memory_order_acquire);
}''',
     '''bool SelectionGateState::AckedSelectionPending() const {
  // rel-0163 r3: a local transaction counts once its generation is bound (its first keyframe), not before.
  return pending.load(std::memory_order_acquire) && !awaitingAck.load(std::memory_order_acquire) &&
         !(localAckless.load(std::memory_order_acquire) && expectedGeneration.load(std::memory_order_acquire) == 0);
}'''),
])

# ---------------------------------------------------------------- the receive path: bind on the first admitted keyframe
edit('viewer_video_receiver_frame.cpp', [
    ('''    if (ctx.sel.AdmitGeneration(h.streamGeneration) != SelectionAdmit::Accept) {
      ++st.skippedQueued;
      return true;
    }
''',
     '''    if (ctx.sel.AdmitGeneration(h.streamGeneration) != SelectionAdmit::Accept) {
      ++st.skippedQueued;
      return true;
    }
    // rel-0163 r3: a local (logon-screen desktop) selection takes the generation of the first KEYFRAME admitted
    // after it began (the decoder was reset for it and waits for a keyframe); from here frames of another
    // generation drop as after an ack, and the reveal below can only come from that generation.
    if ((h.flags & kEncodedFrameFlagKeyFrame) != 0 && ctx.sel.LocalUnbound() &&
        ctx.sel.BindLocalGeneration(h.streamGeneration)) {
      std::cout << "[native-video-client][picker] local selection bound to streamGeneration=" << h.streamGeneration
                << " epoch=" << ctx.sel.epoch.load(std::memory_order_acquire) << "''' + NL + '''";
    }
'''),
])

# ---------------------------------------------------------------- the pick
edit('viewer_picker.cpp', [
    ('''bool begin_pc_target_selection(ViewerState& ctx, uint64_t windowId, const char* statusText) {
  if (ctx.sel.pending.load(std::memory_order_acquire)) return false;
  if (!ctx.control.connected.load(std::memory_order_relaxed)) return false;''',
     '''bool begin_pc_target_selection(ViewerState& ctx, uint64_t windowId, const char* statusText) {
  if (ctx.sel.pending.load(std::memory_order_acquire)) return false;
  if (!ctx.control.connected.load(std::memory_order_relaxed)) return false;
  // rel-0163 r3 (Codex codex_rel_0163_e1_decision.md): what the host takes depends on its role, known from its
  // first pong. Before that nothing is sent blind (a logon-screen host closes the link on a WindowSelect): the
  // pick waits as this connection's intent and runs once the role is known (kMsgRunDeferredPick).
  if (!ctx.session.hostRoleKnown.load(std::memory_order_acquire)) {
    ctx.sel.deferredPick.store(windowId + 1, std::memory_order_release);
    ctx.picker.windowPanel.SetStatus("waiting_host_role");
    std::cout << "[native-video-client][picker] pick held until the host's role is known id=" << windowId << "''' + NL + '''";
    if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
    return true;
  }
  ctx.sel.deferredPick.store(0, std::memory_order_release);
  // A host known to be at the LOGON screen (kCaptureFlagPreloginV1 -- not the lock screen's LockCred, not a
  // secure desktop): it takes no WindowSelect, and serves exactly one desktop. A desktop pick there is a local
  // transaction (the gate's localAckless); any other target is not available there.
  if ((ctx.session.hostCaptureFlags.load(std::memory_order_acquire) & remote60::native_poc::kCaptureFlagPreloginV1) != 0) {
    if (windowId != 0) {
      ctx.picker.windowPanel.SetStatus("window_select_unavailable_prelogin");
      std::cout << "[native-video-client][picker] window pick refused: the host is at the logon screen id=" << windowId
                << "''' + NL + '''";
      if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
      return false;
    }
    if (ctx.picker.windowPanel.Snapshot().selectionLocked) return false;
    ctx.sel.BeginLocal();
    restore_note_pick(ctx, 0);
    ctx.picker.windowPanel.SetStatus("desktop_select_local");
    ctx.control.streamState.Request(true);
    request_keyframe(ctx, 1);  // allowed at the logon screen (ControlRequestKeyFrame)
    ctx.cursor.Publish(RemoteCursorSample{});
    std::cout << "[native-video-client][picker] desktop pick at the logon screen: local selection (no host ack) epoch="
              << ctx.sel.epoch.load(std::memory_order_acquire) << "''' + NL + '''";
    if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
    return true;
  }'''),
    ('''void clear_pc_target_selection(ViewerState& ctx) {
  ctx.sel.Clear();
}''',
     '''void clear_pc_target_selection(ViewerState& ctx) {
  ctx.sel.Clear();
  ctx.sel.deferredPick.store(0, std::memory_order_release);  // rel-0163 r3: a held pick ends with it
}

// rel-0163 r3: the held pick, now that the host's role is known (UI thread; once).
void run_deferred_pc_selection(ViewerState& ctx) {
  const uint64_t held = ctx.sel.deferredPick.exchange(0, std::memory_order_acq_rel);
  if (held == 0) return;
  if (!ctx.picker.visible.load(std::memory_order_relaxed) || !ctx.control.connected.load(std::memory_order_relaxed)) return;
  const uint64_t windowId = held - 1;
  (void)begin_pc_target_selection(ctx, windowId, windowId == 0 ? "desktop_select_requested" : "window_select_requested");
}

// rel-0163 r3: a WindowSelect the control thread could not send (the host is known to be at the logon screen,
// its allowlist has no WindowSelect). Its op continues as a local desktop selection, or -- another target --
// fails the way a refused ack does. Never silently: the op is resolved either way.
void resolve_unsendable_pc_selection(ViewerState& ctx, uint64_t windowId, uint64_t opEpoch) {
  if (windowId == 0 && ctx.sel.ConvertToLocal(opEpoch)) {
    ctx.picker.windowPanel.SetStatus("desktop_select_local");
    request_keyframe(ctx, 1);
    std::cout << "[native-video-client][picker] desktop select not sendable at the logon screen: the same op continues as a local selection epoch="
              << opEpoch << "''' + NL + '''";
    return;
  }
  if (ctx.sel.pending.load(std::memory_order_acquire) && ctx.sel.awaitingAck.load(std::memory_order_acquire) &&
      ctx.sel.epoch.load(std::memory_order_acquire) == opEpoch) {
    ctx.control.streamState.Request(false);
    clear_pc_target_selection(ctx);
    ctx.picker.windowPanel.SetStatus("window_select_unavailable_prelogin");
    std::cout << "[native-video-client][picker] window select not sendable at the logon screen: refused id=" << windowId
              << "''' + NL + '''";
  }
}'''),
])

edit('viewer_picker.hpp', [
    ('''bool begin_pc_target_selection(ViewerState& ctx, uint64_t windowId, const char* statusText);''',
     '''bool begin_pc_target_selection(ViewerState& ctx, uint64_t windowId, const char* statusText);
// rel-0163 r3: a pick held until the host's role was known, run once (UI thread).
void run_deferred_pc_selection(ViewerState& ctx);
// rel-0163 r3: a WindowSelect the control thread could not send to a logon-screen host (UI thread).
void resolve_unsendable_pc_selection(ViewerState& ctx, uint64_t windowId, uint64_t opEpoch);'''),
])

edit('viewer_constants.hpp', [
    ('''constexpr UINT kMsgRestoreStep = WM_APP + 23;''',
     '''constexpr UINT kMsgRestoreStep = WM_APP + 23;
// rel-0163 r3: the host's role became known -- run a pick that was held for it (viewer_picker.cpp).
constexpr UINT kMsgRunDeferredPick = WM_APP + 24;
// rel-0163 r3: a WindowSelect was not sendable to a logon-screen host. wParam = windowId, lParam = the op's
// selection epoch (low 32 bits are enough: it only has to match the live op).
constexpr UINT kMsgUnsendableSelect = WM_APP + 25;'''),
])

edit('viewer_window_proc.cpp', [
    ('''    case kMsgRestoreStep:
      restore_step(ctx, hwnd);
      return 0;''',
     '''    case kMsgRestoreStep:
      restore_step(ctx, hwnd);
      return 0;
    case kMsgRunDeferredPick:  // rel-0163 r3
      run_deferred_pc_selection(ctx);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case kMsgUnsendableSelect:  // rel-0163 r3
      resolve_unsendable_pc_selection(ctx, static_cast<uint64_t>(wParam), static_cast<uint64_t>(lParam));
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;'''),
])

edit('viewer_control_client.cpp', [
    ('''    ctx.session.hostRoleKnown.store(true, std::memory_order_release);  // r7 H6''',
     '''    const bool roleWasKnown = ctx.session.hostRoleKnown.exchange(true, std::memory_order_acq_rel);  // r7 H6
    // rel-0163 r3: a pick held until now runs once, on the UI thread.
    if (!roleWasKnown && ctx.sel.deferredPick.load(std::memory_order_acquire) != 0 && ctx.session.hwnd) {
      PostMessageW(ctx.session.hwnd, kMsgRunDeferredPick, 0, 0);
    }'''),
    ('''          !remote60::native_poc::prelogin::IsControlAllowedInPrelogin(remote60::native_poc::outbound_message_type(action))) {
        continue;
      }''',
     '''          !remote60::native_poc::prelogin::IsControlAllowedInPrelogin(remote60::native_poc::outbound_message_type(action))) {
        // rel-0163 r3: a WindowSelect not sent is a pick still waiting for its ack -- resolve that op on the UI
        // thread (continued as a local desktop selection, or refused), never left waiting.
        if (action.kind == ControlOutboundActionKind::WindowSelect && ctx.session.hwnd) {
          PostMessageW(ctx.session.hwnd, kMsgUnsendableSelect, static_cast<WPARAM>(action.windowSelect.windowId),
                       static_cast<LPARAM>(ctx.sel.epoch.load(std::memory_order_acquire)));
        }
        continue;
      }'''),
])
print('ok')
