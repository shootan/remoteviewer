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

  bool operator==(const ProcessTarget& other) const {
    return pid == other.pid && creationTime == other.creationTime && imagePath == other.imagePath;
  }
};

/**
 * Reads a live process's identity. Returns false when it cannot be opened or has already exited.
 *
 * Used both by the production enumerator and by tests to describe the dummies they started, so
 * both sides agree on what "the same process" means.
 */
bool capture_process_identity(uint32_t pid, ProcessTarget* out);

/**
 * True when the process behind `handle` is still the one `target` described.
 *
 * False means the PID was reused or the process is gone -- either way, not our target.
 */
bool process_identity_matches(void* handle, const ProcessTarget& target);

/**
 * Everything the effects need, with nothing supplied by default.
 *
 * `validate()` is what makes the injection structural: an instance missing any required field
 * cannot run at all, so there is no path where an unset install directory quietly becomes the
 * real one.
 */
struct UpdateEffectsConfig {
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

  /** Writes the artifact to `destPath`. Injected so no test ever opens a socket. */
  std::function<bool(const ManifestFields& fields, const std::wstring& destPath)> fetchArtifact;

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

  /** Service registration, firewall, shortcuts, DisplayVersion. Injected. */
  std::function<bool()> registerInstall;

  /** Brings the product back in the configuration it was running in. */
  std::function<bool()> relaunch;

  /** Observes that the new build works (design 3.6). */
  std::function<bool()> healthCheck;

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
  bool Relaunch() override;
  bool HealthCheck() override;
  bool Rollback() override;

  /** Manifest bytes, injected rather than fetched, so tests never reach the network. */
  void set_manifest(std::string document, std::string signatureHex);
  /** What InstalledVersion() reports. Production reads it from the uninstall key. */
  void set_installed_version(std::string version);

  /** Last failure reason, for the log and for test messages. */
  const std::string& last_error() const { return lastError_; }

 private:
  std::wstring staged_artifact_path() const;
  std::wstring install_path(const std::wstring& name) const;
  std::wstring backup_path(const std::wstring& name) const;

  UpdateEffectsConfig config_;
  void* lock_ = nullptr;  // HANDLE
  std::string manifestDocument_;
  std::string manifestSignatureHex_;
  std::string installedVersion_;
  std::string lastError_;
  /** Names moved aside during the current swap, so a partial failure can be undone exactly. */
  std::vector<std::wstring> movedAside_;
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
