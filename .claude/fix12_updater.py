import io

p = 'apps/native_poc/src/updater_main.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- (1) fetch the manifest
old = '''    config.fetchArtifact = [](const ManifestArtifact& artifact, const std::wstring& destPath) {'''
new = '''    // THE step that was missing. Without it FetchManifest had nothing to return and every run
    // ended at the first stage; the signature check, the version comparison, the staging, the
    // swap and everything after it were correct and unreachable.
    //
    // Fetched once, here, and kept: the document and its signature are read together and the rest
    // of the attempt works from that one snapshot. Re-fetching per stage would let the server
    // change what is being installed part way through -- the same hazard releaseId exists for,
    // one level up.
    const std::string manifestUrl = options_.manifestUrl;
    config.fetchManifest = [manifestUrl](std::string* document, std::string* signatureHex) {
      std::string error;
      const FetchStatus doc = https_get_text(manifestUrl, 64 * 1024, document, &error);
      if (doc != FetchStatus::Ok) {
        log_line(std::string("manifest fetch failed: ") + fetch_status_name(doc) + " " + error);
        return false;
      }
      // The detached signature lives beside the document. Fetched separately so the bytes that
      // are verified are exactly the bytes that arrived, with nothing wrapping them.
      const FetchStatus sig =
          https_get_text(manifestUrl + ".sig", 4 * 1024, signatureHex, &error);
      if (sig != FetchStatus::Ok) {
        log_line(std::string("signature fetch failed: ") + fetch_status_name(sig) + " " + error);
        return false;
      }
      while (!signatureHex->empty() &&
             (signatureHex->back() == '\\n' || signatureHex->back() == '\\r' ||
              signatureHex->back() == ' ')) {
        signatureHex->pop_back();
      }
      return true;
    };

    config.fetchArtifact = [](const ManifestArtifact& artifact, const std::wstring& destPath) {'''
assert old in d, 'fetchArtifact anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- (2) the verified version flows
old = '''  UpdateOutcome run(const std::string& platform) {'''
new = '''  UpdateOutcome run(const std::string& platform) {
    /**
     * The version this attempt is installing, learned from the verified manifest.
     *
     * Shared and read late on purpose. Registration and relaunch are configured before
     * run_update starts, at which point nobody knows what version the server is offering -- and
     * a field filled in then would carry THIS binary's compile-time version into DisplayVersion
     * and into the health check. That is the same reference-point poisoning that ruled out
     * re-running the old installer for registration (history #440), arrived at from the other
     * side.
     *
     * Set at exactly one point: after VerifyDownload succeeds, which is the first moment the
     * version is both known and trustworthy.
     */
    auto versionToInstall = std::make_shared<std::string>();
    // Starts as what is installed now, so a rollback path -- which puts the OLD files back --
    // checks the health of the version that is actually on disk afterwards.
    auto expectedVersion = std::make_shared<std::string>(options_.installedVersion);'''
assert old in d, 'run anchor'
d = d.replace(old, new, 1)

old = '''    install::RegistrationTarget target = registrationTarget_;
    target.version = versionToInstall_.empty() ? std::wstring(remote60::native_poc::kProductVersion)
                                               : versionToInstall_;
    RegistrationEffects registration =
        make_registration_effects(target, production_registration_ops());
    config.captureRegistration = registration.capture;
    config.registerInstall = registration.apply;
    config.restoreRegistration = registration.restore;'''
new = '''    // Built lazily, when each callback is actually invoked, so they see the verified version
    // rather than a placeholder captured before the manifest was read.
    install::RegistrationTarget baseTarget = registrationTarget_;
    auto registration = std::make_shared<RegistrationEffects>();
    const auto ensureRegistration = [baseTarget, registration, versionToInstall]() {
      if (registration->apply) return;
      install::RegistrationTarget target = baseTarget;
      target.version = widen(*versionToInstall);
      *registration = make_registration_effects(target, production_registration_ops());
    };
    // capture runs before the swap and before RegisterInstall, and it only reads what is already
    // registered -- so it does not need the new version, but it must share the same snapshot as
    // the restore that may follow it.
    config.captureRegistration = [ensureRegistration, registration]() {
      ensureRegistration();
      return registration->capture();
    };
    config.registerInstall = [ensureRegistration, registration, versionToInstall]() {
      if (versionToInstall->empty()) return false;  // nothing verified: nothing to register as
      ensureRegistration();
      return registration->apply();
    };
    config.restoreRegistration = [ensureRegistration, registration]() {
      ensureRegistration();
      return registration->restore();
    };'''
assert old in d, 'registration anchor'
d = d.replace(old, new, 1)

old = '''    relaunchConfig.expectedVersion = to_utf8(target.version);'''
new = '''    // Read when relaunch runs, not now: after a rollback this holds the version that is back on
    // disk, and health must check for THAT rather than for the one the update was carrying.'''
assert old in d, 'expectedVersion anchor'
d = d.replace(old, new, 1)

old = '''    config.relaunch = [this, relaunchConfig, stopped, relaunchEffects]() {
      *relaunchEffects = make_relaunch_effects(relaunchConfig, *stopped);'''
new = '''    config.relaunch = [this, relaunchConfig, stopped, relaunchEffects, expectedVersion]() {
      RelaunchConfig live = relaunchConfig;
      live.expectedVersion = *expectedVersion;
      *relaunchEffects = make_relaunch_effects(live, *stopped);'''
assert old in d, 'relaunch lambda anchor'
d = d.replace(old, new, 1)

old = '''    ReadySignaller signaller(effects, options_.readyEventName);'''
new = '''    ReadySignaller signaller(effects, options_.readyEventName, versionToInstall, expectedVersion);'''
assert old in d, 'signaller anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('updater: manifest fetch + verified version flow')
