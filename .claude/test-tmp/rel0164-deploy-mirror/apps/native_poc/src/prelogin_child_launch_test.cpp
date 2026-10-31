// Counter-example tests for the prelogin child-launch adoption gate (prelogin_child_launch.hpp, r6 G9).
//
// The defect G9 names: the launcher ignored Job-assignment failure and resumed the child anyway, leaving
// a SYSTEM worker that outlives the service. The negative controls below assert that a child which could
// NOT be bound to the Job (or could not be resumed) is terminated, never adopted -- and that a launch
// token that is no longer a supported admin aborts the handoff.

#include <cstdio>

#include "prelogin_child_launch.hpp"

using namespace remote60::native_poc::prelogin;

static int failures = 0;
static void ok(bool cond, const char* what) {
  if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
}

int main() {
  // Adoption gate: the ONLY path that adopts is created+jobbed+resumed.
  ok(DecideChildAdoption(true, true, true) == ChildLaunchDisposition::Adopt,
     "created + jobbed + resumed -> Adopt");

  // Counter-example (the G9 bug): created suspended but Job assignment failed -> MUST terminate, not adopt.
  ok(DecideChildAdoption(true, false, true) == ChildLaunchDisposition::TerminateUnboundChild,
     "created but Job NOT assigned -> TerminateUnboundChild (never resume an un-jobbed SYSTEM child)");
  // Resume failure after jobbing is likewise fatal -- we do not leave a half-started child adopted.
  ok(DecideChildAdoption(true, true, false) == ChildLaunchDisposition::TerminateUnboundChild,
     "created + jobbed but resume failed -> TerminateUnboundChild");
  ok(DecideChildAdoption(true, false, false) == ChildLaunchDisposition::TerminateUnboundChild,
     "created but neither jobbed nor resumed -> TerminateUnboundChild");
  // Creation itself failed -> nothing to clean up.
  ok(DecideChildAdoption(false, false, false) == ChildLaunchDisposition::AbortNotCreated,
     "CreateProcess failed -> AbortNotCreated");

  // Launch-token re-judgement: only the supported admin forms may launch the user Host.
  ok(LaunchTokenAcceptable(UserPrivilege::AdminFull), "AdminFull acceptable");
  ok(LaunchTokenAcceptable(UserPrivilege::AdminLimitedLinked), "AdminLimitedLinked acceptable");
  ok(LaunchTokenAcceptable(UserPrivilege::AdminDefaultEffective), "AdminDefaultEffective acceptable");
  // Negative controls: a token that drifted to standard / became unresolvable at launch aborts.
  ok(!LaunchTokenAcceptable(UserPrivilege::Unsupported),
     "Unsupported at launch -> NOT acceptable (abort handoff)");
  ok(!LaunchTokenAcceptable(UserPrivilege::PrivilegeQueryError),
     "PrivilegeQueryError at launch -> NOT acceptable (abort handoff)");

  if (failures != 0) { std::printf("prelogin_child_launch_test: FAIL (%d)\n", failures); return 1; }
  std::printf("prelogin_child_launch_test: PASS\n");
  return 0;
}
