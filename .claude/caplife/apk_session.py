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

# ---------------------------------------------------------------- panel
edit('native_video_client_shared_core.hpp', [
    ('''  WindowSelectApplyResult ApplyWindowSelectedFor(const ControlWindowSelectedMessage& msg, uint64_t selectionTag);
  void Scroll(int deltaSteps, int visibleCount);''',
     '''  WindowSelectApplyResult ApplyWindowSelectedFor(const ControlWindowSelectedMessage& msg, uint64_t selectionTag);
  // apk-0232 r1: a LOCAL desktop selection (a host at the logon screen: no WindowSelect, no answer). Begin: the pick
  // is the selection now requested (`selectionTag`), with nothing queued for the wire -- a pick still queued is
  // replaced, as a new RequestSelect replaces it. Convert: the queued pick `selectionTag` (taken off the queue, not
  // sent) becomes local -- only if it is still the selection requested. Applied: its generation is bound (its
  // first IDR reached the decoder): shown as selected, like an answer, only if it is still the selection requested.
  // Refused: the pick `selectionTag` cannot be made at the logon screen (a window), shown as failed if current.
  bool BeginLocalSelect(uint64_t selectionTag, const char* statusText);
  bool ConvertSelectToLocal(uint64_t selectionTag, const char* statusText);
  WindowSelectApplyResult ApplyLocalSelectedFor(uint64_t selectionTag, uint64_t streamGeneration);
  bool RefuseSelectFor(uint64_t selectionTag, const char* reason);
  void Scroll(int deltaSteps, int visibleCount);'''),
])

edit('native_video_client_shared_core.cpp', [
    ('''void WindowPanelStateModel::Scroll(int deltaSteps, int visibleCount) {''',
     '''bool WindowPanelStateModel::BeginLocalSelect(uint64_t selectionTag, const char* statusText) {
  std::lock_guard<std::mutex> lk(mu_);
  if (state_.selectionLocked) return false;
  selectRequestPending_ = false;
  pendingSelectTag_ = 0;
  currentSelectTag_ = selectionTag;
  if (statusText) state_.status = statusText;
  return true;
}

bool WindowPanelStateModel::ConvertSelectToLocal(uint64_t selectionTag, const char* statusText) {
  std::lock_guard<std::mutex> lk(mu_);
  if (selectionTag != currentSelectTag_) return false;
  if (statusText) state_.status = statusText;
  return true;
}

WindowSelectApplyResult WindowPanelStateModel::ApplyLocalSelectedFor(uint64_t selectionTag, uint64_t streamGeneration) {
  WindowSelectApplyResult result{};
  std::lock_guard<std::mutex> lk(mu_);
  if (selectionTag == 0 || selectionTag != currentSelectTag_ || streamGeneration == 0) {
    result.shown = false;
    return result;
  }
  state_.lastSelectOk = true;
  state_.lastSelectWindowId = 0;
  state_.lastSelectStreamGeneration = streamGeneration;
  state_.lastSelectHostSendQpcUs = 0;
  state_.selectedId = 0;
  state_.selectedTitle = "desktop";
  set_selected_target_dimensions(&state_);
  state_.status = "window_selected: desktop (logon screen)";
  std::ostringstream oss;
  oss << "[native-video-client][control] window-selected local (logon screen) streamGen=" << streamGeneration
      << " selection=" << selectionTag;
  result.logLine = oss.str();
  result.ok = true;
  return result;
}

bool WindowPanelStateModel::RefuseSelectFor(uint64_t selectionTag, const char* reason) {
  std::lock_guard<std::mutex> lk(mu_);
  if (selectionTag != 0 && selectionTag != currentSelectTag_) return false;
  state_.lastSelectOk = false;
  state_.status = std::string("window_select_failed: ") + (reason ? reason : "unknown");
  return true;
}

void WindowPanelStateModel::Scroll(int deltaSteps, int visibleCount) {'''),
])

# ---------------------------------------------------------------- the sink interface
edit('native_video_client_session.hpp', [
    ('''  virtual void OnWindowSelectionControlResultFor(const ControlWindowSelectedMessage& msg, uint64_t /* requestTag */) {
    OnWindowSelectionControlResult(msg);
  }
};''', '''  virtual void OnWindowSelectionControlResultFor(const ControlWindowSelectedMessage& msg, uint64_t /* requestTag */) {
    OnWindowSelectionControlResult(msg);
  }
  // apk-0232 r1: a desktop pick at a host known to be at the logon screen (kCaptureFlagPreloginV1) gets no answer.
  // The selection `tag` (still waiting for one) continues as a LOCAL selection: bound by its own first IDR, not by
  // an answer (SelectionAckGate::BeginLocal). False = not that selection any more (or a sink that cannot) -- then
  // nothing is selected. LocalSelectionBoundFor: the generation that bound it, 0 while unbound. RefuseSelectionFor:
  // the selection `tag` cannot be made there (a window pick): it opens nothing.
  virtual bool BeginLocalSelectionFor(uint64_t /* tag */) { return false; }
  virtual uint64_t LocalSelectionBoundFor(uint64_t /* tag */) { return 0; }
  virtual void RefuseSelectionFor(uint64_t /* tag */) {}
};'''),
    ('''  bool RequestWindowSelect(uint64_t windowId);''',
     '''  // apk-0232 r1: at a host KNOWN to be at the logon screen a desktop pick is a local selection (nothing sent; see
  // ClientEncodedFrameSink::BeginLocalSelectionFor) and a window pick is refused (false). A pick made before the
  // role is known is queued and decided when it would go out (the control loop), the same way.
  bool RequestWindowSelect(uint64_t windowId);'''),
])
print('ok')
