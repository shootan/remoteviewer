import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ---------------------------------------------------------------- the UI check path
edit('apps/native_poc/src/update_check.hpp', [
 ('''/** What a check needs. Empty url or key means NotConfigured -- there is no default endpoint. */
struct CheckConfig {
  std::string manifestUrl;''',
  '''/** What a check needs. Empty url or key means NotConfigured -- there is no default endpoint. */
struct CheckConfig {
  std::string manifestUrl;
  /**
   * Which wire shape the url answers with, and what may be sent to it.
   *
   * `derivedEndpoint` true means our directory's route: one body carrying both the document and
   * its signature, and a credential this process is allowed to send. False means an operator's
   * own url: a document with a detached `url.sig`, and nothing sent with it.
   *
   * Not inferred from the response. A malformed answer of one kind looks like a valid answer of
   * the other, so the kind comes from where the url came from.
   */
  bool derivedEndpoint = false;
  /** "Name: value", no CRLF. Sent only when the fetch destination is `credentialOrigin`. */
  std::string credentialHeader;
  std::string credentialOrigin;'''),
 ('''ManifestFetcher https_manifest_fetcher();''',
  '''/**
 * The fetcher for a url the directory gave us: one body, both fields, credential attached.
 */
ManifestFetcher https_manifest_fetcher();

/**
 * The fetcher for an operator's own url: the document, then `url.sig` beside it, no credential.
 *
 * Both shapes exist because both are real. A static host publishes two files; our server answers
 * one object. Guessing between them by looking at what came back would turn a truncated response
 * of one kind into a plausible response of the other.
 */
ManifestFetcher detached_manifest_fetcher();

/** Picks the fetcher the config's endpoint kind calls for, and binds its credential. */
ManifestFetcher manifest_fetcher_for(const CheckConfig& config);'''),
])

edit('apps/native_poc/src/update_check.cpp', [
 ('''ManifestFetcher https_manifest_fetcher() {
  return [](const std::string& url, size_t maxBytes, std::string* document,
            std::string* signatureHex, std::string* error) {''',
  '''ManifestFetcher detached_manifest_fetcher() {
  // Two files: the document, and its signature beside it. No credential on either -- this shape
  // belongs to an override, and an override is somebody else's server by definition.
  return [](const std::string& url, size_t maxBytes, std::string* document,
            std::string* signatureHex, std::string* error) {
    if (https_get_text(url, maxBytes, document, error) != FetchStatus::Ok) return false;
    if (https_get_text(url + ".sig", 4 * 1024, signatureHex, error) != FetchStatus::Ok) {
      return false;
    }
    while (!signatureHex->empty() &&
           (signatureHex->back() == '\\n' || signatureHex->back() == '\\r' ||
            signatureHex->back() == ' ')) {
      signatureHex->pop_back();
    }
    return true;
  };
}

ManifestFetcher manifest_fetcher_for(const CheckConfig& config) {
  if (!config.derivedEndpoint) return detached_manifest_fetcher();
  const std::string credential = config.credentialHeader;
  const std::string origin = config.credentialOrigin;
  return [credential, origin](const std::string& url, size_t maxBytes, std::string* document,
                              std::string* signatureHex, std::string* error) {
    // Checked against the url about to be fetched, not the one the snapshot was built for.
    UpdateEndpoint endpoint;
    endpoint.derived = true;
    endpoint.credentialHeader = credential;
    endpoint.origin = origin;
    const std::string header =
        directory::update_credential_allowed(endpoint, url) ? credential : std::string();
    return envelope_fetch(url, maxBytes, header, document, signatureHex, error);
  };
}

ManifestFetcher https_manifest_fetcher() {
  return [](const std::string& url, size_t maxBytes, std::string* document,
            std::string* signatureHex, std::string* error) {
    return envelope_fetch(url, maxBytes, {}, document, signatureHex, error);
  };
}

namespace {

/** Our server's shape: one object carrying the document and its detached signature. */
bool envelope_fetch(const std::string& url, size_t maxBytes, const std::string& credentialHeader,
                    std::string* document, std::string* signatureHex, std::string* error) {
  {'''),
])
