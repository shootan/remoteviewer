#pragma once

// Prelogin -> user-host handoff lease + generation fence (prelogin-service A1, contract section 3).
//
// The §3 rules that are easy to get catastrophically wrong, isolated as pure logic:
//   - WTS_SESSION_LOGON must NOT instantly stop the prelogin worker. A supported logon begins user-host
//     startup while the prelogin worker keeps running; the single owner lease transfers ONLY once the
//     user host reports ready IN THE SAME console generation.
//   - A ready report (or any queued callback / input) from an OLD console generation is fenced: it never
//     transfers the lease and never applies to the new desktop.
//   - There is never more than one owner: completing the handoff makes UserHost the owner and stops the
//     prelogin worker in the same step, so new and old never both heartbeat/own.
//
// A console/desktop topology change bumps the generation; everything is keyed on it.

#include <cstdint>

namespace remote60::native_poc::prelogin {

enum class LeaseOwner { None, PreloginWorker, UserHost };

struct HandoffState {
  uint32_t consoleGeneration = 0;      // bumped on each console/desktop topology change
  LeaseOwner owner = LeaseOwner::None; // who holds the single lease right now
  bool preloginWorkerRunning = false;
  bool pendingUserHost = false;        // a supported logon was seen; the user host is coming up
  uint32_t pendingGeneration = 0;      // the generation the pending handoff belongs to
};

enum class HandoffAction {
  None,
  StartPreloginWorker,   // take the lease for the prelogin worker
  BeginUserHostStartup,  // logon seen: start the user host; the prelogin worker KEEPS running
  CompleteHandoff,       // user host ready in the same generation: UserHost takes the lease, prelogin stops
  DiscardStaleHandoff,   // a ready/pending from an old generation -> void it, keep the current owner
  ReturnToPrelogin,      // logoff: drop the user host, go back to prelogin
};

// Console present, authoritatively no user -> run the prelogin worker and give it the lease.
inline HandoffAction OnPreloginShouldRun(HandoffState& s) {
  s.owner = LeaseOwner::PreloginWorker;
  s.preloginWorkerRunning = true;
  s.pendingUserHost = false;
  return HandoffAction::StartPreloginWorker;
}

// A supported user logged on. Do NOT stop the prelogin worker; begin bringing the user host up, tagged
// with the current generation. The lease stays with the prelogin worker until the handoff completes.
inline HandoffAction OnSupportedLogon(HandoffState& s) {
  s.pendingUserHost = true;
  s.pendingGeneration = s.consoleGeneration;
  return HandoffAction::BeginUserHostStartup;
}

// The user host reported ready for `readyGeneration`. Transfer the single lease ONLY if a handoff is
// pending for the current generation; a stale-generation ready is fenced and discarded.
inline HandoffAction OnUserHostReady(HandoffState& s, uint32_t readyGeneration) {
  if (!s.pendingUserHost || readyGeneration != s.consoleGeneration || s.pendingGeneration != s.consoleGeneration) {
    return HandoffAction::DiscardStaleHandoff;
  }
  s.owner = LeaseOwner::UserHost;      // single owner: user host now
  s.preloginWorkerRunning = false;     // ...and the prelogin worker stops in the same step
  s.pendingUserHost = false;
  return HandoffAction::CompleteHandoff;
}

// Console/desktop topology changed. Bump the generation so any pending handoff or queued input from the
// old generation can no longer complete or apply.
inline void OnConsoleChanged(HandoffState& s) {
  ++s.consoleGeneration;
  // A handoff that was pending for the old generation is now stale; it will be discarded on its ready.
}

// Logoff: the user host is gone, return to prelogin ownership.
inline HandoffAction OnLogoff(HandoffState& s) {
  s.owner = LeaseOwner::None;
  s.pendingUserHost = false;
  s.preloginWorkerRunning = false;
  return HandoffAction::ReturnToPrelogin;
}

// May input/a callback tagged (from, inputGeneration) be applied right now? Only from the current owner
// and only in the current generation -- a delayed callback from a superseded generation is fenced out.
inline bool AcceptInput(const HandoffState& s, LeaseOwner from, uint32_t inputGeneration) {
  return from == s.owner && from != LeaseOwner::None && inputGeneration == s.consoleGeneration;
}

}  // namespace remote60::native_poc::prelogin
