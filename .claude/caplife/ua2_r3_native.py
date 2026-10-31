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
    ('''  bool HostRoleKnown() const { return hostRole_.Load().known; }
''', '''  bool HostRoleKnown() const { return hostRole_.Load().known; }
  // ua2-apk r3 (Codex codex_ua2_apk_src_r2.md N3): the host's backend-status capability from ONE read of its role --
  // known (its first pong came) and, from the same pong's flags, whether it answers. Never "known" from one moment and
  // "answers" from another (a first pong between two reads made a new host look like an older one). `stopPoint`
  // (tests) runs after the read: a pong arriving there changes nothing of this snapshot.
  struct DesktopBackendCapabilitySnapshot {
    bool known = false;
    bool answers = false;
  };
  DesktopBackendCapabilitySnapshot DesktopBackendCapability(const std::function<void()>& stopPoint = {}) const {
    const auto role = hostRole_.Load();
    if (stopPoint) stopPoint();
    return {role.known, role.known && (role.flags & kCaptureFlagDesktopBackendStatusV1) != 0};
  }
'''),
])

edit('android_direct_client/app/src/main/cpp/native_bridge.cpp', [
    ('''  const auto i = g_session_controller.DesktopCaptureBackendIntent();
  const jlong v[12] = {s.valid ? 1 : 0, static_cast<jlong>(s.seq), static_cast<jlong>(s.requested),
                       static_cast<jlong>(s.effective), static_cast<jlong>(s.active), static_cast<jlong>(s.flags),
                       static_cast<jlong>(s.changeCount),
                       g_session_controller.HostAnswersDesktopBackendStatus() ? 1 : 0,
                       g_session_controller.HostRoleKnown() ? 1 : 0,''',
     '''  const auto i = g_session_controller.DesktopCaptureBackendIntent();
  // ua2-apk r3 (N3): known and answers from one read of the host's role
  const auto cap = g_session_controller.DesktopBackendCapability();
  const jlong v[12] = {s.valid ? 1 : 0, static_cast<jlong>(s.seq), static_cast<jlong>(s.requested),
                       static_cast<jlong>(s.effective), static_cast<jlong>(s.active), static_cast<jlong>(s.flags),
                       static_cast<jlong>(s.changeCount),
                       cap.answers ? 1 : 0,
                       cap.known ? 1 : 0,'''),
])

edit('native_poc/src/client_session_prelogin_select_test.cpp', [
    ('''// P7: a new connection.''', '''// P10 (ua2-apk r3, Codex N3): the backend-status capability is one read of the host's role. The reader is stopped
// right after its read (stopPoint) while a supporting host's FIRST pong is let through and processed: the snapshot
// must still be one moment -- unknown (checking) or known+answers -- never known-but-not-answering (an older host).
void p10_capability_snapshot() {
  std::printf("\\n--- P10: the host's backend capability, one read across its first pong ---\\n");
  FakeHost h;
  h.pongFlags = n::kCaptureFlagDesktopBackendStatusV1;
  h.holdPongs = true;
  if (!h.Start()) return check("P10: fake host starts", false);
  n::ClientSessionController c;
  GateSink sink;
  check("P10: the session connects (first pong held)", connect_to(c, h, &sink));
  bool pongLanded = false;
  const auto cap = c.DesktopBackendCapability([&] {
    h.holdPongs = false;
    pongLanded = wait_until([&] { return c.HostRoleKnown(); }, 3000);
  });
  check("P10: the first pong landed between the read and the answer", pongLanded);
  check("P10: the snapshot is one moment -- not known-but-not-answering", !(cap.known && !cap.answers),
        "known=" + std::to_string(cap.known) + " answers=" + std::to_string(cap.answers));
  const auto after = c.DesktopBackendCapability();
  check("P10: after the pong: known and answering", after.known && after.answers);
  c.Disconnect();
  h.Stop();
}

// P7: a new connection.'''),
    ('''  p9_commit_vs_cancel();
''', '''  p9_commit_vs_cancel();
  p10_capability_snapshot();
'''),
])
print('ok')
