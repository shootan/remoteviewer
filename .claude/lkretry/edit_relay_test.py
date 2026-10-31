import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert old in s, old
    s = s.replace(old, new, 1)

rep('''  std::atomic<int> statusSeen{0};
''', '''  std::atomic<int> statusSeen{0};
  // lk-ux-retry r1: the LockCredSupersede (kind 9) a retry sends, and how it is answered.
  std::atomic<bool> offerRetry{true};
  std::atomic<uint16_t> supersedeStage{17};  // Superseded
  std::atomic<bool> supersedeTerminal{true};
  std::atomic<bool> stallSupersede{false};
  std::atomic<int> supersedeSeen{0};
  std::atomic<int> challengeSeen{0};
''')
rep('''      if (kind == static_cast<uint16_t>(SecureUnlockKind::StatusRequest)) ++statusSeen;
''', '''      if (kind == static_cast<uint16_t>(SecureUnlockKind::StatusRequest)) ++statusSeen;
      if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredSupersede)) ++supersedeSeen;
      if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredChallengeRequest)) ++challengeSeen;
      if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredSupersede) && stallSupersede.load()) continue;
''')
rep('''        r.features = kSecureUnlockFeatureLockCredV1;
''', '''        r.features = kSecureUnlockFeatureLockCredV1 | (offerRetry.load() ? kSecureUnlockFeatureLockCredRetryV1 : 0u);
''')
rep('''      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::StatusRequest)) {
''', '''      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredSupersede)) {
        SecureUnlockResult r{};
        r.requestId = requestId;
        r.stage = supersedeStage.load();
        r.terminal = supersedeTerminal.load() ? 1 : 0;
        write_msg(pipe, &r, sizeof(r));
      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::StatusRequest)) {
''')
anchor = '  a.Stop();\n  b.Stop();\n  svc.Stop();'
block = open(sys.argv[2], 'rb').read().decode('utf-8').replace('\r\n', '\n')
rep(anchor, block + anchor)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok crlf=%s' % crlf)
