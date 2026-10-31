import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

edit('apps/native_poc/src/update_handoff.hpp', [
 ('''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs,
                          const std::function<uint64_t()>& now, std::string* detail);''',
  '''/**
 * `ownerStillValid` is asked once, immediately before the acknowledgement is sent, and never
 * after. Returning false withholds the acknowledgement, so the updater stands down without
 * stopping anything.
 *
 * It exists because "the owner was right when we launched" is a different statement from "the
 * owner is right now". A download takes minutes; a user can sign out, sign in as somebody else,
 * or repoint the client at another server while it runs. The acknowledgement is the last moment
 * anything can be withheld -- after it, the updater stops the product and swaps files.
 *
 * Empty means no owner condition, which is the behaviour this had before.
 */
HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs,
                          const std::function<uint64_t()>& now, std::string* detail,
                          const std::function<bool()>& ownerStillValid = {});'''),
])

edit('apps/native_poc/src/update_handoff_wait.cpp', [
 ('''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs,
                          const std::function<uint64_t()>& now, std::string* detail) {''',
  '''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs,
                          const std::function<uint64_t()>& now, std::string* detail,
                          const std::function<bool()>& ownerStillValid) {'''),
 ('''  if (step == HandoffStep::ExitNow) {
    if (!ack || !SetEvent(ack)) {''',
  '''  if (step == HandoffStep::ExitNow) {
    // The last moment anything can be withheld. After the acknowledgement the updater stops the
    // product and replaces files, so the owner is checked HERE and not only at launch: the
    // download took minutes, and in those minutes the user may have signed out, signed in as
    // somebody else, or repointed this client at another server.
    if (ownerStillValid && !ownerStillValid()) {
      if (detail) {
        *detail = "the signed-in owner changed while the update was downloading, so the "
                  "acknowledgement was withheld and nothing was stopped";
      }
      return HandoffStep::KeepRunning;
    }
    if (!ack || !SetEvent(ack)) {'''),
])

# ---------------------------------------------------------------- the host supplies the predicate
edit('apps/native_poc/src/host_app_main.cpp', [
 ('''  std::thread([window, readyEvent, ackEvent, bootstrap = pi.hProcess,
               readyName = spec.readyEventName]() {''',
  '''  std::thread([window, readyEvent, ackEvent, bootstrap = pi.hProcess,
               readyName = spec.readyEventName, launchEndpoint]() {'''),
 ('''    const upd::HandoffStep step =
        upd::await_handoff(readyEvent, ackEvent, bootstrap, readyName, 10 * 60 * 1000,
                           nullptr, &why);''',
  '''    const upd::HandoffStep step = upd::await_handoff(
        readyEvent, ackEvent, bootstrap, readyName, 10 * 60 * 1000, nullptr, &why, [&]() {
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
        });'''),
])
