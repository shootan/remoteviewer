src = 'C:\\work\\gnlink-wt\\fix-lk-retry\\apps\\native_poc\\src\\'

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
    ('''    cmd.deadlineMs = GetTickCount64() + timeoutMs;  // lk-ux-retry r2 (Codex N1): one end for the whole retry''',
     '''    // lk-ux-retry r2 (Codex N1): one end for the whole retry -- a little inside the caller's wait, so whatever
    // the worker concludes by it reaches the caller (never a worker result racing the caller's give-up).
    cmd.deadlineMs = GetTickCount64() + timeoutMs - (std::min)(timeoutMs / 4, 250u);'''),
    ('''        if (!stillLive || (cmd.deadlineMs != 0 && GetTickCount64() >= cmd.deadlineMs)) {
          std::cout << "[native-video-host][lock-cred] retry requestId=" << cmd.challenge.requestId
                    << " ended the earlier wait, then " << (stillLive ? "ran out of time" : "was withdrawn")
                    << ": no new challenge''',
     '''        if (!stillLive || (cmd.deadlineMs != 0 && GetTickCount64() >= cmd.deadlineMs)) {
          if (stillLive) {
            // Said as what happened: the earlier wait IS over (Superseded), and this retry got no challenge.
            std::lock_guard<std::mutex> lk(mu_);
            ControlUnlockChallengeMessage m{};
            m.requestId = cmd.challenge.requestId;
            m.status = static_cast<uint16_t>(UnlockStage::Superseded);
            m.reserved = kUnlockPurposeLockCred;
            challengeResults_[TxKey{cmd.cookie, cmd.challenge.requestId}] = m;
          }
          std::cout << "[native-video-host][lock-cred] retry requestId=" << cmd.challenge.requestId
                    << " ended the earlier wait, then " << (stillLive ? "ran out of time" : "was withdrawn")
                    << ": no new challenge'''),
    ('''      cmd.op = Op::LockCredChallenge;  // from here, the ordinary lock-screen challenge
    }''',
     '''      cmd.op = Op::LockCredChallenge;  // from here, the ordinary lock-screen challenge
      retryEndedEarlier = true;
    }'''),
    ('''    uint8_t buf[512];
    if (cmd.op == Op::LockCredRetry) {''',
     '''    uint8_t buf[512];
    bool retryEndedEarlier = false;  // lk-ux-retry r2: this challenge follows the retry's confirmed supersede
    if (cmd.op == Op::LockCredRetry) {'''),
    ('''      } else {
        out.requestId = cmd.challenge.requestId;
        out.status = static_cast<uint16_t>(UnlockStage::InternalError);
        io_failed();
      }''',
     '''      } else {
        out.requestId = cmd.challenge.requestId;
        // lk-ux-retry r2 (Codex N1/N2): after a retry ended the earlier wait, a fresh challenge that could not be
        // had (no answer within the retry's time) is said as that: the earlier one is over, nothing new issued.
        out.status = static_cast<uint16_t>(retryEndedEarlier ? UnlockStage::Superseded : UnlockStage::InternalError);
        out.reserved = kUnlockPurposeLockCred;
        io_failed();
      }'''),
])

edit('viewer_control_client.cpp', [
    ('''          if (supersedes != 0 && progress.challengeIssued) {''',
     '''          // (Superseded as the retry's own answer: the earlier wait ended, but no new challenge could be had.)
          const bool priorEnded =
              progress.challengeIssued || stage == static_cast<uint16_t>(remote60::native_poc::UnlockStage::Superseded);
          if (supersedes != 0 && priorEnded) {'''),
])
print('ok')
