#pragma once

// The primary token a SYSTEM child of the secure-input service runs with in another session (lk-abcd r2, Codex
// R2). Before, the three steps were one && chain: a failure returned nothing and said nothing -- the worker
// "FAILED" with no API and no Win32 code. Now the failing step and its GetLastError (taken at once, before any
// cleanup) come back, for the service's diag.
//
// The steps are the service's own (launch_system_child_in_session): OpenProcessToken on this process,
// DuplicateTokenEx to a primary token, SetTokenInformation(TokenSessionId). Run as anyone but SYSTEM (TCB), the
// last fails -- the safe fault the unit test uses; nothing is started either way.

#include <windows.h>

namespace remote60::native_poc::prelogin {

enum class ChildTokenStage { Ok = 0, OpenProcessToken = 1, DuplicateTokenEx = 2, SetTokenSessionId = 3 };

struct ChildTokenResult {
  HANDLE token = nullptr;  // the primary token (caller owns) when stage == Ok
  ChildTokenStage stage = ChildTokenStage::Ok;
  DWORD win32 = 0;  // GetLastError of the failing step
};

const char* ChildTokenStageName(ChildTokenStage stage);

// This process's token, duplicated as a primary token for `sessionId`.
ChildTokenResult PrepareChildTokenForSession(DWORD sessionId);

}  // namespace remote60::native_poc::prelogin
