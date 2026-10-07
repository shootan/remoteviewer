#pragma once

// Prelogin handoff readiness signal (prelogin-service r4 item 7). The Session-0 service must transfer
// the single owner lease to the user Host only once that Host is actually up -- registered with the
// directory and serving -- not merely because its process exists. The user Host sets a named, manual-
// reset event when it reaches that point; the service resets it just before it launches a fresh user
// Host and waits for it before completing the handoff. The event is in the Global\ namespace so it
// crosses the session boundary (service in session 0, Host in the console session); both the elevated
// user Host and the SYSTEM service can create/open it.
//
// Win32 lives in prelogin_ready_signal.cpp.

namespace remote60::native_poc::prelogin {

// Host side: create (if needed) and set the ready event. Idempotent; returns false on a Win32 failure.
bool SignalUserHostReady();

// Service side: true iff the ready event currently exists and is signaled.
bool UserHostReadySignaled();

// Service side: clear the ready event before launching a fresh user Host, so a previous session's
// signal cannot be mistaken for the new Host's readiness.
void ResetUserHostReady();

}  // namespace remote60::native_poc::prelogin
