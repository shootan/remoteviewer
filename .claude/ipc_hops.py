import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ================================================================ hop 2: bootstrap -> worker
edit('apps/native_poc/src/updater_main.cpp', [
 ('''bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs) {
  const std::wstring copyPath = updater_copy_path(options.workDir, self_image_path());''',
  '''/**
 * Hands the credential on to the working copy, over a second pipe of the bootstrap's own.
 *
 * Two hops because the launch has two. The bootstrap cannot forward the pipe it read from -- that
 * one was proved against the bootstrap's own process id -- so it opens its own, names it in the
 * copy's arguments, and applies the same check to whoever connects.
 *
 * The handle from CreateProcessW is kept for the whole exchange. The bootstrap used to close it
 * immediately and leave; closing it here would leave the check comparing against a process id
 * that is no longer pinned to anything, which is exactly the case the check exists to refuse.
 */
bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs,
                   const std::string& credential);

bool run_from_copy(const UpdaterOptions& options, const std::vector<std::wstring>& originalArgs,
                   const std::string& credential) {
  const std::wstring copyPath = updater_copy_path(options.workDir, self_image_path());'''),
 ('''    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                  CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP, nullptr,
                                  options.workDir.c_str(), &si, &pi);''',
  '''    // Created before the copy is started, so the name never exists without an owner.
    CredentialServer credentialServer;
    if (!credential.empty()) {
      std::string error;
      if (!credentialServer.Create(childPipeName, &error)) {
        log_line("could not open the credential channel for the working copy: " + error);
        return false;
      }
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                  CREATE_BREAKAWAY_FROM_JOB | CREATE_NEW_PROCESS_GROUP, nullptr,
                                  options.workDir.c_str(), &si, &pi);'''),
 ('''    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
  };''',
  '''    CloseHandle(pi.hThread);

    bool handedOver = true;
    if (!credential.empty()) {
      // The handle is still open here, and stays open until the exchange is finished: that is
      // what keeps the process id from being recycled underneath the check.
      std::string payload = credential;
      std::string error;
      handedOver = credentialServer.Serve(pi.hProcess, payload, kCredentialDeadlineMs, &error);
      if (!handedOver) {
        log_line("the working copy did not receive the credential: " + error);
      }
    }
    CloseHandle(pi.hProcess);
    // A copy that never got its credential will fail its own fetch and stop. Reported as a
    // failure here so the exit code says so, rather than reporting success for a run that cannot
    // do the thing it was started for.
    return handedOver;
  };'''),
 ('''  if (!options.runningFromCopy) {
    // Hand over to a copy of ourselves so the installed original can be replaced by this update.
    return run_from_copy(options, arguments) ? 0 : 3;
  }''',
  '''  // The credential, if one is coming, before anything else happens. Read once here and once in
  // the working copy; it is never written down in between.
  std::string credential;
  if (!options.credentialPipeName.empty()) {
    std::string error;
    if (!receive_credential(options.credentialPipeName, kCredentialDeadlineMs, &credential,
                            &error)) {
      // Not "carry on without it": the fetch would then get a 401 nobody can explain, and the
      // failure would look like the server's rather than ours.
      log_line("no credential arrived: " + error);
      return 5;
    }
  }

  if (!options.runningFromCopy) {
    // Hand over to a copy of ourselves so the installed original can be replaced by this update.
    const bool ok = run_from_copy(options, arguments, credential);
    SecureZeroMemory(credential.data(), credential.size());
    return ok ? 0 : 3;
  }'''),
 ('''  UpdaterEffects effects(options, production_updater_deps(log_line));''',
  '''  // What the worker may send, and where. Derived urls are on the directory's own origin by
  // construction, so that is the one origin this credential may reach; every artifact url is
  // judged against it separately.
  UpdateEndpoint endpoint;
  endpoint.url = options.manifestUrl;
  endpoint.derived = options.derivedEndpoint;
  if (options.derivedEndpoint && !credential.empty()) {
    endpoint.credentialHeader = credential;
    endpoint.origin = url_origin_key(options.manifestUrl);
  }

  UpdaterEffects effects(options, production_updater_deps(log_line, endpoint));'''),
])

# ================================================================ hop 1: host -> bootstrap
edit('apps/native_poc/src/host_app_main.cpp', [
 ('''  // ⚠️ No credential travels with it. Handing one to an elevated worker needs an access-limited
  // channel bound to that process, which is a separate decision -- so a derived url gets a 401
  // from the worker today, and that is an update failure, not a sign-out.
  {
    const upd::UpdateEndpoint launchEndpoint = directory::update_endpoint_for(
        remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
        g.cache.directoryUrl, "windows");
    spec.manifestUrl = launchEndpoint.url;
    spec.derivedEndpoint = launchEndpoint.derived;
  }''',
  '''  const upd::UpdateEndpoint launchEndpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken);
  spec.manifestUrl = launchEndpoint.url;
  spec.derivedEndpoint = launchEndpoint.derived;
  // A name, not the credential. The credential goes over the pipe, and only after the process
  // that connects has been shown to be the one this launch started.
  if (!launchEndpoint.credentialHeader.empty()) {
    spec.credentialPipeName =
        upd::make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
  }'''),
])

# ================================================================ hop 1: client -> bootstrap
edit('apps/native_poc/src/client_shell_main.cpp', [
 ('''  // The same snapshot the check used. ⚠️ No credential travels to the elevated worker -- that
  // needs an access-limited channel bound to that process, which is a separate decision.
  {
    const upd::UpdateEndpoint launchEndpoint =
        remote60::native_poc::directory::update_endpoint_for(
            remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
            configured_directory_url(), "windows");
    spec.manifestUrl = launchEndpoint.url;
    spec.derivedEndpoint = launchEndpoint.derived;
  }''',
  '''  // The same snapshot the check used.
  std::string launchSession;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    launchSession = gSessionToken;
  }
  const upd::UpdateEndpoint launchEndpoint =
      remote60::native_poc::directory::update_endpoint_for(
          remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
          configured_directory_url(), "windows",
          launchSession.empty() ? std::string() : "Authorization: Bearer " + launchSession);
  spec.manifestUrl = launchEndpoint.url;
  spec.derivedEndpoint = launchEndpoint.derived;
  // A name, not the credential.
  if (!launchEndpoint.credentialHeader.empty()) {
    spec.credentialPipeName =
        upd::make_credential_pipe_name(GetCurrentProcessId(), GetTickCount64());
  }'''),
])
