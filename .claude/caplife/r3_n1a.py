src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'
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

edit('host_capture_session.hpp', [
    ('''  // Main-loop retry state outlives the request flag and a failed attachment.
  bool restartPending = false;
  uint64_t restartRetryAtUs = 0;
  uint32_t restartFailures = 0;''',
     '''  // Main-loop retry state outlives the request flag and a failed attachment.
  bool restartPending = false;
  uint64_t restartRetryAtUs = 0;
  uint32_t restartFailures = 0;
  // capture-lifecycle r3 (Codex N1-a): whose restart failed (restart_capture_session's caller). A failed restart
  // has already detached -- the generic retry (stage_time_limit) must not take over a recovery that keeps its
  // own budget: a desktop rebuild's retries are the backend stage's (bounded per desktop generation), a
  // full-frame reacquire's retry stays that episode's own (no new source boundary). Main loop only.
  enum class RestartOwner : uint8_t { General, DesktopRebuild, FullFrame };
  RestartOwner restartOwner = RestartOwner::General;'''),
])

edit('host_main_loop.hpp', [
    ('''// Helpers (former main() lambdas).
bool restart_capture_session(HostContext& hx);''',
     '''// Helpers (former main() lambdas).
// owner (capture-lifecycle r3): who asks -- kept with a failure (CaptureState::restartOwner) so the retry that
// follows is that recovery's own.
bool restart_capture_session(HostContext& hx, CaptureState::RestartOwner owner = CaptureState::RestartOwner::General);'''),
])

edit('host_loop_helpers.cpp', [
    ('''bool restart_capture_session(HostContext& hx) {''',
     '''bool restart_capture_session(HostContext& hx, CaptureState::RestartOwner owner) {'''),
    ('''  capture.dxgiPointerUpdateUs.store(0, std::memory_order_release);
  if (!capture.RestartCaptureSessionImpl(res, backend, clientSession, encoder, stop, useH264, item, token)) {
    capture.restartPending = true;''',
     '''  capture.dxgiPointerUpdateUs.store(0, std::memory_order_release);
  // capture-lifecycle r3 (Codex N1-a/N4): a full-frame reacquire's own restart -- first try or its retry -- is
  // no new source for the episode budget (DetachCaptureSession reads this).
  capture.fullFrameOwnRestart = owner == CaptureState::RestartOwner::FullFrame;
  const bool started = capture.RestartCaptureSessionImpl(res, backend, clientSession, encoder, stop, useH264, item, token);
  capture.fullFrameOwnRestart = false;
  if (!started) {
    capture.restartOwner = owner;
    capture.restartPending = true;'''),
    ('''  capture.restartPending = false;
  capture.restartRetryAtUs = 0;
  capture.restartFailures = 0;
  uint32_t finalW = 0, finalH = 0;''',
     '''  capture.restartPending = false;
  capture.restartRetryAtUs = 0;
  capture.restartFailures = 0;
  capture.restartOwner = CaptureState::RestartOwner::General;
  uint32_t finalW = 0, finalH = 0;'''),
])

edit('host_stage_time_limit.cpp', [
    ('''  // Attachment retries cannot depend on callbacks from the attachment that just failed.
  if (capture.restartPending && clientSession.streamControlActive.load(std::memory_order_acquire) &&
      nowUs >= capture.restartRetryAtUs && restart_capture_session(hx)) {''',
     '''  // Attachment retries cannot depend on callbacks from the attachment that just failed.
  // capture-lifecycle r3 (Codex N1-a): ...except a desktop rebuild's -- the backend stage retries it within its
  // bound for that desktop generation, and this generic retry would go past it (8 s backoff, no limit). A
  // full-frame reacquire's failure is retried here as that episode's own (no new source).
  if (capture.restartPending && capture.restartOwner != CaptureState::RestartOwner::DesktopRebuild &&
      clientSession.streamControlActive.load(std::memory_order_acquire) &&
      nowUs >= capture.restartRetryAtUs && restart_capture_session(hx, capture.restartOwner)) {'''),
])

edit('host_backend_policy.hpp', [
    ('''  uint64_t desktopRebuildGen = 0;
  uint32_t desktopRebuildAttempts = 0;''',
     '''  uint64_t desktopRebuildGen = 0;
  uint64_t desktopRebuildEpoch = 0;  // r3: a new control session starts its own count for the same generation
  uint32_t desktopRebuildAttempts = 0;'''),
])

edit('host_stage_backend.cpp', [
    ('''  {
    const uint64_t dgen = capture.desktopGeneration.load(std::memory_order_acquire);
    const bool stale = capture.attachmentDesktopGen.load(std::memory_order_acquire) != dgen;
    if (stale && streamActiveNow && !capture.idleDetached) {
      if (backend.desktopRebuildGen != dgen) {
        backend.desktopRebuildGen = dgen;
        backend.desktopRebuildAttempts = 0;
        backend.desktopRebuildRetryAtUs = 0;
      }
      const uint64_t nowRebuildUs = qpc_now_us();
      if (backend.desktopRebuildAttempts < DesktopBackendState::kMaxDesktopRebuildAttempts &&
          nowRebuildUs >= backend.desktopRebuildRetryAtUs) {
        ++backend.desktopRebuildAttempts;
        powerKeepalive.SetStreaming(true, true);
        bool seamFail = false;
#ifdef REMOTE60_STREAM_TEST_SEAM
        // TEST BUILD ONLY (capture-lifecycle r2): GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL=1 -- every rebuild for an
        // odd desktop generation (the secure side of a round trip from Default) fails, as a rebuild that cannot
        // open its capture would: what crosses an edge whose rebuild never comes.
        {
          const char* v = std::getenv("GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL");
          seamFail = v && std::strcmp(v, "1") == 0 && (dgen % 2) == 1;
        }
#endif
        const bool ok = !seamFail && restart_capture_session(hx);''',
     '''  // r3 (Codex N1-a): a rebuild that FAILED has already detached (DetachCaptureSession stamps the generation the
  // next attachment would have had), so "stale" alone would call it done. The rebuild is open until a restart
  // of this generation succeeds: stale, or this generation's rebuild failed and nothing has attached since
  // (restartPending, owned by the rebuild -- the generic retry leaves it alone). Counted per (generation,
  // control session); a newer generation or a new session starts its own count.
  {
    const uint64_t dgen = capture.desktopGeneration.load(std::memory_order_acquire);
    const bool stale = capture.attachmentDesktopGen.load(std::memory_order_acquire) != dgen;
    const bool failedHere = capture.restartPending && capture.restartOwner == CaptureState::RestartOwner::DesktopRebuild &&
                            backend.desktopRebuildGen == dgen;
    if ((stale || failedHere) && streamActiveNow && !capture.idleDetached) {
      if (backend.desktopRebuildGen != dgen || backend.desktopRebuildEpoch != curEpoch) {
        backend.desktopRebuildGen = dgen;
        backend.desktopRebuildEpoch = curEpoch;
        backend.desktopRebuildAttempts = 0;
        backend.desktopRebuildRetryAtUs = 0;
      }
      const uint64_t nowRebuildUs = qpc_now_us();
      // (Never before the restart wrapper's own backoff: an attempt it would refuse unseen is no attempt.)
      if (backend.desktopRebuildAttempts < DesktopBackendState::kMaxDesktopRebuildAttempts &&
          nowRebuildUs >= backend.desktopRebuildRetryAtUs &&
          (!capture.restartPending || nowRebuildUs >= capture.restartRetryAtUs)) {
        ++backend.desktopRebuildAttempts;
        powerKeepalive.SetStreaming(true, true);
        const bool ok = restart_capture_session(hx, CaptureState::RestartOwner::DesktopRebuild);'''),
    ('''    powerKeepalive.SetStreaming(true, true);
    capture.fullFrameOwnRestart = true;  // r2 (Codex N4): the episode's own reacquire keeps its budget
    const bool ok = restart_capture_session(hx);
    capture.fullFrameOwnRestart = false;
    if (ok) restarted_capture("full-frame-wgc-restart");''',
     '''    powerKeepalive.SetStreaming(true, true);
    // r2/r3 (Codex N4/N1-a): the episode's own reacquire keeps its budget -- and so does its retry, if it fails.
    const bool ok = restart_capture_session(hx, CaptureState::RestartOwner::FullFrame);
    if (ok) restarted_capture("full-frame-wgc-restart");'''),
])
print('ok')
