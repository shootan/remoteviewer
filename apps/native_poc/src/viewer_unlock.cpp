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

void show_prelogin_cred_result(HWND owner, const std::wstring& text) {
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
  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, kPreloginResultClass, kPreloginResultTitle,
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
// The one challenge -> seal -> poll exchange. `prelogin` picks the purpose the password is sealed
// under (sealed_unlock BuildPreloginCred* vs BuildKdfInfo/BuildAad); everything else, the wire
// included, is shared. True with *terminal filled when the host gave a terminal result.
bool sealed_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password, bool prelogin,
                     std::string* status, remote60::native_poc::ControlUnlockStatusResultMessage* terminal) {
  using namespace remote60::native_poc;
  const std::string what = prelogin ? "login" : "unlock";
  auto set_status = [&](const std::string& s) { if (status) *status = s; };
  static uint32_t rid = 1;
  const uint32_t requestId = ++rid;

  // 1) Challenge.
  ControlOutboundAction a{};
  a.kind = ControlOutboundActionKind::UnlockChallengeRequest;
  a.expectedResponseType = MessageType::ControlUnlockChallenge;
  a.expectedResponseSize = static_cast<uint16_t>(sizeof(ControlUnlockChallengeMessage));
  a.unlockChallengeReq.header.magic = kMagic;
  a.unlockChallengeReq.header.type = static_cast<uint16_t>(MessageType::ControlUnlockChallengeRequest);
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
  const auto kdf = prelogin ? su::BuildPreloginCredKdfInfo(c) : su::BuildKdfInfo(c);
  const auto aad = prelogin ? su::BuildPreloginCredAad(c) : su::BuildAad(c);
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

  // 4) Poll status until terminal.
  for (int i = 0; i < 40; ++i) {
    ControlOutboundAction st{};
    st.kind = ControlOutboundActionKind::UnlockStatusRequest;
    st.expectedResponseType = MessageType::ControlUnlockStatusResult;
    st.expectedResponseSize = static_cast<uint16_t>(sizeof(ControlUnlockStatusResultMessage));
    st.unlockStatusReq.header.magic = kMagic;
    st.unlockStatusReq.header.type = static_cast<uint16_t>(MessageType::ControlUnlockStatusRequest);
    st.unlockStatusReq.header.size = static_cast<uint16_t>(sizeof(st.unlockStatusReq));
    st.unlockStatusReq.requestId = requestId;
    TcpControlResponse rs{};
    if (!execute_control_action(link, st, &rs) ||
        rs.kind != TcpControlResponseKind::UnlockStatusResult) {
      set_status("status poll failed");
      return false;
    }
    if (rs.unlockStatusResult.requestId != requestId) continue;  // ignore a stale/foreign result
    if (rs.unlockStatusResult.terminal) {
      *terminal = rs.unlockStatusResult;
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
  if (!sealed_exchange(link, password, false, status, &terminal)) return false;
  const uint16_t stage = terminal.stage;
  // Wrong password / bad decrypt -> tell the caller to drop the stored credential so it is not
  // auto-reused (which would march toward a Windows account lockout). (Codex #370 review.)
  if (clearCredential &&
      (static_cast<UnlockStage>(stage) == UnlockStage::AuthFailed ||
       static_cast<UnlockStage>(stage) == UnlockStage::DecryptFailed)) {
    *clearCredential = true;
  }
  return static_cast<UnlockStage>(stage) == UnlockStage::SessionUnlocked;
}

bool run_prelogin_cred_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password,
                                uint16_t* stage, uint32_t* win32, std::string* status) {
  remote60::native_poc::ControlUnlockStatusResultMessage terminal{};
  const bool done = sealed_exchange(link, password, true, status, &terminal);
  if (stage) *stage = done ? terminal.stage : 0;
  if (win32) *win32 = done ? terminal.win32Error : 0;
  return done;
}

std::wstring prelogin_cred_result_text(uint16_t stage, uint32_t win32) {
  using remote60::native_poc::UnlockStage;
  switch (static_cast<UnlockStage>(stage)) {
    case UnlockStage::Injected:
      return L"원격 PC 로그인 화면에 비밀번호를 입력했습니다.\n"
             L"로그인되었는지는 원격 화면에서 확인하세요. (입력함 = 로그인 성공이 아닙니다)";
    case UnlockStage::SessionUnlocked:
      return L"원격 PC에 로그인되었습니다.";
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
