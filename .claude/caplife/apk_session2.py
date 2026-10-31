S = 'C:\\work\\gnlink-wt\\apk-prelogin-select\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = S + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('native_video_client_session.hpp', [
    ('''  uint64_t owedSelection_ = 0;
  uint32_t owedAttempts_ = 0;''',
     '''  uint64_t owedSelection_ = 0;
  uint32_t owedAttempts_ = 0;
  // apk-0232 r1: the local selection (logon screen) whose generation the panel is still waiting to show; 0 = none.
  std::atomic<uint64_t> localSelectTag_{0};
  void PollLocalSelection();'''),
])

edit('native_video_client_session.cpp', [
    # the direct pick
    ('''  decoderRekeyPending_.store(false, std::memory_order_release);
  const uint64_t tag = encodedFrameSink_ ? encodedFrameSink_->CurrentSelectionTag() : 0;
  if (!windowPanel_.RequestSelect(windowId, statusText, tag)) {
    return false;
  }
  pk.unlock();''',
     '''  decoderRekeyPending_.store(false, std::memory_order_release);
  const uint64_t tag = encodedFrameSink_ ? encodedFrameSink_->CurrentSelectionTag() : 0;
  // apk-0232 r1 (Codex codex_rel_0163_e1_decision.md): a host KNOWN to be at the logon screen (PreloginV1 -- not a
  // secure desktop, not LockCred) takes no WindowSelect and serves its one desktop. A desktop pick there is a local
  // selection: nothing is sent, the sink's armed selection continues without an answer and is bound by its own
  // first IDR (asked for through the owed-IDR path). A window pick there is refused. A role not known yet: queued
  // as always and decided where it would go out.
  const HostRoleGate::Role role = hostRole_.Load();
  if (role.known && (role.flags & kCaptureFlagPreloginV1) != 0) {
    if (windowId != 0) {
      windowPanel_.SetStatus("window_select_unavailable_prelogin");
      std::fprintf(stderr, "[native-video-client-session] window pick refused: the host is at the logon screen\\n");
      return false;
    }
    if (!encodedFrameSink_ || tag == 0 || !windowPanel_.BeginLocalSelect(tag, "desktop_select_local") ||
        !encodedFrameSink_->BeginLocalSelectionFor(tag)) {
      return false;
    }
    localSelectTag_.store(tag, std::memory_order_release);
    std::fprintf(stderr, "[native-video-client-session] desktop pick at the logon screen: local selection %llu\\n",
                 static_cast<unsigned long long>(tag));
  } else if (!windowPanel_.RequestSelect(windowId, statusText, tag)) {
    return false;
  }
  pk.unlock();'''),
    # the queued pick decided at the role check
    ('''      if (role.known && (role.flags & kCaptureFlagPreloginV1) != 0 &&
          !prelogin::IsControlAllowedInPrelogin(outbound_message_type(action))) {
        continue;
      }''',
     '''      if (role.known && (role.flags & kCaptureFlagPreloginV1) != 0 &&
          !prelogin::IsControlAllowedInPrelogin(outbound_message_type(action))) {
        // apk-0232 r1: a pick queued before the role was known is not dropped in silence (the APK waited in
        // REQUESTING for an answer that never came). THAT op -- its own tag -- continues: a desktop pick as a
        // local selection, a window pick refused. A tag no longer armed (a later pick replaced it) changes nothing.
        if (action.kind == ControlOutboundActionKind::WindowSelect) ResolveUnsendableSelect(action);
        continue;
      }'''),
    # the poll, every pass
    ('''    // r3 M1: an IDR the decoder is still owed goes to the limiter on every pass, frame or no frame.''',
     '''    PollLocalSelection();  // apk-0232 r1
    // r3 M1: an IDR the decoder is still owed goes to the limiter on every pass, frame or no frame.'''),
    # reset with the session
    ('''  hostRole_.Reset();  // r7 H6: unknown again until the next pong (r10: nothing deferred either)''',
     '''  hostRole_.Reset();  // r7 H6: unknown again until the next pong (r10: nothing deferred either)
  localSelectTag_.store(0, std::memory_order_release);  // apk-0232 r1'''),
    ('''bool ClientSessionController::RequestDesktopMode() {''',
     '''// apk-0232 r1: a WindowSelect queued before the host's role was known, unsendable now that it is the logon screen.
void ClientSessionController::ResolveUnsendableSelect(const ControlOutboundAction& action) {
  const uint64_t tag = action.selectionTag;
  if (action.windowSelect.windowId == 0 && encodedFrameSink_ && tag != 0 &&
      encodedFrameSink_->BeginLocalSelectionFor(tag)) {
    windowPanel_.ConvertSelectToLocal(tag, "desktop_select_local");
    localSelectTag_.store(tag, std::memory_order_release);
    std::fprintf(stderr, "[native-video-client-session] queued desktop pick %llu: the host is at the logon screen, "
                 "continues as a local selection\\n", static_cast<unsigned long long>(tag));
  } else if (action.windowSelect.windowId != 0 || tag == 0 || !encodedFrameSink_) {
    // A window pick (or one no sink tracks): it cannot be made there. Refused, shown as failed if still current.
    if (encodedFrameSink_ && tag != 0) encodedFrameSink_->RefuseSelectionFor(tag);
    windowPanel_.RefuseSelectFor(tag, "unavailable_prelogin");
    std::fprintf(stderr, "[native-video-client-session] queued pick %llu (window %llu) refused: the host is at the "
                 "logon screen\\n", static_cast<unsigned long long>(tag),
                 static_cast<unsigned long long>(action.windowSelect.windowId));
  } else {
    std::fprintf(stderr, "[native-video-client-session] queued desktop pick %llu is no longer armed: nothing to do\\n",
                 static_cast<unsigned long long>(tag));
  }
  const auto panelSnapshot = windowPanel_.Snapshot();
  std::lock_guard<std::mutex> lock(mu_);
  SyncWindowPanelSnapshotLocked(panelSnapshot);
  UpdateConnectedStatusLocked(panelSnapshot.status);
}

// apk-0232 r1: once the local selection's first IDR has reached the decoder, the panel shows it selected with that
// generation (what an answer would have shown) -- only if it is still the selection requested.
void ClientSessionController::PollLocalSelection() {
  const uint64_t tag = localSelectTag_.load(std::memory_order_acquire);
  if (tag == 0 || !encodedFrameSink_) return;
  if (encodedFrameSink_->CurrentSelectionTag() != tag) {
    uint64_t stale = tag;
    localSelectTag_.compare_exchange_strong(stale, 0, std::memory_order_acq_rel);
    return;  // replaced or abandoned: it completes nothing
  }
  const uint64_t generation = encodedFrameSink_->LocalSelectionBoundFor(tag);
  if (generation == 0) return;
  uint64_t expected = tag;
  if (!localSelectTag_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel)) return;
  const auto applied = windowPanel_.ApplyLocalSelectedFor(tag, generation);
  if (!applied.logLine.empty()) std::fprintf(stderr, "%s\\n", applied.logLine.c_str());
  const auto panelSnapshot = windowPanel_.Snapshot();
  std::lock_guard<std::mutex> lock(mu_);
  SyncWindowPanelSnapshotLocked(panelSnapshot);
  UpdateConnectedStatusLocked(panelSnapshot.status);
}

bool ClientSessionController::RequestDesktopMode() {'''),
])

edit('native_video_client_session.hpp', [
    ('''  void PollLocalSelection();''',
     '''  void PollLocalSelection();
  void ResolveUnsendableSelect(const ControlOutboundAction& action);'''),
])
print('ok')
