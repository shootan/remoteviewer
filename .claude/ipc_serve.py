import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ---------------------------------------------------------------- host: create then serve
edit('apps/native_poc/src/host_app_main.cpp', [
 ('''  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  // The host is already elevated, so the child inherits the token and no prompt appears.''',
  '''  // Created BEFORE the updater starts, for the same reason as the handshake events: a name that
  // exists with nobody owning it is a name somebody else can own.
  upd::CredentialServer credentialServer;
  if (!spec.credentialPipeName.empty()) {
    std::string error;
    if (!credentialServer.Create(spec.credentialPipeName, &error)) {
      append_host_app_log("[host-app] update: could not open the credential channel -- " + error);
      CloseHandle(readyEvent);
      CloseHandle(ackEvent);
      MessageBoxW(window, L"업데이트를 시작하지 못했습니다.", kProductName, MB_OK | MB_ICONWARNING);
      return;
    }
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  // The host is already elevated, so the child inherits the token and no prompt appears.'''),
 ('''  CloseHandle(pi.hThread);
  append_host_app_log("[host-app] update: handed over to the updater, waiting for its signal");''',
  '''  CloseHandle(pi.hThread);

  if (!spec.credentialPipeName.empty()) {
    // Re-read now, not reused from the snapshot built minutes ago: if the account or the server
    // changed while the user was deciding, this attempt belongs to nobody and the credential is
    // not sent. The updater then fails its fetch and stops, which is the correct end for an
    // attempt whose owner is gone.
    const upd::UpdateEndpoint nowEndpoint = directory::update_endpoint_for(
        remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
        g.cache.directoryUrl, "windows",
        g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken);
    std::string payload;
    if (nowEndpoint.url == launchEndpoint.url && nowEndpoint.origin == launchEndpoint.origin &&
        !nowEndpoint.credentialHeader.empty()) {
      payload = nowEndpoint.credentialHeader;
    } else {
      append_host_app_log("[host-app] update: the owner changed after the updater was started; "
                          "no credential sent");
    }
    std::string error;
    if (payload.empty() ||
        !credentialServer.Serve(pi.hProcess, payload, 120000, &error)) {
      append_host_app_log("[host-app] update: the updater did not receive the credential -- " +
                          (error.empty() ? std::string("owner changed") : error));
    }
    credentialServer.Close();
  }

  append_host_app_log("[host-app] update: handed over to the updater, waiting for its signal");'''),
])

# ---------------------------------------------------------------- client: create then serve
edit('apps/native_poc/src/client_shell_main.cpp', [
 ('''  SHELLEXECUTEINFOW info{};''',
  '''  // Created before the elevated process starts. The client used to launch and immediately walk
  // away; with a credential to hand over it has to stay for the exchange, and the deadline below
  // is what keeps "stay" from becoming "hang".
  upd::CredentialServer credentialServer;
  if (!spec.credentialPipeName.empty()) {
    std::string error;
    if (!credentialServer.Create(spec.credentialPipeName, &error)) {
      log_line("update: could not open the credential channel -- " + error);
      post_status("error", "업데이트를 시작하지 못했습니다.");
      return;
    }
  }

  SHELLEXECUTEINFOW info{};'''),
 ('''  const BOOL launched = ShellExecuteExW(&info);
  const upd::ElevationOutcome outcome =
      upd::elevation_outcome(launched != FALSE, static_cast<uint32_t>(GetLastError()));
  if (info.hProcess) CloseHandle(info.hProcess);''',
  '''  const BOOL launched = ShellExecuteExW(&info);
  const upd::ElevationOutcome outcome =
      upd::elevation_outcome(launched != FALSE, static_cast<uint32_t>(GetLastError()));

  if (launched && info.hProcess && !spec.credentialPipeName.empty()) {
    // Re-read rather than reused: the user may have signed out or changed servers between the
    // decision to update and the consent prompt they just answered. An attempt whose owner has
    // changed gets no credential, and fails at the fetch instead of installing on behalf of
    // somebody who is no longer there.
    std::string nowSession;
    {
      std::lock_guard<std::mutex> lock(gStateMu);
      nowSession = gSessionToken;
    }
    const upd::UpdateEndpoint nowEndpoint =
        remote60::native_poc::directory::update_endpoint_for(
            remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
            configured_directory_url(), "windows",
            nowSession.empty() ? std::string() : "Authorization: Bearer " + nowSession);
    std::string payload;
    if (nowEndpoint.url == launchEndpoint.url && nowEndpoint.origin == launchEndpoint.origin &&
        !nowEndpoint.credentialHeader.empty()) {
      payload = nowEndpoint.credentialHeader;
    } else {
      log_line("update: the signed-in owner changed after the updater was started; "
               "no credential sent");
    }
    std::string error;
    if (payload.empty() || !credentialServer.Serve(info.hProcess, payload, 120000, &error)) {
      log_line("update: the updater did not receive the credential -- " +
               (error.empty() ? std::string("owner changed") : error));
    }
    credentialServer.Close();
  }

  if (info.hProcess) CloseHandle(info.hProcess);'''),
])
