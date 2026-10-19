#pragma once

// Prelogin supervisor state machine (prelogin-service A1, contract section 1).
//
// This is the PURE decision core of the Session-0 supervisor that decides what the service should be
// doing for the physical console session: run a SYSTEM prelogin worker on the secure desktop, hand
// off to the logged-on user's Host, hold, or surface an unsupported account. It takes already-gathered
// observations (the Win32 queries live in the service layer) and returns a state + an action, so it is
// exhaustively unit-testable off a real machine.
//
// It deliberately encodes the corrections from the Codex r3 review:
//   - A WTSQueryUserToken failure is NOT, by itself, "no user => Prelogin". Only an AUTHORITATIVE
//     "the console session has no interactive user" maps to Prelogin. An ambiguous/permission/not-ready
//     query is a bounded Transition retry that must NOT tear down a healthy child.
//   - TokenElevationTypeDefault does NOT mean "standard user". The Win32 layer resolves the real
//     effective privilege; Default-with-effective-admin is supported, a genuinely unprivileged account
//     is UnsupportedUser. This core never infers "standard" from the elevation type alone.
//   - WTS_SESSION_LOGON does not instantly kill prelogin: a supported login goes to UserStarting and
//     only reaches UserActive once the user Host has reported ready and owns the single lease.
//
// No Windows headers here on purpose -- keep it portable and pure.

#include <cstdint>

namespace remote60::native_poc::prelogin {

// The supervisor's view of the console session.
enum class SupervisorState {
  Disabled,         // feature off -> no prelogin remote access at all
  Provisioning,     // feature on but no valid machine credential yet -> no screen/input offered
  NoConsole,        // no active console session (WTSGetActiveConsoleSessionId == 0xFFFFFFFF) -> hold
  Prelogin,         // console present, authoritatively no interactive user -> SYSTEM secure-desktop worker
  UserStarting,     // a supported user logged on; bringing up the user Host (not yet ready)
  UserActive,       // the user Host is ready and owns the single lease
  UnsupportedUser,  // the logged-on account is not an effectively-privileged (admin v1) account
  Transition,       // ambiguous / in-flight observation -> bounded retry, keep any healthy child
  Fault,            // exceeded the retry budget without resolving
};

// What the console-session user-token observation authoritatively resolved to. The service layer maps
// raw WTSQueryUserToken / GetLastError into exactly one of these -- the ambiguity lives there, not here.
enum class UserTokenObservation {
  NoInteractiveUser,    // AUTHORITATIVE: the console session has no logged-on interactive user
  HasUser,              // a user is logged on; see UserPrivilege
  QueryTransientError,  // query failed for an ambiguous/transient/permission reason -> retry, NOT prelogin
};

// Effective privilege of the logged-on console user, resolved by the service layer (token type + SID +
// linked token + admin-group effective state), per the r3 "real-privilege decision".
enum class UserPrivilege {
  AdminFull,              // elevation type Full, admin effective
  AdminLimitedLinked,     // Limited with a verified linked Full token, admin effective
  AdminDefaultEffective,  // Default (no linked token) but real admin privilege is effective
  Unsupported,            // standard user, or admin group deny-only -> not supported in v1
  PrivilegeQueryError,    // could not determine privilege -> treat as transient, do not guess
};

struct SupervisorInputs {
  bool featureEnabled = false;        // machine record: prelogin remote access explicitly enabled
  bool machineCredentialValid = false;// a usable machine credential is provisioned
  bool consoleSessionValid = false;   // WTSGetActiveConsoleSessionId() != 0xFFFFFFFF
  UserTokenObservation userToken = UserTokenObservation::QueryTransientError;
  UserPrivilege privilege = UserPrivilege::PrivilegeQueryError;  // meaningful only when userToken == HasUser
  bool userHostReady = false;         // the user Host reported ready AND owns the single lease
  uint32_t transientRetries = 0;      // consecutive ambiguous observations so far
  uint32_t maxTransientRetries = 10;  // bound on Transition before Fault
};

enum class SupervisorAction {
  None,                     // steady state, nothing to launch/stop
  StopAll,                  // ensure no prelogin worker / handed-off child is running
  HoldNoCredential,         // provisioning: offer no screen/input
  HoldNoConsole,            // wait for a console session
  RunPreloginWorker,        // start or keep the SYSTEM prelogin secure-desktop worker
  StartUserHostFull,        // hand off using the user's Full token
  StartUserHostLinkedFull,  // hand off using the verified linked Full token
  StartUserHostDefault,     // hand off using the Default (no-linked) token, admin effective
  EnterUnsupportedUser,     // stop prelogin screen/input and surface UnsupportedUser
  RetryTransient,           // ambiguous: keep any healthy child and re-observe
  EnterFault,               // retry budget exhausted
};

struct SupervisorDecision {
  SupervisorState state;
  SupervisorAction action;
};

// The pure decision. Evaluation order is significant and is the contract:
//   featureEnabled -> machineCredential -> console -> token observation -> (privilege) -> ready.
inline SupervisorDecision DecideSupervisor(const SupervisorInputs& in) {
  if (!in.featureEnabled) return {SupervisorState::Disabled, SupervisorAction::StopAll};
  if (!in.machineCredentialValid) return {SupervisorState::Provisioning, SupervisorAction::HoldNoCredential};
  if (!in.consoleSessionValid) return {SupervisorState::NoConsole, SupervisorAction::HoldNoConsole};

  switch (in.userToken) {
    case UserTokenObservation::QueryTransientError:
      // Ambiguous: never treat as prelogin, never tear down a healthy child. Bounded.
      if (in.transientRetries >= in.maxTransientRetries) return {SupervisorState::Fault, SupervisorAction::EnterFault};
      return {SupervisorState::Transition, SupervisorAction::RetryTransient};

    case UserTokenObservation::NoInteractiveUser:
      return {SupervisorState::Prelogin, SupervisorAction::RunPreloginWorker};

    case UserTokenObservation::HasUser:
      switch (in.privilege) {
        case UserPrivilege::PrivilegeQueryError:
          if (in.transientRetries >= in.maxTransientRetries) return {SupervisorState::Fault, SupervisorAction::EnterFault};
          return {SupervisorState::Transition, SupervisorAction::RetryTransient};
        case UserPrivilege::Unsupported:
          return {SupervisorState::UnsupportedUser, SupervisorAction::EnterUnsupportedUser};
        case UserPrivilege::AdminFull:
          return in.userHostReady ? SupervisorDecision{SupervisorState::UserActive, SupervisorAction::None}
                                  : SupervisorDecision{SupervisorState::UserStarting, SupervisorAction::StartUserHostFull};
        case UserPrivilege::AdminLimitedLinked:
          return in.userHostReady ? SupervisorDecision{SupervisorState::UserActive, SupervisorAction::None}
                                  : SupervisorDecision{SupervisorState::UserStarting, SupervisorAction::StartUserHostLinkedFull};
        case UserPrivilege::AdminDefaultEffective:
          return in.userHostReady ? SupervisorDecision{SupervisorState::UserActive, SupervisorAction::None}
                                  : SupervisorDecision{SupervisorState::UserStarting, SupervisorAction::StartUserHostDefault};
      }
      // Unreachable if the enum is exhaustive; treat an unknown privilege as transient, not a guess.
      if (in.transientRetries >= in.maxTransientRetries) return {SupervisorState::Fault, SupervisorAction::EnterFault};
      return {SupervisorState::Transition, SupervisorAction::RetryTransient};
  }
  // Unknown observation -> transient, never prelogin.
  if (in.transientRetries >= in.maxTransientRetries) return {SupervisorState::Fault, SupervisorAction::EnterFault};
  return {SupervisorState::Transition, SupervisorAction::RetryTransient};
}

}  // namespace remote60::native_poc::prelogin
