// Live Win32 gatherers for the prelogin console observation layer (prelogin_console_probe.hpp).

#include "prelogin_console_probe.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace remote60::native_poc::prelogin {

namespace {

// Is Administrators present AND enabled (NOT deny-only) on `token`? CheckTokenMembership honours
// SE_GROUP_USE_FOR_DENY_ONLY (a deny-only admin SID reports NOT a member), which is exactly the
// "effective" test we want. CheckTokenMembership needs an impersonation token, so a primary token is
// duplicated to impersonation first.
bool AdminEffectiveOnToken(HANDLE token) {
  if (!token) return false;
  BYTE sidBuf[SECURITY_MAX_SID_SIZE];
  DWORD sidCb = sizeof(sidBuf);
  if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sidBuf, &sidCb)) return false;

  HANDLE imp = nullptr;
  if (!DuplicateToken(token, SecurityIdentification, &imp)) {
    // Maybe `token` is already an impersonation token usable directly.
    BOOL member = FALSE;
    if (CheckTokenMembership(token, sidBuf, &member)) return member == TRUE;
    return false;
  }
  BOOL member = FALSE;
  const BOOL ok = CheckTokenMembership(imp, sidBuf, &member);
  CloseHandle(imp);
  return ok && member == TRUE;
}

}  // namespace

PrivilegeFacts GatherPrivilegeFactsFromToken(void* tokenHandle) {
  PrivilegeFacts f;
  HANDLE token = reinterpret_cast<HANDLE>(tokenHandle);
  if (!token) return f;  // queryFailed stays true

  TOKEN_ELEVATION_TYPE et = TokenElevationTypeDefault;
  DWORD ret = 0;
  if (!GetTokenInformation(token, TokenElevationType, &et, sizeof(et), &ret)) {
    return f;  // queryFailed
  }
  f.elevationType = static_cast<int>(et);

  if (et == TokenElevationTypeLimited) {
    // The effective privilege lives on the linked Full token; obtain it and judge admin there.
    TOKEN_LINKED_TOKEN linked = {};
    if (!GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &ret) || !linked.LinkedToken) {
      return f;  // Limited without an obtainable linked token -> queryFailed (transient), not a guess
    }
    f.hasLinkedFull = true;
    f.adminEffective = AdminEffectiveOnToken(linked.LinkedToken);
    CloseHandle(linked.LinkedToken);
  } else {
    // Full or Default: judge admin on the token itself.
    f.adminEffective = AdminEffectiveOnToken(token);
  }

  f.queryFailed = false;
  return f;
}

PrivilegeFacts GatherCurrentProcessPrivilegeFacts() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token)) {
    return PrivilegeFacts{};  // queryFailed
  }
  PrivilegeFacts f = GatherPrivilegeFactsFromToken(token);
  CloseHandle(token);
  return f;
}

}  // namespace remote60::native_poc::prelogin
