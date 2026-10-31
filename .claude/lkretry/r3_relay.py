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
    ('''  Io Exchange(void* pipe, const void* req, uint32_t reqSize, uint8_t* buf, uint32_t bufSize, uint32_t replySize,''',
     '''  // lk-ux-retry r3 (Codex N1): `gate` (a retry's command): its write is started only while that retry is still
  // owned (live) and inside its absolute deadline -- checked under mu_ and the write issued in the same
  // critical section, so a caller's withdrawal is either before it (nothing written: Io::Withdrawn) or after.
  Io Exchange(void* pipe, const void* req, uint32_t reqSize, uint8_t* buf, uint32_t bufSize, uint32_t replySize,'''),
])

p = src + 'host_unlock_relay.hpp'
s = open(p, 'rb').read().decode('utf-8')
import re
m = re.search(r'  Io Exchange\(void\* pipe, const void\* req, uint32_t reqSize, uint8_t\* buf, uint32_t bufSize, uint32_t replySize,\r?\n([^;]*);', s)
assert m, 'Exchange decl'
decl = m.group(0)
newdecl = decl[:-2] + ', const struct Command* gate = nullptr);' if decl.endswith(');') else None
assert newdecl
s = s.replace(decl, newdecl)
open(p, 'wb').write(s.encode('utf-8'))

edit('host_unlock_relay.hpp', [
    ('''  enum class Op { Challenge, LockCredChallenge, LockCredRetry, Sealed, Capabilities, Status, Release };''',
     '''  enum class Op { Challenge, LockCredChallenge, LockCredRetry, Sealed, Capabilities, Status, Release };
 public:
  // TEST ONLY (lk-ux-retry r3): called on the worker right before a retry's exchange ("supersede" /
  // "challenge"), after its fence and before Exchange's own gate -- a test holds it there.
  void SetRetryIoHookForTest(std::function<void(const char*)> hook) { retryIoHookForTest_ = std::move(hook); }
 private:
  std::function<void(const char*)> retryIoHookForTest_;'''),
])

edit('host_unlock_relay.cpp', [
    ('''                                              uint32_t bufSize, uint32_t replySize, uint32_t deadlineMs) {
  HANDLE pipe = static_cast<HANDLE>(pipeHandle);
  const ULONGLONG deadline = GetTickCount64() + deadlineMs;
  auto run = [&](bool write, void* data, DWORD size, DWORD* done) -> Io {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return Io::Failed;
    BOOL started = write ? WriteFile(pipe, data, size, done, &ov) : ReadFile(pipe, data, size, done, &ov);''',
     '''                                              uint32_t bufSize, uint32_t replySize, uint32_t deadlineMs,
                                              const Command* gate) {
  HANDLE pipe = static_cast<HANDLE>(pipeHandle);
  const ULONGLONG deadline = GetTickCount64() + deadlineMs;
  auto run = [&](bool write, void* data, DWORD size, DWORD* done) -> Io {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return Io::Failed;
    BOOL started = FALSE;
    if (write && gate) {
      // lk-ux-retry r3 (Codex N1): the linearization point -- owned and in time, checked and the write started
      // under the lock the caller withdraws under. Past it, nothing is started (never a 1 ms extension).
      std::lock_guard<std::mutex> lk(mu_);
      if (!IsLiveLocked(*gate, gate->challenge.requestId) ||
          (gate->deadlineMs != 0 && GetTickCount64() >= gate->deadlineMs)) {
        CloseHandle(ov.hEvent);
        return Io::Withdrawn;
      }
      started = WriteFile(pipe, data, size, done, &ov);
    } else {
      started = write ? WriteFile(pipe, data, size, done, &ov) : ReadFile(pipe, data, size, done, &ov);
    }'''),
    # supersede: hook, gate; no left=1
    ('''        uint32_t supersedeBudget = budget(shortDeadlineMs_);
        if (cmd.deadlineMs != 0) {
          const uint64_t now = GetTickCount64();
          const uint64_t left = cmd.deadlineMs > now ? cmd.deadlineMs - now : 1;
          supersedeBudget = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(supersedeBudget), left));
        }
        const Io io = Exchange(pipe, &q, sizeof(q), buf, sizeof(buf), sizeof(resp), supersedeBudget);''',
     '''        uint32_t supersedeBudget = budget(shortDeadlineMs_);
        if (cmd.deadlineMs != 0) {
          const uint64_t now = GetTickCount64();
          const uint64_t left = cmd.deadlineMs > now ? cmd.deadlineMs - now : 0;  // 0: the gate refuses it
          supersedeBudget = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(supersedeBudget), left));
        }
        if (retryIoHookForTest_) retryIoHookForTest_("supersede");
        const Io io = Exchange(pipe, &q, sizeof(q), buf, sizeof(buf), sizeof(resp), supersedeBudget, &cmd);'''),
    ('''        // Ended now (Superseded) or already ended on its own (unlocked / unconfirmed): either way it waits no more.
        cleared = valid && resp.terminal != 0 && resp.stage != static_cast<uint16_t>(UnlockStage::RejectedPolicy);
        if (!valid) io_failed();''',
     '''        // Ended now (Superseded) or already ended on its own (unlocked / unconfirmed): either way it waits no more.
        cleared = valid && resp.terminal != 0 && resp.stage != static_cast<uint16_t>(UnlockStage::RejectedPolicy);
        // (Withdrawn: nothing was written -- the earlier wait and its pipe are as they were.)
        if (!valid && io != Io::Withdrawn) io_failed();'''),
    # challenge: hook + gate for a retry, no left=1
    ('''      uint32_t challengeBudget = budget(shortDeadlineMs_);
      if (cmd.deadlineMs != 0) {  // lk-ux-retry r2: a retry's fresh challenge ends with the retry
        const uint64_t now = GetTickCount64();
        const uint64_t left = cmd.deadlineMs > now ? cmd.deadlineMs - now : 1;
        challengeBudget = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(challengeBudget), left));
      }
      const Io io = Exchange(pipe, &req, sizeof(req), buf, sizeof(buf), sizeof(SecureUnlockChallengeResponse),
                             challengeBudget);''',
     '''      uint32_t challengeBudget = budget(shortDeadlineMs_);
      if (cmd.deadlineMs != 0) {  // lk-ux-retry r2: a retry's fresh challenge ends with the retry
        const uint64_t now = GetTickCount64();
        const uint64_t left = cmd.deadlineMs > now ? cmd.deadlineMs - now : 0;  // r3: 0 -- the gate refuses it
        challengeBudget = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(challengeBudget), left));
      }
      if (retryEndedEarlier && retryIoHookForTest_) retryIoHookForTest_("challenge");
      const Io io = Exchange(pipe, &req, sizeof(req), buf, sizeof(buf), sizeof(SecureUnlockChallengeResponse),
                             challengeBudget, retryEndedEarlier ? &cmd : nullptr);'''),
    ('''        out.status = static_cast<uint16_t>(retryEndedEarlier ? UnlockStage::Superseded : UnlockStage::InternalError);
        if (retryEndedEarlier) out.reserved = kUnlockPurposeLockCred;
        io_failed();
      }''',
     '''        out.status = static_cast<uint16_t>(retryEndedEarlier ? UnlockStage::Superseded : UnlockStage::InternalError);
        if (retryEndedEarlier) out.reserved = kUnlockPurposeLockCred;
        if (io != Io::Withdrawn) io_failed();
      }
      // lk-ux-retry r3 (Codex N1): a retry's fresh challenge is ADOPTED (held, cached as issued) only for the retry
      // still owned and in time -- a valid answer that came after its end is never promoted to a new attempt:
      // the earlier wait is over (17), nothing new held.
      if (retryEndedEarlier && out.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued)) {
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
      }'''),
])

# Io::Withdrawn
p = src + 'host_unlock_relay.hpp'
s = open(p, 'rb').read().decode('utf-8')
m = re.search(r'enum class Io \{([^}]*)\};', s)
assert m, 'Io enum'
body = m.group(1)
assert 'Withdrawn' not in body
s = s.replace(m.group(0), 'enum class Io {' + body.rstrip() + ', Withdrawn };  // Withdrawn: lk-ux-retry r3, a retry\'s gate refused to start')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
