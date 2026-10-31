import io

# ---------------------------------------------------------------- header
p = 'apps/native_poc/src/update_effects.hpp'
d = io.open(p, encoding='utf-8').read()

old = '''  /** Writes the artifact to `destPath`. Injected so no test ever opens a socket. */
  std::function<bool(const ManifestFields& fields, const std::wstring& destPath)> fetchArtifact;'''
new = '''  /**
   * Writes one artifact to `destPath`. Injected so no test ever opens a socket.
   *
   * Called once per artifact. The updater does not ask what "latest" is between calls -- it works
   * from the list a single verified manifest gave it, which is what stops a release changing
   * underneath an update in progress.
   */
  std::function<bool(const ManifestArtifact& artifact, const std::wstring& destPath)> fetchArtifact;'''
assert old in d, 'fetchArtifact anchor'
d = d.replace(old, new, 1)

old = '''  std::wstring staged_artifact_path() const;'''
new = '''  std::wstring staged_artifact_path() const;
  /** Where one artifact of a release is staged. Keyed by release, so two cannot be mixed. */
  std::wstring staged_path_for(const std::string& releaseId, const std::wstring& name) const;
  std::wstring staging_dir_for(const std::string& releaseId) const;'''
assert old in d, 'paths anchor'
d = d.replace(old, new, 1)

old = '''  /** Names moved aside during the current swap, so a partial failure can be undone exactly. */
  std::vector<std::wstring> movedAside_;'''
new = '''  /** Names moved aside during the current swap, so a partial failure can be undone exactly. */
  std::vector<std::wstring> movedAside_;
  /**
   * The release currently staged, and the files staged for it.
   *
   * Set by Download and read by Swap. A Swap that finds a different release than the one that was
   * verified refuses -- that is the mechanism that keeps one attempt to one release.
   */
  std::string stagedReleaseId_;
  std::vector<std::wstring> stagedNames_;'''
assert old in d, 'members anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('header updated')

# ---------------------------------------------------------------- implementation
p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''std::wstring WindowsUpdateEffects::staged_artifact_path() const {
  return config_.stagingDir + L"\\\\artifact.staged";
}'''
new = '''std::wstring WindowsUpdateEffects::staged_artifact_path() const {
  return config_.stagingDir + L"\\\\artifact.staged";
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
  return config_.stagingDir + L"\\\\r-" + wide;
}

std::wstring WindowsUpdateEffects::staged_path_for(const std::string& releaseId,
                                                   const std::wstring& name) const {
  // The name has already been through check_payload_names by the time a verified manifest exists,
  // but it can contain a separator (ui\\shell.html), so the leaf is flattened rather than creating
  // subdirectories in staging. The destination keeps the real relative path.
  std::wstring flat = name;
  for (wchar_t& c : flat) {
    if (c == L'\\\\' || c == L'/') c = L'_';
  }
  return staging_dir_for(releaseId) + L"\\\\" + flat;
}'''
assert old in d, 'staged path anchor'
d = d.replace(old, new, 1)

# Download: all artifacts, one release
old = '''bool WindowsUpdateEffects::Download(const ManifestFields& fields) {
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
}'''
new = '''bool WindowsUpdateEffects::Download(const ManifestFields& fields) {
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
}'''
assert old in d, 'Download anchor'
d = d.replace(old, new, 1)

# VerifyDownload: every artifact
old_start = d.find('bool WindowsUpdateEffects::VerifyDownload(const ManifestFields& fields) {')
old_end = d.find('void WindowsUpdateEffects::DiscardDownload()')
assert old_start >= 0 and old_end > old_start, 'VerifyDownload block'
new_verify = '''bool WindowsUpdateEffects::VerifyDownload(const ManifestFields& fields) {
  if (stagedReleaseId_.empty() || stagedReleaseId_ != fields.releaseId) {
    lastError_ = "nothing staged for this release";
    return false;
  }
  if (fields.artifacts.empty()) {
    lastError_ = "manifest lists no artifacts";
    return false;
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

'''
d = d[:old_start] + new_verify + d[old_end:]

# DiscardDownload: whole release
old = '''void WindowsUpdateEffects::DiscardDownload() {
  DeleteFileW(staged_artifact_path().c_str());
}'''
new = '''void WindowsUpdateEffects::DiscardDownload() {
  DeleteFileW(staged_artifact_path().c_str());
  if (stagedReleaseId_.empty()) return;
  const std::wstring dir = staging_dir_for(stagedReleaseId_);
  for (const std::wstring& name : stagedNames_) {
    DeleteFileW(staged_path_for(stagedReleaseId_, name).c_str());
  }
  RemoveDirectoryW(dir.c_str());
  stagedReleaseId_.clear();
  stagedNames_.clear();
}'''
assert old in d, 'Discard anchor'
d = d.replace(old, new, 1)

# Swap: place the staged files, not one blob
old = '''  // Phase two: put the new files in. The staged artifact is a single file in this build; each
  // payload name is written from it. Splitting a multi-file payload out of one artifact is the
  // installer's job and is not modelled here.
  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    if (!CopyFileW(staged_artifact_path().c_str(), live.c_str(), FALSE)) {
      lastError_ = "could not place " + to_utf8(name);
      (void)Rollback();
      return false;
    }
  }'''
new = '''  // Phase two: put the new files in, each from its own staged file. Every one of them was
  // verified before this function ran, and they all came from the same release.
  for (const std::wstring& name : config_.payloadNames) {
    const std::wstring live = install_path(name);
    const std::wstring staged = staged_path_for(stagedReleaseId_, name);
    if (!CopyFileW(staged.c_str(), live.c_str(), FALSE)) {
      lastError_ = "could not place " + to_utf8(name);
      (void)Rollback();
      return false;
    }
  }'''
assert old in d, 'Swap phase two anchor'
d = d.replace(old, new, 1)

# Swap guard: only ever from a staged, verified release
old = '''  // Before a single file moves. Capturing later would capture the state the update is creating,
  // and a rollback to that would leave the previous build's files under the new version's name.
  if (!config_.captureRegistration()) {'''
new = '''  // Nothing moves unless a whole release is staged. This is what makes "one attempt, one release"
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
  if (!config_.captureRegistration()) {'''
assert old in d, 'Swap guard anchor'
d = d.replace(old, new, 1)

# a small helper
old = '''bool file_exists(const std::wstring& path) {'''
new = '''/** Artifact names are ASCII by contract (payload_name refuses anything else). */
std::wstring widen_name(const std::string& name) {
  std::wstring out;
  out.reserve(name.size());
  for (unsigned char c : name) out.push_back(static_cast<wchar_t>(c));
  return out;
}

bool file_exists(const std::wstring& path) {'''
assert old in d, 'helper anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('implementation updated')
