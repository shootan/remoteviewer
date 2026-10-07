#pragma once

// Prelogin supervisor composition (prelogin-service A1, contract section 1) -- the object the Session-0
// service drives on its reconcile tick / session events. It composes the already-verified pure cores:
//   - DecideSupervisor() (prelogin_supervisor_state.hpp): observation -> {state, action};
//   - the handoff lease + generation fence (prelogin_handoff.hpp).
// It owns only the small mutable bookkeeping the cores need across ticks (the transient-retry counter
// and the handoff state), so the whole lifecycle is still exhaustively testable off a real machine by
// feeding observation sequences. The live Win32 (WTS/token/desktop/child launch) stays in the service.

#include <cstdint>

#include "prelogin_handoff.hpp"
#include "prelogin_supervisor_state.hpp"

namespace remote60::native_poc::prelogin {

// One reconcile's worth of already-gathered facts about the console session (the service fills these
// from WTS/token queries; see prelogin_console_probe.hpp for the classifiers).
struct SupervisorObservation {
  bool featureEnabled = false;
  bool machineCredentialValid = false;
  bool consoleSessionValid = false;
  bool consoleChanged = false;  // a console/desktop topology change occurred since the last reconcile
  UserTokenObservation userToken = UserTokenObservation::QueryTransientError;
  UserPrivilege privilege = UserPrivilege::PrivilegeQueryError;  // meaningful only when userToken == HasUser
  bool userHostReady = false;   // the user Host for the CURRENT generation reported ready + owns its lease
};

class PreloginSupervisor {
 public:
  struct Outcome {
    SupervisorState state;      // the decided state this tick
    SupervisorAction action;    // the decided action (what to launch/stop/hold)
    HandoffAction handoff;      // the concrete handoff transition taken this tick (None if steady)
  };

  explicit PreloginSupervisor(uint32_t maxTransientRetries = 10) : maxRetries_(maxTransientRetries) {}

  Outcome Reconcile(const SupervisorObservation& obs) {
    if (obs.consoleChanged) OnConsoleChanged(h_);

    SupervisorInputs in;
    in.featureEnabled = obs.featureEnabled;
    in.machineCredentialValid = obs.machineCredentialValid;
    in.consoleSessionValid = obs.consoleSessionValid;
    in.userToken = obs.userToken;
    in.privilege = obs.privilege;
    in.userHostReady = obs.userHostReady;
    in.transientRetries = retries_;
    in.maxTransientRetries = maxRetries_;

    const SupervisorDecision d = DecideSupervisor(in);
    retries_ = (d.action == SupervisorAction::RetryTransient) ? retries_ + 1 : 0;

    HandoffAction ha = HandoffAction::None;
    switch (d.action) {
      case SupervisorAction::RunPreloginWorker:
        if (h_.owner != LeaseOwner::PreloginWorker) ha = OnPreloginShouldRun(h_);
        break;
      case SupervisorAction::StartUserHostFull:
      case SupervisorAction::StartUserHostLinkedFull:
      case SupervisorAction::StartUserHostDefault:
        // Begin (or re-begin for a new generation) user-host startup; the prelogin worker keeps running.
        if (!h_.pendingUserHost || h_.pendingGeneration != h_.consoleGeneration) ha = OnSupportedLogon(h_);
        break;
      case SupervisorAction::None:
        // Ready in the current generation completes the single-owner handoff exactly once.
        if (d.state == SupervisorState::UserActive && obs.userHostReady && h_.owner != LeaseOwner::UserHost) {
          ha = OnUserHostReady(h_, h_.consoleGeneration);
        }
        break;
      case SupervisorAction::StopAll:
      case SupervisorAction::EnterUnsupportedUser:
      case SupervisorAction::EnterFault:
        // Release the lease and stop prelogin screen/input; keep the generation counter monotonic.
        ReleaseLease();
        break;
      case SupervisorAction::HoldNoCredential:
      case SupervisorAction::HoldNoConsole:
        ReleaseLease();  // offer no screen/input while not provisioned / no console
        break;
      case SupervisorAction::RetryTransient:
        break;  // keep any healthy child; do not touch the lease (the fence)
    }
    lastState_ = d.state;
    return {d.state, d.action, ha};
  }

  const HandoffState& handoff() const { return h_; }
  uint32_t transientRetries() const { return retries_; }
  SupervisorState lastState() const { return lastState_; }

 private:
  void ReleaseLease() {
    const uint32_t gen = h_.consoleGeneration;
    h_ = HandoffState{};
    h_.consoleGeneration = gen;
  }

  HandoffState h_;
  uint32_t retries_ = 0;
  uint32_t maxRetries_;
  SupervisorState lastState_ = SupervisorState::Disabled;
};

}  // namespace remote60::native_poc::prelogin
