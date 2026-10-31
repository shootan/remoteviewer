import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ------------------------------------------------------------------ host: x-host-token
edit('apps/native_poc/src/host_app_main.cpp', [
 ('''  upd::CheckConfig config;
  // The override still wins; otherwise it comes from the directory this host is registered with,
  // so a machine configured with only a server address can check for updates.
  config.manifestUrl = directory::update_manifest_url_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows");''',
  '''  // One snapshot: the url, the shape it answers with, and what may be sent to it. Built here,
  // where the account and the server address are known, rather than read back later from state
  // that can have changed by then.
  //
  // A host authenticates with its own token, not a session -- it has no session, and the server
  // accepts either on this route.
  const directory::UpdateEndpoint endpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken);

  upd::CheckConfig config;
  config.manifestUrl = endpoint.url;
  config.derivedEndpoint = endpoint.derived;
  config.credentialHeader = endpoint.credentialHeader;
  config.credentialOrigin = endpoint.origin;'''),
 ('''  upd::check_for_update_async(
      config, upd::https_manifest_fetcher(), upd::default_verifier(),
      [window](upd::CheckResult result) {''',
  '''  upd::check_for_update_async(
      config, upd::manifest_fetcher_for(config), upd::default_verifier(),
      [window](upd::CheckResult result) {'''),
 ('''  // The same url the check used, from the same rule: the updater must fetch what the user was
  // told about, not a second opinion about where updates live.
  spec.manifestUrl = directory::update_manifest_url_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows");''',
  '''  // The same snapshot the check used: the updater must fetch what the user was told about, not
  // a second opinion about where updates live.
  //
  // ⚠️ No credential travels with it. Handing one to an elevated worker needs an access-limited
  // channel bound to that process, which is a separate decision -- so a derived url gets a 401
  // from the worker today, and that is an update failure, not a sign-out.
  {
    const directory::UpdateEndpoint launchEndpoint = directory::update_endpoint_for(
        remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
        g.cache.directoryUrl, "windows");
    spec.manifestUrl = launchEndpoint.url;
    spec.derivedEndpoint = launchEndpoint.derived;
  }'''),
])

# ------------------------------------------------------------------ PC client: session bearer
edit('apps/native_poc/src/client_shell_main.cpp', [
 ('''  upd::CheckConfig config;
  // The override still wins; otherwise it is derived from the directory address this shell uses.
  config.manifestUrl = remote60::native_poc::directory::update_manifest_url_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      configured_directory_url(), "windows");''',
  '''  // One snapshot, built where the session and the server address are known. A shell that has
  // not signed in yet has no credential to send, which is normal: the server says so, and that
  // answer is an update-check failure rather than a reason to sign out.
  std::string session;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    session = gSessionToken;
  }
  const remote60::native_poc::directory::UpdateEndpoint endpoint =
      remote60::native_poc::directory::update_endpoint_for(
          remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
          configured_directory_url(), "windows",
          session.empty() ? std::string() : "Authorization: Bearer " + session);

  upd::CheckConfig config;
  config.manifestUrl = endpoint.url;
  config.derivedEndpoint = endpoint.derived;
  config.credentialHeader = endpoint.credentialHeader;
  config.credentialOrigin = endpoint.origin;'''),
 ('''  upd::check_for_update_async(
      config, upd::https_manifest_fetcher(), upd::default_verifier(),
      [](upd::CheckResult result) {''',
  '''  upd::check_for_update_async(
      config, upd::manifest_fetcher_for(config), upd::default_verifier(),
      [](upd::CheckResult result) {'''),
 ('''  // The same rule the check used: the updater fetches what the user was told about.
  spec.manifestUrl = remote60::native_poc::directory::update_manifest_url_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      configured_directory_url(), "windows");''',
  '''  // The same snapshot the check used. ⚠️ No credential travels to the elevated worker -- that
  // needs an access-limited channel bound to that process, which is a separate decision.
  {
    const remote60::native_poc::directory::UpdateEndpoint launchEndpoint =
        remote60::native_poc::directory::update_endpoint_for(
            remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
            configured_directory_url(), "windows");
    spec.manifestUrl = launchEndpoint.url;
    spec.derivedEndpoint = launchEndpoint.derived;
  }'''),
])
