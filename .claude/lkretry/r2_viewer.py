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

edit('viewer_unlock.hpp', [
    ('''constexpr uint16_t kLoginPending = 0xFFFB;       // lockscreen-cred: the previous password still waits for its unlock''',
     '''constexpr uint16_t kLoginPending = 0xFFFB;       // lockscreen-cred: the previous password still waits for its unlock
// lk-ux-retry r2 (Codex N2): the retry ended the earlier wait (the host issued the new challenge), and then the
// NEW password ... was never sealed/sent (a local failure) -- or was sent, but its answer never came.
constexpr uint16_t kLockRetryNotSent = 0xFFFA;
constexpr uint16_t kLockRetryUnknown = 0xFFF9;
// How far one lock-screen exchange got: the challenge it asked for was issued (for a retry: the earlier wait IS
// over), and its sealed password was handed to the link.
struct LockCredProgress {
  bool challengeIssued = false;
  bool sealSent = false;
};'''),
    ('''bool run_lock_cred_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password, uint16_t* stage,
                            uint32_t* win32, bool* waiting, uint32_t* requestId, std::string* status,
                            uint32_t supersedesRequestId = 0);''',
     '''bool run_lock_cred_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password, uint16_t* stage,
                            uint32_t* win32, bool* waiting, uint32_t* requestId, std::string* status,
                            uint32_t supersedesRequestId = 0, LockCredProgress* progress = nullptr);'''),
])

edit('viewer_unlock.cpp', [
    ('''                     uint32_t* requestIdOut = nullptr, uint32_t supersedesRequestId = 0) {''',
     '''                     uint32_t* requestIdOut = nullptr, uint32_t supersedesRequestId = 0,
                     LockCredProgress* progress = nullptr) {'''),
    ('''    return terminal != nullptr;  // lockscreen-cred r1: the refusal is the answer, said to the user as such
  }''',
     '''    return terminal != nullptr;  // lockscreen-cred r1: the refusal is the answer, said to the user as such
  }
  if (progress) progress->challengeIssued = true;  // lk-ux-retry r2: for a retry, the earlier wait is over now'''),
    ('''  // 2) Seal the password to the host's ephemeral key.
  if (password.size() > su::kMaxPasswordUtf16) {''',
     '''  // 2) Seal the password to the host's ephemeral key.
#ifdef REMOTE60_VIEWER_UNLOCK_TEST_SEAM
  // TEST BUILD ONLY (lk-ux-retry r2, Codex N2): the sealing fails right here, after the challenge was issued
  // (GNLINK_VIEWER_TEST_SEAL_FAIL=1) -- a local failure before anything of the password left this process.
  if (GetEnvironmentVariableW(L"GNLINK_VIEWER_TEST_SEAL_FAIL", nullptr, 0) != 0) {
    set_status("seal failed (test seam)");
    return false;
  }
#endif
  if (password.size() > su::kMaxPasswordUtf16) {'''),
    ('''  su::SecureZero(cipher, sizeof(cipher));
  TcpControlResponse ra{};
  const bool acc = execute_control_action(link, s, &ra);''',
     '''  su::SecureZero(cipher, sizeof(cipher));
  if (progress) progress->sealSent = true;  // lk-ux-retry r2: from here it may be typed, answer or not
  TcpControlResponse ra{};
  const bool acc = execute_control_action(link, s, &ra);'''),
    ('''                            uint32_t supersedesRequestId) {
  remote60::native_poc::ControlUnlockStatusResultMessage res{};
  uint32_t rid = 0;
  const bool done = sealed_exchange(link, password, SealAs::LockCred, status, &res, &rid, supersedesRequestId);''',
     '''                            uint32_t supersedesRequestId, LockCredProgress* progress) {
  remote60::native_poc::ControlUnlockStatusResultMessage res{};
  uint32_t rid = 0;
  const bool done = sealed_exchange(link, password, SealAs::LockCred, status, &res, &rid, supersedesRequestId, progress);'''),
    ('''    case UnlockStage::Superseded:
      // lk-ux-retry r1: the earlier typed password's wait, ended by this connection's own retry (not shown normally:
      // the new attempt's result follows).
      return L"앞서 입력한 비밀번호의 결과는 더 기다리지 않습니다. 새로 입력한 비밀번호로 진행합니다.";''',
     '''    case UnlockStage::Superseded:
      // lk-ux-retry r2 (Codex N2): seen only when the retry's own answer did not reach the viewer (it said 18, or
      // nothing) while the host did end the earlier wait -- so the new password was not sent. Never "2 minutes".
      return L"앞서 입력한 비밀번호의 대기는 다시 누른 요청으로 끝났지만, 이번 비밀번호는 보내지 않았습니다.\\n"
             L"'Windows 로그인'을 다시 누르세요.";'''),
    ('''  if (stage == kLoginPending) {''',
     '''  if (stage == kLockRetryNotSent) {  // lk-ux-retry r2
    return L"앞서 입력한 비밀번호의 대기는 끝냈지만, 새 비밀번호는 이 PC에서 암호화하지 못해 보내지 않았습니다.\\n"
           L"'Windows 로그인'을 다시 누르세요.";
  }
  if (stage == kLockRetryUnknown) {
    return L"앞서 입력한 비밀번호의 대기는 끝냈고 새 비밀번호를 보냈지만, 원격 PC의 답을 받지 못했습니다.\\n"
           L"원격 화면에서 비밀번호 칸을 확인하세요.";
  }
  if (stage == kLoginPending) {'''),
])

edit('viewer_control_client.cpp', [
    ('''          (void)run_lock_cred_exchange(*controlLink, pw, &stage, &win32, &waiting, &lockRequestId, &status, supersedes);
          // The host answers 18 for every "not ended / nothing asked"; no answer at all (stage 0) is not known
          // either. Only otherwise has the earlier wait ended.
          if (supersedes != 0 && stage != 0 &&
              stage != static_cast<uint16_t>(remote60::native_poc::UnlockStage::SupersedeRefused)) {''',
     '''          LockCredProgress progress;
          const bool exchanged =
              run_lock_cred_exchange(*controlLink, pw, &stage, &win32, &waiting, &lockRequestId, &status, supersedes, &progress);
          // lk-ux-retry r2 (Codex N2): the earlier wait is over exactly when the host issued THIS retry's challenge
          // -- whatever then happened to the new password. 18, a refusal or no answer: it still waits (followed).
          if (supersedes != 0 && progress.challengeIssued) {
            if (!exchanged) {
              // The new password: never sealed (a local failure) / sent, but its answer never came.
              stage = progress.sealSent ? kLockRetryUnknown : kLockRetryNotSent;
              win32 = 0;
            }'''),
    ('''        if (stage != static_cast<uint16_t>(remote60::native_poc::UnlockStage::SessionUnlocked)) {
          stage = static_cast<uint16_t>(remote60::native_poc::UnlockStage::UnlockUnconfirmed);
        }''',
     '''        // lk-ux-retry r2 (Codex N2): an explicit end of it (Superseded) is said as that, never as "2 minutes".
        if (stage != static_cast<uint16_t>(remote60::native_poc::UnlockStage::SessionUnlocked) &&
            stage != static_cast<uint16_t>(remote60::native_poc::UnlockStage::Superseded)) {
          stage = static_cast<uint16_t>(remote60::native_poc::UnlockStage::UnlockUnconfirmed);
        }'''),
])
print('ok')
