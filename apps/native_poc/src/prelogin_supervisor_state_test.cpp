// Tests for the prelogin supervisor state machine (prelogin_supervisor_state.hpp). The positive paths
// matter, but the NEGATIVE controls are the point of the contract: an ambiguous token query must not
// become Prelogin, a Default token must not be guessed as unsupported, a logon must not instantly tear
// down, and retries must be bounded. (prelogin-service A1 section 1; Codex r3 section 1.)

#include "prelogin_supervisor_state.hpp"

#include <cstdio>
#include <initializer_list>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}

// A fully-enabled, provisioned, console-present base; individual tests vary one axis.
SupervisorInputs base() {
  SupervisorInputs in;
  in.featureEnabled = true;
  in.machineCredentialValid = true;
  in.consoleSessionValid = true;
  in.userToken = UserTokenObservation::NoInteractiveUser;
  in.privilege = UserPrivilege::PrivilegeQueryError;
  in.userHostReady = false;
  in.transientRetries = 0;
  in.maxTransientRetries = 10;
  return in;
}

void TestGuards() {
  std::printf("precondition guards (feature/credential/console) short-circuit in order\n");
  SupervisorInputs in = base();
  in.featureEnabled = false;
  auto d = DecideSupervisor(in);
  check(d.state == SupervisorState::Disabled && d.action == SupervisorAction::StopAll,
        "feature OFF overrides everything -> Disabled/StopAll");

  in = base();
  in.machineCredentialValid = false;
  d = DecideSupervisor(in);
  check(d.state == SupervisorState::Provisioning && d.action == SupervisorAction::HoldNoCredential,
        "no machine credential -> Provisioning (no screen/input)");

  in = base();
  in.consoleSessionValid = false;  // 0xFFFFFFFF
  d = DecideSupervisor(in);
  check(d.state == SupervisorState::NoConsole && d.action == SupervisorAction::HoldNoConsole,
        "no console session -> NoConsole/Hold (NOT Prelogin)");
}

void TestPrelogin() {
  std::printf("authoritative no-user -> Prelogin worker\n");
  SupervisorInputs in = base();
  in.userToken = UserTokenObservation::NoInteractiveUser;
  auto d = DecideSupervisor(in);
  check(d.state == SupervisorState::Prelogin && d.action == SupervisorAction::RunPreloginWorker,
        "NoInteractiveUser -> Prelogin/RunPreloginWorker");
}

void TestTransientIsNotPrelogin() {
  std::printf("NEGATIVE: an ambiguous token query is Transition, never Prelogin\n");
  SupervisorInputs in = base();
  in.userToken = UserTokenObservation::QueryTransientError;
  auto d = DecideSupervisor(in);
  check(d.state == SupervisorState::Transition && d.action == SupervisorAction::RetryTransient,
        "QueryTransientError -> Transition/RetryTransient");
  check(d.action != SupervisorAction::RunPreloginWorker, "  and specifically NOT RunPreloginWorker");

  // Bounded: at the budget it becomes Fault, not an infinite retry.
  in.transientRetries = in.maxTransientRetries;
  d = DecideSupervisor(in);
  check(d.state == SupervisorState::Fault && d.action == SupervisorAction::EnterFault,
        "transient retries exhausted -> Fault (bounded)");

  // One below the budget still retries.
  in.transientRetries = in.maxTransientRetries - 1;
  d = DecideSupervisor(in);
  check(d.state == SupervisorState::Transition, "one below budget still retries");
}

void TestPrivilegeMatrix() {
  std::printf("Q4 token matrix: Full / Limited+linked / Default-effective all start the user Host\n");
  for (auto pr : {UserPrivilege::AdminFull, UserPrivilege::AdminLimitedLinked, UserPrivilege::AdminDefaultEffective}) {
    SupervisorInputs in = base();
    in.userToken = UserTokenObservation::HasUser;
    in.privilege = pr;
    in.userHostReady = false;
    auto d = DecideSupervisor(in);
    const SupervisorAction want = pr == UserPrivilege::AdminFull            ? SupervisorAction::StartUserHostFull
                                  : pr == UserPrivilege::AdminLimitedLinked ? SupervisorAction::StartUserHostLinkedFull
                                                                            : SupervisorAction::StartUserHostDefault;
    check(d.state == SupervisorState::UserStarting && d.action == want,
          pr == UserPrivilege::AdminFull            ? "AdminFull -> UserStarting/StartUserHostFull"
          : pr == UserPrivilege::AdminLimitedLinked ? "AdminLimitedLinked -> UserStarting/StartUserHostLinkedFull"
                                                    : "AdminDefaultEffective -> UserStarting/StartUserHostDefault");

    // Ready flips to UserActive with no (re)launch.
    in.userHostReady = true;
    d = DecideSupervisor(in);
    check(d.state == SupervisorState::UserActive && d.action == SupervisorAction::None,
          "  ready -> UserActive/None (single owner, no relaunch)");
  }
}

void TestDefaultIsNotGuessedUnsupported() {
  std::printf("NEGATIVE: Default elevation is NOT auto-unsupported (r3 correction)\n");
  SupervisorInputs in = base();
  in.userToken = UserTokenObservation::HasUser;
  in.privilege = UserPrivilege::AdminDefaultEffective;  // the service layer decided real admin is effective
  auto d = DecideSupervisor(in);
  check(d.action == SupervisorAction::StartUserHostDefault && d.state == SupervisorState::UserStarting,
        "Default-with-effective-admin is SUPPORTED, not UnsupportedUser");
  check(d.action != SupervisorAction::EnterUnsupportedUser, "  and specifically NOT EnterUnsupportedUser");
}

void TestUnsupportedUser() {
  std::printf("a genuinely unprivileged account -> UnsupportedUser, no user-host launch\n");
  SupervisorInputs in = base();
  in.userToken = UserTokenObservation::HasUser;
  in.privilege = UserPrivilege::Unsupported;
  auto d = DecideSupervisor(in);
  check(d.state == SupervisorState::UnsupportedUser && d.action == SupervisorAction::EnterUnsupportedUser,
        "Unsupported -> UnsupportedUser/EnterUnsupportedUser");
  check(d.action != SupervisorAction::StartUserHostFull && d.action != SupervisorAction::StartUserHostLinkedFull &&
            d.action != SupervisorAction::StartUserHostDefault && d.action != SupervisorAction::RunPreloginWorker,
        "  and no user-host launch and no prelogin worker for an unsupported account");
}

void TestPrivilegeQueryErrorIsTransient() {
  std::printf("NEGATIVE: a user is present but privilege is unknown -> Transition, not a guess\n");
  SupervisorInputs in = base();
  in.userToken = UserTokenObservation::HasUser;
  in.privilege = UserPrivilege::PrivilegeQueryError;
  auto d = DecideSupervisor(in);
  check(d.state == SupervisorState::Transition && d.action == SupervisorAction::RetryTransient,
        "HasUser + PrivilegeQueryError -> Transition/RetryTransient");
  check(d.action != SupervisorAction::EnterUnsupportedUser && d.action != SupervisorAction::StartUserHostFull,
        "  neither unsupported nor a launch is guessed");
  in.transientRetries = in.maxTransientRetries;
  d = DecideSupervisor(in);
  check(d.state == SupervisorState::Fault, "  bounded -> Fault at budget");
}

void TestLogonDoesNotInstantlyTearDown() {
  std::printf("NEGATIVE: a supported logon is UserStarting, not an instant UserActive teardown\n");
  SupervisorInputs in = base();
  in.userToken = UserTokenObservation::HasUser;
  in.privilege = UserPrivilege::AdminFull;
  in.userHostReady = false;  // LOGON fired but the host is not ready yet
  auto d = DecideSupervisor(in);
  check(d.state == SupervisorState::UserStarting, "logon-without-ready stays UserStarting (prelogin not yet declared done)");
  check(d.state != SupervisorState::UserActive, "  and is NOT UserActive before ready");
}

}  // namespace

int main() {
  TestGuards();
  TestPrelogin();
  TestTransientIsNotPrelogin();
  TestPrivilegeMatrix();
  TestDefaultIsNotGuessedUnsupported();
  TestUnsupportedUser();
  TestPrivilegeQueryErrorIsTransient();
  TestLogonDoesNotInstantlyTearDown();

  if (gFailures != 0) {
    std::printf("prelogin_supervisor_state_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_supervisor_state_test: PASS\n");
  return 0;
}
