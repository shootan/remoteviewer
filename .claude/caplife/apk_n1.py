S = 'C:/work/gnlink-wt/apk-prelogin-select/apps/native_poc/src/'

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
    ('''  // apk-0232 r1: the local selection (logon screen) whose generation the panel is still waiting to show; 0 = none.
  std::atomic<uint64_t> localSelectTag_{0};''', '''  // apk-0232 r1: the local selection (logon screen) whose generation the panel is still waiting to show; 0 = none.
  std::atomic<uint64_t> localSelectTag_{0};
  // apk-0232 r2 (Codex codex_apk_prelogin_select_src_r1.md N1): a local selection's start -- the sink, the panel and
  // localSelectTag_ -- and the poll that completes it are one step each under this lock, so a queued pick turned
  // local late cannot publish its tag over a newer pick's (nor clear it). Taken after parkMu_, never before it; no
  // network under it.
  std::mutex localSelectMu_;'''),
    ('''#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
  std::mutex testHookMu_;
  std::function<void(bool)> testPassHook_;
#endif''', '''#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
  std::mutex testHookMu_;
  std::function<void(bool)> testPassHook_;
  std::function<void(uint64_t)> testAfterLocalSinkBeginHook_;
#endif'''),
    ('''    std::lock_guard<std::mutex> lk(testHookMu_);
    testPassHook_ = std::move(hook);
  }
#endif''', '''    std::lock_guard<std::mutex> lk(testHookMu_);
    testPassHook_ = std::move(hook);
  }
  // TEST BUILD ONLY (apk-0232 r2 N1): called when a queued desktop pick has just been turned local in the sink,
  // before the panel and the tracked tag follow -- a test holds it there and makes a newer pick.
  void SetTestAfterLocalSinkBeginHook(std::function<void(uint64_t tag)> hook) {
    std::lock_guard<std::mutex> lk(testHookMu_);
    testAfterLocalSinkBeginHook_ = std::move(hook);
  }
#endif'''),
])

edit('native_video_client_session.cpp', [
    ('''    if (!encodedFrameSink_ || tag == 0 || !windowPanel_.BeginLocalSelect(tag, "desktop_select_local") ||
        !encodedFrameSink_->BeginLocalSelectionFor(tag)) {
      return false;
    }
    localSelectTag_.store(tag, std::memory_order_release);''', '''    // r2 (N1): the sink, the panel and the tracked tag in one step (after parkMu_, which this path holds).
    std::lock_guard<std::mutex> ls(localSelectMu_);
    if (!encodedFrameSink_ || tag == 0 || !encodedFrameSink_->BeginLocalSelectionFor(tag) ||
        !windowPanel_.BeginLocalSelect(tag, "desktop_select_local")) {
      return false;
    }
    localSelectTag_.store(tag, std::memory_order_release);'''),
    ('''  const uint64_t tag = action.selectionTag;
  if (action.windowSelect.windowId == 0 && encodedFrameSink_ && tag != 0 &&
      encodedFrameSink_->BeginLocalSelectionFor(tag)) {
    windowPanel_.ConvertSelectToLocal(tag, "desktop_select_local");
    localSelectTag_.store(tag, std::memory_order_release);
    std::fprintf(stderr, "[native-video-client-session] queued desktop pick %llu: the host is at the logon screen, "
                 "continues as a local selection\\n", static_cast<unsigned long long>(tag));
  } else if''', '''  const uint64_t tag = action.selectionTag;
  // r2 (N1): the sink's turn to local, the panel's and the tracked tag are one step -- a newer pick made meanwhile
  // waits for it, then publishes its own; and a panel that already holds a newer pick takes no tag from this one.
  std::unique_lock<std::mutex> ls(localSelectMu_);
  bool sinkLocal = false;
  if (action.windowSelect.windowId == 0 && encodedFrameSink_ && tag != 0) sinkLocal = encodedFrameSink_->BeginLocalSelectionFor(tag);
#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
  if (sinkLocal) {
    std::function<void(uint64_t)> hook;
    {
      std::lock_guard<std::mutex> lk(testHookMu_);
      hook = testAfterLocalSinkBeginHook_;
    }
    if (hook) hook(tag);
  }
#endif
  if (sinkLocal) {
    if (windowPanel_.ConvertSelectToLocal(tag, "desktop_select_local")) {
      localSelectTag_.store(tag, std::memory_order_release);
      std::fprintf(stderr, "[native-video-client-session] queued desktop pick %llu: the host is at the logon screen, "
                   "continues as a local selection\\n", static_cast<unsigned long long>(tag));
    } else {
      std::fprintf(stderr, "[native-video-client-session] queued desktop pick %llu: a newer pick holds the panel, "
                   "nothing tracked for it\\n", static_cast<unsigned long long>(tag));
    }
  } else if'''),
    ('''    std::fprintf(stderr, "[native-video-client-session] queued desktop pick %llu is no longer armed: nothing to do\\n",
                 static_cast<unsigned long long>(tag));
  }
  const auto panelSnapshot = windowPanel_.Snapshot();''', '''    std::fprintf(stderr, "[native-video-client-session] queued desktop pick %llu is no longer armed: nothing to do\\n",
                 static_cast<unsigned long long>(tag));
  }
  ls.unlock();
  const auto panelSnapshot = windowPanel_.Snapshot();'''),
    ('''void ClientSessionController::PollLocalSelection() {
  const uint64_t tag = localSelectTag_.load(std::memory_order_acquire);
  if (tag == 0 || !encodedFrameSink_) return;''', '''void ClientSessionController::PollLocalSelection() {
  if (localSelectTag_.load(std::memory_order_acquire) == 0 || !encodedFrameSink_) return;
  std::unique_lock<std::mutex> ls(localSelectMu_);  // r2 (N1): read, judge and complete against one start
  const uint64_t tag = localSelectTag_.load(std::memory_order_acquire);
  if (tag == 0) return;'''),
    ('''  const auto applied = windowPanel_.ApplyLocalSelectedFor(tag, generation);
  if (!applied.logLine.empty()) std::fprintf(stderr, "%s\\n", applied.logLine.c_str());
  const auto panelSnapshot = windowPanel_.Snapshot();''', '''  const auto applied = windowPanel_.ApplyLocalSelectedFor(tag, generation);
  ls.unlock();
  if (!applied.logLine.empty()) std::fprintf(stderr, "%s\\n", applied.logLine.c_str());
  const auto panelSnapshot = windowPanel_.Snapshot();'''),
])
print('ok')
