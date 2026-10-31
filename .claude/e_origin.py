import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

edit('apps/native_poc/src/directory_client.hpp', [
 ('''bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError, bool* outSecure = nullptr);''',
  '''bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError, bool* outSecure = nullptr);

/**
 * "scheme://host:port" for deciding whether two spellings mean the same server.
 *
 * Whether a cached host token still belongs where it was issued used to be a string comparison
 * against the url as typed. So `http://rem.example:8080/` and `http://rem.example:8080` were two
 * servers, and so were `http://rem.example` and `http://rem.example:80` -- and each mismatch
 * threw the token away and re-registered. Nothing was unsafe about it; it just made a trailing
 * slash cost an unattended PC its cached credentials.
 *
 * The port is always written out and the host is lowercased, so the defaults and the case cannot
 * make two names of one server look different. The scheme stays in it: http and https are not the
 * same origin, and a token issued to one has no business being sent to the other.
 *
 * This is for comparison only. What the user typed is what stays stored and shown -- rewriting
 * that under them is a different kind of surprise.
 */
std::string directory_origin_key(const std::string& url);'''),
])

edit('apps/native_poc/src/directory_client.cpp', [
 ('''bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,''',
  '''std::string directory_origin_key(const std::string& url) {
  std::string host;
  uint16_t port = 0;
  bool secure = false;
  if (!parse_directory_url(url, &host, &port, nullptr, &secure)) {
    // Not parseable, so there is no origin to name. Returned lowercased and trimmed so an
    // unusable value still compares equal to itself rather than to nothing.
    std::string fallback = trim(url);
    for (char& c : fallback) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return fallback;
  }
  for (char& c : host) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return (secure ? "https://" : "http://") + host + ":" + std::to_string(port);
}

bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,'''),
 ('''  // A token is only meaningful for the account, server and machine it was issued against.
  if (cached.directoryUrl != cfg_.url || cached.machineId != machineId_) return false;''',
  '''  // A token is only meaningful for the account, server and machine it was issued against.
  // Compared by origin, so a trailing slash or a written-out default port does not read as a
  // different server -- and so http and https still do.
  if (directory_origin_key(cached.directoryUrl) != directory_origin_key(cfg_.url) ||
      cached.machineId != machineId_) {
    return false;
  }'''),
])

edit('apps/native_poc/src/host_app_main.cpp', [
 ('''  if (g.cache.accountId != account || g.cache.directoryUrl != url) {''',
  '''  if (g.cache.accountId != account ||
      directory::directory_origin_key(g.cache.directoryUrl) !=
          directory::directory_origin_key(url)) {'''),
])

A = 'apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/'
edit(A + 'DirectoryClient.kt', [
 ('''    /** Public because the log uploader needs the same answer; two of these is how they drift. */
    fun normalizedUrl(url: String): String = normalize(url)''',
  '''    /** Public because the log uploader needs the same answer; two of these is how they drift. */
    fun normalizedUrl(url: String): String = normalize(url)

    /**
     * "scheme://host:port" for deciding whether two spellings mean the same server.
     *
     * A trailing slash and a written-out default port are the same server; http and https are
     * not. Used for comparison only -- what the user typed stays stored and shown, because
     * rewriting that under them is its own surprise.
     */
    fun originKey(url: String): String =
        try {
            val parsed = java.net.URL(normalize(url))
            val port = if (parsed.port > 0) parsed.port else if (parsed.protocol == "https") 443 else 80
            parsed.protocol.lowercase() + "://" + parsed.host.lowercase() + ":" + port
        } catch (e: Exception) {
            normalize(url).lowercase()
        }'''),
])
