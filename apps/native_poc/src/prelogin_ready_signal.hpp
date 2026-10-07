#pragma once

// Prelogin handoff readiness signal (prelogin-service r4 item 7). The Session-0 service must transfer
// the single owner lease to the user Host only once that Host is actually up -- registered with the
// directory and serving -- not merely because its process exists. The user Host sets a named, manual-
// reset event when it reaches that point; the service resets it just before it launches a fresh user
// Host and waits for it before completing the handoff. The event is in the Global\ namespace so it
// crosses the session boundary (service in session 0, Host in the console session); both the elevated
// user Host and the SYSTEM service can create/open it.
//
// r6 G9: the readiness object name is now bound to a PER-LAUNCH nonce the service hands the Host it just
// started (env REMOTE60_PRELOGIN_READY_NONCE). A fixed name let ANY GNLinkStream/Host of the install
// satisfy the handoff; with a nonce only the host THIS launch started can signal the event the service
// waits on. An empty nonce keeps the legacy base name (standalone/self-test).
//
// Win32 lives in prelogin_ready_signal.cpp.

#include <string>

namespace remote60::native_poc::prelogin {

// Host side: create (if needed) and set the ready event. The nonce is read from the environment variable
// REMOTE60_PRELOGIN_READY_NONCE that the launching service set. Idempotent; returns false on Win32 failure.
bool SignalUserHostReady();

// Service side: true iff the ready event for `nonce` currently exists and is signaled. Empty nonce = the
// legacy base name.
bool UserHostReadySignaled(const std::wstring& nonce = std::wstring());

// Service side: clear the ready event for `nonce` before launching a fresh user Host, so a previous
// session's (or a foreign host's) signal cannot be mistaken for the new Host's readiness.
void ResetUserHostReady(const std::wstring& nonce = std::wstring());

// Host-side test hook: read the nonce the launching service injected (empty if none). Exposed so the
// unit test can exercise the env-driven name selection without a second process.
std::wstring ReadReadyNonceFromEnv();

}  // namespace remote60::native_poc::prelogin
