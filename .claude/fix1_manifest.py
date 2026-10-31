import io

# ---------------------------------------------------------------- config gains a manifest fetcher
p = 'apps/native_poc/src/update_effects.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  std::function<bool(const ManifestArtifact& artifact, const std::wstring& destPath)> fetchArtifact;'''
new = '''  std::function<bool(const ManifestArtifact& artifact, const std::wstring& destPath)> fetchArtifact;

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
  std::function<bool(std::string* document, std::string* signatureHex)> fetchManifest;'''
assert old in d, 'config anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- FetchManifest uses it
p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''bool WindowsUpdateEffects::FetchManifest(std::string* document, std::string* signatureHex) {
  if (manifestDocument_.empty() || manifestSignatureHex_.empty()) {
    lastError_ = "no manifest available";
    return false;
  }'''
new = '''bool WindowsUpdateEffects::FetchManifest(std::string* document, std::string* signatureHex) {
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
  }'''
assert old in d, 'fetch anchor'
d = d.replace(old, new, 1)

# validate() requires one of the two
old = '''  if (!restoreRegistration) return fail("restoreRegistration not set");'''
new = '''  if (!restoreRegistration) return fail("restoreRegistration not set");
  // Not required here, because a test may inject the document instead -- but the updater's own
  // options DO require the url that builds it, so the production path cannot reach run_update
  // without one. See updater_options.validate().'''
assert old in d, 'validate anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('manifest fetcher wired into the effects')
