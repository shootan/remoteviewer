// "Windows 로그인" -- the pre-login worker's end of the sealed credential (prelogin-ui r3, item 5; r5 G7).
// See prelogin_cred_host.hpp.

#include "prelogin_cred_host.hpp"

#include <windows.h>
#include <UIAutomation.h>

#include <cstring>
#include <string>
#include <vector>

#include "prelogin_logon_desktop.hpp"
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
  if (stillServing && !stillServing()) {  // r17 R16-1: this worker has left the logon desktop
    m.status = static_cast<uint16_t>(UnlockStage::RejectedPolicy);
    return m;
  }
  // The same limit as the service: an unauthenticated peer must not spin key generation.
  const uint64_t now = now_ms();
  if (lastChallengeMs_ != 0 && now - lastChallengeMs_ < 500) {
    m.status = static_cast<uint16_t>(UnlockStage::RejectedPolicy);
    return m;
  }
  lastChallengeMs_ = now;
  if (inflight_) cancel_.store(true, std::memory_order_release);  // a new challenge drops the one waiting
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
  c.expiresMs = now + challengeLifetimeMs_;
  c.accountId = 0;  // no account before login: the binding is this worker, this session, this challenge
  FillChallengeFromContext(c, req.requestId, salt_, &m);
  m.status = static_cast<uint16_t>(UnlockStage::ChallengeIssued);
  challenge_ = m;
  challengeExpiresMs_ = c.expiresMs;
  challengeCookie_ = cookie;
  state_.Issue(challengeId, cookie, generation_, c.issuedMs, c.expiresMs);
  haveChallenge_ = true;
  haveResult_ = false;
  return m;
}

void PreloginCredReceiver::Finish(uint32_t requestId, UnlockStage stage, uint32_t win32) {
  result_ = ControlUnlockStatusResultMessage{};
  result_.requestId = requestId;
  result_.stage = static_cast<uint16_t>(stage);
  result_.terminal = 1;
  result_.win32Error = win32;
  haveResult_ = true;
  if (onResult) onResult(result_.requestId, result_.stage, result_.win32Error);
}

void PreloginCredReceiver::Sealed(const ControlUnlockSealedRequestMessage& req, uint64_t cookie) {
  std::lock_guard<std::mutex> lock(mu_);
  if (haveResult_ && result_.requestId == req.requestId) return;  // a retransmit: typed once, answered the same
  if (inflight_) return;  // one credential at a time; a retransmit of it (or anything else) waits for its result
  if (!haveChallenge_ || req.requestId != challenge_.requestId || (stillServing && !stillServing())) {
    // r17 R16-1: also a challenge issued before this worker left the logon desktop
    Finish(req.requestId, UnlockStage::RejectedStaleTopology, 0);
    return;
  }
  const su::ChallengeVerdict v = state_.Verify(req.challengeId, cookie, generation_, now_ms());
  if (v == su::ChallengeVerdict::Expired) {
    Finish(req.requestId, UnlockStage::Timeout, 0);
    return;
  }
  if (v != su::ChallengeVerdict::Valid) {
    Finish(req.requestId, UnlockStage::RejectedStaleTopology, 0);
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
    Finish(req.requestId, UnlockStage::DecryptFailed, 0);
    return;
  }
  state_.Consume(req.challengeId);
  uint16_t units[su::kMaxPasswordUtf16] = {};
  uint16_t count = 0;
  const bool unpacked = su::UnpackPassword(plain, units, &count);
  su::SecureZero(plain, sizeof(plain));
  if (!unpacked || count == 0) {
    su::SecureZero(units, sizeof(units));
    Finish(req.requestId, UnlockStage::DecryptFailed, 0);
    return;
  }
  // The wait for the logon UI and the typing run off the control thread (the viewer's 44 polls carry
  // the outcome); the control thread answers 43 at once and goes on serving pongs and input.
  if (worker_.joinable()) worker_.join();  // the previous one already left (inflight_ was false)
  inflight_ = true;
  inflightCookie_ = cookie;
  cancel_.store(false, std::memory_order_release);
  const uint64_t environment = environment_ ? environment_() : 0;  // where it was opened
  auto* held = new uint16_t[su::kMaxPasswordUtf16];
  std::memcpy(held, units, sizeof(units));
  su::SecureZero(units, sizeof(units));
  worker_ = std::thread([this, held, count, requestId = req.requestId, environment, expires = challengeExpiresMs_] {
    Deliver(held, count, requestId, environment, expires);
  });
}

void PreloginCredReceiver::Deliver(uint16_t* units, uint16_t count, uint32_t requestId, uint64_t environment,
                                   uint64_t expiresMs) {
  // What it was sent for must still hold, every time it is looked at: not dropped (its control session
  // ended / a new challenge), not expired, same console session and input desktop.
  UnlockStage dropped = UnlockStage::Injected;  // Injected = still valid
  auto still_valid = [&] {
    if (cancel_.load(std::memory_order_acquire) || (stillServing && !stillServing())) {
      dropped = UnlockStage::RejectedStaleTopology;
    } else if (now_ms() >= expiresMs) {
      dropped = UnlockStage::Timeout;
    } else if ((environment_ ? environment_() : 0) != environment) {
      dropped = UnlockStage::RejectedStaleTopology;
    }
    return dropped == UnlockStage::Injected;
  };
  // Typed only into the logon UI: if the input target is not it (within a short wait), nothing is typed.
  uint64_t target = 0;
  for (uint32_t waited = 0; still_valid(); waited += 100) {
    target = target_ ? target_() : 0;
    if (target != 0 || waited >= targetWaitMs_) break;
    Sleep(100);
  }
  UnlockStage stage = UnlockStage::RejectedPolicy;
  uint32_t err = 0;
  bool typed = false;
  if (dropped != UnlockStage::Injected) {
    stage = dropped;
  } else if (target != 0) {
    // r7 H5: one step, under commitMu_ (EndSession waits for it, then cancels): still valid -> the same
    // target taken again -> the whole password in one SendInput -> once more. A request cancelled before
    // this step is never typed; one that is typing finishes before the cancel returns.
    std::lock_guard<std::mutex> commit(commitMu_);
    if (!still_valid()) {
      stage = dropped;
    } else if ((target_ ? target_() : 0) == target) {
      typed = inject_ && inject_(units, count, &err);
      if (!typed) {
        stage = UnlockStage::InternalError;
      } else if ((target_ ? target_() : 0) != target) {
        stage = UnlockStage::InternalError;
        err = ERROR_INVALID_WINDOW_HANDLE;
      } else {
        stage = UnlockStage::Injected;
      }
    }
  }
  su::SecureZero(units, su::kMaxPasswordUtf16 * sizeof(uint16_t));
  delete[] units;
  std::lock_guard<std::mutex> lock(mu_);
  if (typed) ++injections_;
  Finish(requestId, stage, stage == UnlockStage::InternalError ? err : 0);
  inflight_ = false;
}

void PreloginCredReceiver::EndSession(uint64_t cookie) {
  std::lock_guard<std::mutex> commit(commitMu_);  // r7 H5: not while a typing is being committed
  std::lock_guard<std::mutex> lock(mu_);
  if (inflight_ && inflightCookie_ == cookie) cancel_.store(true, std::memory_order_release);
  if (haveChallenge_ && challengeCookie_ == cookie) haveChallenge_ = false;  // its challenge dies with it
}

PreloginCredReceiver::~PreloginCredReceiver() {
  cancel_.store(true, std::memory_order_release);
  if (worker_.joinable()) worker_.join();
}

PreloginCredReceiver::InjectFn ProductCredInject() {
  return [](const uint16_t* units, uint16_t count, uint32_t* err) {
    // One SendInput for all of it: the system does not interleave other input into one call's events.
    std::vector<INPUT> in(static_cast<size_t>(count) * 2);
    for (uint16_t i = 0; i < count; ++i) {
      for (int k = 0; k < 2; ++k) {
        INPUT& e = in[static_cast<size_t>(i) * 2 + k];
        e.type = INPUT_KEYBOARD;
        e.ki.wScan = units[i];
        e.ki.dwFlags = KEYEVENTF_UNICODE | (k == 1 ? KEYEVENTF_KEYUP : 0);
      }
    }
    const UINT sent = SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
    const DWORD e = GetLastError();
    SecureZeroMemory(in.data(), in.size() * sizeof(INPUT));
    if (sent != in.size()) {
      *err = e ? e : ERROR_GEN_FAILURE;
      return false;
    }
    return true;
  };
}

namespace {
std::wstring input_desktop_name() {
  std::wstring name;
  if (HDESK d = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS)) {
    wchar_t buf[128] = {};
    DWORD need = 0;
    if (GetUserObjectInformationW(d, UOI_NAME, buf, sizeof(buf), &need)) name = buf;
    CloseDesktop(d);
  }
  return name;
}

bool process_is_local_system(HANDLE proc) {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(proc, TOKEN_QUERY, &tok)) return false;
  BYTE buf[SECURITY_MAX_SID_SIZE + sizeof(TOKEN_USER)] = {};
  DWORD n = 0;
  bool system = false;
  if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n)) {
    BYTE sys[SECURITY_MAX_SID_SIZE];
    DWORD sysSize = sizeof(sys);
    system = CreateWellKnownSid(WinLocalSystemSid, nullptr, sys, &sysSize) &&
             EqualSid(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, sys);
  }
  CloseHandle(tok);
  return system;
}

// r7 H5: the element that has the keyboard focus is a password field, in process `pid`.
bool focused_password_field_in(DWORD pid) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool ok = false;
  IUIAutomation* uia = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) && uia) {
    IUIAutomationElement* focused = nullptr;
    if (SUCCEEDED(uia->GetFocusedElement(&focused)) && focused) {
      BOOL isPassword = FALSE;
      int owner = 0;
      ok = SUCCEEDED(focused->get_CurrentIsPassword(&isPassword)) && isPassword &&
           SUCCEEDED(focused->get_CurrentProcessId(&owner)) && static_cast<DWORD>(owner) == pid;
      focused->Release();
    }
    uia->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return ok;
}

uint64_t identity_of(HWND w, DWORD pid) {
  return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(w)) & 0xFFFFFFFFull) | (static_cast<uint64_t>(pid) << 32) | 1ull;
}
}  // namespace

PreloginCredReceiver::TargetFn ProductCredTargetWith(std::function<std::wstring()> desktopName,
                                                     std::function<uint64_t()> logonUiPasswordField, std::wstring served) {
  return [desktopName, logonUiPasswordField, served]() -> uint64_t {
    const std::wstring now = desktopName();
    PreloginSawDesktop(now, served);  // r18 R17-1: another desktop seen here is the worker's leave
    if (now.empty() || _wcsicmp(now.c_str(), served.c_str()) != 0) return 0;
    return logonUiPasswordField();  // on the served desktop: 0 while the password field is not there yet
  };
}

PreloginCredReceiver::EnvironmentFn ProductCredEnvironmentWith(std::function<std::wstring()> desktopName, std::wstring served) {
  return [desktopName, served]() -> uint64_t {
    uint64_t h = 1469598103934665603ull;
    const DWORD console = WTSGetActiveConsoleSessionId();
    for (int i = 0; i < 4; ++i) {
      h ^= (console >> (8 * i)) & 0xFF;
      h *= 1099511628211ull;
    }
    const std::wstring now = desktopName();
    PreloginSawDesktop(now, served);  // r18 R17-1
    for (wchar_t c : now) {
      h ^= static_cast<uint16_t>(c);
      h *= 1099511628211ull;
    }
    return h;
  };
}

PreloginCredReceiver::TargetFn ProductCredTarget() {
#ifdef REMOTE60_STREAM_TEST_SEAM
  // TEST BUILD ONLY (GNLinkStreamMigrationTest): the test's own window, by its title.
  wchar_t want[256] = {};
  const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_CRED_TARGET_TITLE", want, 256);
  const std::wstring title(want, n < 256 ? n : 0);
  return [title]() -> uint64_t {
    if (title.empty()) return 0;
    PreloginSawDesktop(input_desktop_name(), PreloginServedDesktop());  // r18 R17-1: the same leave
    HWND fg = GetForegroundWindow();
    wchar_t got[256] = {};
    DWORD pid = 0;
    if (!fg || GetWindowTextW(fg, got, 256) <= 0 || title != got || !GetWindowThreadProcessId(fg, &pid)) return 0;
    if (!focused_password_field_in(pid)) return 0;  // r7 H5: the same field rule as the product
    return identity_of(fg, pid);
  };
#else
  wchar_t sysDir[MAX_PATH] = {};
  const UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
  const std::wstring logonUi = std::wstring(sysDir, len < MAX_PATH ? len : 0) + L"\\LogonUI.exe";
  // The input is on the Winlogon desktop (r18 R17-1: any other desktop seen here is the worker's leave),
  // its foreground window belongs to the system's LogonUI.exe (that full path, not just a process called so),
  // running as LocalSystem in this worker's session.
  return ProductCredTargetWith(input_desktop_name, [logonUi]() -> uint64_t {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg || !GetWindowThreadProcessId(fg, &pid) || pid == 0) return 0;
    DWORD mine = 0, theirs = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &mine) || !ProcessIdToSessionId(pid, &theirs) || mine != theirs) return 0;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return 0;
    wchar_t path[MAX_PATH * 2] = {};
    DWORD plen = MAX_PATH * 2;
    const bool image = QueryFullProcessImageNameW(p, 0, path, &plen) != FALSE &&
                       CompareStringOrdinal(path, static_cast<int>(plen), logonUi.c_str(), static_cast<int>(logonUi.size()),
                                            TRUE) == CSTR_EQUAL;
    const bool system = image && process_is_local_system(p);
    CloseHandle(p);
    // r7 H5: and the focus is in a password field of it -- not the user-name box of the same window.
    return system && focused_password_field_in(pid) ? identity_of(fg, pid) : 0;
  }, L"Winlogon");
#endif
}

PreloginCredReceiver::EnvironmentFn ProductCredEnvironment() {
  return ProductCredEnvironmentWith(input_desktop_name, PreloginServedDesktop());
}

uint32_t ProductCredTargetWaitMs() {
#ifdef REMOTE60_STREAM_TEST_SEAM
  wchar_t v[16] = {};
  const DWORD n = GetEnvironmentVariableW(L"GNLINK_STREAM_TEST_CRED_WAIT_MS", v, 16);
  if (n > 0 && n < 16) {
    const unsigned long ms = wcstoul(v, nullptr, 10);
    if (ms > 0 && ms <= 60000) return static_cast<uint32_t>(ms);
  }
#endif
  return 3000;
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
