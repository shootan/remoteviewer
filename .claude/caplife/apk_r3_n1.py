wt = 'C:/work/gnlink-wt/apk-prelogin-select/apps/'

def edit(rel, pairs):
    p = wt + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (rel, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('native_poc/src/native_video_client_session.hpp', [
    ('''  virtual void RefuseSelectionFor(uint64_t /* tag */) {}
};''', '''  virtual void RefuseSelectionFor(uint64_t /* tag */) {}
  // apk-0232 r3 (Codex N1, the last boundary): the local selection `tag`'s completion, judged and recorded in ONE
  // step against the sink's own selection state. If `tag` is still the selection and its IDR bound it, `commit`
  // runs with that generation WHILE the sink holds its state -- a cancel (Abort) or a new Prepare, which take the
  // same state, lands wholly before (nothing completes) or wholly after (a completion already recorded, which it
  // then supersedes). `commit` must not call back into the sink. Abandoned: `tag` is no longer the selection.
  enum class LocalCommit : uint8_t { Abandoned = 0, Unbound = 1, Committed = 2 };
  virtual LocalCommit CommitLocalSelectionFor(uint64_t /* tag */, const std::function<void(uint64_t generation)>& /* commit */) {
    return LocalCommit::Abandoned;
  }
};'''),
    ('''  void SetTestAfterLocalSinkBeginHook(std::function<void(uint64_t tag)> hook) {
    std::lock_guard<std::mutex> lk(testHookMu_);
    testAfterLocalSinkBeginHook_ = std::move(hook);
  }''', '''  void SetTestAfterLocalSinkBeginHook(std::function<void(uint64_t tag)> hook) {
    std::lock_guard<std::mutex> lk(testHookMu_);
    testAfterLocalSinkBeginHook_ = std::move(hook);
  }
  // TEST BUILD ONLY (apk-0232 r3 N1): called by the poll once the local selection is found bound to `generation`,
  // right before the panel records it -- a test cancels or prepares a newer pick there.
  void SetTestBeforeLocalCommitHook(std::function<void(uint64_t tag, uint64_t generation)> hook) {
    std::lock_guard<std::mutex> lk(testHookMu_);
    testBeforeLocalCommitHook_ = std::move(hook);
  }'''),
])

edit('native_poc/src/native_video_client_session.cpp', [
    ('''  if (encodedFrameSink_->CurrentSelectionTag() != tag) {
    uint64_t stale = tag;
    localSelectTag_.compare_exchange_strong(stale, 0, std::memory_order_acq_rel);
    return;  // replaced or abandoned: it completes nothing
  }
  const uint64_t generation = encodedFrameSink_->LocalSelectionBoundFor(tag);
  if (generation == 0) return;
  uint64_t expected = tag;
  if (!localSelectTag_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel)) return;
  const auto applied = windowPanel_.ApplyLocalSelectedFor(tag, generation);
  ls.unlock();''', '''  // r3 (N1): the sink's "still this selection, bound by G" and the panel's record are one step under the sink's
  // state, the one a cancel (nativeAbortVideoSwitch) or a new Prepare takes -- never a read, then a record later.
  // Lock order local -> sink -> panel; nothing takes them the other way.
  WindowSelectApplyResult applied;
  const auto result = encodedFrameSink_->CommitLocalSelectionFor(tag, [&](uint64_t generation) {
#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
    std::function<void(uint64_t, uint64_t)> hook;
    {
      std::lock_guard<std::mutex> lk(testHookMu_);
      hook = testBeforeLocalCommitHook_;
    }
    if (hook) hook(tag, generation);
#endif
    uint64_t expected = tag;
    if (!localSelectTag_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel)) return;
    applied = windowPanel_.ApplyLocalSelectedFor(tag, generation);
  });
  if (result == ClientEncodedFrameSink::LocalCommit::Abandoned) {
    uint64_t stale = tag;
    localSelectTag_.compare_exchange_strong(stale, 0, std::memory_order_acq_rel);
    return;  // replaced or abandoned: it completes nothing
  }
  if (result == ClientEncodedFrameSink::LocalCommit::Unbound) return;
  ls.unlock();'''),
])
print('ok')
edit('native_poc/src/native_video_client_session.hpp', [
    ('''  std::function<void(uint64_t)> testAfterLocalSinkBeginHook_;''',
     '''  std::function<void(uint64_t)> testAfterLocalSinkBeginHook_;
  std::function<void(uint64_t, uint64_t)> testBeforeLocalCommitHook_;'''),
])
print('ok2')
