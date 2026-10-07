// "Windows 로그인" -- the pre-login worker's end of the sealed credential (prelogin-ui r3, item 5).
// See prelogin_cred_host.hpp.

#include "prelogin_cred_host.hpp"

#include <windows.h>

#include <cstring>
#include <string>

#include "unlock_wire.hpp"

namespace remote60::native_poc::prelogin {

namespace su = sealed_unlock;

namespace {
uint64_t now_ms() { return GetTickCount64(); }
}  // namespace

ControlUnlockChallengeMessage PreloginCredReceiver::Challenge(const ControlUnlockChallengeRequestMessage& req, uint64_t cookie) {
  std::lock_guard<std::mutex> lock(mu_);
  ControlUnlockChallengeMessage m{};
  m.requestId = req.requestId;
  // The same limit as the service: an unauthenticated peer must not spin key generation.
  const uint64_t now = now_ms();
  if (lastChallengeMs_ != 0 && now - lastChallengeMs_ < 500) {
    m.status = static_cast<uint16_t>(UnlockStage::RejectedPolicy);
    return m;
  }
  lastChallengeMs_ = now;
  haveChallenge_ = false;
  uint8_t challengeId[su::kChallengeIdBytes] = {};
  su::UnlockContext c{};
  if (!key_.Generate() || !key_.ExportPublic(c.hostPub) || !su::RandomBytes(salt_, su::kSaltBytes) ||
      !su::RandomBytes(challengeId, su::kChallengeIdBytes)) {
    m.status = static_cast<uint16_t>(UnlockStage::InternalError);
    return m;
  }
  ++generation_;
  DWORD mySession = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &mySession);
  c.protocolVersion = 1;
  c.hostId = hostId_;
  c.clientSessionCookie = cookie;
  std::memcpy(c.challengeId, challengeId, su::kChallengeIdBytes);
  c.requestId = req.requestId;
  c.requesterSession = mySession;
  c.consoleSession = WTSGetActiveConsoleSessionId();
  c.lockGeneration = generation_;
  c.topologyGeneration = generation_;
  c.issuedMs = now;
  c.expiresMs = now + 30000;
  c.accountId = 0;  // no account before login: the binding is this worker, this session, this challenge
  FillChallengeFromContext(c, req.requestId, salt_, &m);
  m.status = static_cast<uint16_t>(UnlockStage::ChallengeIssued);
  challenge_ = m;
  state_.Issue(challengeId, cookie, generation_, c.issuedMs, c.expiresMs);
  haveChallenge_ = true;
  haveResult_ = false;
  return m;
}

void PreloginCredReceiver::Sealed(const ControlUnlockSealedRequestMessage& req, uint64_t cookie) {
  std::lock_guard<std::mutex> lock(mu_);
  if (haveResult_ && result_.requestId == req.requestId) return;  // a retransmit: typed once, answered the same
  if (inflight_) return;  // one credential at a time; a retransmit of it (or anything else) waits for its result
  auto finish = [&](UnlockStage stage, uint32_t win32) {
    result_ = ControlUnlockStatusResultMessage{};
    result_.requestId = req.requestId;
    result_.stage = static_cast<uint16_t>(stage);
    result_.terminal = 1;
    result_.win32Error = win32;
    haveResult_ = true;
    if (onResult) onResult(result_.requestId, result_.stage, result_.win32Error);
  };
  if (!haveChallenge_ || req.requestId != challenge_.requestId) {
    finish(UnlockStage::RejectedStaleTopology, 0);
    return;
  }
  const su::ChallengeVerdict v = state_.Verify(req.challengeId, cookie, generation_, now_ms());
  if (v == su::ChallengeVerdict::Expired) {
    finish(UnlockStage::Timeout, 0);
    return;
  }
  if (v != su::ChallengeVerdict::Valid) {
    finish(UnlockStage::RejectedStaleTopology, 0);
    return;
  }
  // The prelogin purpose only: a blob sealed for the lock-screen unlock does not open here.
  const su::UnlockContext c = ContextFromChallenge(challenge_, req.clientPub);
  const auto kdf = su::BuildPreloginCredKdfInfo(c);
  const auto aad = su::BuildPreloginCredAad(c);
  uint8_t aesKey[su::kAesKeyBytes] = {};
  uint8_t plain[su::kPlaintextBytes] = {};
  const bool opened = su::DeriveAesKey(key_, req.clientPub, salt_, kdf.data(), kdf.size(), aesKey) &&
                      su::AesGcmOpen(aesKey, req.nonce, aad.data(), aad.size(), req.cipher, su::kPlaintextBytes, req.tag, plain);
  su::SecureZero(aesKey, sizeof(aesKey));
  if (!opened) {
    su::SecureZero(plain, sizeof(plain));
    finish(UnlockStage::DecryptFailed, 0);
    return;
  }
  state_.Consume(req.challengeId);
  uint16_t units[su::kMaxPasswordUtf16] = {};
  uint16_t count = 0;
  const bool unpacked = su::UnpackPassword(plain, units, &count);
  su::SecureZero(plain, sizeof(plain));
  if (!unpacked || count == 0) {
    su::SecureZero(units, sizeof(units));
    finish(UnlockStage::DecryptFailed, 0);
    return;
  }
  // The wait for the logon UI and the typing run off the control thread (the viewer's 44 polls carry
  // the outcome); the control thread answers 43 at once and goes on serving pongs and input.
  if (worker_.joinable()) worker_.join();  // the previous one already left (inflight_ was false)
  inflight_ = true;
  inflightId_ = req.requestId;
  auto* held = new uint16_t[su::kMaxPasswordUtf16];
  std::memcpy(held, units, sizeof(units));
  su::SecureZero(units, sizeof(units));
  worker_ = std::thread([this, held, count, requestId = req.requestId] { Deliver(held, count, requestId); });
}

void PreloginCredReceiver::Deliver(uint16_t* units, uint16_t count, uint32_t requestId) {
  // Typed only into the logon UI: if the input target is not it (within a short wait), nothing is typed.
  bool atTarget = target_ && target_();
  for (uint32_t waited = 0; !atTarget && waited < targetWaitMs_; waited += 100) {
    Sleep(100);
    atTarget = target_ && target_();
  }
  uint32_t err = 0;
  const bool typed = atTarget && inject_ && inject_(units, count, &err);
  su::SecureZero(units, su::kMaxPasswordUtf16 * sizeof(uint16_t));
  delete[] units;
  std::lock_guard<std::mutex> lock(mu_);
  if (typed) ++injections_;
  result_ = ControlUnlockStatusResultMessage{};
  result_.requestId = requestId;
  result_.stage = static_cast<uint16_t>(!atTarget ? UnlockStage::RejectedPolicy
                                                  : typed ? UnlockStage::Injected : UnlockStage::InternalError);
  result_.terminal = 1;
  result_.win32Error = atTarget && !typed ? err : 0;
  haveResult_ = true;
  inflight_ = false;
  if (onResult) onResult(result_.requestId, result_.stage, result_.win32Error);
}

PreloginCredReceiver::~PreloginCredReceiver() {
  if (worker_.joinable()) worker_.join();
}

PreloginCredReceiver::InjectFn ProductCredInject() {
  return [](const uint16_t* units, uint16_t count, uint32_t* err) {
    for (uint16_t i = 0; i < count; ++i) {
      INPUT in[2]{};
      for (int k = 0; k < 2; ++k) {
        in[k].type = INPUT_KEYBOARD;
        in[k].ki.wScan = units[i];
        in[k].ki.dwFlags = KEYEVENTF_UNICODE | (k == 1 ? KEYEVENTF_KEYUP : 0);
      }
      if (SendInput(2, in, sizeof(INPUT)) != 2) {
        *err = GetLastError();
        SecureZeroMemory(in, sizeof(in));
        return false;
      }
      SecureZeroMemory(in, sizeof(in));
    }
    return true;
  };
}

PreloginCredReceiver::TargetFn ProductCredTarget() {
#ifdef REMOTE60_STREAM_TEST_SEAM
  // TEST BUILD ONLY (GNLinkStreamMigrationTest): the test's own window, by its title.
  wchar_t want[256] = {};
  const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_CRED_TARGET_TITLE", want, 256);
  const std::wstring title(want, n < 256 ? n : 0);
  return [title] {
    if (title.empty()) return false;
    HWND fg = GetForegroundWindow();
    wchar_t got[256] = {};
    return fg && GetWindowTextW(fg, got, 256) > 0 && title == got;
  };
#else
  return [] {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg || !GetWindowThreadProcessId(fg, &pid) || pid == 0) return false;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    wchar_t path[MAX_PATH] = {};
    DWORD len = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(p, 0, path, &len) != FALSE;
    CloseHandle(p);
    if (!ok) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    return _wcsicmp(name ? name + 1 : path, L"LogonUI.exe") == 0;
  };
#endif
}

uint64_t ProductCredHostId() {
  wchar_t name[256] = {};
  DWORD n = 256;
  uint64_t h = 1469598103934665603ull;
  if (GetComputerNameW(name, &n)) {
    for (DWORD i = 0; i < n; ++i) {
      h ^= static_cast<uint16_t>(name[i]);
      h *= 1099511628211ull;
    }
  }
  return h;
}

bool PreloginCredReceiver::PollResult(uint32_t requestId, ControlUnlockStatusResultMessage* out) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!haveResult_ || result_.requestId != requestId) return false;
  *out = result_;
  return true;
}

}  // namespace remote60::native_poc::prelogin
