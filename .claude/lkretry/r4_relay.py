src = 'C:\\work\\gnlink-wt\\fix-lk-retry\\apps\\native_poc\\src\\'
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

edit('host_unlock_relay.cpp', [
    ('''  HANDLE pipe = static_cast<HANDLE>(pipeHandle);
  const ULONGLONG deadline = GetTickCount64() + deadlineMs;''',
     '''  HANDLE pipe = static_cast<HANDLE>(pipeHandle);
  ULONGLONG deadline = GetTickCount64() + deadlineMs;
  // lk-ux-retry r4 (Codex N1): a retry's exchange -- its write AND its read -- never waits past the retry's own
  // absolute end, whatever relative budget was worked out before it got here.
  if (gate && gate->deadlineMs != 0 && gate->deadlineMs < deadline) deadline = gate->deadlineMs;'''),
    ('''      if (retryEndedEarlier && out.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued)) {
        if (retryIoHookForTest_) retryIoHookForTest_("adopt");
        bool inTime = false;
        {
          std::lock_guard<std::mutex> lk(mu_);
          inTime = IsLiveLocked(cmd, cmd.challenge.requestId) &&
                   (cmd.deadlineMs == 0 || GetTickCount64() < cmd.deadlineMs);
        }
        if (!inTime) {
          std::cout << "[native-video-host][lock-cred] retry requestId=" << cmd.challenge.requestId
                    << " challenge answered after the retry ended: not adopted''' + NL + '''";
          out = ControlUnlockChallengeMessage{};
          out.requestId = cmd.challenge.requestId;
          out.status = static_cast<uint16_t>(UnlockStage::Superseded);
          out.reserved = kUnlockPurposeLockCred;
        }
      }
      // Q2: an issued challenge holds the pipe for its seal (the service binds it to this connection) -- no
      // longer than the challenge lives; a refused one lets it go.
      std::lock_guard<std::mutex> lk(mu_);
      const TxKey key{cmd.cookie, cmd.challenge.requestId};
      // r3 L1-R: published only for the transaction still alive -- its session may have ended (or started a
      // newer one) while this exchange ran; then nothing is kept and nothing is held for it.
      const bool live = IsLiveLocked(cmd, cmd.challenge.requestId);''',
     '''      if (retryEndedEarlier && retryIoHookForTest_) retryIoHookForTest_("adopt");
      // Q2: an issued challenge holds the pipe for its seal (the service binds it to this connection) -- no
      // longer than the challenge lives; a refused one lets it go.
      std::lock_guard<std::mutex> lk(mu_);
      const TxKey key{cmd.cookie, cmd.challenge.requestId};
      // r3 L1-R: published only for the transaction still alive -- its session may have ended (or started a
      // newer one) while this exchange ran; then nothing is kept and nothing is held for it.
      const bool live = IsLiveLocked(cmd, cmd.challenge.requestId);
      // lk-ux-retry r3/r4 (Codex N1): a retry's fresh challenge is ADOPTED (held, cached as issued) only while the
      // retry is still owned AND in time -- decided here, in the one critical section that holds/caches it. A
      // valid answer after its end is never promoted: the earlier wait is over (17), nothing new held.
      if (retryEndedEarlier && out.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued) &&
          !(live && (cmd.deadlineMs == 0 || GetTickCount64() < cmd.deadlineMs))) {
        std::cout << "[native-video-host][lock-cred] retry requestId=" << cmd.challenge.requestId
                  << " challenge answered after the retry ended: not adopted''' + NL + '''";
        out = ControlUnlockChallengeMessage{};
        out.requestId = cmd.challenge.requestId;
        out.status = static_cast<uint16_t>(UnlockStage::Superseded);
        out.reserved = kUnlockPurposeLockCred;
      }'''),
])
print('ok')
