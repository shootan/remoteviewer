#include "update_state_machine.hpp"

#include <algorithm>

namespace remote60::native_poc::update {
namespace {

/** Releases the lock on every exit path, including the early ones. */
class LockGuard {
 public:
  explicit LockGuard(UpdateEffects& effects) : effects_(effects) {}
  ~LockGuard() { if (held_) effects_.ReleaseLock(); }
  bool acquire() {
    held_ = effects_.AcquireLock();
    return held_;
  }

 private:
  UpdateEffects& effects_;
  bool held_ = false;
};

}  // namespace

bool UpdateOutcome::entered(UpdateState s) const {
  return std::find(visited.begin(), visited.end(), s) != visited.end();
}

UpdateState UpdateOutcome::last() const {
  return visited.empty() ? UpdateState::Idle : visited.back();
}

const char* state_name(UpdateState s) {
  switch (s) {
    case UpdateState::Idle: return "Idle";
    case UpdateState::CheckRequested: return "CheckRequested";
    case UpdateState::Evaluate: return "Evaluate";
    case UpdateState::Download: return "Download";
    case UpdateState::Verify: return "Verify";
    case UpdateState::Prepare: return "Prepare";
    case UpdateState::Quiesce: return "Quiesce";
    case UpdateState::Swap: return "Swap";
    case UpdateState::Register: return "Register";
    case UpdateState::Relaunch: return "Relaunch";
    case UpdateState::Health: return "Health";
    case UpdateState::Done: return "Done";
    case UpdateState::Rollback: return "Rollback";
  }
  return "?";
}

const char* result_name(UpdateResult r) {
  switch (r) {
    case UpdateResult::NothingToDo: return "NothingToDo";
    case UpdateResult::Updated: return "Updated";
    case UpdateResult::UpdatedButNotRelaunched: return "UpdatedButNotRelaunched";
    case UpdateResult::AbandonedBeforeSwap: return "AbandonedBeforeSwap";
    case UpdateResult::AbandonedNotRelaunched: return "AbandonedNotRelaunched";
    case UpdateResult::RolledBack: return "RolledBack";
    case UpdateResult::RestoredButUnhealthy: return "RestoredButUnhealthy";
    case UpdateResult::RolledBackNotRelaunched: return "RolledBackNotRelaunched";
    case UpdateResult::RollbackFailed: return "RollbackFailed";
  }
  return "?";
}

UpdateOutcome run_update(UpdateEffects& effects,
                         const SignatureVerifier& verifier,
                         const std::string& platform) {
  UpdateOutcome out;
  const auto enter = [&out](UpdateState s) { out.visited.push_back(s); };

  /**
   * Gives up without having changed anything, and puts back whatever left on our account.
   *
   * The second half is the part that was missing. A caller may already have exited because this
   * update told it the download was verified, and abandoning after that point left a machine with
   * every file intact and nothing running -- which for a remote user is an unreachable machine,
   * arrived at without a single thing going wrong. Nothing to repair, nothing to find.
   *
   * Relaunch is safe on the paths where nothing left: no targets were captured, so the plan is
   * empty and it does nothing. Attempted once, never in a loop.
   */
  const auto abandon = [&](const char* why) {
    out.detail = why;
    // Anything that did not come back matters here, required or not: nothing was changed, so the
    // only thing this path can get wrong is leaving something down that it took down.
    // Both kinds, and both count. Nothing on disk was changed, so there is no rollback to
    // protect and no reason to hold the optional ones back -- the only thing this path can get
    // wrong is leaving something down that it took down.
    const RelaunchVerdict required = effects.RelaunchRequired();
    const RelaunchVerdict optional = effects.RelaunchOptional();
    const bool everything =
        required == RelaunchVerdict::AllBack && optional == RelaunchVerdict::AllBack;
    out.result = everything ? UpdateResult::AbandonedBeforeSwap
                            : UpdateResult::AbandonedNotRelaunched;
    return out;
  };

  // Everything past this point either rolls back or is abandoned cleanly, so the rollback path
  // is written once here and reused.
  const auto rollback = [&](const char* why) {
    enter(UpdateState::Rollback);
    out.detail = why;
    if (effects.Rollback()) {
      // Putting the files back is only half of it. The host was stopped to do the swap, and a
      // machine whose files are correct but whose host is not running is one a remote user cannot
      // reach -- there is no way back in to fix anything. So the previous version is started
      // again, and whether that worked is reported rather than assumed.
      //
      // Once. No retry: a relaunch that failed once is unlikely to succeed on a second identical
      // attempt, and a loop here would sit between the user and a machine that is already in its
      // restored state.
      enter(UpdateState::Relaunch);
      // The same order as a successful update, for the same reasons: what the machine needs
      // first, then its health, and only then the rest. A client that will not come back must
      // not stop this path from establishing whether the restored host is up.
      if (effects.RelaunchRequired() != RelaunchVerdict::AllBack) {
        out.result = UpdateResult::RolledBackNotRelaunched;
      } else {
        // And the restored build is checked the same way a new one would be. What is on disk now
        // is the previous version, so this is a question about that.
        //
        // The answer is USED. It was discarded, which meant a restore that came back broken was
        // reported as a successful rollback -- "we put it back" and "it works" are different
        // claims and only the first one had been established.
        enter(UpdateState::Health);
        const bool healthy = effects.HealthCheck();
        // The old client goes back last, and whether it makes it does not change the verdict
        // above. Reported, because the user will notice, but a window that did not reopen is not
        // the same as a machine nobody can reach -- and the old outcome name said it was.
        const RelaunchVerdict optional = effects.RelaunchOptional();
        out.result = healthy ? UpdateResult::RolledBack : UpdateResult::RestoredButUnhealthy;
        if (healthy && optional != RelaunchVerdict::AllBack) {
          out.detail = std::string(why) + " -- restored, but something optional did not come back";
        }
      }
    } else {
      // The only outcome where the install may be inconsistent. Named distinctly so it cannot be
      // mistaken for an ordinary failed update in a log.
      out.result = UpdateResult::RollbackFailed;
    }
    effects.DiscardDownload();
    return out;
  };

  enter(UpdateState::Idle);

  // ---- mutual exclusion (design 3.3). Never waited on: deferring an update costs a cycle, two
  // processes replacing the same directory costs the installation.
  LockGuard lock(effects);
  if (!lock.acquire()) {
    out.result = UpdateResult::NothingToDo;
    out.detail = "another update or installer holds the lock";
    return out;
  }

  enter(UpdateState::CheckRequested);
  std::string document;
  std::string signatureHex;
  if (!effects.FetchManifest(&document, &signatureHex)) {
    // A server that cannot be reached must never disturb a working installation. This is not a
    // failure, it is "not now".
    out.result = UpdateResult::NothingToDo;
    out.detail = "could not fetch the manifest";
    return out;
  }

  enter(UpdateState::Evaluate);
  // The signature is checked before anything in the document is believed. That ordering lives in
  // load_manifest and is enforced by its types; it is not re-implemented here, because a second
  // copy of a rule is a second place for it to rot.
  const ManifestResult manifest = load_manifest(document, signatureHex, platform, verifier);
  if (manifest.status != ManifestStatus::Ok || !manifest.manifest) {
    out.result = UpdateResult::NothingToDo;
    out.detail = std::string("manifest rejected: ") + manifest.detail;
    return out;
  }
  if (!manifest.manifest->is_newer_than(effects.InstalledVersion())) {
    out.result = UpdateResult::NothingToDo;
    out.detail = "not newer than what is installed";
    return out;
  }
  const ManifestFields& fields = manifest.manifest->fields();

  enter(UpdateState::Download);
  if (!effects.Download(fields)) {
    effects.DiscardDownload();  // a partial file must not survive to be picked up later
    return abandon("download did not complete");
  }

  enter(UpdateState::Verify);
  if (!effects.VerifyDownload(fields)) {
    effects.DiscardDownload();
    return abandon("downloaded artifact did not match the manifest");
  }

  enter(UpdateState::Prepare);
  if (!effects.PrepareForSwap()) {
    // Deliberately does NOT escalate to a forced shutdown. The product is in the middle of doing
    // something, and an update is worth less than whatever that is. The verified download is kept
    // so the next attempt starts here rather than at the top.
    //
    // Past the point where a waiting caller has been released, so this owes a restoration.
    return abandon("product did not reach a safe point");
  }

  enter(UpdateState::Quiesce);
  if (!effects.Quiesce()) {
    // Same rule, same reason. Nothing on disk has been touched yet, so backing out costs nothing
    // -- except that some of the product may already have stopped, and putting it back is this
    // path's job.
    return abandon("product did not shut down cleanly");
  }

  // ---- past here the installation is being modified, so every failure rolls back.

  enter(UpdateState::Swap);
  if (!effects.Swap()) return rollback("swap failed");

  enter(UpdateState::Register);
  if (!effects.RegisterInstall()) return rollback("registration failed");

  // ---- required first, health, commit, and only then the rest.
  //
  // The order is the safety property. While a rollback is still possible, the only processes
  // started are ones this attempt can stop: the host and the service, both launched with a handle
  // in hand. The client is not, because the route that starts it as the logged-on user may fall
  // through to the shell, and a shell launch hands nothing back -- no handle, no proof of which
  // process is ours, no way to stop it. A rollback has to move the files it would be holding.
  //
  // Starting it first and reclassifying afterwards was the obvious repair and it is too late by
  // construction: by the time the fall-through is discovered the process exists, and the way back
  // has been closed by the act of putting the product back on its feet. So the optional images
  // wait until there is nothing left to undo.
  enter(UpdateState::Relaunch);
  if (effects.RelaunchRequired() != RelaunchVerdict::AllBack) {
    // The files are the new version and they are fine. The machine is not reachable, and a
    // machine nobody can reach is worth less than an older one somebody can -- so this goes back,
    // which is only possible because nothing has been committed yet and the backups are still
    // there.
    return rollback("something the machine needs did not come back");
  }

  // "Required started" means CreateProcess returned. It does not mean the host came up, and the
  // difference is the whole reason this question is asked before anything is committed.
  enter(UpdateState::Health);
  if (!effects.HealthCheck()) return rollback("new build did not come up healthy");

  enter(UpdateState::Done);
  // The point of no return, and it is deliberate that it comes before the optional relaunch
  // rather than after. Everything that could have sent this back has now been answered.
  effects.Commit();
  effects.DiscardDownload();

  // Past the commit. Nothing started here can force a rollback, because there is no longer one to
  // force -- which is exactly the condition under which starting something unownable is safe.
  const RelaunchVerdict optional = effects.RelaunchOptional();
  if (optional != RelaunchVerdict::AllBack) {
    // Not a failure of the update and not hidden as a success either. The install is correct and
    // healthy; something a person expects to see did not reappear, and they need to be told so
    // they start it themselves instead of assuming the update broke it.
    out.result = UpdateResult::UpdatedButNotRelaunched;
    out.detail = "installed and healthy, but something optional did not come back up";
    return out;
  }

  out.result = UpdateResult::Updated;
  out.detail = fields.version;
  return out;
}

}  // namespace remote60::native_poc::update
