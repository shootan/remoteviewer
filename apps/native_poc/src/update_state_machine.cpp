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
    case UpdateResult::RolledBack: return "RolledBack";
    case UpdateResult::RollbackFailed: return "RollbackFailed";
  }
  return "?";
}

UpdateOutcome run_update(UpdateEffects& effects,
                         const SignatureVerifier& verifier,
                         const std::string& platform) {
  UpdateOutcome out;
  const auto enter = [&out](UpdateState s) { out.visited.push_back(s); };

  // Everything past this point either rolls back or is abandoned cleanly, so the rollback path
  // is written once here and reused.
  const auto rollback = [&](const char* why) {
    enter(UpdateState::Rollback);
    out.detail = why;
    if (effects.Rollback()) {
      out.result = UpdateResult::RolledBack;
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
    out.result = UpdateResult::AbandonedBeforeSwap;
    out.detail = "download did not complete";
    return out;
  }

  enter(UpdateState::Verify);
  if (!effects.VerifyDownload(fields)) {
    effects.DiscardDownload();
    out.result = UpdateResult::AbandonedBeforeSwap;
    out.detail = "downloaded artifact did not match the manifest";
    return out;
  }

  enter(UpdateState::Prepare);
  if (!effects.PrepareForSwap()) {
    // Deliberately does NOT escalate to a forced shutdown. The product is in the middle of doing
    // something, and an update is worth less than whatever that is. The verified download is kept
    // so the next attempt starts here rather than at the top.
    out.result = UpdateResult::AbandonedBeforeSwap;
    out.detail = "product did not reach a safe point";
    return out;
  }

  enter(UpdateState::Quiesce);
  if (!effects.Quiesce()) {
    // Same rule, same reason. Nothing on disk has been touched yet, so backing out costs nothing.
    out.result = UpdateResult::AbandonedBeforeSwap;
    out.detail = "product did not shut down cleanly";
    return out;
  }

  // ---- past here the installation is being modified, so every failure rolls back.

  enter(UpdateState::Swap);
  if (!effects.Swap()) return rollback("swap failed");

  enter(UpdateState::Register);
  if (!effects.RegisterInstall()) return rollback("registration failed");

  enter(UpdateState::Relaunch);
  const bool relaunched = effects.Relaunch();

  if (!relaunched) {
    // NOT a rollback. The files are the new version and they are consistent; undoing a good
    // install because it did not restart itself would be the worse outcome, and the user can
    // start it from the Start menu. Reported distinctly so it is visible rather than silent.
    // Committed too: this outcome does not roll back, so the way back is no longer needed and
    // leaving stale backups beside the install would be litter that a later rollback might trust.
    effects.Commit();
    effects.DiscardDownload();
    out.result = UpdateResult::UpdatedButNotRelaunched;
    out.detail = "installed, but the product did not come back up";
    return out;
  }

  enter(UpdateState::Health);
  if (!effects.HealthCheck()) return rollback("new build did not come up healthy");

  enter(UpdateState::Done);
  // Only here. Everything before this point could still have ended in a rollback.
  effects.Commit();
  effects.DiscardDownload();
  out.result = UpdateResult::Updated;
  out.detail = fields.version;
  return out;
}

}  // namespace remote60::native_poc::update
