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

bool file_exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

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
  if (!fetchArtifact) return fail("fetchArtifact not set");
  if (!enumerateTargets) return fail("enumerateTargets not set");
  if (!requestStop) return fail("requestStop not set");
  if (!registerInstall) return fail("registerInstall not set");
  if (!relaunch) return fail("relaunch not set");
  if (!healthCheck) return fail("healthCheck not set");
  // Nothing reads these yet. Requiring them now means a future RegisterInstall cannot be
  // written against hardcoded HKLM and GNLinkSecureInput without this check failing first.
  if (registryRoot.empty()) return fail("registryRoot not set");
  if (serviceName.empty()) return fail("serviceName not set");

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
  if (!CreateDirectoryW(config_.stagingDir.c_str(), nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    lastError_ = "could not create the staging directory";
    return false;
  }
  // Anything left from a previous attempt is removed first, so a short write cannot be mistaken
  // for a complete one by whatever runs next.
  DeleteFileW(staged_artifact_path().c_str());
  if (!config_.fetchArtifact(fields, staged_artifact_path())) {
    lastError_ = "artifact fetch failed";
    return false;
  }
  return true;
}

bool WindowsUpdateEffects::VerifyDownload(const ManifestFields& fields) {
  uint64_t size = 0;
  if (!file_size_bytes(staged_artifact_path(), &size)) {
    lastError_ = "staged artifact is not readable";
    return false;
  }
  // Size first: it is cheap, and a size mismatch means the hash is going to fail anyway.
  if (size != fields.size) {
    lastError_ = "size mismatch";
    return false;
  }
  const std::string actual = sha256_file_hex(staged_artifact_path());
  if (actual.empty()) {
    lastError_ = "could not hash the staged artifact";
    return false;
  }
  if (actual != fields.sha256) {
    lastError_ = "sha256 mismatch";
    return false;
  }
  return true;
}

void WindowsUpdateEffects::DiscardDownload() {
  DeleteFileW(staged_artifact_path().c_str());
}

// ---------------------------------------------------------------- stopping the product

bool WindowsUpdateEffects::PrepareForSwap() {
  // Asking is separate from waiting. If a target cannot even be asked to stop, the attempt is
  // abandoned here -- before anything on disk is touched -- rather than escalated.
  const std::vector<ProcessTarget> targets = config_.enumerateTargets();
  for (const ProcessTarget& target : targets) {
    if (!config_.requestStop(target)) {
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
  const DWORD deadline = GetTickCount() + config_.quiesceTimeoutMs;
  for (const ProcessTarget& target : config_.enumerateTargets()) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
    if (!h) {
      // Already gone, or not ours to wait on. Either way there is nothing to wait for.
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
      lastError_ = "pid " + std::to_string(target.pid) + " did not exit";
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------- swap and rollback

bool WindowsUpdateEffects::Swap() {
  movedAside_.clear();
  swapped_ = false;

  // Phase one: move every existing file aside. Nothing new is put in place yet, so a failure here
  // is undone by moving back exactly what was moved.
  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    if (!file_exists(live)) continue;  // a file that is not there does not need a backup
    const std::wstring backup = backup_path(name);
    DeleteFileW(backup.c_str());
    if (!MoveFileExW(live.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      lastError_ = "could not move aside " + to_utf8(name);
      (void)Rollback();
      return false;
    }
    movedAside_.push_back(name);
  }

  // Phase two: put the new files in. The staged artifact is a single file in this build; each
  // payload name is written from it. Splitting a multi-file payload out of one artifact is the
  // installer's job and is not modelled here.
  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    if (!CopyFileW(staged_artifact_path().c_str(), live.c_str(), FALSE)) {
      lastError_ = "could not place " + to_utf8(name);
      (void)Rollback();
      return false;
    }
  }

  swapped_ = true;
  return true;
}

bool WindowsUpdateEffects::Rollback() {
  bool ok = true;
  // Remove whatever was placed, then put back exactly the files that were moved aside. Files that
  // were never moved are left alone -- restoring something that was not backed up would be
  // inventing state.
  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    const std::wstring backup = backup_path(name);
    const bool hasBackup =
        std::find(movedAside_.begin(), movedAside_.end(), name) != movedAside_.end();
    if (!hasBackup) continue;
    if (file_exists(live) && !DeleteFileW(live.c_str())) {
      ok = false;
      continue;
    }
    if (!MoveFileExW(backup.c_str(), live.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      ok = false;
    }
  }
  movedAside_.clear();
  swapped_ = false;
  if (!ok) lastError_ = "rollback could not restore every file";
  return ok;
}

bool WindowsUpdateEffects::RegisterInstall() {
  if (!config_.registerInstall()) {
    lastError_ = "registration failed";
    return false;
  }
  // The backups are only dropped once the new install is registered. Until then they are the way
  // back, and deleting them earlier would trade a recoverable failure for an unrecoverable one.
  for (const std::wstring& name : movedAside_) {
    DeleteFileW(backup_path(name).c_str());
  }
  movedAside_.clear();
  return true;
}

bool WindowsUpdateEffects::Relaunch() {
  if (!config_.relaunch()) {
    lastError_ = "relaunch failed";
    return false;
  }
  return true;
}

bool WindowsUpdateEffects::HealthCheck() {
  if (!config_.healthCheck()) {
    lastError_ = "health check failed";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- process identity

namespace {

uint64_t creation_time_of(HANDLE process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
  ULARGE_INTEGER v{};
  v.LowPart = created.dwLowDateTime;
  v.HighPart = created.dwHighDateTime;
  return v.QuadPart;
}

std::wstring image_path_of(HANDLE process) {
  wchar_t buffer[MAX_PATH * 2]{};
  DWORD size = static_cast<DWORD>(std::size(buffer));
  if (!QueryFullProcessImageNameW(process, 0, buffer, &size)) return {};
  return std::wstring(buffer, size);
}

}  // namespace

bool capture_process_identity(uint32_t pid, ProcessTarget* out) {
  if (!out) return false;
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return false;
  out->pid = pid;
  out->creationTime = creation_time_of(h);
  out->imagePath = image_path_of(h);
  CloseHandle(h);
  // A creation time of zero would make every later comparison vacuous, so it is treated as a
  // failure to identify rather than as an identity.
  return out->creationTime != 0;
}

bool process_identity_matches(void* handle, const ProcessTarget& target) {
  HANDLE h = static_cast<HANDLE>(handle);
  if (!h) return false;
  if (target.creationTime == 0) return false;  // nothing to compare against
  if (creation_time_of(h) != target.creationTime) return false;
  // The creation time alone is already decisive in practice; the path is compared too because it
  // costs nothing and makes a deliberately crafted collision harder.
  if (!target.imagePath.empty() && image_path_of(h) != target.imagePath) return false;
  return true;
}

// ---------------------------------------------------------------- file helpers

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
