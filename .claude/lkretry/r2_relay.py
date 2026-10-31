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

edit('host_unlock_relay.hpp', [
    ('''    uint32_t supersedesRequestId = 0;  // lk-ux-retry r1: LockCredRetry only''',
     '''    uint32_t supersedesRequestId = 0;  // lk-ux-retry r1: LockCredRetry only
    // lk-ux-retry r2 (Codex N1): the whole retry's absolute end (GetTickCount64; 0 = none) -- its wait in the
    // queue, the supersede and the fresh challenge share it: past it nothing more of the retry is done.
    uint64_t deadlineMs = 0;'''),
])

edit('host_unlock_relay.cpp', [
    # the retry's first fence: never started once its caller has gone or its time is over
    ('''    if (cmd.op == Op::Release) {  // the control session ended: its held transaction ends with it''',
     '''    if (cmd.op == Op::LockCredRetry) {
      // lk-ux-retry r2 (Codex N1): a retry whose caller already answered 18 (timed out: its transaction was
      // withdrawn) or whose session ended does nothing at all -- no supersede, no challenge, no pipe.
      bool live = false;
      {
        std::lock_guard<std::mutex> lk(mu_);
        live = IsLiveLocked(cmd, cmd.challenge.requestId);
      }
      if (!live || (cmd.deadlineMs != 0 && GetTickCount64() >= cmd.deadlineMs)) {
        std::cout << "[native-video-host][lock-cred] retry requestId=" << cmd.challenge.requestId
                  << " not started: " << (live ? "past its deadline" : "withdrawn") << "''' + NL + '''";
        activeCookie_.store(0, std::memory_order_release);
        continue;
      }
    }
    if (cmd.op == Op::Release) {  // the control session ended: its held transaction ends with it'''),
    # the supersede's own budget never past the retry's end
    ('''        const Io io = Exchange(pipe, &q, sizeof(q), buf, sizeof(buf), sizeof(resp), budget(shortDeadlineMs_));
        if (io == Io::Ok) std::memcpy(&resp, buf, sizeof(resp));
        const bool valid = io == Io::Ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
                           resp.kind == static_cast<uint16_t>(SecureUnlockKind::Result) &&
                           resp.requestId == cmd.supersedesRequestId;''',
     '''        uint32_t supersedeBudget = budget(shortDeadlineMs_);
        if (cmd.deadlineMs != 0) {
          const uint64_t now = GetTickCount64();
          const uint64_t left = cmd.deadlineMs > now ? cmd.deadlineMs - now : 1;
          supersedeBudget = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(supersedeBudget), left));
        }
        const Io io = Exchange(pipe, &q, sizeof(q), buf, sizeof(buf), sizeof(resp), supersedeBudget);
        if (io == Io::Ok) std::memcpy(&resp, buf, sizeof(resp));
        const bool valid = io == Io::Ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
                           resp.kind == static_cast<uint16_t>(SecureUnlockKind::Result) &&
                           resp.requestId == cmd.supersedesRequestId;'''),
    # second fence: after the supersede, before the fresh challenge
    ('''      cmd.op = Op::LockCredChallenge;  // from here, the ordinary lock-screen challenge
    }''',
     '''      // lk-ux-retry r2 (Codex N1): the earlier wait is over (its own final answer stays with it, above); the
      // fresh challenge is asked only for a retry still owned and in time. One whose caller already answered
      // (timed out -> 18) or whose session ended gets none: no challenge, no hold of the pipe for it.
      {
        bool stillLive = false;
        {
          std::lock_guard<std::mutex> lk(mu_);
          stillLive = IsLiveLocked(cmd, cmd.challenge.requestId);
        }
        if (!stillLive || (cmd.deadlineMs != 0 && GetTickCount64() >= cmd.deadlineMs)) {
          std::cout << "[native-video-host][lock-cred] retry requestId=" << cmd.challenge.requestId
                    << " ended the earlier wait, then " << (stillLive ? "ran out of time" : "was withdrawn")
                    << ": no new challenge''' + NL + '''";
          release_if_idle();
          cv_.notify_all();
          activeCookie_.store(0, std::memory_order_release);
          continue;
        }
      }
      cmd.op = Op::LockCredChallenge;  // from here, the ordinary lock-screen challenge
    }'''),
    # the fresh challenge's budget never past the retry's end either
    ('''      const Io io = Exchange(pipe, &req, sizeof(req), buf, sizeof(buf), sizeof(SecureUnlockChallengeResponse),
                             budget(shortDeadlineMs_));''',
     '''      uint32_t challengeBudget = budget(shortDeadlineMs_);
      if (cmd.deadlineMs != 0) {  // lk-ux-retry r2: a retry's fresh challenge ends with the retry
        const uint64_t now = GetTickCount64();
        const uint64_t left = cmd.deadlineMs > now ? cmd.deadlineMs - now : 1;
        challengeBudget = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(challengeBudget), left));
      }
      const Io io = Exchange(pipe, &req, sizeof(req), buf, sizeof(buf), sizeof(SecureUnlockChallengeResponse),
                             challengeBudget);'''),
    # the caller: one deadline; at its end the retry is withdrawn
    ('''    cmd.supersedesRequestId = supersedesRequestId;
    cmd.cookie = clientSessionCookie;
    cmd.txGen = liveTx_[key] = ++txGenSeq_;
    queue_.push_back(cmd);
  }
  cv_.notify_all();
  std::unique_lock<std::mutex> lk(mu_);
  const bool got = cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return challengeResults_.count(key) != 0; });
  if (!got) return false;''',
     '''    cmd.supersedesRequestId = supersedesRequestId;
    cmd.cookie = clientSessionCookie;
    cmd.txGen = liveTx_[key] = ++txGenSeq_;
    cmd.deadlineMs = GetTickCount64() + timeoutMs;  // lk-ux-retry r2 (Codex N1): one end for the whole retry
    queue_.push_back(cmd);
  }
  cv_.notify_all();
  std::unique_lock<std::mutex> lk(mu_);
  const bool got = cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return challengeResults_.count(key) != 0; });
  if (!got) {
    // lk-ux-retry r2 (Codex N1): the caller answers 18 now, so the retry is withdrawn with it: not started ->
    // removed from the queue; started -> its transaction is gone, and the worker's fences stop it before any
    // fresh challenge (a supersede already on the pipe completes: the earlier request keeps its real end).
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
      if (it->op == Op::LockCredRetry && it->cookie == clientSessionCookie && it->challenge.requestId == req.requestId) {
        queue_.erase(it);
        break;
      }
    }
    liveTx_.erase(key);
    challengeResults_.erase(key);
    return false;
  }'''),
])
print('ok')
