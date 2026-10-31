import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

edit('apps/native_poc/src/client_shell_main.cpp', [
 ('''std::string gAccountId;''',
  '''std::string gAccountId;
/**
 * Counts sign-ins and server changes in this process.
 *
 * The account name and the server address are not enough to tell one sign-in from the next: sign
 * out and back in as the same user and both are identical, while the session token -- the thing
 * actually handed to an updater -- is a different one. An attempt authorised under the old
 * session must not inherit the new token, and comparing urls cannot see that.
 */
uint64_t gOwnerEpoch = 0;'''),
 ('''      gServerUrl = server;
      gAccountId = accountId;
      gSessionToken = token;''',
  '''      gServerUrl = server;
      gAccountId = accountId;
      gSessionToken = token;
      ++gOwnerEpoch;'''),
 ('''  std::string session;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    session = gSessionToken;
  }
  const upd::UpdateEndpoint endpoint =
      remote60::native_poc::directory::update_endpoint_for(
          remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
          configured_directory_url(), "windows",
          session.empty() ? std::string() : "Authorization: Bearer " + session);''',
  '''  std::string session;
  std::string owner;
  uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    session = gSessionToken;
    owner = gAccountId;
    epoch = gOwnerEpoch;
  }
  const upd::UpdateEndpoint endpoint =
      remote60::native_poc::directory::update_endpoint_for(
          remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
          configured_directory_url(), "windows",
          session.empty() ? std::string() : "Authorization: Bearer " + session, owner, epoch);'''),
 ('''  std::string launchSession;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    launchSession = gSessionToken;
  }
  const upd::UpdateEndpoint launchEndpoint =
      remote60::native_poc::directory::update_endpoint_for(
          remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
          configured_directory_url(), "windows",
          launchSession.empty() ? std::string() : "Authorization: Bearer " + launchSession);''',
  '''  std::string launchSession;
  std::string launchOwner;
  uint64_t launchEpoch = 0;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    launchSession = gSessionToken;
    launchOwner = gAccountId;
    launchEpoch = gOwnerEpoch;
  }
  const upd::UpdateEndpoint launchEndpoint =
      remote60::native_poc::directory::update_endpoint_for(
          remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
          configured_directory_url(), "windows",
          launchSession.empty() ? std::string() : "Authorization: Bearer " + launchSession,
          launchOwner, launchEpoch);'''),
 ('''    std::string nowSession;
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
    }''',
  '''    std::string nowSession;
    std::string nowOwner;
    uint64_t nowEpoch = 0;
    {
      std::lock_guard<std::mutex> lock(gStateMu);
      nowSession = gSessionToken;
      nowOwner = gAccountId;
      nowEpoch = gOwnerEpoch;
    }
    const upd::UpdateEndpoint nowEndpoint =
        remote60::native_poc::directory::update_endpoint_for(
            remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
            configured_directory_url(), "windows",
            nowSession.empty() ? std::string() : "Authorization: Bearer " + nowSession, nowOwner,
            nowEpoch);
    std::string payload;
    // The owner, not the destination. Comparing the url and the origin asks whether the SERVER
    // changed; sign out and back in on the same server and both are identical while the token is
    // a different one -- so that comparison would hand an attempt authorised by one sign-in the
    // credential of the next.
    if (nowEndpoint.same_owner_as(launchEndpoint) && nowEndpoint.url == launchEndpoint.url &&
        nowEndpoint.origin == launchEndpoint.origin && !nowEndpoint.credentialHeader.empty()) {
      payload = encode_update_descriptor(nowEndpoint);
      if (payload.empty()) {
        log_line("update: the credential descriptor could not be built; no credential sent");
      }
    } else {
      log_line("update: the signed-in owner changed after the updater was started; "
               "no credential sent");
    }'''),
])
