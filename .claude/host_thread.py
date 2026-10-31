import io

p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- one snapshot, taken under the lock
old = '''void sign_out(bool keepAccount) {'''
new = '''/**
 * The update endpoint as it stands right now, read once under the lock.
 *
 * A function rather than four reads at each call site: read separately, the account, the machine
 * id, the server address, the token and the epoch can come from either side of a sign-out, and
 * the answer is then a mixture of two owners that never existed together.
 */
upd::UpdateEndpoint current_update_endpoint() {
  std::lock_guard<std::mutex> lock(g.ownerMu);
  return directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken,
      g.cache.accountId + "|" + g.cache.machineId, g.ownerEpoch);
}

void sign_out(bool keepAccount) {'''
assert d.count(old) == 1, 'sign_out anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- call sites use it
pairs = [
 ('''  const upd::UpdateEndpoint endpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken,
      g.cache.accountId + "|" + g.cache.machineId, g.ownerEpoch);

  upd::CheckConfig config;''',
  '''  const upd::UpdateEndpoint endpoint = current_update_endpoint();

  upd::CheckConfig config;'''),
 ('''  const upd::UpdateEndpoint launchEndpoint = directory::update_endpoint_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows",
      g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken,
      g.cache.accountId + "|" + g.cache.machineId, g.ownerEpoch);''',
  '''  const upd::UpdateEndpoint launchEndpoint = current_update_endpoint();'''),
]
for i, (old_, new_) in enumerate(pairs):
    assert d.count(old_) == 1, 'pair %d' % i
    d = d.replace(old_, new_, 1)

# ---------------------------------------------------------------- serve moves onto the thread
start = d.find('  if (!spec.credentialPipeName.empty()) {\n    // Re-read now, not reused from the snapshot')
assert start > 0, 'serve block'
end = d.find('  append_host_app_log("[host-app] update: handed over to the updater, waiting for its signal");',
             start)
assert end > start, 'serve end'
d = d[:start] + d[end:]

old2 = '''  std::thread([window, readyEvent, ackEvent, bootstrap = pi.hProcess,
               readyName = spec.readyEventName, launchEndpoint]() {'''
new2 = '''  // Everything after the launch runs here, including the credential handover. It used to be
  // inline: the menu item's handler waited up to two minutes on the UI thread while the elevated
  // process connected. Moving only the client left the host doing exactly that.
  //
  // The process handle travels with it and is closed here, not before: the handle is what pins
  // the process id the channel's check compares against.
  auto credentialServerOwned =
      std::make_shared<upd::CredentialServer>(std::move(credentialServer));
  std::thread([window, readyEvent, ackEvent, bootstrap = pi.hProcess,
               readyName = spec.readyEventName, launchEndpoint, credentialServerOwned,
               pipeName = spec.credentialPipeName]() {
    if (!pipeName.empty()) {
      // Re-read at the moment of sending, not reused from the snapshot built before the user
      // answered. The owner is the question, not the destination: a re-register keeps the url and
      // the origin and changes the token.
      const upd::UpdateEndpoint nowEndpoint = current_update_endpoint();
      std::string payload;
      if (nowEndpoint.same_owner_as(launchEndpoint) && nowEndpoint.url == launchEndpoint.url &&
          nowEndpoint.origin == launchEndpoint.origin &&
          !nowEndpoint.credentialHeader.empty()) {
        payload = upd::encode_update_descriptor(nowEndpoint);
        if (payload.empty()) {
          append_host_app_log("[host-app] update: the credential descriptor could not be built");
        }
      } else {
        append_host_app_log("[host-app] update: the owner changed after the updater was started; "
                            "no credential sent");
      }
      std::string error;
      if (payload.empty() ||
          !credentialServerOwned->Serve(bootstrap, payload, 120000, &error)) {
        append_host_app_log("[host-app] update: the updater did not receive the credential -- " +
                            (error.empty() ? std::string("owner changed") : error));
      }
      credentialServerOwned->Close();
    }
'''
assert d.count(old2) == 1, 'thread head'
d = d.replace(old2, new2, 1)

# ---------------------------------------------------------------- the ready predicate uses it too
old3 = '''        readyEvent, ackEvent, bootstrap, readyName, 10 * 60 * 1000, nullptr, &why, [&]() {
          // Asked at the acknowledgement, not at the launch. A re-register during the download
          // issues a new token for the same account on the same server -- identical url, identical
          // origin -- so only the owner comparison can see it.
          const upd::UpdateEndpoint nowEndpoint = directory::update_endpoint_for(
              remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
              g.cache.directoryUrl, "windows",
              g.cache.hostToken.empty() ? std::string() : "x-host-token: " + g.cache.hostToken,
              g.cache.accountId + "|" + g.cache.machineId, g.ownerEpoch);
          return nowEndpoint.same_owner_as(launchEndpoint) &&
                 nowEndpoint.url == launchEndpoint.url;
        });'''
new3 = '''        readyEvent, ackEvent, bootstrap, readyName, 10 * 60 * 1000, nullptr, &why, [&]() {
          // Asked at the acknowledgement, not at the launch. A re-register during the download
          // issues a new token for the same account on the same machine -- identical url,
          // identical origin -- so only the owner comparison can see it.
          //
          // And a credential has to still exist. Signing out clears the token and bumps the
          // epoch; without the emptiness check a build where the epoch did not move would
          // acknowledge an attempt for a user who is no longer signed in.
          const upd::UpdateEndpoint atReady = current_update_endpoint();
          return atReady.same_owner_as(launchEndpoint) && atReady.url == launchEndpoint.url &&
                 !atReady.credentialHeader.empty();
        });'''
assert d.count(old3) == 1, 'predicate'
d = d.replace(old3, new3, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok host thread')
