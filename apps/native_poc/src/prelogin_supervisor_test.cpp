// Tests for the prelogin supervisor composition (prelogin_supervisor.hpp): the reconcile-driven
// lifecycle the Session-0 service runs. Feeds observation sequences and checks the composed state +
// handoff, with the §1/§3 negative controls (transient flap does not tear down and is bounded; a logon
// keeps the prelogin worker until ready; a console change mid-startup re-targets the handoff to the new
// generation; unsupported / disabled release the lease).

#include "prelogin_supervisor.hpp"

#include <cstdio>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}

SupervisorObservation noUser() {
  SupervisorObservation o;
  o.featureEnabled = true;
  o.machineCredentialValid = true;
  o.consoleSessionValid = true;
  o.userToken = UserTokenObservation::NoInteractiveUser;
  return o;
}
SupervisorObservation adminLogon(bool ready) {
  SupervisorObservation o = noUser();
  o.userToken = UserTokenObservation::HasUser;
  o.privilege = UserPrivilege::AdminFull;
  o.userHostReady = ready;
  return o;
}
}  // namespace

int main() {
  std::printf("prelogin supervisor composition\n");

  std::printf("[reboot -> prelogin -> logon -> ready -> active -> logoff -> prelogin]\n");
  {
    PreloginSupervisor sup;
    auto o = sup.Reconcile(noUser());
    check(o.state == SupervisorState::Prelogin && o.handoff == HandoffAction::StartPreloginWorker &&
              sup.handoff().owner == LeaseOwner::PreloginWorker,
          "boot: no user -> prelogin worker owns the lease");

    o = sup.Reconcile(adminLogon(/*ready=*/false));
    check(o.state == SupervisorState::UserStarting && o.handoff == HandoffAction::BeginUserHostStartup &&
              sup.handoff().owner == LeaseOwner::PreloginWorker && sup.handoff().preloginWorkerRunning,
          "logon (not ready): UserStarting, prelogin worker STILL running (not torn down)");

    o = sup.Reconcile(adminLogon(/*ready=*/false));
    check(o.handoff == HandoffAction::None && sup.handoff().owner == LeaseOwner::PreloginWorker,
          "still-not-ready reconcile does not re-trigger startup or hand off");

    o = sup.Reconcile(adminLogon(/*ready=*/true));
    check(o.state == SupervisorState::UserActive && o.handoff == HandoffAction::CompleteHandoff &&
              sup.handoff().owner == LeaseOwner::UserHost && !sup.handoff().preloginWorkerRunning,
          "ready: single-owner handoff to the user host, prelogin worker stopped");

    o = sup.Reconcile(adminLogon(/*ready=*/true));
    check(o.state == SupervisorState::UserActive && o.handoff == HandoffAction::None &&
              sup.handoff().owner == LeaseOwner::UserHost,
          "steady UserActive: no repeated handoff");

    o = sup.Reconcile(noUser());
    check(o.state == SupervisorState::Prelogin && sup.handoff().owner == LeaseOwner::PreloginWorker,
          "logoff -> back to prelogin");
  }

  std::printf("[NEGATIVE: a transient token error does not tear down, and is bounded]\n");
  {
    PreloginSupervisor sup(/*maxTransientRetries=*/3);
    sup.Reconcile(noUser());  // prelogin worker owns the lease
    SupervisorObservation flap = noUser();
    flap.userToken = UserTokenObservation::QueryTransientError;
    for (int i = 0; i < 3; ++i) {
      auto o = sup.Reconcile(flap);
      check(o.state == SupervisorState::Transition && sup.handoff().owner == LeaseOwner::PreloginWorker,
            "transient: Transition, prelogin worker lease UNCHANGED (healthy child kept)");
    }
    auto o = sup.Reconcile(flap);  // 4th consecutive -> budget exhausted
    check(o.state == SupervisorState::Fault && sup.handoff().owner == LeaseOwner::None,
          "bounded: retries exhausted -> Fault, lease released");
    // Recovery: a clean no-user observation resets and re-enters prelogin.
    o = sup.Reconcile(noUser());
    check(o.state == SupervisorState::Prelogin && sup.transientRetries() == 0,
          "a clean observation clears the retry counter and re-enters prelogin");
  }

  std::printf("[NEGATIVE: a console change mid-startup re-targets the handoff to the new generation]\n");
  {
    PreloginSupervisor sup;
    sup.Reconcile(noUser());
    sup.Reconcile(adminLogon(false));  // pending for generation 0
    const uint32_t gen0 = sup.handoff().consoleGeneration;

    SupervisorObservation changed = adminLogon(false);
    changed.consoleChanged = true;     // fast-user-switch / reconnect bumps the generation
    auto o = sup.Reconcile(changed);
    check(sup.handoff().consoleGeneration == gen0 + 1, "console change bumped the generation");
    check(o.handoff == HandoffAction::BeginUserHostStartup && sup.handoff().pendingGeneration == gen0 + 1,
          "startup re-pended for the NEW generation (the gen-0 pending is abandoned)");

    o = sup.Reconcile(adminLogon(true));  // ready in the new generation
    check(o.handoff == HandoffAction::CompleteHandoff && sup.handoff().owner == LeaseOwner::UserHost,
          "ready in the new generation completes the handoff");
  }

  std::printf("[NEGATIVE: unsupported and disabled release the lease]\n");
  {
    PreloginSupervisor sup;
    sup.Reconcile(noUser());  // prelogin
    SupervisorObservation unsup = noUser();
    unsup.userToken = UserTokenObservation::HasUser;
    unsup.privilege = UserPrivilege::Unsupported;
    auto o = sup.Reconcile(unsup);
    check(o.state == SupervisorState::UnsupportedUser && sup.handoff().owner == LeaseOwner::None &&
              !sup.handoff().preloginWorkerRunning,
          "unsupported account -> prelogin screen/input released (no worker, no user host)");

    sup.Reconcile(noUser());  // back to prelogin
    SupervisorObservation off = noUser();
    off.featureEnabled = false;
    o = sup.Reconcile(off);
    check(o.state == SupervisorState::Disabled && sup.handoff().owner == LeaseOwner::None,
          "feature off -> Disabled, lease released");
  }

  if (gFailures != 0) {
    std::printf("prelogin_supervisor_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_supervisor_test: PASS\n");
  return 0;
}
