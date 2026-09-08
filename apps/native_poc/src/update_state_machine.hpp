#pragma once

// The updater's state machine -- the order of the steps and what each failure does, with none of
// the machinery that actually kills processes or replaces files.
//
// That separation is deliberate and it is the whole point of this file. The decisions worth
// getting right are decisions about ORDER and about FAILURE: whether a swap is attempted after a
// quiesce that did not finish, whether a relaunch that fails should roll a good install back,
// whether an unreachable server can undo a working product. Those can all be settled and tested
// now, deterministically, against a seam -- and they should be, because once real process
// termination is attached the failure paths become nearly impossible to reproduce on demand.
//
// UpdateEffects is that seam. Production implementations of it do not exist yet: attaching them
// is a separate step (design 3.1/3.2), and doing it before the ordering is pinned would be
// building the dangerous half first.
//
// Design: docs/업데이트_기능_설계.md sections 3.1 (states), 3.3 (mutual exclusion), 3.6
// (completion conditions).

#include <cstdint>
#include <string>
#include <vector>

#include "update_manifest.hpp"

namespace remote60::native_poc::update {

/** The stages of one update attempt, in the order design 3.1 lays them out. */
enum class UpdateState {
  Idle,
  CheckRequested,   // mutual exclusion taken, manifest asked for
  Evaluate,         // signature checked, version compared
  Download,         // artifact fetched to staging
  Verify,           // size and hash checked against the manifest
  Prepare,          // "we are about to swap" handshake with the running product
  Quiesce,          // the product shuts down cleanly
  Swap,             // files replaced
  Register,         // service, firewall, shortcuts, DisplayVersion
  Relaunch,         // the product comes back
  Health,           // the new build is observed working
  Done,
  Rollback,
};

/** How an attempt ended. Deliberately distinguishes "nothing to do" from "something went wrong". */
enum class UpdateResult {
  // Nothing was attempted, and that is fine: another update holds the lock, the server could not
  // be reached, or the available version is not newer. None of these are failures.
  NothingToDo,
  // The update completed and the new build was observed healthy.
  Updated,
  // The new build is in place but did not come back up on its own. NOT rolled back -- the files
  // are the new version and the user can start it from the Start menu.
  UpdatedButNotRelaunched,
  // Stopped before anything on disk was touched. The install is untouched and still works, and
  // whatever left on our account has been started again.
  AbandonedBeforeSwap,
  /**
   * Nothing on disk was touched, and something that left is not back.
   *
   * The worst-looking outcome for its cause. Nothing went wrong with the update -- no file was
   * replaced, no registration changed -- and yet the machine may be unreachable, because the
   * product was asked to make way and then nobody asked it back. Harder to diagnose than a failed
   * update precisely because there is no damage to find.
   */
  AbandonedNotRelaunched,
  // The swap or what follows it failed and the previous version was restored, AND the product
  // that was running before is running again and reported itself healthy.
  RolledBack,
  /**
   * Restored and running, and it does not look right.
   *
   * The previous version is back on disk and started, but it did not report itself healthy --
   * which may mean the restore was incomplete, or that whatever broke the update also affects the
   * version being restored to. Not reported as a successful rollback, because "we put it back"
   * and "it works" are different claims and only the first one has been established.
   */
  RestoredButUnhealthy,
  /**
   * Restored, but nothing came back up.
   *
   * Separate from RolledBack because for a remote user it is the worse outcome of the two. The
   * files on disk are correct and the machine is unreachable: the host is one of the processes
   * that was stopped to do the swap, and if it does not return there is no way back in to fix
   * anything. "Restored" and "usable" are not the same claim.
   */
  RolledBackNotRelaunched,
  // The swap failed AND the rollback failed. The worst outcome, reported distinctly because it is
  // the only one where the install may be inconsistent and a human has to look.
  RollbackFailed,
};

/**
 * What a relaunch achieved, in the only terms the decision needs.
 *
 * A bool cannot express this. A client that did not come back is an inconvenience the user fixes
 * from the Start menu; a HOST that did not come back leaves a remote user with no way into the
 * machine at all, and the right response to those is not the same one. Folding both into false
 * meant every relaunch failure committed the update and dropped the backups -- including the one
 * case where the backups were the only way back.
 */
enum class RelaunchVerdict {
  /** Everything that was supposed to come back did. */
  AllBack,
  /**
   * Something came back but something optional did not -- in practice the client.
   *
   * Not a rollback. The files are the new version and they are consistent, and undoing a good
   * install because a window did not reopen would be the worse outcome.
   */
  OptionalMissing,
  /**
   * Something the machine needs did not come back: the host, or the input service.
   *
   * This is a rollback. Not because the files are wrong -- they are fine -- but because a machine
   * nobody can reach is worth less than an older version somebody can.
   */
  RequiredMissing,
};

/** Inline so that reading a verdict does not drag the whole state machine into a link. */
inline const char* relaunch_verdict_name(RelaunchVerdict verdict) {
  switch (verdict) {
    case RelaunchVerdict::AllBack: return "all-back";
    case RelaunchVerdict::OptionalMissing: return "optional-missing";
    case RelaunchVerdict::RequiredMissing: return "required-missing";
  }
  return "unknown";
}

/**
 * Everything the state machine needs the outside world to do.
 *
 * Every method returns a plain bool because the machine's decisions never depend on WHY a step
 * failed -- only on whether it did, and on where in the order it happened. Detail belongs in the
 * log the implementation writes, not in a code the machine would branch on.
 */
class UpdateEffects {
 public:
  virtual ~UpdateEffects() = default;

  /** Global mutual exclusion (design 3.3). False means another updater or the installer holds it. */
  virtual bool AcquireLock() = 0;
  virtual void ReleaseLock() = 0;

  /** Fetches the manifest document and its detached signature. False = could not reach it. */
  virtual bool FetchManifest(std::string* document, std::string* signatureHex) = 0;

  /** The version currently installed, as the uninstall registry records it. */
  virtual std::string InstalledVersion() = 0;

  /** Downloads the artifact into staging. False = did not complete. */
  virtual bool Download(const ManifestFields& fields) = 0;

  /** Size and content hash of what actually landed, checked against the manifest. */
  virtual bool VerifyDownload(const ManifestFields& fields) = 0;

  /** Removes staged bytes. Called on every path that abandons after downloading. */
  virtual void DiscardDownload() = 0;

  /** Tells the running product a swap is coming and waits for it to reach a safe point. */
  virtual bool PrepareForSwap() = 0;

  /** Ends the product cleanly -- every image in the payload, counted individually (design 3.2). */
  virtual bool Quiesce() = 0;

  /** Replaces the installed files. Must leave either all-old or all-new, never a mixture. */
  virtual bool Swap() = 0;

  /** Service registration, firewall rule, shortcuts, and DisplayVersion. */
  virtual bool RegisterInstall() = 0;

  /** Brings the product back in whatever configuration it was running in before. */
  virtual RelaunchVerdict Relaunch() = 0;

  /** Observes that the new build is actually working (design 3.6). */
  virtual bool HealthCheck() = 0;

  /** Puts the previous version back. */
  virtual bool Rollback() = 0;

  /**
   * The update succeeded and will not be rolled back.
   *
   * Until this is called the previous version has to remain restorable, which means the backups
   * a swap made must survive registration AND relaunch AND the health check -- every one of
   * those can still fail into a rollback. Dropping them any earlier turns a recoverable failure
   * into an unrecoverable one, which is how this method came to exist: a test rolled back after
   * a health failure and found nothing left to restore.
   */
  virtual void Commit() = 0;
};

struct UpdateOutcome {
  UpdateResult result = UpdateResult::NothingToDo;
  /** Every state entered, in order. Tests assert on this; the log prints it. */
  std::vector<UpdateState> visited;
  /** Short reason, for the log. Never carries the manifest or the signature. */
  std::string detail;

  bool entered(UpdateState s) const;
  UpdateState last() const;
};

/**
 * Runs one update attempt to completion.
 *
 * `platform` is matched against the manifest's platform field; `verifier` checks the detached
 * signature (production passes default_verifier(), tests pass their own).
 *
 * The rules this encodes, each of which has a test:
 *   - The lock is taken first, and failing to take it does nothing else at all. It is never
 *     waited on -- an update is deferrable, and two processes replacing the same directory is
 *     not a thing to risk for the sake of one cycle.
 *   - A manifest that cannot be fetched ends the attempt as NothingToDo. A server that is down
 *     must never be able to disturb a working installation.
 *   - The signature is checked before the version is compared -- enforced by load_manifest's
 *     types, not re-implemented here.
 *   - Prepare or Quiesce failing means the attempt is abandoned WITHOUT a swap and without
 *     escalating to force. The staged download is kept for the next attempt.
 *   - Once the swap begins, any failure through Health rolls back.
 *   - A failed Relaunch does NOT roll back. The files on disk are the new version and they are
 *     consistent; undoing a good install because it did not restart itself would be the worse
 *     outcome.
 */
/**
 * NOTE on the order of Quiesce and readiness.
 *
 * A caller may be waiting to exit on this update's behalf, and it is released as soon as the
 * download verifies -- before Quiesce enumerates anything. That means a process which left
 * BECAUSE of that release is already gone by the time targets are enumerated, so it cannot appear
 * in the list of things to bring back. Implementations that signal readiness must therefore
 * capture the target identities BEFORE they signal, not when Quiesce asks.
 *
 * The state machine cannot enforce that -- it does not know a signal exists -- so it is stated
 * here and the updater's decorator does it.
 */
UpdateOutcome run_update(UpdateEffects& effects,
                         const SignatureVerifier& verifier,
                         const std::string& platform);

/** Name for logs and test failure messages. */
const char* state_name(UpdateState s);
const char* result_name(UpdateResult r);

}  // namespace remote60::native_poc::update
