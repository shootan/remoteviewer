import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ---------------------------------------------------------------- client: an owner, not a url
edit('apps/native_poc/src/client_shell_main.cpp', [
 ('''std::string gAccountId;''',
  '''std::string gAccountId;
/**
 * Counts sign-ins and server changes in this process.
 *
 * Needed because the account name and the server address are not enough to tell one sign-in from
 * the next: sign out and back in as the same user and both are identical, while the session token
 * -- the thing actually being handed to an updater -- is a different one. An attempt authorised
 * under the old session must not inherit the new token.
 */
uint64_t gOwnerEpoch = 0;'''),
 ('''      gServerUrl = server;
      gAccountId = accountId;
      gSessionToken = token;''',
  '''      gServerUrl = server;
      gAccountId = accountId;
      gSessionToken = token;
      ++gOwnerEpoch;'''),
 ('''  // One snapshot, built where the session and the server address are known. A shell that has
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
          session.empty() ? std::string() : "Authorization: Bearer " + session);''',
  '''  // One snapshot, built where the session, the account and the server address are known. A shell
  // that has not signed in yet has no credential to send, which is normal: the server says so,
  // and that answer is an update-check failure rather than a reason to sign out.
  std::string session;
  std::string owner;
  uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    session = gSessionToken;
    owner = gAccountId;
    epoch = gOwnerEpoch;
  }
  const remote60::native_poc::directory::UpdateEndpoint endpoint =
      remote60::native_poc::directory::update_endpoint_for(
          remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
          configured_directory_url(), "windows",
          session.empty() ? std::string() : "Authorization: Bearer " + session, owner, epoch);'''),
])

# ---------------------------------------------------------------- host: the same
edit('apps/native_poc/src/host_app_main.cpp', [
 ('''  // A host authenticates with its own token, not a session -- it has no session, and the server
  // accepts either on this route.
  const upd::UpdateEndpoint endpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken);''',
  '''  // A host authenticates with its own token, not a session -- it has no session, and the server
  // accepts either on this route. The owner is the account and the machine the token was issued
  // for; the epoch counts re-registrations, because a re-register issues a NEW token and an
  // attempt authorised under the old one must not inherit it.
  const upd::UpdateEndpoint endpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken,
      host_owner_key(), g.ownerEpoch);'''),
])
