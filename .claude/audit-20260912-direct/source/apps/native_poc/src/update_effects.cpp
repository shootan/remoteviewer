#include "update_effects.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace remote60::native_poc::update {
namespace {

constexpr wchar_t kBackupSuffix[] = L".gnlink-old";

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
  return true;
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
  // Ownership is verified, never assumed: the parent has to be one of these targets, have a
  // window of its own, and have started before the child. Without all three a standalone console
  // process would be quietly adopted by whichever GUI process happened to be in the list, and
  // then nobody would ask it to stop at all.
  const auto owner_of = [this](const ProcessTarget& child) -> const ProcessTarget* {
    if (child.parentPid == 0 || child.creationTime == 0) return nullptr;
    for (const ProcessTarget& parent : preparedTargets_) {
      if (parent.pid != child.parentPid) continue;
      if (!parent.hasWindow) return nullptr;
      // A parent that started after its child is not the parent: the pid was reused.
      if (parent.creationTime == 0 || parent.creationTime >= child.creationTime) return nullptr;
      return &parent;
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
constexpr int kMaxStaleBackups = 50;

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

bool WindowsUpdateEffects::Rollback() {
  bool ok = true;
  // First, and before a single file moves. When the rollback is happening BECAUSE something this
  // attempt started is unhealthy, that something is running and holding the new files open --
  // and the restore would fail on precisely the files it exists to restore. Only what this
  // attempt started, identified by more than a pid; nothing else on the machine is ours to stop.
  if (config_.releaseBeforeRollback && !config_.releaseBeforeRollback()) {
    // Refused before a single file moves. Everything stays exactly as it is -- the backups, the
    // placed files, the registration -- because a partial restore is worse than none and leaves
    // nothing to try again from. The caller reports this as a rollback that did not happen.
    lastError_ = "not rolling back: something this attempt started could not be stopped";
    return false;
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
      if (wasPlaced && file_exists(live) && !DeleteFileW(live.c_str())) ok = false;
      continue;
    }
    if (file_exists(live) && !DeleteFileW(live.c_str())) {
      ok = false;
      continue;
    }
    if (!MoveFileExW(backup.c_str(), live.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      ok = false;
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
    lastError_ = "rollback could not restore the registration";
  }
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
