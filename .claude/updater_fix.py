import io

BS = chr(92)
p = 'apps/native_poc/src/updater_main.cpp'
d = io.open(p, encoding='utf-8').read()

# fetch: FetchStatus, not bool; and the artifact size is the exact cap
d = d.replace('''    config.fetchArtifact = [](const ManifestArtifact& artifact, const std::wstring& destPath) {
      std::string error;
      return https_get_file(artifact.url, destPath, artifact.size, &error);
    };''',
'''    config.fetchArtifact = [](const ManifestArtifact& artifact, const std::wstring& destPath) {
      std::string error;
      // The manifest's own size is the cap. A body larger than what was signed for is refused
      // while it is arriving rather than after it has been written.
      const FetchStatus status = https_get_file(artifact.url, destPath, artifact.size, &error);
      if (status != FetchStatus::Ok) {
        log_line(std::string("fetch failed for ") + artifact.name + ": " +
                 fetch_status_name(status) + " " + error);
        return false;
      }
      return true;
    };''', 1)

# payload names: derive from the product image list plus the files that are not processes
d = d.replace('    config.payloadNames = product_payload_names();',
'''    // What a swap replaces. The executables come from the same list the process enumerator
    // uses, so the set that is stopped and the set that is replaced cannot drift apart, and the
    // two data files are named alongside them.
    config.payloadNames = product_image_names();
    config.payloadNames.push_back(L"ui@@shell.html");
    config.payloadNames.push_back(L"ui@@macro.html");'''.replace('@@', BS * 2), 1)

# ReadySignaller is declared after use in run(); move the private class ahead by forward use
d = d.replace('''    WindowsUpdateEffects effects(config);
    effects.set_installed_version(options_.installedVersion);
    ReadySignaller signaller(effects, options_.readyEventName);
    return run_update(signaller, default_verifier(), platform);''',
'''    WindowsUpdateEffects effects(config);
    effects.set_installed_version(options_.installedVersion);
    ReadySignaller signaller(effects, options_.readyEventName);
    const UpdateOutcome outcome = run_update(signaller, default_verifier(), platform);
    return outcome;''', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('updater_main adjusted')
