#include "update_effects.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>

#include <algorithm>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace remote60::native_poc::update {
namespace {

constexpr wchar_t kBackupSuffix[] = L".gnlink-old";
// How many set-aside copies of one backup name are tolerated before the swap gives up. Bounded so
// a recurring failure becomes visible instead of becoming an ever-growing pile.
constexpr int kMaxStaleBackups = 50;

std::string to_utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(need), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), need,
                      nullptr, nullptr);
  return out;
}

/** Artifact names are ASCII by contract (payload_name refuses anything else). */
std::wstring widen_name(const std::string& name) {
  std::wstring out;
  out.reserve(name.size());
  for (unsigned char c : name) out.push_back(static_cast<wchar_t>(c));
  return out;
}

/**
 * Creates every missing directory on the way to `path`'s parent, appending each one it actually
 * created to `created` so a rollback can undo exactly that much.
 *
 * The name behind `path` has already been through check_payload_names, which is what makes this
 * safe to do at all: the relative part cannot climb out of the install directory, name a drive,
 * or carry an alternate data stream. This creates directories under a checked relative path; it
 * does not decide what that path may be.
 */
bool ensure_parent_dirs(const std::wstring& root,
                        const std::wstring& relative,
                        std::vector<std::wstring>* created) {
  std::wstring current = root;
  size_t start = 0;
  for (size_t i = 0; i < relative.size(); ++i) {
    if (relative[i] != L'\\' && relative[i] != L'/') continue;
    const std::wstring component = relative.substr(start, i - start);
    start = i + 1;
    if (component.empty()) continue;
    current += L'\\';
    current += component;
    if (CreateDirectoryW(current.c_str(), nullptr)) {
      created->push_back(current);
      continue;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
  }
  return true;
}

bool file_exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

std::vector<std::string> UpdateEffectsConfig::unwired() const {
  // Every std::function on this struct, by name. The list is here rather than at a call site so
  // that adding a seam and forgetting to wire it is one edit away from being reported, instead of
  // depending on someone noticing at the point where it would have been used.
  std::vector<std::string> missing;
  if (!fetchManifest) missing.push_back("fetchManifest");
  if (!fetchArtifact) missing.push_back("fetchArtifact");
  if (!enumerateTargets) missing.push_back("enumerateTargets");
  if (!requestStop) missing.push_back("requestStop");
  if (!captureRegistration) missing.push_back("captureRegistration");
  if (!registerInstall) missing.push_back("registerInstall");
  if (!restoreRegistration) missing.push_back("restoreRegistration");
  if (!relaunchRequired) missing.push_back("relaunchRequired");
  if (!relaunchOptional) missing.push_back("relaunchOptional");
  if (!healthCheck) missing.push_back("healthCheck");
  if (!releaseBeforeRollback) missing.push_back("releaseBeforeRollback");
  return missing;
}

bool UpdateEffectsConfig::validate(std::string* detail) const {
  const auto fail = [detail](const char* why) {
    if (detail) *detail = why;
    return false;
  };
  // No field defaults to anything real, so every one of these is a genuine "the caller did not
  // say" rather than "the caller did not override".
  if (installDir.empty()) return fail("installDir not set");
  if (stagingDir.empty()) return fail("stagingDir not set");
  if (lockName.empty()) return fail("lockName not set");
  if (payloadNames.empty()) return fail("payloadNames is empty");
  {
    // Defence in depth: a payload name is manifest data on its way to becoming a path, and
    // the updater writes with administrator rights. Refused rather than sanitised.
    size_t bad = 0;
    PayloadNameVerdict verdict = PayloadNameVerdict::Ok;
    if (!check_payload_names(payloadNames, &bad, &verdict)) {
      if (detail) {
        *detail = std::string("payload name ") + std::to_string(bad) + " rejected: " +
                  payload_name_verdict_name(verdict);
      }
      return false;
    }
  }
  if (!fetchArtifact) return fail("fetchArtifact not set");
  if (!enumerateTargets) return fail("enumerateTargets not set");
  if (!requestStop) return fail("requestStop not set");
  if (!captureRegistration) return fail("captureRegistration not set");
  if (!registerInstall) return fail("registerInstall not set");
  if (!restoreRegistration) return fail("restoreRegistration not set");
  // Not required here, because a test may inject the document instead -- but the updater's own
  // options DO require the url that builds it, so the production path cannot reach run_update
  // without one. See updater_options.validate().
  if (!relaunchRequired) return fail("relaunchRequired not set");
  if (!relaunchOptional) return fail("relaunchOptional not set");
  if (!healthCheck) return fail("healthCheck not set");
  // Nothing reads these yet. Requiring them now means a future RegisterInstall cannot be
  // written against hardcoded HKLM and GNLinkSecureInput without this check failing first.
  if (registryRoot.empty()) return fail("registryRoot not set");
  if (serviceName.empty()) return fail("serviceName not set");

  // The updater replacing itself mid-swap would move the running executable aside and then fail
  // to put anything back. It lives outside installDir by design (3.2), but design is not a
  // guarantee -- a config that named its own image would sail straight into it, so this checks.
  if (!updaterImagePath.empty()) {
    // Compared as full DESTINATION PATHS, not as file names.
    //
    // It used to compare names, and that was too coarse in a way that mattered. The updater has
    // to be replaceable by an update -- otherwise it becomes a second permanent maintenance
    // binary frozen at its compile-time constants, which is the exact trap that ruled out
    // re-running the old installer for registration (history #440). The way it stays replaceable
    // is to run from a copy of itself outside installDir while the installed copy is swapped.
    // With a name comparison that is refused, even though the file being replaced is not the file
    // running. What actually has to be true is narrower: no destination of this swap may be the
    // image this process is executing.
    for (const std::wstring& name : payloadNames) {
      const std::wstring destination = installDir + L"\\" + name;
      if (_wcsicmp(destination.c_str(), updaterImagePath.c_str()) == 0) {
        return fail("a payload destination is the running updater");
      }
    }
    // And the updater's own file must not sit inside the directory being replaced.
    if (updaterImagePath.size() > installDir.size() &&
        _wcsnicmp(updaterImagePath.c_str(), installDir.c_str(), installDir.size()) == 0 &&
        (updaterImagePath[installDir.size()] == L'\\' ||
         updaterImagePath[installDir.size()] == L'/')) {
      return fail("the updater is inside the directory it would replace");
    }
  }

  // Staging inside the directory being replaced would make the updater's own working files part
  // of the swap. Design 3.2 puts it alongside instead.
  const bool stagingInsideInstall =
      stagingDir.size() > installDir.size() &&
      _wcsnicmp(stagingDir.c_str(), installDir.c_str(), installDir.size()) == 0 &&
      (stagingDir[installDir.size()] == L'\\' || stagingDir[installDir.size()] == L'/');
  if (stagingInsideInstall) return fail("stagingDir is inside installDir");
  return true;
}

WindowsUpdateEffects::WindowsUpdateEffects(UpdateEffectsConfig config)
    : config_(std::move(config)) {}

WindowsUpdateEffects::~WindowsUpdateEffects() { ReleaseLock(); }

std::wstring WindowsUpdateEffects::install_path(const std::wstring& name) const {
  return config_.installDir + L"\\" + name;
}
std::wstring WindowsUpdateEffects::backup_path(const std::wstring& name) const {
  return install_path(name) + kBackupSuffix;
}
std::wstring WindowsUpdateEffects::staged_artifact_path() const {
  return config_.stagingDir + L"\\artifact.staged";
}

std::wstring WindowsUpdateEffects::staging_dir_for(const std::string& releaseId) const {
  // Keyed by release. Two releases cannot occupy the same staging directory, so a half-downloaded
  // attempt at one cannot be finished with files from another.
  std::wstring wide;
  for (unsigned char c : releaseId) {
    // Release identities go into a path, so they get the same treatment every other name gets:
    // anything outside a conservative set becomes an underscore rather than being trusted.
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '.' || c == '_';
    wide.push_back(safe ? static_cast<wchar_t>(c) : L'_');
  }
  return config_.stagingDir + L"\\r-" + wide;
}

std::wstring WindowsUpdateEffects::staged_path_for(const std::string& releaseId,
                                                   const std::wstring& name) const {
  // The name has already been through check_payload_names by the time a verified manifest exists,
  // but it can contain a separator (ui\shell.html), so the leaf is flattened rather than creating
  // subdirectories in staging. The destination keeps the real relative path.
  std::wstring flat = name;
  for (wchar_t& c : flat) {
    if (c == L'\\' || c == L'/') c = L'_';
  }
  return staging_dir_for(releaseId) + L"\\" + flat;
}

void WindowsUpdateEffects::set_manifest(std::string document, std::string signatureHex) {
  manifestDocument_ = std::move(document);
  manifestSignatureHex_ = std::move(signatureHex);
}
void WindowsUpdateEffects::set_installed_version(std::string version) {
  installedVersion_ = std::move(version);
}

// ---------------------------------------------------------------- mutual exclusion

bool WindowsUpdateEffects::AcquireLock() {
  std::string why;
  if (!config_.validate(&why)) {
    lastError_ = "config invalid: " + why;
    return false;
  }
  if (lock_) return true;

  HANDLE h = CreateMutexW(nullptr, FALSE, config_.lockName.c_str());
  if (!h) {
    lastError_ = "could not create the update mutex";
    return false;
  }
  // Zero timeout: never wait. An update is deferrable; two processes replacing the same directory
  // is not something to risk for one cycle (design 3.3).
  const DWORD waited = WaitForSingleObject(h, 0);
  if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
    CloseHandle(h);
    lastError_ = "another update or installer holds the lock";
    return false;
  }
  lock_ = h;
  // Litter from a previous attempt, cleared now that the lock says nobody else is mid-update.
  //
  // The swap already copes with a backup sitting on a name it needs -- it deletes it, or renames
  // it to .gnlink-old.N and carries on -- so this is not what makes an update possible. What it
  // does is stop the leavings accumulating: once the process that was holding a file has finally
  // exited, nothing ever came back to remove it, so a directory that had one bad update kept the
  // evidence forever and the .N pile only ever grew.
  //
  // Best effort in the literal sense: every failure here is ignored apart from being counted. A
  // file that is still locked is still locked, and that is the swap's problem to report, not a
  // reason to refuse an update that has not started.
  SweepStaleBackups();
  return true;
}

void WindowsUpdateEffects::SweepStaleBackups() {
  size_t removed = 0;
  size_t stuck = 0;
  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring backup = backup_path(name);
    if (DeleteFileW(backup.c_str())) {
      ++removed;
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
      ++stuck;
    }
    // The set-aside copies the swap makes when a backup will not delete. Nothing else removes
    // these, so without this they are permanent.
    for (int n = 1; n <= kMaxStaleBackups; ++n) {
      const std::wstring aside = backup + L"." + std::to_wstring(n);
      if (GetFileAttributesW(aside.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
      if (DeleteFileW(aside.c_str())) {
        ++removed;
      } else {
        ++stuck;
      }
    }
  }
  if ((removed || stuck) && config_.trace) {
    config_.trace("stale backups: removed " + std::to_string(removed) + ", still held " +
                  std::to_string(stuck));
  }
}

void WindowsUpdateEffects::ReleaseLock() {
  if (!lock_) return;
  ReleaseMutex(static_cast<HANDLE>(lock_));
  CloseHandle(static_cast<HANDLE>(lock_));
  lock_ = nullptr;
}

// ---------------------------------------------------------------- manifest and version

bool WindowsUpdateEffects::FetchManifest(std::string* document, std::string* signatureHex) {
  // An injected document wins, so a test that supplies one never reaches the network. Production
  // supplies no document and a fetcher instead.
  if (manifestDocument_.empty() && config_.fetchManifest) {
    std::string fetched;
    std::string fetchedSignature;
    if (!config_.fetchManifest(&fetched, &fetchedSignature)) {
      lastError_ = "could not fetch the manifest";
      return false;
    }
    // Kept, so everything downstream in this attempt reads the same bytes that were fetched once.
    // Re-fetching per stage would let the server change what is being installed mid-update, which
    // is the same hazard releaseId exists for one level down.
    manifestDocument_ = std::move(fetched);
    manifestSignatureHex_ = std::move(fetchedSignature);
  }
  if (manifestDocument_.empty() || manifestSignatureHex_.empty()) {
    lastError_ = "no manifest available";
    return false;
  }
  *document = manifestDocument_;
  *signatureHex = manifestSignatureHex_;
  return true;
}

std::string WindowsUpdateEffects::InstalledVersion() { return installedVersion_; }

// ---------------------------------------------------------------- download and verification

bool WindowsUpdateEffects::Download(const ManifestFields& fields) {
  stagedReleaseId_.clear();
  stagedNames_.clear();

  if (fields.artifacts.empty()) {
    lastError_ = "manifest lists no artifacts";
    return false;
  }
  if (fields.releaseId.empty()) {
    lastError_ = "manifest has no release identity";
    return false;
  }

  if (!CreateDirectoryW(config_.stagingDir.c_str(), nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    lastError_ = "could not create the staging directory";
    return false;
  }
  const std::wstring dir = staging_dir_for(fields.releaseId);
  if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    lastError_ = "could not create the release staging directory";
    return false;
  }

  // Every file of ONE release, from the list a single verified manifest gave. Nothing here asks
  // what "latest" is between files: doing so is how an update ends up half one build and half
  // another.
  for (const ManifestArtifact& artifact : fields.artifacts) {
    const std::wstring dest = staged_path_for(fields.releaseId, widen_name(artifact.name));
    // Anything left from a previous attempt goes first, so a short write cannot be mistaken for a
    // complete one by whatever runs next.
    DeleteFileW(dest.c_str());
    if (!config_.fetchArtifact(artifact, dest)) {
      lastError_ = "fetch failed for " + artifact.name;
      // A partial release is not kept. Leaving some files behind would invite a later attempt to
      // treat them as already done.
      DiscardDownload();
      return false;
    }
    stagedNames_.push_back(widen_name(artifact.name));
  }
  stagedReleaseId_ = fields.releaseId;
  return true;
}

bool WindowsUpdateEffects::VerifyDownload(const ManifestFields& fields) {
  if (stagedReleaseId_.empty() || stagedReleaseId_ != fields.releaseId) {
    lastError_ = "nothing staged for this release";
    return false;
  }
  if (fields.artifacts.empty()) {
    lastError_ = "manifest lists no artifacts";
    return false;
  }

  // Everything this swap will need, checked here rather than at the swap itself.
  //
  // Swap already refuses when a payload file is missing from staging -- but by then the product
  // has been asked to stop and the user is looking at a closed application. 0.2.109 shipped a
  // manifest without GNLinkSetup.exe and that is exactly what happened: download, signature and
  // hashes all passed, and the release died at the swap with the host already going down. The
  // same answer is available here, before anything is asked to close.
  for (const std::wstring& name : config_.payloadNames) {
    bool named = false;
    for (const ManifestArtifact& artifact : fields.artifacts) {
      if (_wcsicmp(widen_name(artifact.name).c_str(), name.c_str()) == 0) {
        named = true;
        break;
      }
    }
    if (!named) {
      lastError_ = "the manifest does not name " + to_utf8(name) + ", which this update replaces";
      return false;
    }
  }

  // EVERY file, before the update is allowed anywhere near the install directory. One bad file
  // means the whole release is abandoned -- there is no such thing as updating most of it.
  for (const ManifestArtifact& artifact : fields.artifacts) {
    const std::wstring staged = staged_path_for(fields.releaseId, widen_name(artifact.name));

    uint64_t size = 0;
    if (!file_size_bytes(staged, &size)) {
      lastError_ = "staged " + artifact.name + " is not readable";
      return false;
    }
    // Size first: it is cheap, and a size mismatch means the hash is going to fail anyway.
    if (size != artifact.size) {
      lastError_ = "size mismatch for " + artifact.name;
      return false;
    }
    const std::string actual = sha256_file_hex(staged);
    if (actual.empty()) {
      lastError_ = "could not hash staged " + artifact.name;
      return false;
    }
    if (actual != artifact.sha256) {
      lastError_ = "sha256 mismatch for " + artifact.name;
      return false;
    }
  }

  // The version the artifacts claim, checked against what we were told to expect. Catches a
  // manifest paired with the wrong build -- a publishing mistake rather than an attack, since an
  // attacker choosing the bytes would have had to defeat the signature first.
  if (!config_.expectedVersion.empty()) {
    if (!fields.version.empty() && fields.version != config_.expectedVersion) {
      lastError_ = "manifest version " + fields.version + " does not match the expected " +
                   config_.expectedVersion;
      return false;
    }
    // Only the artifact that carries a version string is checked for it; the others are data.
    for (const ManifestArtifact& artifact : fields.artifacts) {
      if (artifact.name.find("Setup") == std::string::npos) continue;
      const std::wstring staged = staged_path_for(fields.releaseId, widen_name(artifact.name));
      if (!file_contains_utf16_version(staged, config_.expectedVersion)) {
        lastError_ = artifact.name + " does not carry version " + config_.expectedVersion;
        return false;
      }
    }
  }
  return true;
}

void WindowsUpdateEffects::DiscardDownload() {
  DeleteFileW(staged_artifact_path().c_str());
  if (stagedReleaseId_.empty()) return;
  const std::wstring dir = staging_dir_for(stagedReleaseId_);
  for (const std::wstring& name : stagedNames_) {
    DeleteFileW(staged_path_for(stagedReleaseId_, name).c_str());
  }
  RemoveDirectoryW(dir.c_str());
  stagedReleaseId_.clear();
  stagedNames_.clear();
}

// ---------------------------------------------------------------- stopping the product

bool WindowsUpdateEffects::PrepareForSwap() {
  // Asking is separate from waiting. If a target cannot even be asked to stop, the attempt is
  // abandoned here -- before anything on disk is touched -- rather than escalated.
  preparedTargets_ = config_.enumerateTargets();
  ownedChildPids_.clear();
  orphanPids_.clear();

  // Who can be asked, and who belongs to somebody who can.
  //
  // A process without a window cannot be asked directly. The only mechanism for one is a console
  // control event, and that needs a console the supervisor does not share with the children it
  // starts -- so for GNLinkStream and GNLinkCapture the request could never succeed, and an
  // update could not proceed while either was running. It was not a flaky path; it was a path
  // with no success case.
  //
  // So a windowless target that was started by a supervisor we are already asking is left to that
  // supervisor, the way closing an application leaves it to close its own children.
  //
  // Ownership is verified, never assumed: every link has to be one of these targets and has to
  // have started before the process it owns. Without that a standalone console process would be
  // quietly adopted by whichever GUI process happened to be in the list, and then nobody would
  // ask it to stop at all.
  //
  // The chain is followed, not just the first link. This used to stop at the immediate parent and
  // require IT to have a window, which could only ever recognise a two-tier product. Ours has
  // three: GNLinkHost owns a window and starts GNLinkStream, which has none and starts
  // GNLinkCapture, which has none either. So GNLinkCapture failed the check, was not an orphan
  // (its parent is alive), fell through to a direct request, and a windowless process cannot be
  // asked -- which abandoned the update. GDI capture mode made that reachable already; a preview
  // helper would make it common.
  //
  // What is NOT relaxed: each edge still needs a real parentPid match, a known creation time, and
  // a parent older than its child, and the walk only ever looks inside preparedTargets_. An
  // intermediate link that is missing, unknown, or a reused pid ends the walk at nullptr and the
  // process goes back to the conservative path it took before. "Windowless with a parent" is not
  // ownership; reaching a verified stop target that owns a window is.
  const auto owner_of = [this](const ProcessTarget& child) -> const ProcessTarget* {
    // Deep enough for any supervisor tree this product has, shallow enough that a malformed one
    // cannot turn into a long walk. Combined with `seen` below, termination does not depend on
    // the process table being well formed.
    constexpr size_t kMaxChainDepth = 8;
    std::vector<uint32_t> seen{child.pid};
    const ProcessTarget* current = &child;

    for (size_t depth = 0; depth < kMaxChainDepth; ++depth) {
      if (current->parentPid == 0 || current->creationTime == 0) return nullptr;

      const ProcessTarget* parent = nullptr;
      for (const ProcessTarget& candidate : preparedTargets_) {
        if (candidate.pid != current->parentPid) continue;
        parent = &candidate;
        break;
      }
      // A parent outside this list is not something we are stopping, so there is nobody to leave
      // this process to. Same answer as before.
      if (!parent) return nullptr;
      // A parent that started after its child is not the parent: the pid was reused.
      if (parent->creationTime == 0 || parent->creationTime >= current->creationTime) return nullptr;
      // A pid reached twice is a cycle in what should be a tree, which means the table is lying.
      if (std::find(seen.begin(), seen.end(), parent->pid) != seen.end()) return nullptr;
      seen.push_back(parent->pid);

      // Somebody who can actually be asked. Everything below it is left to them.
      if (parent->hasWindow) return parent;
      // Windowless: it will be left to ITS owner, so keep climbing to find out whether one exists.
      current = parent;
    }
    return nullptr;
  };

  // An orphan is a narrower thing than "its parent is not in this list", and getting that wrong
  // is how the first version of this broke every caller that describes its targets by hand: a
  // ProcessTarget with parentPid == 0 means "not known", and treating an unknown as a dead parent
  // routed processes into a wait that nothing had asked to stop.
  //
  // Unknown parentage is asked directly -- the conservative answer, and the one the field code
  // took before any of this. A parent that is ALIVE but not one of our targets is not an orphan
  // either: it has a supervisor, just not one we are stopping, so waiting for it would be waiting
  // for nobody. Only a parent that has actually exited leaves a process with nobody to ask.
  const auto parent_has_exited = [](uint32_t pid) {
    if (pid == 0) return false;  // not known, which is not the same as gone
    SetLastError(0);
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h) {
      const bool signalled = WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
      CloseHandle(h);
      return signalled;
    }
    // ERROR_INVALID_PARAMETER is a pid that is no longer a process. Anything else (access denied)
    // means the question could not be asked, and an unanswered question is not a yes.
    return GetLastError() == ERROR_INVALID_PARAMETER;
  };

  for (const ProcessTarget& target : preparedTargets_) {
    // It is running and could not be identified, so it cannot be asked and cannot be waited for.
    // Said here, where the reason is still known: without this the attempt still stops, but it
    // stops reporting "could not ask pid N to stop", which describes the symptom and hides that
    // the process was never something this updater could see in the first place.
    if (!target.identityKnown) {
      lastError_ = "cannot identify pid " + std::to_string(target.pid) + " (" +
                   to_utf8(target.imagePath) +
                   ") -- it is running and this updater cannot open it, so it can be neither asked "
                   "to stop nor waited for";
      if (config_.trace) config_.trace(lastError_);
      return false;
    }
    if (!target.hasWindow) {
      if (owner_of(target) != nullptr) {
        ownedChildPids_.push_back(target.pid);
        continue;
      }
      if (parent_has_exited(target.parentPid)) {
        // An orphan: no window, and the process that started it has exited. This is what the
        // field log recorded -- the host had already closed (the relaunch line proves it), leaving
        // a console child behind, and with the parent gone the ownership check could not pass. It
        // fell through to a direct request, which for a windowless process cannot succeed, and the
        // attempt was abandoned.
        //
        // There is nobody to ask, and asking it is the thing that does not work. So it is waited
        // for instead: a child whose supervisor has gone is usually already on its way out. If it
        // is not gone by the deadline the update is abandoned anyway -- which is exactly what
        // happens today, so this can only turn a certain failure into a possible success.
        orphanPids_.push_back(target.pid);
        continue;
      }
    }
    if (!config_.requestStop(target)) {
      // Asking can fail because the process is already leaving. That is the outcome this step
      // wants, not a reason to abandon the update.
      //
      // 2026-09-11, three attempts in a row: the host acknowledged the handoff and began
      // standing down, and ~70ms later the updater asked its window to close. The window was
      // already destroyed, the request failed, and the attempt ended as AbandonedBeforeSwap --
      // "could not ask pid 7332 to stop" about a process that was doing exactly what had just
      // been asked of it. The enumeration and the request are two moments and the target moved
      // between them.
      //
      // Only a target that is genuinely gone is forgiven. A live process that refused is still a
      // failure, and nothing here terminates anything.
      if (parent_has_exited(target.pid)) continue;

      // Gone is forgiven above. GOING is not, and going is what a handoff produces.
      //
      // Measured (remote60_update_stop_process_test): a process that has acknowledged and
      // destroyed its window is, for the tens of milliseconds before it exits, neither askable nor
      // gone. The ask falls through to GenerateConsoleCtrlEvent, which needs a console the updater
      // does not have, and returns false. On 2026-09-21 that false ended the attempt 89 ms after
      // the caller said it was standing down -- and the same shape has ended 23 attempts.
      //
      // So the question is asked again, briefly. Nothing is terminated and nothing new is
      // attempted: it waits for the process to finish what it already agreed to do, and abandons
      // only if it is still there afterwards. A process that genuinely refuses still fails, one
      // poll interval later than before.
      {
        const uint64_t deadline =
            static_cast<uint64_t>(GetTickCount64()) + config_.stopSettleMs;
        bool left = false;
        while (static_cast<uint64_t>(GetTickCount64()) < deadline) {
          Sleep(config_.stopSettlePollMs);
          if (parent_has_exited(target.pid)) { left = true; break; }
        }
        if (left) {
          if (config_.trace) {
            config_.trace("stop-settle pid=" + std::to_string(target.pid) +
                          " left while we waited");
          }
          continue;
        }
        if (config_.trace) {
          config_.trace("stop-settle pid=" + std::to_string(target.pid) +
                        " still running after " + std::to_string(config_.stopSettleMs) + "ms");
        }
      }
      lastError_ = "could not ask pid " + std::to_string(target.pid) + " to stop";
      return false;
    }
  }
  return true;
}

bool WindowsUpdateEffects::Quiesce() {
  // Waits for the exact processes that were asked to stop. No image-name sweep, no /T tree kill,
  // and no forced termination: a target that will not exit means the update does not happen
  // (design 3.2, and the state machine turns this into AbandonedBeforeSwap).
  //
  // The list is the one PrepareForSwap worked from, not a fresh enumeration. Enumerating again
  // meant waiting for whatever had started in between -- and, worse, silently not waiting for
  // something that had already been asked and was on its way out.
  const DWORD deadline = GetTickCount() + config_.quiesceTimeoutMs;
  const std::vector<ProcessTarget> fallback =
      preparedTargets_.empty() ? config_.enumerateTargets() : std::vector<ProcessTarget>{};
  const std::vector<ProcessTarget>& targets =
      preparedTargets_.empty() ? fallback : preparedTargets_;
  for (const ProcessTarget& target : targets) {
    // Running, and the enumerator could not identify it. There is nothing to wait on here -- that
    // needs a handle we were refused -- and "cannot tell" must not become "it exited". One line,
    // naming the pid and the image, because the alternative is this update going ahead over it and
    // leaving behind a stale .gnlink-old that nobody can explain.
    if (!target.identityKnown) {
      lastError_ = "cannot identify pid " + std::to_string(target.pid) + " (" +
                   to_utf8(target.imagePath) +
                   ") -- it is running and this updater cannot open it, so whether it still holds "
                   "the files this update replaces is unknown";
      if (config_.trace) config_.trace(lastError_);
      return false;
    }
    SetLastError(0);
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
    if (!h) {
      // Gone is not the same as "cannot look". ERROR_ACCESS_DENIED means the answer is unknown,
      // and an unknown treated as "it exited" is how an update proceeds over a process that is
      // still holding the files it is about to replace.
      if (GetLastError() == ERROR_ACCESS_DENIED) {
        lastError_ = "cannot tell whether pid " + std::to_string(target.pid) +
                     " exited (access denied)";
        return false;
      }
      // Anything else here means the pid is not a process any more, which is what we wanted.
      continue;
    }
    // The PID may have been reused between enumeration and now. If what is behind it is not the
    // process that was described, our target has already exited -- which is what we were waiting
    // for -- and waiting on the stranger would be waiting for the wrong thing.
    if (!process_identity_matches(h, target)) {
      CloseHandle(h);
      continue;
    }
    const DWORD now = GetTickCount();
    const DWORD remaining = (deadline > now) ? (deadline - now) : 0;
    const DWORD waited = WaitForSingleObject(h, remaining);
    CloseHandle(h);
    if (waited != WAIT_OBJECT_0) {
      // Two different failures, and they were one message. "Could not be asked" is a supervisor
      // that has no window; "outlived its parent" is a child whose supervisor was asked, went
      // away, and left it behind. Only the second says the supervisor contract is broken.
      const bool owned =
          std::find(ownedChildPids_.begin(), ownedChildPids_.end(), target.pid) !=
          ownedChildPids_.end();
      const bool orphan =
          std::find(orphanPids_.begin(), orphanPids_.end(), target.pid) != orphanPids_.end();
      // Three different situations that used to share one sentence. Which one it was decides
      // where to look: a supervisor that ignored the request, a child its supervisor left behind,
      // or a windowless process nobody was left to ask.
      if (owned) {
        lastError_ = "pid " + std::to_string(target.pid) + " (started by pid " +
                     std::to_string(target.parentPid) + ") outlived its parent";
      } else if (orphan) {
        lastError_ = "orphaned windowless pid " + std::to_string(target.pid) +
                     " did not exit (its parent " + std::to_string(target.parentPid) +
                     " was already gone, so there was nobody to ask)";
      } else {
        lastError_ = "pid " + std::to_string(target.pid) + " did not exit";
      }
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------- swap and rollback

/**
 * How many undeletable backups may be set aside before a swap gives up.
 *
 * Each one is a file that could not be removed, so letting them accumulate without limit would
 * hide a repeating failure behind a growing directory. Reaching the limit is reported as its own
 * cause rather than as the move-aside failure it produces.
 */

bool WindowsUpdateEffects::Swap() {
  movedAside_.clear();
  createdDirs_.clear();
  placed_.clear();
  swapped_ = false;

  // Nothing moves unless a whole release is staged. This is what makes "one attempt, one release"
  // structural rather than a convention: a Swap with nothing staged, or with a release other than
  // the one that was verified, simply does not happen.
  if (stagedReleaseId_.empty()) {
    lastError_ = "no verified release is staged";
    return false;
  }
  for (const std::wstring& name : config_.payloadNames) {
    if (GetFileAttributesW(staged_path_for(stagedReleaseId_, name).c_str()) ==
        INVALID_FILE_ATTRIBUTES) {
      lastError_ = "the staged release does not contain " + to_utf8(name);
      return false;
    }
  }

  // Before a single file moves. Capturing later would capture the state the update is creating,
  // and a rollback to that would leave the previous build's files under the new version's name.
  if (!config_.captureRegistration()) {
    lastError_ = "could not record the current registration";
    return false;
  }

  // Phase one: move every existing file aside. Nothing new is put in place yet, so a failure here
  // is undone by moving back exactly what was moved.
  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    if (!file_exists(live)) continue;  // a file that is not there does not need a backup
    const std::wstring backup = backup_path(name);
    // Why the name could not be freed, when it could not be. Empty in the ordinary case.
    std::string blocked;
    // A backup from an earlier update may still be sitting on this name. Normally it deletes and
    // the question does not arise; when it will not, the whole update used to stop here -- a file
    // that is only litter blocking the next release from being installed at all.
    //
    // So it is moved out of the name instead. RENAMING a file nothing can delete is allowed --
    // that asymmetry is the reason the swap works this way at all, since it renames images that
    // are running -- and it costs one more piece of litter to keep the update possible. The
    // survivor is recorded, not forgotten, for the same reason the commit records its own.
    if (!DeleteFileW(backup.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
      // Bounded on purpose. Every one of these is a file nobody could delete, so an unbounded
      // count would quietly turn a recurring failure into an accumulating pile -- the failure
      // should become visible, not become litter.
      bool moved = false;
      for (int n = 1; n <= kMaxStaleBackups; ++n) {
        const std::wstring aside = backup + L"." + std::to_wstring(n);
        if (GetFileAttributesW(aside.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        if (MoveFileExW(backup.c_str(), aside.c_str(), 0)) {
          moved = true;
          orphanedBackups_.push_back(name);
          lastError_ = "an older backup of " + to_utf8(name) +
                       " could not be deleted and was moved to " + to_utf8(aside);
        }
        break;
      }
      if (!moved) {
        // Carried to the failure below rather than written to lastError_ here, because the
        // move-aside about to fail would overwrite it -- leaving the operator the symptom ("the
        // file would not move") and none of the cause. That is exactly what happened the first
        // time this was written.
        blocked = " -- an older backup of the same name cannot be deleted and there is nowhere "
                  "left to put it (" + std::to_string(kMaxStaleBackups) + " already set aside)";
      }
    }
    if (!MoveFileExW(live.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      lastError_ = "could not move aside " + to_utf8(name) + " (error " +
                   std::to_string(GetLastError()) + ")" + blocked;
      (void)Rollback();
      return false;
    }
    movedAside_.push_back(name);
  }

  // Phase two: put the new files in, each from its own staged file. Every one of them was
  // verified before this function ran, and they all came from the same release.
  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    const std::wstring staged = staged_path_for(stagedReleaseId_, name);
    // A release may add a file in a folder this install does not have yet.
    if (!ensure_parent_dirs(config_.installDir, name, &createdDirs_)) {
      lastError_ = "could not create the destination folder for " + to_utf8(name);
      (void)Rollback();
      return false;
    }
    if (!CopyFileW(staged.c_str(), live.c_str(), FALSE)) {
      lastError_ = "could not place " + to_utf8(name);
      (void)Rollback();
      return false;
    }
    placed_.push_back(name);
  }

  swapped_ = true;
  return true;
}

namespace {

/**
 * Whether any running process is THIS executable -- the file at this path, not merely one with
 * the same name. Read-only: nothing is opened for write and no process is touched.
 */
bool image_is_running(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  const std::wstring leaf = slash == std::wstring::npos ? path : path.substr(slash + 1);
  if (leaf.empty()) return false;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  if (Process32FirstW(snap, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, leaf.c_str()) != 0) continue;
      // The name alone is not enough, and the release suite caught that: a fixture installation
      // whose payload is called GNLinkHost.exe was refused a rollback because the real
      // GNLinkHost.exe was running from %ProgramFiles%. In the field the same mistake blocks a
      // legitimate rollback whenever a stale copy runs from somewhere else.
      HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
      if (!proc) { found = true; break; }  // cannot look; assume it is the one
      wchar_t full[MAX_PATH * 2]{};
      DWORD size = static_cast<DWORD>(std::size(full));
      const bool readable = QueryFullProcessImageNameW(proc, 0, full, &size) != FALSE;
      CloseHandle(proc);
      // A matching name whose path cannot be read IS counted: the alternative is starting a
      // destructive restore on a guess.
      if (!readable || _wcsicmp(full, path.c_str()) == 0) { found = true; break; }
    } while (Process32NextW(snap, &entry));
  }
  CloseHandle(snap);
  return found;
}

/**
 * What stands between this file and its removal, asked without removing anything. (D2)
 *
 * Two questions, because one answer covers two different failures. The DELETE-access open is
 * refused by an ACL and granted by a running image -- measured, not assumed
 * (remote60_rollback_denial_probe) -- so the open separates permission from everything else. What
 * it cannot tell us is whether the unlink would then succeed, since a running image grants the
 * open and refuses only the unlink. That question is answered by asking whether anything is
 * running this executable, which is the condition the refusal actually means.
 *
 * Deliberately conservative: a name that matches a running process is reported as in the way even
 * if that process is running a different copy of it. Waiting a little longer is the cheap mistake;
 * starting a destructive restore is the expensive one.
 */
RemovalProbe probe_removal_for_real(const std::wstring& path) {
  RemovalProbe probe;
  probe.exists = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
  if (!probe.exists) return probe;

  SetLastError(0);
  HANDLE h = CreateFileW(path.c_str(), DELETE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  probe.openForDeleteOk = h != INVALID_HANDLE_VALUE;
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    probe.removeError = image_is_running(path) ? ERROR_ACCESS_DENIED : 0;
  } else {
    probe.removeError = GetLastError();
  }
  return probe;
}

}  // namespace

bool WindowsUpdateEffects::Rollback() {
  bool ok = true;
  const auto trace = [this](const std::string& text) {
    if (config_.trace) config_.trace("rollback-trace " + text);
  };
  trace("begin moved=" + std::to_string(movedAside_.size()) +
        " placed=" + std::to_string(placed_.size()) + " trigger=" + lastError_);
  const auto fileFailure = [&](const char* operation, const std::wstring& name, DWORD error) {
    const std::string detail = std::string(operation) + " file=" + to_utf8(name) +
                               " win32=" + std::to_string(error);
    trace(detail);
    lastError_ += "; rollback " + detail;
    ok = false;
  };
  // First, and before a single file moves. When the rollback is happening BECAUSE something this
  // attempt started is unhealthy, that something is running and holding the new files open --
  // and the restore would fail on precisely the files it exists to restore. Only what this
  // attempt started, identified by more than a pid; nothing else on the machine is ours to stop.
  if (config_.releaseBeforeRollback && !config_.releaseBeforeRollback()) {
    // Refused before a single file moves. Everything stays exactly as it is -- the backups, the
    // placed files, the registration -- because a partial restore is worse than none and leaves
    // nothing to try again from. The caller reports this as a rollback that did not happen.
    lastError_ = "not rolling back: something this attempt started could not be stopped";
    trace("release-before-rollback failed");
    return false;
  }
  trace("release-before-rollback ok");

  // Nothing is touched until everything that would be touched can be. (D2)
  //
  // The rollback used to delete and restore file by file, so a file held by something it had not
  // started -- a Host's child, an installed service -- stopped it halfway and left an installation
  // that was half one build and half the other. Neither half was wrong about its own file; the
  // mistake was starting.
  //
  // Waiting is only for a running image, which goes when its process does. A permission failure is
  // not waited on: it will not change, and every second spent on it is a second the product is
  // down. When the budget is spent this returns having moved NOTHING, so the backups and the
  // placed files are all still there to try again from -- which is the difference between a
  // rollback that did not happen and a recovery that half happened.
  if (!config_.probeRemoval) config_.probeRemoval = probe_removal_for_real;
  {
    const uint64_t startMs = static_cast<uint64_t>(GetTickCount64());
    QuiesceDecision decision = QuiesceDecision::WaitMore;
    std::string blocked;
    for (;;) {
      QuiesceInputs inputs;
      inputs.waitedMs = static_cast<uint64_t>(GetTickCount64()) - startMs;
      inputs.budgetMs = config_.rollbackQuiesceMs;
      blocked.clear();
      for (const std::wstring& name : config_.payloadNames) {
        const bool hasBackup =
            std::find(movedAside_.begin(), movedAside_.end(), name) != movedAside_.end();
        const bool wasPlaced = std::find(placed_.begin(), placed_.end(), name) != placed_.end();
        if (!hasBackup && !wasPlaced) continue;  // nothing here is this attempt's to undo
        const RemovalObstacle obstacle = classify_removal(config_.probeRemoval(install_path(name)));
        if (obstacle != RemovalObstacle::None) {
          if (!blocked.empty()) blocked += ", ";
          blocked += to_utf8(name) + "=" + removal_obstacle_name(obstacle);
        }
        inputs.obstacles.push_back(obstacle);
      }
      decision = quiesce_decide(inputs);
      if (decision != QuiesceDecision::WaitMore) break;
      Sleep(config_.rollbackQuiescePollMs);
    }
    trace(std::string("quiescence ") + quiesce_decision_name(decision) +
          (blocked.empty() ? "" : " blocked=" + blocked));
    if (decision != QuiesceDecision::Proceed) {
      lastError_ = std::string("not rolling back: ") + quiesce_decision_name(decision) +
                   (blocked.empty() ? "" : " (" + blocked + ")") +
                   " -- the installation is left as it is, with every backup still in place";
      trace("end ok=0 nothing-moved");
      return false;
    }
  }
  // Remove whatever was placed, then put back exactly the files that were moved aside. Files that
  // were never moved are left alone -- restoring something that was not backed up would be
  // inventing state.
  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    const std::wstring backup = backup_path(name);
    const bool hasBackup =
        std::find(movedAside_.begin(), movedAside_.end(), name) != movedAside_.end();
    if (!hasBackup) {
      // Nothing was moved aside for this name, so if something is there now, this attempt put it
      // there and it did not exist before. Removing it is what restores the installation.
      const bool wasPlaced = std::find(placed_.begin(), placed_.end(), name) != placed_.end();
      if (wasPlaced && file_exists(live) && !DeleteFileW(live.c_str())) {
        fileFailure("remove-new", name, GetLastError());
      }
      continue;
    }
    if (file_exists(live) && !DeleteFileW(live.c_str())) {
      fileFailure("remove-live", name, GetLastError());
      continue;
    }
    if (!MoveFileExW(backup.c_str(), live.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      fileFailure("restore-backup", name, GetLastError());
    } else {
      trace("restored file=" + to_utf8(name));
    }
  }
  movedAside_.clear();
  placed_.clear();
  swapped_ = false;

  // Folders this attempt created are removed deepest first, and only while empty -- a directory
  // that has something else in it was not this update's to take away.
  for (auto it = createdDirs_.rbegin(); it != createdDirs_.rend(); ++it) {
    RemoveDirectoryW(it->c_str());
  }
  createdDirs_.clear();

  // The registration goes back to what was captured, not to the version being abandoned.
  if (!config_.restoreRegistration()) {
    ok = false;
    lastError_ += "; rollback could not restore the registration";
    trace("restore-registration failed");
  }
  trace(std::string("end ok=") + (ok ? "1" : "0"));
  if (!ok && lastError_.empty()) lastError_ = "rollback could not restore every file";
  return ok;
}

bool WindowsUpdateEffects::RegisterInstall() {
  if (!config_.registerInstall()) {
    lastError_ = "registration failed";
    return false;
  }
  // NOT where the backups are dropped. Registration succeeding does not mean the update is
  // finished -- relaunch and the health check can still fail into a rollback, and that rollback
  // needs these files. They go in Commit().
  return true;
}

void WindowsUpdateEffects::Commit() {
  // The update is not going to be rolled back, so the way back is no longer needed.
  //
  // The result of each delete is READ. It was not, and the list was cleared regardless, so a
  // backup that could not be removed was forgotten twice over: the file stayed beside the
  // installation and nothing recorded that it had. That is the mechanism behind a stale
  // .gnlink-old nobody could explain.
  std::vector<std::wstring> stuck;
  std::vector<DWORD> reasons;
  for (const std::wstring& name : movedAside_) {
    const std::wstring backup = backup_path(name);
    // Not retried. It was, for a second, and it changed nothing -- the failure below is not the
    // transient "the image section has not been released yet" that a retry would clear. What the
    // second did do was slow every commit down and shift the timing of the scenarios around it,
    // which is a cost paid for no result.
    if (DeleteFileW(backup.c_str())) continue;
    const DWORD why = GetLastError();
    if (why == ERROR_FILE_NOT_FOUND) continue;  // already gone is the desired state
    stuck.push_back(name);
    reasons.push_back(why);
  }
  movedAside_.clear();
  orphanedBackups_ = stuck;
  if (!stuck.empty()) {
    std::string names;
    for (size_t i = 0; i < stuck.size(); ++i) {
      if (!names.empty()) names += ", ";
      // The error code, because "it did not delete" and "why it did not delete" are different
      // facts and only the second one can be acted on. 32 is a sharing violation -- something has
      // it open; 5 is access denied -- typically a process is RUNNING it.
      const DWORD attrs = GetFileAttributesW(backup_path(stuck[i]).c_str());
      names += to_utf8(stuck[i]) + " (error " + std::to_string(reasons[i]) + ", attrs " +
               std::to_string(attrs) + ")";
    }
    // Not a failure of the update -- the files on disk are correct -- but litter that a person
    // should be able to find out about, and that a later attempt will trip over when it tries to
    // move a file aside onto a name that is already taken.
    lastError_ = "backups left behind after commit: " + names;
  }
}

RelaunchVerdict WindowsUpdateEffects::RelaunchRequired() {
  const RelaunchVerdict verdict = config_.relaunchRequired();
  if (verdict != RelaunchVerdict::AllBack) {
    lastError_ = std::string("relaunch (required): ") + relaunch_verdict_name(verdict);
  }
  return verdict;
}

RelaunchVerdict WindowsUpdateEffects::RelaunchOptional() {
  const RelaunchVerdict verdict = config_.relaunchOptional();
  if (verdict != RelaunchVerdict::AllBack) {
    // Said separately so a log can tell "the machine did not come back" from "a window did not
    // reopen". They arrive at different points in the sequence and mean different things.
    lastError_ = std::string("relaunch (optional): ") + relaunch_verdict_name(verdict);
  }
  return verdict;
}

bool WindowsUpdateEffects::HealthCheck() {
  if (!config_.healthCheck()) {
    lastError_ = "health check failed";
    return false;
  }
  return true;
}

// Process identity lives in update_process_identity.cpp.
//
// It moved because update_relaunch.cpp needs to ask "is this still the process I captured?" and
// linking all of this file into that test to get one comparison would have dragged the whole file
// and registry layer in with it. What actually happened first was worse: rather than link it, the
// liveness check was left unwired.

// ---------------------------------------------------------------- file helpers

bool file_contains_utf16_version(const std::wstring& path, const std::string& version) {
  if (version.empty()) return false;
  // The needle is the version as UTF-16LE, which is how a wide string literal sits in a PE image.
  std::string needle;
  needle.reserve(version.size() * 2);
  for (char c : version) {
    needle.push_back(c);
    needle.push_back('\0');
  }

  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;

  // Read in chunks with an overlap, so a needle straddling a boundary is still found.
  const size_t chunk = 1024 * 1024;
  const size_t overlap = needle.size();
  std::string buffer;
  buffer.resize(chunk + overlap);
  size_t carried = 0;
  bool found = false;
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data() + carried, static_cast<DWORD>(chunk), &read, nullptr)) break;
    if (read == 0) break;
    const size_t have = carried + read;
    if (buffer.find(needle, 0) != std::string::npos && buffer.find(needle) < have) {
      found = true;
      break;
    }
    carried = (have >= overlap) ? overlap : have;
    if (carried > 0) {
      std::memmove(buffer.data(), buffer.data() + have - carried, carried);
    }
  }
  CloseHandle(file);
  return found;
}

bool file_size_bytes(const std::wstring& path, uint64_t* out) {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
  ULARGE_INTEGER size{};
  size.HighPart = data.nFileSizeHigh;
  size.LowPart = data.nFileSizeLow;
  *out = size.QuadPart;
  return true;
}

std::string sha256_file_hex(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};

  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string result;
  do {
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) break;
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) != STATUS_SUCCESS) break;

    std::vector<uint8_t> buffer(64 * 1024);
    bool readOk = true;
    for (;;) {
      DWORD read = 0;
      if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
        readOk = false;
        break;
      }
      if (read == 0) break;
      if (BCryptHashData(hash, buffer.data(), read, 0) != STATUS_SUCCESS) {
        readOk = false;
        break;
      }
    }
    if (!readOk) break;

    uint8_t digest[32]{};
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != STATUS_SUCCESS) break;

    static const char* kHex = "0123456789abcdef";
    result.reserve(64);
    for (uint8_t b : digest) {
      result.push_back(kHex[b >> 4]);
      result.push_back(kHex[b & 0x0F]);
    }
  } while (false);

  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(file);
  return result;
}

}  // namespace remote60::native_poc::update
