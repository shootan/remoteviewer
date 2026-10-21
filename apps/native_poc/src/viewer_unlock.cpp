#include "viewer_unlock.hpp"

#include <dpapi.h>
#include <wincrypt.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "poc_protocol.hpp"
#include "sealed_unlock.hpp"
#include "unlock_wire.hpp"

#pragma comment(lib, "crypt32.lib")

namespace remote60::native_poc::viewer {
namespace su = remote60::native_poc::sealed_unlock;

namespace {

std::wstring cred_path(uint64_t hostId) {
  wchar_t base[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) == 0) return {};
  std::wstring dir = std::wstring(base) + L"\\GNLink";
  CreateDirectoryW(dir.c_str(), nullptr);
  wchar_t name[64]{};
  std::swprintf(name, 64, L"\\unlock_%016llx.cred", static_cast<unsigned long long>(hostId));
  return dir + name;
}

}  // namespace

bool save_unlock_password(uint64_t hostId, const std::wstring& password) {
  const std::wstring path = cred_path(hostId);
  if (path.empty()) return false;
  DATA_BLOB in{};
  in.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(password.c_str()));
  in.cbData = static_cast<DWORD>((password.size() + 1) * sizeof(wchar_t));
  DATA_BLOB out{};
  // User-scope DPAPI; CRYPTPROTECT_UI_FORBIDDEN so it never blocks on a prompt.
  if (!CryptProtectData(&in, L"GNLink-unlock", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &out)) {
    return false;
  }
  bool ok = false;
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    ok = WriteFile(f, out.pbData, out.cbData, &written, nullptr) && written == out.cbData;
    CloseHandle(f);
  }
  if (out.pbData) LocalFree(out.pbData);
  return ok;
}

bool load_unlock_password(uint64_t hostId, std::wstring* out) {
  if (!out) return false;
  out->clear();
  const std::wstring path = cred_path(hostId);
  if (path.empty()) return false;
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  std::vector<uint8_t> blob;
  uint8_t chunk[1024];
  DWORD read = 0;
  while (ReadFile(f, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
    blob.insert(blob.end(), chunk, chunk + read);
  }
  CloseHandle(f);
  if (blob.empty()) return false;
  DATA_BLOB in{};
  in.pbData = blob.data();
  in.cbData = static_cast<DWORD>(blob.size());
  DATA_BLOB clear{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &clear)) {
    return false;
  }
  if (clear.pbData && clear.cbData >= sizeof(wchar_t)) {
    *out = std::wstring(reinterpret_cast<wchar_t*>(clear.pbData),
                        clear.cbData / sizeof(wchar_t) - 1);  // drop trailing NUL
  }
  if (clear.pbData) {
    su::SecureZero(clear.pbData, clear.cbData);
    LocalFree(clear.pbData);
  }
  return !out->empty();
}

bool has_unlock_password(uint64_t hostId) {
  const std::wstring path = cred_path(hostId);
  return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void clear_unlock_password(uint64_t hostId) {
  const std::wstring path = cred_path(hostId);
  if (!path.empty()) DeleteFileW(path.c_str());
}

// --- password prompt (minimal modal window with a password edit + OK/Cancel) --------------------

namespace {
struct PromptState {
  std::wstring* out = nullptr;
  bool ok = false;
  bool done = false;
  HWND edit = nullptr;
};

LRESULT CALLBACK prompt_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* st = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_COMMAND:
      if (LOWORD(wp) == 1 /*OK*/ || LOWORD(wp) == 2 /*Cancel*/) {
        if (LOWORD(wp) == 1 && st && st->out) {
          wchar_t buf[256]{};
          GetWindowTextW(st->edit, buf, 256);
          *st->out = buf;
          SecureZeroMemory(buf, sizeof(buf));
          st->ok = true;
        }
        DestroyWindow(hwnd);
      }
      return 0;
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (st) st->done = true;  // do NOT PostQuitMessage: that is the whole UI thread's quit signal
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
// One modal password window; the lock-screen unlock and the pre-login "Windows 로그인" differ only in
// their words (and class, so a test finds the one it means).
bool prompt_password_window(HWND owner, const wchar_t* kClass, const wchar_t* title, const wchar_t* label,
                            const wchar_t* okText, std::wstring* out) {
  if (!out) return false;
  WNDCLASSW wc{};
  wc.lpfnWndProc = prompt_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kClass;
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  RegisterClassW(&wc);

  PromptState st;
  st.out = out;
  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, kClass,
                              title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              0, 0, 360, 150, owner, nullptr, wc.hInstance, nullptr);
  if (!hwnd) return false;
  // Center on the owner (or screen).
  RECT wr{};
  GetWindowRect(hwnd, &wr);
  const int w = wr.right - wr.left, h = wr.bottom - wr.top;
  const int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
  const int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  SetWindowPos(hwnd, HWND_TOPMOST, sx, sy, 0, 0, SWP_NOSIZE);
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

  CreateWindowW(L"STATIC", label,
                WS_CHILD | WS_VISIBLE, 12, 10, 330, 20, hwnd, nullptr, wc.hInstance, nullptr);
  st.edit = CreateWindowW(L"EDIT", L"",
                          WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL, 12, 36,
                          330, 24, hwnd, nullptr, wc.hInstance, nullptr);
  CreateWindowW(L"BUTTON", okText, WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 176, 74, 78, 26,
                hwnd, reinterpret_cast<HMENU>(1), wc.hInstance, nullptr);
  CreateWindowW(L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE, 262, 74, 78, 26, hwnd,
                reinterpret_cast<HMENU>(2), wc.hInstance, nullptr);
  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);
  SetFocus(st.edit);
  if (owner) EnableWindow(owner, FALSE);

  MSG m{};
  while (!st.done) {
    const BOOL got = GetMessageW(&m, nullptr, 0, 0);
    if (got == 0) {          // app is quitting: re-post so the outer viewer loop still sees WM_QUIT
      PostQuitMessage(static_cast<int>(m.wParam));
      break;
    }
    if (got == -1) break;    // GetMessage error
    if (m.hwnd == st.edit && m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
      SendMessageW(hwnd, WM_COMMAND, 1, 0);
      continue;
    }
    if (!IsDialogMessageW(hwnd, &m)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  if (owner) {
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
  }
  return st.ok && !out->empty();
}
}  // namespace

bool prompt_unlock_password(HWND owner, std::wstring* out) {
  return prompt_password_window(owner, L"GNLinkUnlockPrompt", L"GNLink 잠금해제 비밀번호",
                                L"이 호스트의 Windows 로그인 비밀번호를 입력하세요:", L"잠금해제", out);
}

bool prompt_prelogin_password(HWND owner, std::wstring* out) {
  return prompt_password_window(owner, kPreloginPromptClass, kPreloginPromptTitle,
                                L"원격 PC 로그인 화면에 입력할 비밀번호:", L"입력", out);
}

bool prompt_lock_screen_password(HWND owner, std::wstring* out) {
  // lockscreen-cred r1: the same window (its class, ids and button) -- only the words say which screen.
  return prompt_password_window(owner, kPreloginPromptClass, kPreloginPromptTitle,
                                L"원격 PC 잠금 화면에 입력할 Windows 비밀번호:", L"입력", out);
}

void show_prelogin_cred_result(HWND owner, const std::wstring& text) {
  show_prelogin_notice(owner, kPreloginResultTitle, text);
}

std::wstring reboot_result_text(uint8_t result, uint32_t win32) {
  switch (result) {
    case 0:
      return L"원격 PC가 다시 시작 요청을 받아들였습니다 (몇 초 뒤 다시 시작).\n"
             L"연결이 끊기면 다시 시작이 끝난 뒤 접속하세요.";
    case 1:
      return L"원격 PC가 거절했습니다: 이미 다시 시작하는 중입니다.";
    case 2:
      return L"원격 PC가 거절했습니다: 이 연결로는 다시 시작할 수 없습니다 (서버 인증 연결만 가능).";
    case 3:
      return L"원격 PC가 다시 시작하지 못했습니다 (오류 " + std::to_wstring(win32) + L").";
    case kRebootNotSent:
      return L"보내지 않았습니다: 확인한 뒤 연결이 바뀌었거나 이 PC가 원격 다시 시작을 지원하지 않습니다.";
    default:
      return L"원격 PC의 답을 받지 못했습니다. 다시 시작됐는지는 원격 화면이나 재접속으로 확인하세요.";
  }
}

std::wstring sas_result_text(uint8_t result, uint32_t win32, uint8_t flags) {
  const std::wstring cad = (flags & 0x01) != 0  // kPreloginStatusFlagCadNotRequired
                               ? L"\n이 PC는 로그인할 때 Ctrl+Alt+Del 이 필요 없도록 설정돼 있습니다."
                               : L"";
  switch (result) {
    case 0:
      return L"원격 PC 로그인 화면에 Ctrl+Alt+Del 을 보냈습니다.\n화면이 바뀌었는지는 원격 화면에서 확인하세요." + cad;
    case 5:
      return L"원격 PC 로그인 화면에 Ctrl+Alt+Del 을 보냈습니다.\n이 PC의 정책(SoftwareSASGeneration)이 설정돼 있지 않아 "
             L"받아들여졌는지는 확인되지 않습니다. 로그인 화면이 바뀌었는지 원격 화면에서 확인하세요." + cad;
    case 1:
      return L"원격 PC의 정책이 Ctrl+Alt+Del 을 프로그램이 보내는 것을 허용하지 않습니다 "
             L"(SoftwareSASGeneration=" + std::to_wstring(win32) + L"). 관리자가 정책을 허용해야 합니다." + cad;
    case 2:
      return L"보내지 않았습니다: 원격 PC가 지금 로그인 화면 상태가 아닙니다.";
    case 3:
      return L"원격 PC가 거절했습니다: 이 연결로는 보낼 수 없습니다 (서버 인증 연결만 가능).";
    case 4:
      return L"원격 PC가 Ctrl+Alt+Del 을 보내지 못했습니다 (오류 " + std::to_wstring(win32) + L").";
    case kSasNotSent:
      return L"보내지 않았습니다: 누른 뒤 연결이 바뀌었거나 이 PC가 지원하지 않습니다.";
    default:
      return L"원격 PC의 답을 받지 못했습니다.";
  }
}

void show_prelogin_notice(HWND owner, const wchar_t* title, const std::wstring& text) {
  // Its own small window rather than MessageBox: the same shape as the prompt, with a fixed class and
  // control ids, so what it says can be read and its button pressed by a test like any other control.
  WNDCLASSW wc{};
  wc.lpfnWndProc = prompt_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kPreloginResultClass;
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  RegisterClassW(&wc);
  PromptState st;  // no `out`: the OK button only closes it
  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, kPreloginResultClass, title,
                              WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0, 440, 170, owner, nullptr, wc.hInstance, nullptr);
  if (!hwnd) return;
  RECT wr{};
  GetWindowRect(hwnd, &wr);
  SetWindowPos(hwnd, HWND_TOPMOST, (GetSystemMetrics(SM_CXSCREEN) - (wr.right - wr.left)) / 2,
               (GetSystemMetrics(SM_CYSCREEN) - (wr.bottom - wr.top)) / 2, 0, 0, SWP_NOSIZE);
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));
  CreateWindowW(L"STATIC", text.c_str(), WS_CHILD | WS_VISIBLE, 14, 12, 404, 64, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreloginResultTextId)), wc.hInstance, nullptr);
  HWND ok = CreateWindowW(L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 340, 92, 78, 26,
                          hwnd, reinterpret_cast<HMENU>(1), wc.hInstance, nullptr);
  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);
  SetFocus(ok);
  if (owner) EnableWindow(owner, FALSE);
  MSG m{};
  while (!st.done) {
    const BOOL got = GetMessageW(&m, nullptr, 0, 0);
    if (got == 0) {
      PostQuitMessage(static_cast<int>(m.wParam));
      break;
    }
    if (got == -1) break;
    if (!IsDialogMessageW(hwnd, &m)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  if (owner) EnableWindow(owner, TRUE);
}

// --- the exchange -------------------------------------------------------------------------------

namespace {
uint16_t utf16_of(const std::wstring& s, uint16_t out[su::kMaxPasswordUtf16]) {
  const uint16_t n = static_cast<uint16_t>(
      s.size() > su::kMaxPasswordUtf16 ? su::kMaxPasswordUtf16 : s.size());
  for (uint16_t i = 0; i < n; ++i) out[i] = static_cast<uint16_t>(s[i]);
  return n;
}

const char* stage_name(uint16_t stage) {
  switch (static_cast<remote60::native_poc::UnlockStage>(stage)) {
    case remote60::native_poc::UnlockStage::SessionUnlocked: return "unlocked";
    case remote60::native_poc::UnlockStage::Injected: return "typed";
    case remote60::native_poc::UnlockStage::WtsConnectAccepted: return "connect-accepted";
    case remote60::native_poc::UnlockStage::AuthFailed: return "wrong-password";
    case remote60::native_poc::UnlockStage::RejectedPolicy: return "rejected(policy/not-locked)";
    case remote60::native_poc::UnlockStage::RejectedStaleTopology: return "rejected(session-changed)";
    case remote60::native_poc::UnlockStage::DecryptFailed: return "decrypt-failed";
    case remote60::native_poc::UnlockStage::Timeout: return "timeout";
    default: return "error";
  }
}
}  // namespace

namespace {
// The purpose a password is sealed under (sealed_unlock): the disconnected-session unlock, the pre-login
// worker's credential, or (lockscreen-cred r1) the lock screen's credential.
enum class SealAs { Unlock, Prelogin, LockCred };

// One status poll (44 -> 45) for `requestId`. False when the exchange failed.
bool poll_status(remote60::native_poc::ControlLink& link, uint32_t requestId,
                 remote60::native_poc::ControlUnlockStatusResultMessage* out) {
  using namespace remote60::native_poc;
  ControlOutboundAction st{};
  st.kind = ControlOutboundActionKind::UnlockStatusRequest;
  st.expectedResponseType = MessageType::ControlUnlockStatusResult;
  st.expectedResponseSize = static_cast<uint16_t>(sizeof(ControlUnlockStatusResultMessage));
  st.unlockStatusReq.header.magic = kMagic;
  st.unlockStatusReq.header.type = static_cast<uint16_t>(MessageType::ControlUnlockStatusRequest);
  st.unlockStatusReq.header.size = static_cast<uint16_t>(sizeof(st.unlockStatusReq));
  st.unlockStatusReq.requestId = requestId;
  TcpControlResponse rs{};
  if (!execute_control_action(link, st, &rs) || rs.kind != TcpControlResponseKind::UnlockStatusResult) return false;
  *out = rs.unlockStatusResult;
  return true;
}

// The one challenge -> seal -> poll exchange. `as` picks the purpose the password is sealed under (and,
// for LockCred, the challenge request 88 and the purpose the answer must carry); everything else, the
// wire included, is shared. True with *terminal filled when the host gave a terminal result -- or, for
// LockCred, the first Injected: typed, and now waiting (not terminal) for the session to unlock, which the
// caller follows with poll_status between other work. *requestIdOut: this exchange's requestId.
bool sealed_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password, SealAs as,
                     std::string* status, remote60::native_poc::ControlUnlockStatusResultMessage* terminal,
                     uint32_t* requestIdOut = nullptr) {
  using namespace remote60::native_poc;
  const std::string what = as == SealAs::Prelogin ? "login" : as == SealAs::LockCred ? "lock-screen login" : "unlock";
  auto set_status = [&](const std::string& s) { if (status) *status = s; };
  static uint32_t rid = 1;
  const uint32_t requestId = ++rid;
  if (requestIdOut) *requestIdOut = requestId;
  if (terminal) *terminal = ControlUnlockStatusResultMessage{};

  // 1) Challenge.
  ControlOutboundAction a{};
  a.kind = ControlOutboundActionKind::UnlockChallengeRequest;
  a.expectedResponseType = MessageType::ControlUnlockChallenge;
  a.expectedResponseSize = static_cast<uint16_t>(sizeof(ControlUnlockChallengeMessage));
  a.unlockChallengeReq.header.magic = kMagic;
  a.unlockChallengeReq.header.type = static_cast<uint16_t>(
      as == SealAs::LockCred ? MessageType::ControlLockCredChallengeRequest : MessageType::ControlUnlockChallengeRequest);
  a.unlockChallengeReq.header.size = static_cast<uint16_t>(sizeof(a.unlockChallengeReq));
  a.unlockChallengeReq.requestId = requestId;
  TcpControlResponse r{};
  if (!execute_control_action(link, a, &r) || r.kind != TcpControlResponseKind::UnlockChallenge) {
    set_status("no response from host");
    return false;
  }
  const ControlUnlockChallengeMessage ch = r.unlockChallenge;
  if (ch.requestId != requestId) { set_status("response id mismatch"); return false; }
  if (ch.status != static_cast<uint16_t>(UnlockStage::ChallengeIssued)) {
    set_status(std::string("challenge rejected: ") + stage_name(ch.status));
    if (terminal) {
      terminal->requestId = requestId;
      terminal->stage = ch.status;
      terminal->terminal = 1;
    }
    return terminal != nullptr;  // lockscreen-cred r1: the refusal is the answer, said to the user as such
  }
  // lockscreen-cred r1: a lock-screen challenge must say it was issued for that purpose -- the password is
  // never sealed for a challenge of another purpose.
  if (as == SealAs::LockCred && ch.reserved != kUnlockPurposeLockCred) {
    set_status("challenge purpose mismatch");
    return false;
  }

  // 2) Seal the password to the host's ephemeral key.
  if (password.size() > su::kMaxPasswordUtf16) {  // never silently truncate (Codex: truncation -> lockout)
    set_status("password too long");
    return false;
  }
  su::EcdhKeyPair key;
  uint8_t clientPub[su::kPubKeyBytes] = {};
  if (!key.Generate() || !key.ExportPublic(clientPub)) { set_status("crypto init failed"); return false; }
  auto c = ContextFromChallenge(ch, clientPub);
  const auto kdf = as == SealAs::LockCred   ? su::BuildLockCredKdfInfo(c)
                   : as == SealAs::Prelogin ? su::BuildPreloginCredKdfInfo(c)
                                            : su::BuildKdfInfo(c);
  const auto aad = as == SealAs::LockCred   ? su::BuildLockCredAad(c)
                   : as == SealAs::Prelogin ? su::BuildPreloginCredAad(c)
                                            : su::BuildAad(c);
  uint8_t aesKey[su::kAesKeyBytes] = {};
  bool ok = su::DeriveAesKey(key, ch.hostPub, ch.salt, kdf.data(), kdf.size(), aesKey);
  uint16_t pwUnits[su::kMaxPasswordUtf16] = {};
  const uint16_t pwCount = utf16_of(password, pwUnits);
  uint8_t plain[su::kPlaintextBytes] = {};
  ok = ok && su::PackPassword(pwUnits, pwCount, plain);
  su::SecureZero(pwUnits, sizeof(pwUnits));
  uint8_t nonce[su::kNonceBytes] = {};
  uint8_t cipher[su::kPlaintextBytes] = {};
  uint8_t tag[su::kTagBytes] = {};
  ok = ok && su::RandomBytes(nonce, su::kNonceBytes) &&
       su::AesGcmSeal(aesKey, nonce, aad.data(), aad.size(), plain, su::kPlaintextBytes, cipher, tag);
  su::SecureZero(aesKey, sizeof(aesKey));
  su::SecureZero(plain, sizeof(plain));
  if (!ok) { set_status("seal failed"); return false; }

  // 3) SealedRequest.
  ControlOutboundAction s{};
  s.kind = ControlOutboundActionKind::UnlockSealedRequest;
  s.expectedResponseType = MessageType::ControlUnlockAccepted;
  s.expectedResponseSize = static_cast<uint16_t>(sizeof(ControlUnlockAcceptedMessage));
  s.unlockSealed.header.magic = kMagic;
  s.unlockSealed.header.type = static_cast<uint16_t>(MessageType::ControlUnlockSealedRequest);
  s.unlockSealed.header.size = static_cast<uint16_t>(sizeof(s.unlockSealed));
  s.unlockSealed.requestId = requestId;
  std::memcpy(s.unlockSealed.challengeId, ch.challengeId, sizeof(s.unlockSealed.challengeId));
  std::memcpy(s.unlockSealed.clientPub, clientPub, sizeof(s.unlockSealed.clientPub));
  std::memcpy(s.unlockSealed.nonce, nonce, sizeof(s.unlockSealed.nonce));
  std::memcpy(s.unlockSealed.tag, tag, sizeof(s.unlockSealed.tag));
  std::memcpy(s.unlockSealed.cipher, cipher, sizeof(s.unlockSealed.cipher));
  su::SecureZero(cipher, sizeof(cipher));
  TcpControlResponse ra{};
  const bool acc = execute_control_action(link, s, &ra);
  su::SecureZero(&s.unlockSealed, sizeof(s.unlockSealed));
  if (!acc || ra.kind != TcpControlResponseKind::UnlockAccepted ||
      ra.unlockAccepted.requestId != requestId || ra.unlockAccepted.accepted != 1) {
    set_status("host did not accept");
    return false;
  }

  // 4) Poll status until terminal (lockscreen-cred: or until typed -- the unlock is followed afterwards).
  for (int i = 0; i < 40; ++i) {
    ControlUnlockStatusResultMessage res{};
    if (!poll_status(link, requestId, &res)) {
      set_status("status poll failed");
      return false;
    }
    if (res.requestId != requestId) continue;  // ignore a stale/foreign result
    const bool typedWaiting = as == SealAs::LockCred && !res.terminal &&
                              res.stage == static_cast<uint16_t>(UnlockStage::Injected);
    if (res.terminal || typedWaiting) {
      *terminal = res;
      set_status(what + ": " + stage_name(terminal->stage));
      return true;
    }
    Sleep(250);
  }
  set_status(what + " timed out");
  return false;
}
}  // namespace

bool run_unlock_exchange(remote60::native_poc::ControlLink& link, uint64_t /*hostId*/,
                         const std::wstring& password, std::string* status, bool* clearCredential) {
  using namespace remote60::native_poc;
  if (clearCredential) *clearCredential = false;
  ControlUnlockStatusResultMessage terminal{};
  if (!sealed_exchange(link, password, SealAs::Unlock, status, &terminal)) return false;
  const uint16_t stage = terminal.stage;
  // Wrong password / bad decrypt -> tell the caller to drop the stored credential so it is not
  // auto-reused (which would march toward a Windows account lockout). (Codex #370 review.)
  // lockscreen-cred r1 (Codex contract 2): and when the host refused it as policy (not a disconnected locked
  // session) -- a stored password is not kept to be replayed against a screen it does not unlock.
  if (clearCredential &&
      (static_cast<UnlockStage>(stage) == UnlockStage::AuthFailed ||
       static_cast<UnlockStage>(stage) == UnlockStage::DecryptFailed ||
       static_cast<UnlockStage>(stage) == UnlockStage::RejectedPolicy)) {
    *clearCredential = true;
  }
  return static_cast<UnlockStage>(stage) == UnlockStage::SessionUnlocked;
}

bool run_unlock_exchange_result(remote60::native_poc::ControlLink& link, const std::wstring& password, uint16_t* stage,
                                uint32_t* win32, bool* clearCredential, std::string* status) {
  using namespace remote60::native_poc;
  if (clearCredential) *clearCredential = false;
  ControlUnlockStatusResultMessage terminal{};
  const bool done = sealed_exchange(link, password, SealAs::Unlock, status, &terminal);
  if (stage) *stage = done ? terminal.stage : 0;
  if (win32) *win32 = done ? terminal.win32Error : 0;
  const auto s = static_cast<UnlockStage>(done ? terminal.stage : 0);
  if (clearCredential && done &&
      (s == UnlockStage::AuthFailed || s == UnlockStage::DecryptFailed || s == UnlockStage::RejectedPolicy)) {
    *clearCredential = true;
  }
  return done;
}

bool run_prelogin_cred_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password,
                                uint16_t* stage, uint32_t* win32, std::string* status) {
  remote60::native_poc::ControlUnlockStatusResultMessage terminal{};
  const bool done = sealed_exchange(link, password, SealAs::Prelogin, status, &terminal);
  if (stage) *stage = done ? terminal.stage : 0;
  if (win32) *win32 = done ? terminal.win32Error : 0;
  return done;
}

bool run_lock_cred_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password, uint16_t* stage,
                            uint32_t* win32, bool* waiting, uint32_t* requestId, std::string* status) {
  remote60::native_poc::ControlUnlockStatusResultMessage res{};
  uint32_t rid = 0;
  const bool done = sealed_exchange(link, password, SealAs::LockCred, status, &res, &rid);
  if (stage) *stage = done ? res.stage : 0;
  if (win32) *win32 = done ? res.win32Error : 0;
  if (waiting) *waiting = done && !res.terminal;
  if (requestId) *requestId = rid;
  return done;
}

bool poll_lock_cred_status(remote60::native_poc::ControlLink& link, uint32_t requestId, uint16_t* stage, bool* terminal,
                           uint32_t* win32) {
  remote60::native_poc::ControlUnlockStatusResultMessage res{};
  if (!poll_status(link, requestId, &res) || res.requestId != requestId) return false;
  if (stage) *stage = res.stage;
  if (terminal) *terminal = res.terminal != 0;
  if (win32) *win32 = res.win32Error;
  return true;
}

std::wstring windows_login_result_text(int role, uint16_t stage, uint32_t win32) {
  using remote60::native_poc::UnlockStage;
  if (stage == kLoginCancelled) return L"취소했습니다. 아무것도 보내지 않았습니다.";
  if (stage == kLoginPending) {
    return L"앞서 입력한 비밀번호의 잠금 해제를 기다리는 중이라 보내지 않았습니다.\n"
           L"원격 화면에서 Enter 를 눌렀는지 확인하세요. 결과가 나오면(최대 2분) 다시 누를 수 있고, 그때는 칸의 내용을 새로 바꿔 입력합니다.";
  }
  if (stage == kLoginUnsupported) {
    return L"이 원격 PC에는 지금 'Windows 로그인'을 쓸 수 없습니다.\n"
           L"원격 PC가 로그인 화면이나 잠금 화면이 아니거나, 원격 PC의 GNLink 가 이 기능보다 오래된 버전입니다.";
  }
  if (role == kLoginRolePrelogin) return prelogin_cred_result_text(stage, win32);
  if (role == kLoginRoleUnlock) {
    // The disconnected-session unlock (a stored password, Ctrl+Alt+U when neither screen is up).
    switch (static_cast<UnlockStage>(stage)) {
      case UnlockStage::SessionUnlocked:
        return L"원격 PC 세션의 잠금이 해제되었습니다.";
      case UnlockStage::WtsConnectAccepted:
        return L"원격 PC가 잠금 해제 요청을 받아들였지만 해제는 확인되지 않았습니다. 원격 화면을 확인하세요.";
      case UnlockStage::AuthFailed:
        return L"비밀번호가 맞지 않아 잠금을 해제하지 못했습니다. 저장된 비밀번호를 지웠습니다.\n"
               L"다시 시도하면 비밀번호를 새로 묻습니다.";
      case UnlockStage::RejectedPolicy:
        return L"원격 PC 세션이 지금 '연결이 끊긴 잠금' 상태가 아니어서 보내지 않았습니다. 저장된 비밀번호를 지웠습니다.\n"
               L"원격 PC가 잠금 화면이면 툴바의 'Windows 로그인'(Ctrl+Alt+U)이 나타납니다.";
      case UnlockStage::DecryptFailed:
        return L"비밀번호를 안전하게 전달하지 못했습니다. 저장된 비밀번호를 지웠습니다. 다시 시도하세요.";
      case UnlockStage::RejectedStaleTopology:
      case UnlockStage::Timeout:
        return L"요청이 만료되었거나 원격 PC 상태가 바뀌었습니다. 다시 시도하세요.";
      default:
        break;
    }
    if (stage == 0) return L"원격 PC가 응답하지 않았습니다. 연결을 확인한 뒤 다시 시도하세요.";
    return L"원격 PC가 잠금을 해제하지 못했습니다 (단계 " + std::to_wstring(stage) + L", 오류 " + std::to_wstring(win32) + L").";
  }
  // kLoginRoleLockScreen (lockscreen-cred r1).
  switch (static_cast<UnlockStage>(stage)) {
    case UnlockStage::Injected:
      return L"원격 PC 잠금 화면의 비밀번호 칸에 입력했습니다.\n"
             L"이 창을 닫고 뷰어에서 Enter 를 누르면 로그인합니다. (입력함 = 로그인 성공이 아닙니다)";
    case UnlockStage::SessionUnlocked:
      return L"원격 PC의 잠금이 해제되었습니다.";
    case UnlockStage::UnlockUnconfirmed:
      return L"비밀번호는 입력했지만 2분 안에 잠금 해제가 확인되지 않았습니다.\n"
             L"Enter 를 눌렀는지, 비밀번호가 맞는지 원격 화면에서 확인하세요.";
    case UnlockStage::TargetNotReady:
      return L"잠금 화면의 비밀번호 칸이 준비되지 않아 입력하지 않았습니다.\n"
             L"원격 화면을 한 번 클릭해 비밀번호 칸을 띄운 뒤 다시 누르세요.";
    case UnlockStage::InjectFailed:
      return L"비밀번호를 입력하지 못했습니다 (오류 " + std::to_wstring(win32) + L"). 다시 시도하세요.";
    case UnlockStage::RejectedPolicy:
      return L"원격 PC가 지금 잠금 화면이 아니어서 보내지 않았습니다.";
    case UnlockStage::DecryptFailed:
      return L"비밀번호를 안전하게 전달하지 못해 입력하지 않았습니다. 다시 시도하세요.";
    case UnlockStage::RejectedStaleTopology:
    case UnlockStage::Timeout:
      return L"요청이 만료되었거나 원격 PC 상태가 바뀌었습니다. 다시 시도하세요.";
    case UnlockStage::InternalError:
      return L"원격 PC가 입력하지 못했습니다 (오류 " + std::to_wstring(win32) + L").";
    default:
      break;
  }
  if (stage == kPreloginNotAtLogon) return L"원격 PC가 지금 잠금 화면 상태가 아니어서 보내지 않았습니다.";
  if (stage == 0) return L"원격 PC가 응답하지 않았습니다. 연결을 확인한 뒤 다시 시도하세요.";
  return L"원격 PC가 입력하지 못했습니다 (단계 " + std::to_wstring(stage) + L").";
}

std::wstring prelogin_cred_result_text(uint16_t stage, uint32_t win32) {
  using remote60::native_poc::UnlockStage;
  if (stage == kPreloginLeftLogon) {
    // lockscreen-cred r1 (Codex contract 5): the host leaving the pre-login screen after the typing is not by
    // itself a login this viewer saw -- said as that, not as success.
    return L"비밀번호 입력 뒤 원격 PC가 로그인 화면을 벗어났습니다.\n"
           L"로그인이 됐는지는 확인되지 않았습니다. 원격 화면에서 확인하세요.";
  }
  switch (static_cast<UnlockStage>(stage)) {
    case UnlockStage::Injected:
      return L"원격 PC 로그인 화면에 비밀번호를 입력했습니다.\n"
             L"이 창을 닫고 뷰어에서 Enter 를 누르면 로그인합니다. (입력함 = 로그인 성공이 아닙니다)";
    case UnlockStage::SessionUnlocked:
      // r5 G11: what was observed, and no more -- the host left the logon screen after the typing. Which
      // account, and whether its desktop is up, is not something this viewer has seen.
      return L"원격 PC가 로그인 화면을 벗어났습니다 (비밀번호 입력 뒤).\n"
             L"로그인된 계정과 화면은 원격 화면에서 확인하세요.";
    case UnlockStage::RejectedPolicy:
      return L"원격 PC의 입력 대상이 로그인 화면이 아니어서 입력하지 않았습니다.\n"
             L"원격 화면에서 비밀번호 칸을 띄운 뒤 다시 시도하세요.";
    case UnlockStage::DecryptFailed:
      return L"비밀번호를 안전하게 전달하지 못해 입력하지 않았습니다. 다시 시도하세요.";
    case UnlockStage::RejectedStaleTopology:
    case UnlockStage::Timeout:
      return L"요청이 만료되었거나 원격 PC 상태가 바뀌었습니다. 다시 시도하세요.";
    case UnlockStage::InternalError:
      return L"원격 PC가 입력하지 못했습니다 (오류 " + std::to_wstring(win32) + L").";
    default:
      break;
  }
  if (stage == kPreloginNotAtLogon) return L"원격 PC가 지금 로그인 화면 상태가 아니어서 보내지 않았습니다.";
  if (stage == 0) return L"원격 PC가 응답하지 않았습니다. 연결을 확인한 뒤 다시 시도하세요.";
  return L"원격 PC가 입력하지 못했습니다 (단계 " + std::to_wstring(stage) + L").";
}

}  // namespace remote60::native_poc::viewer
