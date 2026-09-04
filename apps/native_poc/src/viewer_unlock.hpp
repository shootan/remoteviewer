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
                         const std::wstring& password, std::string* status);

}  // namespace remote60::native_poc::viewer
