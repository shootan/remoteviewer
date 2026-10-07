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
// The exchange under the prelogin purpose. True when the host answered with a terminal stage
// (*stage, *win32); false with *stage 0 when it did not.
bool run_prelogin_cred_exchange(remote60::native_poc::ControlLink& link, const std::wstring& password,
                                uint16_t* stage, uint32_t* win32, std::string* status);
// What the result window says for a stage (kPreloginNotAtLogon and 0 included).
std::wstring prelogin_cred_result_text(uint16_t stage, uint32_t win32);
// The result window (modal, owned by `owner`): the text and a 확인 button. Returns when it is closed.
void show_prelogin_cred_result(HWND owner, const std::wstring& text);

}  // namespace remote60::native_poc::viewer
