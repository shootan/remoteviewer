import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ---------------------------------------------------------------- the rule, once
edit('apps/native_poc/src/directory_client.hpp', [
 ('''std::string directory_origin_key(const std::string& url);''',
  '''std::string directory_origin_key(const std::string& url);

/**
 * Where update manifests live, derived from the directory the user already signed in to.
 *
 * Until now the manifest url only came from an environment variable, so a machine configured with
 * nothing but a server address could never check for updates -- the address was there, the route
 * was there, and nothing joined them.
 *
 * Built from the origin, not by pasting strings: `directory_origin_key()` decides the scheme,
 * host and port so this cannot disagree with the client about which server it is, and the path
 * and the platform query are appended whole. The server hands back the document and its detached
 * signature in one response, so there is no second `.sig` request to get wrong.
 *
 * Empty when there is nothing to derive from, and **empty for an http directory**. This is not a
 * place to guess: upgrading the caller to https on its behalf would invent a server nobody
 * configured, and following the directory down to http would fetch an administrator-privileged
 * artifact over a hop anyone can rewrite. An http deployment that wants updates says so with an
 * explicit https override.
 */
std::string directory_update_manifest_url(const std::string& directoryUrl,
                                          const std::string& platform);

/**
 * The explicit override when there is one, otherwise the derived url.
 *
 * The override keeps working exactly as before -- a deployment that set it is not moved onto a
 * different server by this change.
 */
std::string update_manifest_url_for(const std::string& override_, const std::string& directoryUrl,
                                    const std::string& platform);'''),
])

edit('apps/native_poc/src/directory_client.cpp', [
 ('''bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,''',
  '''std::string directory_update_manifest_url(const std::string& directoryUrl,
                                          const std::string& platform) {
  if (directoryUrl.empty() || platform.empty()) return {};
  // http is refused rather than upgraded or followed. See the header.
  if (!directory_url_is_secure(directoryUrl)) return {};
  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!parse_directory_url(directoryUrl, &host, &port, nullptr, &secure) || !secure) return {};
  return directory_origin_key(directoryUrl) + "/api/update/manifest?platform=" + platform;
}

std::string update_manifest_url_for(const std::string& override_, const std::string& directoryUrl,
                                    const std::string& platform) {
  if (!override_.empty()) return override_;
  return directory_update_manifest_url(directoryUrl, platform);
}

bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,'''),
])

# ---------------------------------------------------------------- the host's two call sites
edit('apps/native_poc/src/host_app_main.cpp', [
 ('''  upd::CheckConfig config;
  config.manifestUrl = remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL");''',
  '''  upd::CheckConfig config;
  // The override still wins; otherwise it comes from the directory this host is registered with,
  // so a machine configured with only a server address can check for updates.
  config.manifestUrl = directory::update_manifest_url_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows");'''),
 ('''  spec.manifestUrl = remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL");
  spec.platform = "windows";
  spec.installedVersion = narrow(kProductVersion);
  spec.healthLogPath = log_file_path();''',
  '''  // The same url the check used, from the same rule: the updater must fetch what the user was
  // told about, not a second opinion about where updates live.
  spec.manifestUrl = directory::update_manifest_url_for(
      remote60::native_poc::env_string_or_empty("REMOTE60_UPDATE_MANIFEST_URL"),
      g.cache.directoryUrl, "windows");
  spec.platform = "windows";
  spec.installedVersion = narrow(kProductVersion);
  spec.healthLogPath = log_file_path();'''),
])
