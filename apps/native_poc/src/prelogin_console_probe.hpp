#pragma once

// Prelogin console observation layer (prelogin-service A1, contract section 1 / Q4 token matrix).
//
// The supervisor state machine (prelogin_supervisor_state.hpp) decides what to do from an already-
// resolved {UserTokenObservation, UserPrivilege}. THIS unit resolves those from the live Windows
// token/WTS facts -- and, critically, encodes the r3 corrections at the resolution boundary:
//   - A WTSQueryUserToken failure is mapped to NoInteractiveUser ONLY for the authoritative "no token"
//     code. Access/privilege failures (e.g. a caller without SE_TCB_NAME, or a session still coming up)
//     map to QueryTransientError -- NEVER to "no user => prelogin".
//   - TokenElevationTypeDefault is NOT "standard user": the effective admin membership is checked on the
//     correct token (the Full / linked-Full / Default token) with deny-only honoured, and only a
//     genuinely non-privileged account becomes Unsupported.
//
// The pure classifiers are exhaustively unit-testable off a real machine; the live gatherers are thin
// Win32 wrappers (definitions in the .cpp). The "no user" error code is a hypothesis pinned here and
// CONFIRMED under SYSTEM in A0/G2 -- if the measured code differs, only kErrNoToken changes.

#include <cstdint>

#include "prelogin_supervisor_state.hpp"  // UserTokenObservation, UserPrivilege

namespace remote60::native_poc::prelogin {

// Windows TOKEN_ELEVATION_TYPE mirrors (keep the pure classifier free of windows.h).
constexpr int kElevationTypeDefault = 1;  // TokenElevationTypeDefault: no linked token
constexpr int kElevationTypeFull = 2;     // TokenElevationTypeFull
constexpr int kElevationTypeLimited = 3;  // TokenElevationTypeLimited: a linked Full token exists

// Windows error-code mirrors.
constexpr uint32_t kErrNoToken = 1008;           // ERROR_NO_TOKEN (authoritative: no interactive user)
constexpr uint32_t kErrAccessDenied = 5;         // ERROR_ACCESS_DENIED  -> transient
constexpr uint32_t kErrPrivilegeNotHeld = 1314;  // ERROR_PRIVILEGE_NOT_HELD (no SE_TCB) -> transient

struct PrivilegeFacts {
  bool queryFailed = true;      // a required token query failed -> transient, never a guess
  int elevationType = 0;        // TOKEN_ELEVATION_TYPE (1/2/3)
  bool hasLinkedFull = false;   // a linked Full token was obtained (meaningful for Limited)
  bool adminEffective = false;  // Administrators present AND enabled (not deny-only) on the EFFECTIVE token
};

// Pure: resolve effective privilege. `adminEffective` is evaluated by the live layer on the correct
// token (Full / linked-Full / Default as appropriate), so this stays uniform.
inline UserPrivilege ClassifyPrivilege(const PrivilegeFacts& f) {
  if (f.queryFailed) return UserPrivilege::PrivilegeQueryError;
  if (!f.adminEffective) return UserPrivilege::Unsupported;  // standard user, or admin deny-only w/o elevation
  switch (f.elevationType) {
    case kElevationTypeFull:
      return UserPrivilege::AdminFull;
    case kElevationTypeLimited:
      // Limited MUST have a linked Full token; if we could not obtain it, that is a query problem, not
      // a decision -- never silently downgrade to "unsupported".
      return f.hasLinkedFull ? UserPrivilege::AdminLimitedLinked : UserPrivilege::PrivilegeQueryError;
    case kElevationTypeDefault:
      return UserPrivilege::AdminDefaultEffective;
    default:
      return UserPrivilege::PrivilegeQueryError;
  }
}

// Pure: map WTSQueryUserToken's outcome into an authoritative observation. ONLY the "no token" code is
// authoritatively no-user; everything else that is a failure is transient (security-critical invariant).
inline UserTokenObservation ClassifyTokenObservation(bool queryUserTokenSucceeded, uint32_t win32Error) {
  if (queryUserTokenSucceeded) return UserTokenObservation::HasUser;
  if (win32Error == kErrNoToken) return UserTokenObservation::NoInteractiveUser;
  return UserTokenObservation::QueryTransientError;  // access denied / privilege not held / anything else
}

// --- live (Windows; definitions in prelogin_console_probe.cpp) --------------------------------------
// Gather privilege facts from an already-open token handle (primary or impersonation). Does NOT close
// the handle. On any required-query failure, returns {queryFailed=true}.
PrivilegeFacts GatherPrivilegeFactsFromToken(void* tokenHandle);
// Open the current process token and gather its facts (self-check / diagnostic).
PrivilegeFacts GatherCurrentProcessPrivilegeFacts();

}  // namespace remote60::native_poc::prelogin
