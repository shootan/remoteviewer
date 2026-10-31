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

edit('viewer_unlock.hpp', [
    ('''constexpr uint16_t kLockRetryNotSent = 0xFFFA;
constexpr uint16_t kLockRetryUnknown = 0xFFF9;''',
     '''constexpr uint16_t kLockRetryNotSent = 0xFFFA;
constexpr uint16_t kLockRetryUnknown = 0xFFF9;
// lk-ux-retry r3 (Codex N2-a): a password (no retry) handed to the host whose answer has not come yet -- followed on.
constexpr uint16_t kLockCredSentUnknown = 0xFFF8;'''),
    ('''struct LockCredProgress {
  bool challengeIssued = false;
  bool sealSent = false;
};''',
     '''// lk-ux-retry r3 (Codex N2-b): both are set only for an answer that passed every check (request id, purpose).
struct LockCredProgress {
  bool challengeIssued = false;
  bool sealSent = false;
  bool supersededAnswer = false;  // a retry answered Superseded (LockCred purpose): earlier over, nothing new issued
};'''),
])

edit('viewer_unlock.cpp', [
    ('''  if (ch.status != static_cast<uint16_t>(UnlockStage::ChallengeIssued)) {
    set_status(std::string("challenge rejected: ") + stage_name(ch.status));''',
     '''  if (ch.status != static_cast<uint16_t>(UnlockStage::ChallengeIssued)) {
    if (as == SealAs::LockCred && supersedesRequestId != 0 &&
        ch.status == static_cast<uint16_t>(UnlockStage::Superseded)) {
      // lk-ux-retry r3 (Codex N2-b): "the earlier wait is over" is believed only from an answer of this purpose.
      if (ch.reserved != kUnlockPurposeLockCred) {
        set_status("challenge purpose mismatch");
        return false;
      }
      if (progress) progress->supersededAnswer = true;
    }
    set_status(std::string("challenge rejected: ") + stage_name(ch.status));'''),
    ('''  if (progress) progress->challengeIssued = true;  // lk-ux-retry r2: for a retry, the earlier wait is over now
  // lockscreen-cred r1: a lock-screen challenge must say it was issued for that purpose -- the password is
  // never sealed for a challenge of another purpose.
  if (as == SealAs::LockCred && ch.reserved != kUnlockPurposeLockCred) {
    set_status("challenge purpose mismatch");
    return false;
  }''',
     '''  // lockscreen-cred r1: a lock-screen challenge must say it was issued for that purpose -- the password is
  // never sealed for a challenge of another purpose.
  if (as == SealAs::LockCred && ch.reserved != kUnlockPurposeLockCred) {
    set_status("challenge purpose mismatch");
    return false;
  }
  // lk-ux-retry r2/r3 (Codex N2-b): for a retry the earlier wait is over now -- said only after every check.
  if (progress) progress->challengeIssued = true;'''),
    ('''  // 4) Poll status until terminal (lockscreen-cred: or until typed -- the unlock is followed afterwards).
  for (int i = 0; i < 40; ++i) {''',
     '''  // 4) Poll status until terminal (lockscreen-cred: or until typed -- the unlock is followed afterwards).
  int polls = 40;
#ifdef REMOTE60_VIEWER_UNLOCK_TEST_SEAM
  // TEST BUILD ONLY (lk-ux-retry r3, Codex N2-a): stop asking here after N looks (GNLINK_VIEWER_TEST_STATUS_POLLS)
  // -- the "sent, its answer not come yet" state, made on demand; the caller then follows it as it would.
  {
    wchar_t v[16] = {};
    if (GetEnvironmentVariableW(L"GNLINK_VIEWER_TEST_STATUS_POLLS", v, 16) > 0) polls = _wtoi(v);
  }
#endif
  for (int i = 0; i < polls; ++i) {'''),
    ('''  if (stage == kLockRetryUnknown) {
    return L"앞서 입력한 비밀번호의 대기는 끝냈고 새 비밀번호를 보냈지만, 원격 PC의 답을 받지 못했습니다.\\n"
           L"원격 화면에서 비밀번호 칸을 확인하세요.";
  }''',
     '''  if (stage == kLockRetryUnknown) {  // lk-ux-retry r3: and it is followed on
    return L"앞서 입력한 비밀번호의 대기는 끝냈고 새 비밀번호를 보냈습니다. 결과는 아직 오지 않아 계속 확인합니다.\\n"
           L"원격 화면에서 비밀번호 칸을 확인하세요. 다시 누르면 이 비밀번호를 새 비밀번호로 바꿉니다.";
  }
  if (stage == kLockCredSentUnknown) {
    return L"비밀번호를 보냈습니다. 결과는 아직 오지 않아 계속 확인합니다.\\n"
           L"원격 화면에서 비밀번호 칸을 확인하세요. 다시 누르면 이 비밀번호를 새 비밀번호로 바꿉니다.";
  }'''),
])

edit('viewer_control_client.cpp', [
    ('''          const bool priorEnded =
              progress.challengeIssued || stage == static_cast<uint16_t>(remote60::native_poc::UnlockStage::Superseded);
          if (supersedes != 0 && priorEnded) {
            if (!exchanged) {
              // The new password: never sealed (a local failure) / sent, but its answer never came.
              stage = progress.sealSent ? kLockRetryUnknown : kLockRetryNotSent;
              win32 = 0;
            }''',
     '''          // (r3, Codex N2-b: both only from an answer that passed its checks -- request id and purpose.)
          const bool priorEnded = progress.challengeIssued || progress.supersededAnswer;
          if (supersedes != 0 && priorEnded) {
            if (!exchanged) {
              // The new password: never sealed (a local failure) / sent, but its answer never came.
              stage = progress.sealSent ? kLockRetryUnknown : kLockRetryNotSent;
              win32 = 0;
            }'''),
    ('''            lockCredPollRequestId = 0;
            ctx.session.lockCredPending.store(false, std::memory_order_release);
          }
        } else {
          (void)run_prelogin_cred_exchange(*controlLink, pw, &stage, &win32, &status);''',
     '''            lockCredPollRequestId = 0;
            ctx.session.lockCredPending.store(false, std::memory_order_release);
          }
          // lk-ux-retry r3 (Codex N2-a): a password the host may already be typing (its seal went out) but whose
          // answer has not come is NOT forgotten: it is followed like a typed one -- so the next press is a retry
          // that names it (the host ends its wait first), never an ordinary request refused as "pending".
          if (!exchanged && progress.sealSent) {
            if (supersedes == 0) stage = kLockCredSentUnknown;
            waiting = true;
          }
        } else {
          (void)run_prelogin_cred_exchange(*controlLink, pw, &stage, &win32, &status);'''),
])
print('ok')
