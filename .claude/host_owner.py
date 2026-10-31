import io

p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()

pairs = [
 ('''  directory::HostCache cache;''',
  '''  directory::HostCache cache;
  /**
   * Counts sign-ins in this process.
   *
   * A re-register issues a NEW host token for the same account on the same machine, so the
   * account name, the machine id and the url are all identical across it. An attempt authorised
   * before that must not be handed the token issued after it, and no comparison of the
   * destination can see the difference.
   */
  uint64_t ownerEpoch = 0;'''),
 ('''        g.cache.hostToken = result->hostToken;''',
  '''        g.cache.hostToken = result->hostToken;
        ++g.ownerEpoch;'''),
 ('''  const upd::UpdateEndpoint endpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken);

  upd::CheckConfig config;''',
  '''  const upd::UpdateEndpoint endpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken,
      g.cache.accountId + "|" + g.cache.machineId, g.ownerEpoch);

  upd::CheckConfig config;'''),
 ('''  const upd::UpdateEndpoint launchEndpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken);''',
  '''  const upd::UpdateEndpoint launchEndpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken,
      g.cache.accountId + "|" + g.cache.machineId, g.ownerEpoch);'''),
 ('''    const upd::UpdateEndpoint nowEndpoint = directory::update_endpoint_for(
        remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
        g.cache.directoryUrl, "windows",
        g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken);
    std::string payload;
    if (nowEndpoint.url == launchEndpoint.url && nowEndpoint.origin == launchEndpoint.origin &&
        !nowEndpoint.credentialHeader.empty()) {
      payload = nowEndpoint.credentialHeader;
    } else {''',
  '''    const upd::UpdateEndpoint nowEndpoint = directory::update_endpoint_for(
        remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
        g.cache.directoryUrl, "windows",
        g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken,
        g.cache.accountId + "|" + g.cache.machineId, g.ownerEpoch);
    std::string payload;
    // The owner, not the destination: a re-register keeps the url and the origin and changes the
    // token, so comparing those two would hand the new token to an older attempt.
    if (nowEndpoint.same_owner_as(launchEndpoint) && nowEndpoint.url == launchEndpoint.url &&
        nowEndpoint.origin == launchEndpoint.origin && !nowEndpoint.credentialHeader.empty()) {
      payload = upd::encode_update_descriptor(nowEndpoint);
      if (payload.empty()) {
        append_host_app_log("[host-app] update: the credential descriptor could not be built");
      }
    } else {'''),
]
for i, (old, new) in enumerate(pairs):
    assert d.count(old) == 1, 'pair %d: %s' % (i, old[:60])
    d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok host')
