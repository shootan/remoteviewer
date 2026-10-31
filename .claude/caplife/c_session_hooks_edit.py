ROOT = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src'


def load(p):
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    return raw.decode('utf-8').replace('\r\n', '\n'), crlf


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


p = ROOT + r'\native_video_client_session.hpp'
s, crlf = load(p)
s = rep(s, '''  void SetTestPassBoundaryHook(std::function<void(bool parking)> hook) {
    std::lock_guard<std::mutex> lk(testHookMu_);
    testPassHook_ = std::move(hook);
  }
''', '''  void SetTestPassBoundaryHook(std::function<void(bool parking)> hook) {
    std::lock_guard<std::mutex> lk(testHookMu_);
    testPassHook_ = std::move(hook);
  }
  // TEST BUILD ONLY (apk-inbox C, D8): called at the session's two key-request points -- "held" (before a request the
  // limiter refused goes to it again) and "accepted" (before a KeyframeRequest exchange starts) -- right before the
  // sink is asked which causes still hold: a test changes the sink's state there (another binding, a pick).
  void SetTestKeyRequestHook(std::function<void(const char* point)> hook) {
    std::lock_guard<std::mutex> lk(testHookMu_);
    testKeyHook_ = std::move(hook);
  }
  // TEST BUILD ONLY (apk-inbox C): the keyframe limiter's parameters, so a test can make it refuse (a HELD request).
  void TestConfigureKeyframeLimiter(uint64_t minIntervalUs, uint64_t tokenRefillUs, uint32_t tokenCapacity) {
    keyframeRequests_.Configure(minIntervalUs, tokenRefillUs, tokenCapacity);
  }
''')
s = rep(s, '''  std::function<void(bool)> testPassHook_;
''', '''  std::function<void(bool)> testPassHook_;
  std::function<void(const char*)> testKeyHook_;  // apk-inbox C
  void RunTestKeyHook(const char* point) {
    std::function<void(const char*)> hook;
    {
      std::lock_guard<std::mutex> lk(testHookMu_);
      hook = testKeyHook_;
    }
    if (hook) hook(point);
  }
''')
save(p, s, crlf)

p = ROOT + r'\native_video_client_session.cpp'
s, crlf = load(p)
s = rep(s, '''      uint32_t valid = held.causes;
      if (!held.empty() && encodedFrameSink_) {''', '''#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
      if (!held.empty()) RunTestKeyHook("held");
#endif
      uint32_t valid = held.causes;
      if (!held.empty() && encodedFrameSink_) {''')
s = rep(s, '''        if (!sentKey.empty() && encodedFrameSink_) {
          REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));''', '''#ifdef REMOTE60_CLIENT_SESSION_TEST_HOOKS
        if (!sentKey.empty()) RunTestKeyHook("accepted");
#endif
        if (!sentKey.empty() && encodedFrameSink_) {
          REMOTE60_LT(livenessTrace_.Wk(LtPhase::WkSinkHook, now_us()));''')
save(p, s, crlf)
print('ok')
