#pragma once

// The production side of UpdateEffects: the real lock, the real staging directory, the real file
// swap and rollback.
//
// Everything this touches is INJECTED. There is no default install path, no default list of
// process images, no default lock name -- a partially configured instance refuses to run rather
// than falling back to something real. That is not politeness toward tests; on the machine this
// was written on, GNLinkHost, GNLinkStream and GNLinkInputService were running while these tests
// executed, and a default that pointed at %ProgramFiles%\GNLink would have been a live session
// away from being replaced out from under someone.
//
// The strongest part of that isolation is not in this header. Finding processes by image name --
// the one operation that could reach a real GNLinkHost.exe -- lives in update_process_targets.cpp
// and is NOT linked into the test binary. The test cannot call it because it is not there.
//
// Design: docs/업데이트_기능_설계.md 3.1-3.6, especially 3.2 (no /T tree kill, exact targets,
// job lifetime, staging outside the directory being replaced).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "payload_name.hpp"
#include "update_rollback_safety.hpp"
#include "update_state_machine.hpp"

namespace remote60::native_poc::update {

/**
 * A process to stop, identified by more than its number.
 *
 * A PID alone is not an identity. Windows reuses them, and the window between enumerating a
 * process and waiting on it is long enough for the original to exit and something unrelated to
 * inherit the number -- at which point stopping "that PID" stops a stranger. So the image path
 * and the creation time are captured alongside it, and an opened handle is checked against both
 * before it is treated as the process that was meant.
 *
 * A mismatch is not an error. It means the target is already gone, which is exactly the outcome
 * Quiesce was waiting for.
 */
struct ProcessTarget {
  uint32_t pid = 0;
  std::wstring imagePath;    // full path as observed at enumeration time
  uint64_t creationTime = 0; // FILETIME as a single value; 0 when it could not be read
  // Who started it, and whether it can be asked to close the way a person would close it.
  //
  // Both exist so that a windowless process can be routed to its supervisor instead of being
  // asked directly. Asking directly is what could not work: the only mechanism for a process
  // without a window is a console control event, and that needs a shared console the product's
  // own supervisor does not give it. 0 / false mean "not known", and a target that is not known
  // to be owned is asked directly rather than assumed to belong to someone.
  uint32_t parentPid = 0;
  bool hasWindow = false;

  /**
   * False for a process that IS running and could not be identified.
   *
   * There are two ways to fail to identify a process and they are not the same answer. One is that
   * the pid is no longer a process -- it exited between the snapshot and the open -- and that is
   * exactly what an update wants. The other is that it is there and this updater cannot look at
   * it, and treating that as "gone" is how an update proceeds over a process still holding the
   * files it is about to replace.
   *
   * The enumerator used to drop both on the floor, so the second case never reached the two places
   * that handle it correctly (request_process_stop and Quiesce both refuse on access-denied). A
   * target carrying this flag is carried forward specifically so the swap can refuse.
   */
  bool identityKnown = true;

  // Identity only. parentPid and hasWindow describe the moment, not the process: the same process
  // is the same process whether or not it had opened a window yet.
  bool operator==(const ProcessTarget& other) const {
    return pid == other.pid && creationTime == other.creationTime && imagePath == other.imagePath;
  }
};

/**
 * What a failed OpenProcess means -- the ONE place that decides it.
 *
 * Five places used to answer this question and three of them answered it differently. The shape
 * that kept coming back was "anything that is not access denied means the process is gone", which
 * reads as a safe default and is the opposite: an unfamiliar error then removed a RUNNING process
 * from the target list, or reported a stop that never happened, and the swap proceeded over
 * something still holding its files.
 *
 * Exactly one error says the pid is not a process. Every other error -- including ones nobody has
 * seen yet -- is a question that did not get answered, and an unanswered question is never an
 * exit.
 */
enum class OpenFailure : uint8_t {
  /** ERROR_INVALID_PARAMETER, and nothing else: there is no process with that id. */
  NotAProcess = 0,
  /** The question could not be asked. Access denied, and everything else. */
  Unknown,
};

/**
 * Classifies a Win32 error from OpenProcess.
 *
 * Deliberately a pure function of the error code, so the whole mapping can be checked as a table
 * rather than only at the two codes an ordinary session can produce on demand (5 and 87).
 */
OpenFailure classify_open_error(uint32_t win32Error);

/** Name for logs and test failure messages. */
const char* open_failure_name(OpenFailure f);

/** Why an identity could not be captured. The distinction decides whether an update may proceed. */
enum class IdentityFailure {
  None = 0,
  /** The pid is not a process any more. It exited, which is what stopping it was for. */
  Gone,
  /** It is running and cannot be identified -- access denied, or its times could not be read. */
  Unknowable,
};

/**
 * Reads a live process's identity. Returns false when it cannot be opened or has already exited.
 *
 * Used both by the production enumerator and by tests to describe the dummies they started, so
 * both sides agree on what "the same process" means.
 *
 * `why` (optional) says which kind of failure it was, because "it exited" and "it is there and I
 * cannot see it" lead to opposite decisions about replacing files.
 */
bool capture_process_identity(uint32_t pid, ProcessTarget* out, IdentityFailure* why = nullptr);

/**
 * True when the process behind `handle` is still the one `target` described.
 *
 * False means the PID was reused or the process is gone -- either way, not our target.
 */
/**
 * Whether the process behind a handle is the one that was described.
 *
 * Three answers, not two. A query that FAILS -- no creation time, no image path -- is not evidence
 * that the handle belongs to somebody else, and the bool this replaced could not say so: it
 * returned false for "different process" and for "could not tell" alike, and every caller read
 * false as "our target has already exited". On a machine where the query fails, that turns an
 * unanswered question into permission to proceed over a process that is still running.
 */
enum class IdentityMatch : uint8_t {
  Same = 0,
  Different,  // the handle is somebody else's -- ours has gone
  Unknown,    // the question could not be answered; nothing may be concluded from it
};

const char* identity_match_name(IdentityMatch m);

IdentityMatch process_identity_check(void* handle, const ProcessTarget& target);

/** Same/Different only. Kept for callers that already treat Unknown as a refusal. */
bool process_identity_matches(void* handle, const ProcessTarget& target);

/**
 * Everything the effects need, with nothing supplied by default.
 *
 * `validate()` is what makes the injection structural: an instance missing any required field
 * cannot run at all, so there is no path where an unset install directory quietly becomes the
 * real one.
 */
struct UpdateEffectsConfig {
  // Optional diagnostics. Receives operation/file/error metadata, never credentials.
  std::function<void(const std::string&)> trace;
  /** Where the product lives. Replaced in place, so its path must stay stable (design 3.1 S1). */
  std::wstring installDir;
  /** Where the download is staged. Must be OUTSIDE installDir -- see design 3.2. */
  std::wstring stagingDir;
  /**
   * File names, relative to installDir, that a swap replaces.
   *
   * These come from a manifest, which means they are data being turned into paths. Every
   * one is checked by check_payload_names() before anything runs -- traversal, absolute
   * paths, drive letters, alternate data streams, reserved device names and duplicates are
   * all refused. The signature check is the first lock on that door; this is the second,
   * and it does not assume the first one held.
   */
  std::vector<std::wstring> payloadNames;
  /** Named mutex for mutual exclusion. `Global\` prefixed in production (design 3.3). */
  std::wstring lockName;

  /**
   * Writes one artifact to `destPath`. Injected so no test ever opens a socket.
   *
   * Called once per artifact. The updater does not ask what "latest" is between calls -- it works
   * from the list a single verified manifest gave it, which is what stops a release changing
   * underneath an update in progress.
   */
  std::function<bool(const ManifestArtifact& artifact, const std::wstring& destPath)> fetchArtifact;

  /**
   * Fetches the manifest and its detached signature. Required unless one was injected.
   *
   * This was missing, and its absence was not visible from anywhere: FetchManifest returned the
   * document set_manifest had been given, set_manifest had no production caller, and so the
   * updater ended every run at its first step reporting "no manifest available". Every later
   * stage was correct and unreachable. Making it a config field means a production instance that
   * neither injects a document nor supplies a fetcher fails validate() instead of failing
   * silently at run time.
   */
  std::function<bool(std::string* document, std::string* signatureHex)> fetchManifest;

  /**
   * The processes to stop, each with a full identity.
   *
   * Injected rather than discovered here, and that is the point: a test supplies dummies it
   * started itself. The production enumerator that finds them by image name is in a separate
   * translation unit the test does not link.
   */
  std::function<std::vector<ProcessTarget>()> enumerateTargets;

  /** Asks one process to exit cleanly. False means it could not even be asked. */
  std::function<bool(const ProcessTarget& target)> requestStop;

  /**
   * The registry root and service name a registration would touch.
   *
   * There is no production RegisterInstall yet, so nothing reads these -- they exist now so that
   * when one is written it has to be handed a root and a service name rather than reaching for
   * HKLM\...\Uninstall\GNLink and GNLinkSecureInput on its own. A test that supplies scratch
   * values cannot be made to disturb the real installation later.
   */
  std::wstring registryRoot;
  std::wstring serviceName;

  /**
   * Records what is registered now, so a rollback can put it back.
   *
   * Called before the swap. Separate from registerInstall because the two have to happen at
   * different moments -- capturing after the swap would capture the new state, and rolling back
   * to that would leave old files claiming to be the new version.
   */
  std::function<bool()> captureRegistration;

  /** Service registration, firewall, shortcuts, DisplayVersion. Injected. */
  std::function<bool()> registerInstall;

  /** Puts the captured registration back. Called during rollback, never with the new values. */
  std::function<bool()> restoreRegistration;

  /**
   * Brings the product back in the configuration it was running in, and says what it managed.
   *
   * A verdict rather than a bool: a missing client and a missing host call for different
   * responses, and folding them together committed the update in the one case where the backups
   * were the only way back.
   */
  std::function<RelaunchVerdict()> relaunchRequired;
  /**
   * The optional images, run only after the commit.
   *
   * Its own seam because the two happen at different points in the sequence, and the whole reason
   * for the split is when rather than how -- see UpdateEffects::RelaunchOptional.
   */
  std::function<RelaunchVerdict()> relaunchOptional;

  /** Observes that the new build works (design 3.6). */
  std::function<bool()> healthCheck;

  /**
   * Stops what this attempt started, before a rollback touches the files they hold open.
   *
   * Rollback used to begin with DeleteFile and MoveFile, which is the wrong first step when the
   * reason for rolling back is that a process this attempt just launched is unhealthy: it is
   * running, it has the new files open, and the restore fails on exactly the files that matter.
   * Returns how many it stopped, for the log.
   *
   * Returns false when something it started could not be stopped -- and a rollback then does NOT
   * begin. Restoring files while something may still hold them leaves a half-restored
   * installation, and the reason reads as the rollback's fault rather than as this.
   *
   * Optional. Absent means nothing was started, which is true on every path that rolls back
   * before Relaunch.
   */
  std::function<bool()> releaseBeforeRollback;

  /**
   * Whether one file could be removed right now, and if not, what is in the way. (D2)
   *
   * A seam because the honest answer involves a running process and a file system, and a test that
   * needed both would be testing Windows. Left empty, the real one is used.
   */
  std::function<RemovalProbe(const std::wstring& path)> probeRemoval;

  /** How long a rollback may wait for what this attempt did not start to go quiet. */
  /**
   * How long PrepareForSwap waits for a target that could not be asked but may be leaving anyway.
   *
   * A process that has acknowledged a handoff destroys its window and then takes a moment to go.
   * In that moment it cannot be asked and is not yet gone, and abandoning there is what ended 23
   * attempts. Nothing is terminated; this only waits for what was already agreed.
   */
  uint32_t stopSettleMs = 3000;
  uint32_t stopSettlePollMs = 100;

  uint32_t rollbackQuiesceMs = 10000;
  uint32_t rollbackQuiescePollMs = 250;

  /**
   * The version the manifest claims. Checked against what the staged artifact carries.
   *
   * Empty disables the check, which is what the older tests do; production sets it from a
   * verified manifest.
   */
  std::string expectedVersion;

  /**
   * Full path of the running updater, so it can refuse to replace itself.
   *
   * The updater lives outside installDir by design (3.2), but "by design" is not a guarantee --
   * a mistaken config could name its own image among the payload and the swap would move the
   * running executable aside mid-update. Supplying this makes validate() check rather than trust.
   */
  std::wstring updaterImagePath;

  /** How long Quiesce waits for the targets to go away before giving up. Never forces. */
  uint32_t quiesceTimeoutMs = 15000;

  /**
   * The names of every injected seam that nothing has filled in.
   *
   * Exists because of how the defects in this layer were found -- one at a time, each after a
   * test had set a seam that production never set. A seam nobody wires is not a missing feature
   * that shows up as a failure; it is a step that silently does not happen, which is why the
   * manifest was never fetched and the liveness check never ran. Enumerating them turns "did
   * anyone remember" into a question a caller can actually ask.
   */
  std::vector<std::string> unwired() const;

  /** False when anything required is missing. Reason goes to `detail` when given. */
  bool validate(std::string* detail = nullptr) const;
};

/**
 * UpdateEffects backed by the real filesystem, a real named mutex, and injected everything else.
 *
 * THE FILE SWAP is all-or-nothing: every file is moved aside to a `.gnlink-old` sibling first,
 * and if any single move fails the ones already done are put back before returning. That is what
 * closes ledger item I01 -- the installer's current behaviour overwrites files one at a time and
 * leaves a mixture behind when one is locked.
 *
 * The UPDATE as a whole is not atomic and cannot be made so here. Replacing several files and
 * then registering four separate system facts -- a service, a firewall rule, two shortcuts, a
 * registry key -- is a sequence, not a transaction. What it has instead is a recoverable-step
 * contract: each step reports whether it happened, the file swap never leaves a mixture, and a
 * failure after the swap rolls the files AND the registration back to what was captured before
 * it started. Read any claim about "atomicity" in this code as scoped to the file swap.
 */
/**
 * One handle per stop target, opened and identity-checked BEFORE anything is asked to stop, held
 * until the attempt is over.
 *
 * This exists so that no question about a target is ever asked by pid a second time. A pid is
 * reused; re-opening one and finding it absent, or finding something there, says nothing reliable
 * about the process that was enumerated. A handle opened while the process is still known, and
 * held, cannot be recycled -- so every later question (did the request land, did it exit) is about
 * one process and no other.
 *
 * Closing is the destructor's, not any particular return path's. An earlier version closed these
 * at the end of a loop and leaked every handle opened before an early return.
 *
 * It sits at namespace scope, rather than inside WindowsUpdateEffects where it is used, for one
 * reason: the move assignment below is hand-written, it has already been wrong once -- it dropped
 * a flag -- and a hand-written move that cannot be constructed in a test cannot be checked for
 * that. The counter-examples are in update_identity_match_test.cpp.
 */
struct TargetWatch {
  ProcessTarget target;
  void* handle = nullptr;         // SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, or null
  bool gone = false;              // the pid was already not a process when we looked
  bool requestDelivered = false;  // requestStop said yes
  // requestStop was called AND said no. Distinct from "never asked": a windowless child routed to
  // its supervisor, or an orphan routed to the quiesce wait, is not asked at all, and the settle
  // must not wait for those -- they have their own handling, and waiting for them turned every
  // orphan into an abandoned attempt.
  bool requestFailed = false;
  TargetWatch() = default;
  TargetWatch(const TargetWatch&) = delete;
  TargetWatch& operator=(const TargetWatch&) = delete;
  TargetWatch(TargetWatch&& other) noexcept { *this = std::move(other); }
  /**
   * Moves every field, closes whatever this one was holding, and leaves the source holding
   * nothing.
   *
   * Every flag has to travel. The one that did not -- requestFailed -- meant a watch that had been
   * asked and refused would arrive at the settle looking as though it had never been asked.
   */
  TargetWatch& operator=(TargetWatch&& other) noexcept;
  ~TargetWatch();
};

class WindowsUpdateEffects : public UpdateEffects {
 public:
  explicit WindowsUpdateEffects(UpdateEffectsConfig config);
  ~WindowsUpdateEffects() override;

  bool AcquireLock() override;
  void ReleaseLock() override;
  bool FetchManifest(std::string* document, std::string* signatureHex) override;
  std::string InstalledVersion() override;
  bool Download(const ManifestFields& fields) override;
  bool VerifyDownload(const ManifestFields& fields) override;
  void DiscardDownload() override;
  bool PrepareForSwap() override;
  bool Quiesce() override;
  bool Swap() override;
  bool RegisterInstall() override;
  RelaunchVerdict RelaunchRequired() override;
  RelaunchVerdict RelaunchOptional() override;
  bool HealthCheck() override;
  bool Rollback() override;
  void Commit() override;

  /** Manifest bytes, injected rather than fetched, so tests never reach the network. */
  void set_manifest(std::string document, std::string signatureHex);
  /** What InstalledVersion() reports. Production reads it from the uninstall key. */
  void set_installed_version(std::string version);

  /** Last failure reason, for the log and for test messages. */
  const std::string& last_error() const { return lastError_; }
  /** Backups that survived a commit, if any. Empty is the normal case. */
  const std::vector<std::wstring>& orphaned_backups() const { return orphanedBackups_; }

 private:
  std::wstring staged_artifact_path() const;
  /** Where one artifact of a release is staged. Keyed by release, so two cannot be mixed. */
  std::wstring staged_path_for(const std::string& releaseId, const std::wstring& name) const;
  std::wstring staging_dir_for(const std::string& releaseId) const;
  std::wstring install_path(const std::wstring& name) const;
  std::wstring backup_path(const std::wstring& name) const;
  /**
   * Deletes backups a previous attempt left behind, and the copies the swap set aside when it
   * could not delete one. Best effort: failures are counted and logged, never fatal.
   *
   * Run once the lock is held, because that is the moment nothing else is mid-update. The swap can
   * already work around litter on a name it needs; what was missing was anything that ever cleared
   * it once the process holding it had finally gone.
   */
  void SweepStaleBackups();

  UpdateEffectsConfig config_;
  void* lock_ = nullptr;  // HANDLE
  std::string manifestDocument_;
  std::string manifestSignatureHex_;
  std::string installedVersion_;
  std::string lastError_;
  /** Names moved aside during the current swap, so a partial failure can be undone exactly. */
  std::vector<std::wstring> movedAside_;
  /**
   * Names this swap actually placed.
   *
   * Not the same list as movedAside_, and the difference is the point: a release that ADDS a file
   * places something that has no backup, so "put back what was moved aside" would leave it there.
   * A rollback has to remove those too, or an abandoned update still changes the installation.
   */
  std::vector<std::wstring> placed_;
  /**
   * Backups that could not be deleted at commit time.
   *
   * Recorded because they used to vanish: the delete result was ignored and the list cleared, so
   * a file that stayed beside the installation left no trace of itself anywhere.
   */
  std::vector<std::wstring> orphanedBackups_;
  /**
   * Directories this swap created under installDir, deepest last.
   *
   * A payload name may name a destination in a subdirectory (`ui\\shell.html`), and a release
   * that adds a file under a folder that does not exist yet has to be able to place it. Rollback
   * removes these again, so an abandoned update does not leave folders behind that the previous
   * build never had.
   */
  std::vector<std::wstring> createdDirs_;
  /**
   * The release currently staged, and the files staged for it.
   *
   * Set by Download and read by Swap. A Swap that finds a different release than the one that was
   * verified refuses -- that is the mechanism that keeps one attempt to one release.
   */
  std::string stagedReleaseId_;
  std::vector<std::wstring> stagedNames_;
  // The exact processes PrepareForSwap asked about, kept so that Quiesce waits for those and not
  // for whatever a second enumeration happens to find. Re-enumerating meant a process that
  // started in between became something to wait for, and one that had already gone silently
  // stopped being checked at all.
  std::vector<ProcessTarget> preparedTargets_;
  std::vector<uint32_t> ownedChildPids_;

  std::vector<TargetWatch> watches_;

  /**
   * One deadline for the whole stop phase, in 64-bit milliseconds.
   *
   * It starts when PrepareForSwap begins, and every wait after it -- the settle for requests that
   * could not be delivered, the Quiesce wait for the ones that could -- is measured against it, so
   * time already spent is subtracted from the time those waits have left. Separate budgets meant
   * the total grew with the number of targets and with how the failure was distributed between the
   * stages; a bound that moves is not a bound.
   *
   * It bounds the WAITING, not the phase. A synchronous call in progress is never interrupted by
   * it -- an SCM round trip, an EnumWindows, an OpenProcess return when they return -- so the sum
   * of the two budgets is not a wall-clock guarantee and nothing should be sized as though it
   * were. 32-bit GetTickCount is not used anywhere on this path: it wraps every 49 days and the
   * comparison then reads backwards.
   */
  uint64_t stopDeadlineMs_ = 0;
  // Windowless targets with nobody to ask: their parent is not among the targets, which means it
  // has already exited. Waited for rather than asked, and told apart from the owned ones so the
  // log says which situation it was.
  std::vector<uint32_t> orphanPids_;
  bool swapped_ = false;
};

/** SHA-256 of a file, lowercase hex. Empty on failure. */
std::string sha256_file_hex(const std::wstring& path);

/**
 * Whether `version` appears inside `path` as a UTF-16 literal.
 *
 * This is how the project already checks that a built installer carries the version it claims
 * (history #426), and it is used here to catch the realistic mismatch: a manifest paired with the
 * wrong artifact. GNLinkSetup.exe has no VERSIONINFO resource to read instead.
 *
 * Be clear about what it does NOT establish. Finding the string proves the binary mentions that
 * version, not that it IS that version -- a binary could contain several. It is a consistency
 * check between two things that should agree, not an identity proof, and the artifact hash from
 * the manifest is what actually pins which bytes arrived.
 */
bool file_contains_utf16_version(const std::wstring& path, const std::string& version);

/** Size in bytes, or false when the file cannot be read. */
bool file_size_bytes(const std::wstring& path, uint64_t* out);

}  // namespace remote60::native_poc::update
