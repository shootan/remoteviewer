#pragma once

// Viewer (client) side of sealed unlock: stores the host's lock-screen password locally (DPAPI, this
// user only), prompts for it once, and runs the challenge -> seal -> poll exchange when the user asks
// to unlock. The password is sealed to the host's per-challenge ephemeral key (sealed_unlock), so it
// is never on the wire in the clear -- but this is an UNAUTHENTICATED sealed channel (no host
// identity), a tradeoff the user accepted. The plaintext lives only briefly here and is zeroed.
//
// Trigger + status are viewer UI concerns; the exchange itself is synchronous over the control link
// (a rare, user-initiated action, so blocking the control loop for a few seconds is acceptable).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <string>

#include "native_video_client_tcp_control.hpp"

namespace remote60::native_poc::viewer {

struct ViewerState;

// DPAPI-backed credential, keyed per host id. Blob stored under %LOCALAPPDATA%\GNLink with a
// user-only file. Returns false on failure; never logs the password.
bool save_unlock_password(uint64_t hostId, const std::wstring& password);
bool load_unlock_password(uint64_t hostId, std::wstring* out);   // out zeroed by caller after use
bool has_unlock_password(uint64_t hostId);
void clear_unlock_password(uint64_t hostId);

// Modal password prompt (ES_PASSWORD). Returns false if cancelled/closed. `out` should be zeroed by
// the caller after use.
bool prompt_unlock_password(HWND owner, std::wstring* out);

// Runs one full unlock attempt over `link` for the given host id, using the stored (or just-entered)
// password. Drives ChallengeRequest -> (seal) -> SealedRequest -> StatusRequest poll. Writes a short
// human-readable outcome to `status`. Returns true only on an authoritative unlock success.
bool run_unlock_exchange(remote60::native_poc::ControlLink& link, uint64_t hostId,
                         const std::wstring& password, std::string* status, bool* clearCredential);

// --- "Windows 로그인" (prelogin-ui r3, item 5) ---------------------------------------------------
// Offered only while the host says it is at the pre-login screen (Pong kCaptureFlagPreloginV1,
// reboot_menu_policy DecidePreloginCred). The password is asked for each time, never stored, sealed
// under the PRELOGIN purpose (a host's lock-screen unlock cannot open it, nor this worker an unlock
// blob), typed by the host's pre-login worker into the logon UI, and zeroed. Injected (8) means
// "typed", not "logged in"; the result window says which.
constexpr wchar_t kPreloginPromptClass[] = L"GNLinkPreloginLogin";
constexpr wchar_t kPreloginPromptTitle[] = L"GNLink — Windows 로그인";
constexpr wchar_t kPreloginResultTitle[] = L"GNLink — Windows 로그인 결과";
constexpr wchar_t kPreloginResultClass[] = L"GNLinkPreloginResult";
constexpr int kPreloginResultTextId = 10;  // the STATIC that says what happened; the 확인 button is id 1
constexpr uint16_t kPreloginNotAtLogon = 0xFFFF;  // not sent: the host was no longer at the logon screen

// Modal prompt (same window as the unlock prompt, its own class and words). Nothing is saved.
bool prompt_prelogin_password(HWND owner, std::wstring* out);
// lockscreen-cred r1: the same prompt, worded for the lock screen of a logged-on host. Nothing is saved.
bool prompt_lock_screen_password(HWND owner, std::wstring* out);
// The exchange under the prelogin purpose. True when the host answered with a terminal stage
// (*stage, *win32); false with *stage 0 when it did not.
bool run_prelogin_cred_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password,
                                uint16_t* stage, uint32_t* win32, std::string* status);
// What the result window says for a stage (kPreloginNotAtLogon and 0 included).
std::wstring prelogin_cred_result_text(uint16_t stage, uint32_t win32);

// --- lockscreen-cred r1: one "Windows 로그인" for the pre-login screen AND the lock screen -------------
// Which screen a request was made for (and so which purpose it is sealed under). kLoginRoleUnlock is the
// older disconnected-session unlock (Ctrl+Alt+U when neither screen is up), which keeps its stored password.
constexpr int kLoginRoleNone = 0;
constexpr int kLoginRolePrelogin = 1;
constexpr int kLoginRoleLockScreen = 2;
constexpr int kLoginRoleUnlock = 3;
// Local outcomes said in the same result window (never on the wire).
constexpr uint16_t kLoginCancelled = 0xFFFD;     // the user closed the prompt: nothing sent
constexpr uint16_t kLoginUnsupported = 0xFFFC;   // no screen to log in to here, or the host predates it
constexpr uint16_t kPreloginLeftLogon = 0xFFFE;  // typed, then the host left the pre-login screen (not a login seen)
constexpr uint16_t kLoginPending = 0xFFFB;       // lockscreen-cred: the previous password still waits for its unlock
// The result window's message carries the stage in the low 16 bits of wParam and the role above them.
// lockscreen-cred r2 (Codex L5): and the control connection's generation (controlRunGen) it belongs to in the
// high 32 bits -- a result of an earlier connection, still queued when the connection changed, is not shown.
static_assert(sizeof(WPARAM) == 8, "the result window's message carries the generation in the high half");
inline WPARAM make_login_result_wparam(int role, uint16_t stage, uint64_t controlRunGen) {
  return static_cast<WPARAM>(stage) | (static_cast<WPARAM>(role & 0xFF) << 16) |
         (static_cast<WPARAM>(controlRunGen & 0xFFFFFFFFull) << 32);
}
// What the result window says, by role and stage (the local outcomes above included).
std::wstring windows_login_result_text(int role, uint16_t stage, uint32_t win32);
// The lock-screen exchange (88 -> 41 with the LockCred purpose -> 42 -> 43 -> 44/45). True when the host
// answered: *stage terminal, or Injected with *waiting -- typed, now waiting for the unlock, which the
// caller follows with poll_lock_cred_status (one 44 each, between other work). *requestId is the job's.
bool run_lock_cred_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password, uint16_t* stage,
                            uint32_t* win32, bool* waiting, uint32_t* requestId, std::string* status);
bool poll_lock_cred_status(remote60::native_poc::ControlLink& link, uint32_t requestId, uint16_t* stage, bool* terminal,
                           uint32_t* win32);
// The disconnected-session unlock with its outcome for the result window (Codex contract 2: the user sees
// it; *clearCredential when the stored password must go -- wrong, undecryptable, or refused as policy).
bool run_unlock_exchange_result(remote60::native_poc::ControlLink& link, const std::wstring& password, uint16_t* stage,
                                uint32_t* win32, bool* clearCredential, std::string* status);
// The result window (modal, owned by `owner`): the text and a 확인 button. Returns when it is closed.
void show_prelogin_cred_result(HWND owner, const std::wstring& text);
// The same window with another title: the "typing held back at the logon screen" notice (r5 G8) and the
// remote restart's answer (r5 G11). Its class and control ids are the result window's.
void show_prelogin_notice(HWND owner, const wchar_t* title, const std::wstring& text);
constexpr wchar_t kRebootResultTitle[] = L"GNLink — 원격 PC 다시 시작";
constexpr uint8_t kRebootNoAnswer = 0xFF;  // the request went, no valid answer came back
constexpr uint8_t kRebootNotSent = 0xFE;   // not sent: the confirm was for another connection / no capability
// What the restart window says for a RebootResult (0 Accepted, 1 Refused, 2 NotAuthorized, 3 Failed) or
// kRebootNoAnswer.
std::wstring reboot_result_text(uint8_t result, uint32_t win32);
// r9 R5: what "Ctrl+Alt+Del" came to (PreloginSasResult 0..4, or the two local outcomes below).
constexpr wchar_t kSasResultTitle[] = L"GNLink — Ctrl+Alt+Del";
constexpr uint8_t kSasNoAnswer = 0xFF;
constexpr uint8_t kSasNotSent = 0xFE;
std::wstring sas_result_text(uint8_t result, uint32_t win32, uint8_t flags = 0);

}  // namespace remote60::native_poc::viewer
