S = 'C:/work/gnlink-wt/apk-prelogin-select/apps/native_poc/src/'
A = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/cpp/'

def edit(path, pairs):
    s = open(path, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (path, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'wb').write(s.encode('utf-8'))

edit(S + 'native_video_client_shared_core.hpp', [
    ('''  bool Query();

private:''', '''  bool Query();
  // ua2-apk r2 (Codex codex_ua2_apk_src_r1.md N1): which change the app asked for last, which one went out, and with
  // which wire seq -- so an answer can be told to be about THE CURRENT request (its seq or later) or an older one.
  // Ids count Request calls on this connection (Reset: 0).
  struct Intent {
    uint64_t asked = 0;    // the latest Request
    uint64_t sent = 0;     // the Request whose change went out last
    uint32_t sentSeq = 0;  // that change's seq (34)
  };
  Intent CurrentIntent() const;

private:
  mutable std::mutex intentMu_;
  Intent intent_;'''),
])

edit(S + 'native_video_client_shared_core.cpp', [
    ('''void DesktopBackendControl::Reset() {
  pending_.store(false, std::memory_order_relaxed);''', '''void DesktopBackendControl::Reset() {
  {
    std::lock_guard<std::mutex> lk(intentMu_);
    intent_ = Intent{};
  }
  pending_.store(false, std::memory_order_relaxed);'''),
    ('''void DesktopBackendControl::Request(uint16_t backend) {
  if (backend < 1 || backend > 3) return;  // ua2-apk r1: + 3 gdi
  backend_.store(backend, std::memory_order_release);
  pending_.store(true, std::memory_order_release);
}''', '''void DesktopBackendControl::Request(uint16_t backend) {
  if (backend < 1 || backend > 3) return;  // ua2-apk r1: + 3 gdi
  std::lock_guard<std::mutex> lk(intentMu_);  // ua2-apk r2: the intent and the change it asks, one step
  ++intent_.asked;
  backend_.store(backend, std::memory_order_release);
  pending_.store(true, std::memory_order_release);
}

DesktopBackendControl::Intent DesktopBackendControl::CurrentIntent() const {
  std::lock_guard<std::mutex> lk(intentMu_);
  return intent_;
}'''),
    ('''  if (pending_.exchange(false, std::memory_order_acq_rel)) {
    // The change first. Toward a host that answers, its answer is the status a pending question waited for.
    if (answers) queryPending_.store(false, std::memory_order_release);
    out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
    out->backend = backend_.load(std::memory_order_acquire);
    out->wantsStatus = answers;
    return true;
  }''', '''  {
    std::lock_guard<std::mutex> lk(intentMu_);  // ua2-apk r2: the change taken and the intent it carries, one step
    if (pending_.exchange(false, std::memory_order_acq_rel)) {
      // The change first. Toward a host that answers, its answer is the status a pending question waited for.
      if (answers) queryPending_.store(false, std::memory_order_release);
      out->seq = nextSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
      out->backend = backend_.load(std::memory_order_acquire);
      out->wantsStatus = answers;
      intent_.sent = intent_.asked;
      intent_.sentSeq = out->seq;
      return true;
    }
  }'''),
])

edit(S + 'native_video_client_session.hpp', [
    ('''  bool HostAnswersDesktopBackendStatus() const { return desktopBackend_.HostAnswers(); }''',
     '''  bool HostAnswersDesktopBackendStatus() const { return desktopBackend_.HostAnswers(); }
  // ua2-apk r2: the app's backend intent on this connection (N1), and whether the host's role -- its first pong, which
  // carries the backend-status capability -- is known yet (N3: unknown is not "unsupported").
  DesktopBackendControl::Intent DesktopCaptureBackendIntent() const { return desktopBackend_.CurrentIntent(); }
  bool HostRoleKnown() const { return hostRole_.Load().known; }'''),
])

edit(A + 'native_bridge.cpp', [
    ('''// ua2-apk r1: [valid, seq, requested, effective, active, flags, changeCount, hostAnswers] -- this connection's.
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetDesktopBackendStatus(
    JNIEnv* env, jobject /* this */) {
  const auto s = g_session_controller.DesktopCaptureBackendStatus();
  const jlong v[8] = {s.valid ? 1 : 0, static_cast<jlong>(s.seq), static_cast<jlong>(s.requested),
                      static_cast<jlong>(s.effective), static_cast<jlong>(s.active), static_cast<jlong>(s.flags),
                      static_cast<jlong>(s.changeCount),
                      g_session_controller.HostAnswersDesktopBackendStatus() ? 1 : 0};
  jlongArray out = env->NewLongArray(8);
  if (out) env->SetLongArrayRegion(out, 0, 8, v);
  return out;
}''', '''// ua2-apk r1/r2: [valid, seq, requested, effective, active, flags, changeCount, hostAnswers, roleKnown, intentAsked,
// intentSent, intentSentSeq] -- this connection's.
extern "C" JNIEXPORT jlongArray JNICALL
Java_com_remote60_androiddirect_NativeSessionBridge_nativeGetDesktopBackendStatus(
    JNIEnv* env, jobject /* this */) {
  const auto s = g_session_controller.DesktopCaptureBackendStatus();
  const auto i = g_session_controller.DesktopCaptureBackendIntent();
  const jlong v[12] = {s.valid ? 1 : 0, static_cast<jlong>(s.seq), static_cast<jlong>(s.requested),
                       static_cast<jlong>(s.effective), static_cast<jlong>(s.active), static_cast<jlong>(s.flags),
                       static_cast<jlong>(s.changeCount),
                       g_session_controller.HostAnswersDesktopBackendStatus() ? 1 : 0,
                       g_session_controller.HostRoleKnown() ? 1 : 0,
                       static_cast<jlong>(i.asked), static_cast<jlong>(i.sent), static_cast<jlong>(i.sentSeq)};
  jlongArray out = env->NewLongArray(12);
  if (out) env->SetLongArrayRegion(out, 0, 12, v);
  return out;
}'''),
])
print('ok')
