// See prelogin_child_token.hpp (lk-abcd r2, Codex R2).

#include "prelogin_child_token.hpp"

namespace remote60::native_poc::prelogin {

const char* ChildTokenStageName(ChildTokenStage stage) {
  switch (stage) {
    case ChildTokenStage::Ok:
      return "ok";
    case ChildTokenStage::OpenProcessToken:
      return "OpenProcessToken";
    case ChildTokenStage::DuplicateTokenEx:
      return "DuplicateTokenEx";
    case ChildTokenStage::SetTokenSessionId:
      return "SetTokenInformation(TokenSessionId)";
  }
  return "?";
}

ChildTokenResult PrepareChildTokenForSession(DWORD sessionId) {
  ChildTokenResult r;
  HANDLE serviceToken = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY | TOKEN_ADJUST_SESSIONID,
                        &serviceToken)) {
    r.win32 = GetLastError();
    r.stage = ChildTokenStage::OpenProcessToken;
    return r;
  }
  HANDLE childToken = nullptr;
  if (!DuplicateTokenEx(serviceToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &childToken)) {
    r.win32 = GetLastError();
    r.stage = ChildTokenStage::DuplicateTokenEx;
    CloseHandle(serviceToken);
    return r;
  }
  CloseHandle(serviceToken);
  if (!SetTokenInformation(childToken, TokenSessionId, &sessionId, sizeof(sessionId))) {
    r.win32 = GetLastError();
    r.stage = ChildTokenStage::SetTokenSessionId;
    CloseHandle(childToken);
    return r;
  }
  r.token = childToken;
  return r;
}

}  // namespace remote60::native_poc::prelogin
