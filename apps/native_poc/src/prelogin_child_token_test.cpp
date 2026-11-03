// lk-abcd r2 (Codex R2): the service's worker-token steps (prelogin_child_token.cpp, linked as the service links it)
// report the step that failed and its Win32 code. The safe fault: this test is not SYSTEM, so moving a token into
// another session (SetTokenInformation(TokenSessionId), which needs TCB) is refused -- ERROR_PRIVILEGE_NOT_HELD.
// Nothing is created or started. That the stage is the THIRD one shows the first two (open, duplicate) passed.
// Tags: session-privilege (asserts the non-TCB answer; an elevated SYSTEM run would pass the last step instead).

#include <windows.h>

#include <cstdio>
#include <string>

#include "prelogin_child_token.hpp"

namespace pl = remote60::native_poc::prelogin;

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}
bool has_tcb() {
  HANDLE t = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return false;
  LUID tcb{};
  PRIVILEGE_SET ps{};
  BOOL held = FALSE;
  const bool ok = LookupPrivilegeValueW(nullptr, L"SeTcbPrivilege", &tcb);
  ps.PrivilegeCount = 1;
  ps.Privilege[0].Luid = tcb;
  ps.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;
  const bool checked = ok && PrivilegeCheck(t, &ps, &held);
  CloseHandle(t);
  return checked && held;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the worker token steps say which one failed (lk-abcd r2, Codex R2)\n\n");
  check("this run holds no TCB (the fault below is the real one)", !has_tcb());
  DWORD mine = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &mine);
  const DWORD other = mine + 7;  // a session this process is not in
  const pl::ChildTokenResult r = pl::PrepareChildTokenForSession(other);
  check("moving the token to another session fails AT SetTokenInformation(TokenSessionId)",
        !r.token && r.stage == pl::ChildTokenStage::SetTokenSessionId, pl::ChildTokenStageName(r.stage));
  check("...with its Win32 code taken at once: ERROR_PRIVILEGE_NOT_HELD (1314)", r.win32 == ERROR_PRIVILEGE_NOT_HELD,
        std::to_string(r.win32));
  check("the stage name the diag line prints", std::string(pl::ChildTokenStageName(r.stage)) == "SetTokenInformation(TokenSessionId)");
  check("names for every stage", std::string(pl::ChildTokenStageName(pl::ChildTokenStage::OpenProcessToken)) == "OpenProcessToken" &&
                                     std::string(pl::ChildTokenStageName(pl::ChildTokenStage::DuplicateTokenEx)) == "DuplicateTokenEx" &&
                                     std::string(pl::ChildTokenStageName(pl::ChildTokenStage::Ok)) == "ok");
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
