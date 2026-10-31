// Tests for the prelogin->user-host handoff lease + generation fence (prelogin_handoff.hpp). The
// negative controls are the contract (§3): a logon does not instantly tear down prelogin; a stale-
// generation ready never transfers the lease; input from a superseded generation or a non-owner is
// fenced; there is never more than one owner.

#include "prelogin_handoff.hpp"

#include <cstdio>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}
}  // namespace

int main() {
  std::printf("prelogin handoff lease + generation fence\n");

  std::printf("[happy path: prelogin -> logon -> ready -> single-owner user host]\n");
  {
    HandoffState s;
    check(OnPreloginShouldRun(s) == HandoffAction::StartPreloginWorker && s.owner == LeaseOwner::PreloginWorker &&
              s.preloginWorkerRunning,
          "no user -> prelogin worker owns the lease");

    check(OnSupportedLogon(s) == HandoffAction::BeginUserHostStartup, "logon -> begin user-host startup");
    check(s.owner == LeaseOwner::PreloginWorker && s.preloginWorkerRunning,
          "NEGATIVE: logon did NOT stop the prelogin worker or move the lease yet");

    check(OnUserHostReady(s, s.consoleGeneration) == HandoffAction::CompleteHandoff, "ready (same gen) -> complete handoff");
    check(s.owner == LeaseOwner::UserHost && !s.preloginWorkerRunning,
          "single owner: user host owns the lease AND the prelogin worker stopped (no double owner)");
  }

  std::printf("[NEGATIVE: a stale-generation ready is fenced, lease not transferred]\n");
  {
    HandoffState s;
    OnPreloginShouldRun(s);
    OnSupportedLogon(s);                 // pending for generation 0
    const uint32_t staleReadyGen = s.consoleGeneration;  // 0
    OnConsoleChanged(s);                 // generation -> 1 (e.g. fast user switch / reconnect)
    check(s.consoleGeneration == 1, "console change bumped the generation");
    auto a = OnUserHostReady(s, staleReadyGen);  // the old generation's host reports ready late
    check(a == HandoffAction::DiscardStaleHandoff, "stale-generation ready -> DiscardStaleHandoff");
    check(s.owner == LeaseOwner::PreloginWorker && s.preloginWorkerRunning,
          "the lease stayed with the prelogin worker; no handoff to a superseded generation");
  }

  std::printf("[NEGATIVE: ready without a pending logon is not a handoff]\n");
  {
    HandoffState s;
    OnPreloginShouldRun(s);
    check(OnUserHostReady(s, s.consoleGeneration) == HandoffAction::DiscardStaleHandoff,
          "ready with no pending logon -> DiscardStaleHandoff (not a silent transfer)");
    check(s.owner == LeaseOwner::PreloginWorker, "owner unchanged");
  }

  std::printf("[generation fence on input/callbacks]\n");
  {
    HandoffState s;
    OnPreloginShouldRun(s);  // owner=PreloginWorker, gen 0
    check(AcceptInput(s, LeaseOwner::PreloginWorker, 0), "current owner + current generation -> accept");
    check(!AcceptInput(s, LeaseOwner::UserHost, 0), "NEGATIVE: a non-owner is fenced out");
    check(!AcceptInput(s, LeaseOwner::PreloginWorker, 999), "NEGATIVE: a stale-generation callback is fenced out");
    check(!AcceptInput(s, LeaseOwner::None, 0), "NEGATIVE: None never accepts");
    OnConsoleChanged(s);  // gen -> 1
    check(!AcceptInput(s, LeaseOwner::PreloginWorker, 0), "NEGATIVE: input from the pre-change generation is now fenced");
    check(AcceptInput(s, LeaseOwner::PreloginWorker, 1), "input in the new generation from the owner is accepted");
  }

  std::printf("[logoff returns to prelogin]\n");
  {
    HandoffState s;
    OnPreloginShouldRun(s);
    OnSupportedLogon(s);
    OnUserHostReady(s, s.consoleGeneration);
    check(s.owner == LeaseOwner::UserHost, "user host owns the lease");
    check(OnLogoff(s) == HandoffAction::ReturnToPrelogin && s.owner == LeaseOwner::None && !s.preloginWorkerRunning,
          "logoff -> ReturnToPrelogin, lease released");
  }

  if (gFailures != 0) {
    std::printf("prelogin_handoff_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_handoff_test: PASS\n");
  return 0;
}
