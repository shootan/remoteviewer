#pragma once

// Prelogin child-launch adoption gate (prelogin-service r6 G9).
//
// Codex G9: the service created the SYSTEM worker / user Host CREATE_SUSPENDED and then IGNORED the result
// of ensure_child_job()/AssignProcessToJobObject before ResumeThread. A child that could not be bound to
// the kill-on-close Job must NEVER be resumed -- otherwise the comment's "never outlives the service" is a
// lie: an un-jobbed SYSTEM worker survives service stop. The gate below makes that decision pure and
// testable; the launcher calls it and terminates the suspended child unless it is fully bound.
//
// It also decides whether a FINAL re-acquired launch token is acceptable (G9: re-judge the token type at
// the instant of launch, not only at the earlier observation), reusing the same UserPrivilege the pure
// supervisor consumes so the launch cannot proceed on a token that is no longer a supported admin.

#include "prelogin_supervisor_state.hpp"  // UserPrivilege

namespace remote60::native_poc::prelogin {

enum class ChildLaunchDisposition {
  AbortNotCreated,       // CreateProcessAsUser failed -> nothing to clean up
  TerminateUnboundChild, // created suspended but could NOT be bound to the Job (or resume failed) -> kill it
  Adopt,                 // created + jobbed + resumed -> safe to adopt the handle
};

// Pure: a suspended child is adopted ONLY when it was created, assigned to the kill-on-close Job, AND
// resumed. Any failure after creation means we hold a suspended, un-bounded SYSTEM process that must be
// terminated and its handle closed -- never left running, never adopted.
inline ChildLaunchDisposition DecideChildAdoption(bool createdSuspended, bool jobAssigned, bool resumeOk) {
  if (!createdSuspended) return ChildLaunchDisposition::AbortNotCreated;
  if (!jobAssigned || !resumeOk) return ChildLaunchDisposition::TerminateUnboundChild;
  return ChildLaunchDisposition::Adopt;
}

// Pure: the FINAL launch token is acceptable only when it classifies as one of the supported admin forms.
// A standard user, a deny-only admin without elevation, or any token-query error at launch time must abort
// the handoff rather than start a Host with the wrong authority (G9 "launch 직전 token 재판정").
inline bool LaunchTokenAcceptable(UserPrivilege priv) {
  switch (priv) {
    case UserPrivilege::AdminFull:
    case UserPrivilege::AdminLimitedLinked:
    case UserPrivilege::AdminDefaultEffective:
      return true;
    default:
      return false;  // Unsupported / PrivilegeQueryError / anything else -> do not launch
  }
}

}  // namespace remote60::native_poc::prelogin
