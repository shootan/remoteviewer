p = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/android_decoder_idr_recovery_test.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('''  const auto snap = c.Snapshot();
  o.controlLost = host.peerLost.load() || snap.state != n::ClientSessionState::Connected;''', '''  const auto snap = c.Snapshot();
  o.controlLost = host.peerLost.load() || snap.state != n::ClientSessionState::Connected;
  // The test-build progress trace (GNLINK_LIVENESS_TRACE is defined for this target): one line at the end.
  std::printf("  trace: %s | %s\\n", c.LivenessTraceLine().c_str(), sink.LivenessTraceLine().c_str());''')
open(p, 'w', encoding='utf-8').write(s)

q = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/CMakeLists.txt'
t = open(q, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in t else '\n'
a = 'target_compile_definitions(remote60_android_decoder_idr_recovery_test PRIVATE NOMINMAX)'
assert t.count(a) == 1
t = t.replace(a, 'target_compile_definitions(remote60_android_decoder_idr_recovery_test PRIVATE NOMINMAX GNLINK_LIVENESS_TRACE=1)')
open(q, 'w', encoding='utf-8', newline='').write(t)
print('ok')
