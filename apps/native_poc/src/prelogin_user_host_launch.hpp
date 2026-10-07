#pragma once

// How the service starts the logged-on user's GNLinkHost at the pre-login handoff (prelogin-ui r7, H1).
//
// The call that did it passed the environment block in CreateProcessAsUserW's lpCurrentDirectory slot
// (and nullptr as lpEnvironment): the user Host got no environment of its own, no launch nonce -- and
// with a block of NAME=value text as its working directory, normally no process at all. The one call is
// now here, with each argument in its slot, so a test can make it exactly as the service does (with
// its own token) and look at what the child received.
//
// Win32; the service (secure_input_service_main.cpp launch_user_host) and the test call these.

#include <windows.h>

#include <string>
#include <vector>

namespace remote60::native_poc::prelogin {

// The user Host's environment: the user's own block (CreateEnvironmentBlock for `token`) with
// REMOTE60_PRELOGIN_READY_NONCE=`nonce` stated (any earlier one dropped). False -- and nothing in *out
// -- when the user's block cannot be made: a Host is never started with a half environment.
bool BuildUserHostEnvironment(HANDLE token, const std::wstring& nonce, std::vector<wchar_t>* out, DWORD* win32);

// Starts `exe --tray` for `token`, SUSPENDED, on `desktop` (winsta0\Default for the service), with
// `environment` as its environment and the exe's own folder as its working directory. True with *pi
// filled; the caller binds it to its Job and resumes it (or terminates it).
bool StartUserHostSuspended(HANDLE token, const std::wstring& exe, const std::vector<wchar_t>& environment,
                            const wchar_t* desktop, PROCESS_INFORMATION* pi, DWORD* win32);

}  // namespace remote60::native_poc::prelogin
