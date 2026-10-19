#pragma once

// When the service starts the logged-on user's GNLinkHost (prelogin-ui r9, R4/W1). Pure; the service
// (secure_input_service_main.cpp) observes and acts.
//
// Before r9 the service started "GNLinkHost --tray" on every reconcile tick until that launch's ready
// arrived. A GNLinkHost the user already had running -- the very window the feature was switched on from,
// or one started by a Run entry -- holds the single-instance mutex, so each launched one left at once
// (exit 0), never became ready, and was started again a second later; and a user who closed GNLinkHost
// saw it come straight back.
//
// The policy, per logon (a new console session or a new logon in it starts over):
//   * a GNLinkHost of this install already running in the session is the user's Host: it is watched,
//     nothing is started;
//   * one the service started, or the one it watched, that ends with an ordinary exit code was closed on
//     purpose -- 0 (tray Exit, an update standing down, a second instance yielding to the first) and 1 (Task
//     Manager's "End task" is TerminateProcess(..., 1)) alike: not started again for this logon (r10);
//   * one that ends with an exception status (a crash: NTSTATUS 0x8xxxxxxx / 0xCxxxxxxx) is started again,
//     with the same bounded backoff as the pre-login worker (1 s doubling to 60 s while it dies within 15 s;
//     prelogin_worker_launch.hpp);
//   * with none running and none started yet, one is started.

#include <cstdint>

#include "prelogin_worker_launch.hpp"

namespace remote60::native_poc::prelogin {

enum class UserHostDecision {
  None,    // nothing to do now
  Adopt,   // watch the GNLinkHost already running in the session (do not start one)
  Launch,  // start GNLinkHost --tray
};

// What the service sees this tick.
struct UserHostObservation {
  uint64_t logon = 0;           // identity of this logon (changes with the console session / a new logon)
  bool watchedAlive = false;    // the Host the service started or adopted is running
  bool watchedExited = false;   // ...and it has ended (its exit code below); false when nothing is watched
  uint32_t watchedExitCode = 0;
  bool otherRunning = false;    // a GNLinkHost of this install runs in the session that is not the watched one
  uint64_t nowMs = 0;
};

// An exception status, not an exit the program (or the user, through Task Manager) chose.
inline bool ExitWasCrash(uint32_t exitCode) { return (exitCode & 0x80000000u) != 0; }

// r11 U3: what happens, at the top of every reconcile tick, to a Host the service watches (one it started or
// adopted) -- whatever the tick then does. A Host in another session than the console is a user's who is
// still logged on there (a switch of user): let go, never stopped -- also when the new console session has
// nobody logged on yet and the tick starts the pre-login worker (r10 let go only on the user-Host branch, so
// that branch's stop terminated A's Host). Only a Host the service started in the console session itself,
// on a tick where that session has no user any more, is stopped. No console right now: nothing decided.
enum class WatchedHostStep { Keep, LetGo, Stop };

inline WatchedHostStep WatchedHostOnTick(uint32_t hostSession, uint32_t consoleSession, bool preloginWorkerTick,
                                         bool startedByService) {
  if (consoleSession == 0xFFFFFFFFu) return WatchedHostStep::Keep;
  if (hostSession != consoleSession) return WatchedHostStep::LetGo;
  if (preloginWorkerTick) return startedByService ? WatchedHostStep::Stop : WatchedHostStep::LetGo;
  return WatchedHostStep::Keep;
}

class UserHostLaunchPolicy {
 public:
  UserHostDecision Decide(const UserHostObservation& o) {
    if (o.logon != logon_) {  // a new logon: everything starts over
      logon_ = o.logon;
      userClosed_ = false;
      backoff_ = WorkerRestartBackoff{};
      launchedAtMs_ = 0;
    }
    if (o.watchedAlive) return UserHostDecision::None;
    if (o.watchedExited) {
      if (!ExitWasCrash(o.watchedExitCode)) {
        userClosed_ = true;  // closed on purpose (or yielded to another instance): not again this logon
      } else {
        (void)backoff_.OnExit(launchedAtMs_, o.nowMs);  // a crash: started again, but not at once
      }
    }
    if (o.otherRunning) return UserHostDecision::Adopt;
    if (userClosed_) return UserHostDecision::None;
    if (!backoff_.MayLaunch(o.nowMs)) return UserHostDecision::None;
    launchedAtMs_ = o.nowMs;
    return UserHostDecision::Launch;
  }
  // The launch could not even be made (CreateProcess failed): counted like a quick death.
  void OnLaunchFailed(uint64_t nowMs) { (void)backoff_.OnLaunchFailed(nowMs); }
  bool userClosed() const { return userClosed_; }

 private:
  uint64_t logon_ = ~0ull;
  bool userClosed_ = false;
  WorkerRestartBackoff backoff_;
  uint64_t launchedAtMs_ = 0;
};

}  // namespace remote60::native_poc::prelogin
