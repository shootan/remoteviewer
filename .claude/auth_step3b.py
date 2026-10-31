import io

BS = chr(92)

p = 'apps/native_poc/src/update_check.cpp'
d = io.open(p, encoding='utf-8').read()

start = d.find('ManifestFetcher https_manifest_fetcher() {')
assert start > 0
end = d.find('}  // namespace remote60::native_poc::update')
assert end > start
old = d[start:end]
assert old.count('extract("manifest"') == 1

new = '''namespace {

/**
 * Our directory's shape: one object carrying the document and its detached signature.
 *
 * Parsed by hand rather than with a JSON library, because two string fields do not justify a
 * dependency in the product -- and because the document's exact bytes decide whether its
 * signature verifies, so the extraction has to be something that can be read and checked.
 */
bool envelope_fetch(const std::string& url, size_t maxBytes, const std::string& credentialHeader,
                    std::string* document, std::string* signatureHex, std::string* error) {
  std::string body;
  const FetchStatus status = https_get_text(url, maxBytes, &body, error, credentialHeader);
  if (status != FetchStatus::Ok) {
    if (error && error->empty()) *error = fetch_status_name(status);
    return false;
  }

  const auto extract = [&body](const char* key, std::string* out) {
    const std::string needle = std::string("\\"") + key + "\\":\\"";
    const size_t at = body.find(needle);
    if (at == std::string::npos) return false;
    size_t i = at + needle.size();
    out->clear();
    while (i < body.size() && body[i] != '"') {
      if (body[i] == '\\\\' && i + 1 < body.size()) {
        // Only the escapes a manifest actually contains: newlines, and a literal backslash or
        // quote. Anything else is left as-is rather than guessed at.
        const char next = body[i + 1];
        if (next == 'n') out->push_back('\\n');
        else if (next == 'r') out->push_back('\\r');
        else if (next == 't') out->push_back('\\t');
        else out->push_back(next);
        i += 2;
        continue;
      }
      out->push_back(body[i++]);
    }
    return i < body.size();
  };

  if (!extract("manifest", document) || !extract("signature", signatureHex)) {
    if (error) *error = "response did not carry a manifest and a signature";
    return false;
  }
  return true;
}

/** An operator's own url: the document, then `url.sig` beside it. */
bool detached_fetch(const std::string& url, size_t maxBytes, std::string* document,
                    std::string* signatureHex, std::string* error) {
  const FetchStatus status = https_get_text(url, maxBytes, document, error);
  if (status != FetchStatus::Ok) {
    if (error && error->empty()) *error = fetch_status_name(status);
    return false;
  }
  const FetchStatus sig = https_get_text(url + ".sig", 4 * 1024, signatureHex, error);
  if (sig != FetchStatus::Ok) {
    if (error && error->empty()) *error = fetch_status_name(sig);
    return false;
  }
  while (!signatureHex->empty() &&
         (signatureHex->back() == '\\n' || signatureHex->back() == '\\r' ||
          signatureHex->back() == ' ')) {
    signatureHex->pop_back();
  }
  return true;
}

}  // namespace

ManifestFetcher https_manifest_fetcher() {
  return [](const std::string& url, size_t maxBytes, std::string* document,
            std::string* signatureHex, std::string* error) {
    return envelope_fetch(url, maxBytes, {}, document, signatureHex, error);
  };
}

ManifestFetcher detached_manifest_fetcher() {
  return [](const std::string& url, size_t maxBytes, std::string* document,
            std::string* signatureHex, std::string* error) {
    return detached_fetch(url, maxBytes, document, signatureHex, error);
  };
}

ManifestFetcher manifest_fetcher_for(const CheckConfig& config) {
  // The kind decides the shape. Both kinds are real -- a static host publishes two files, our
  // server answers one object -- and choosing between them by looking at what came back would
  // turn a truncated response of one kind into a plausible response of the other.
  if (!config.derivedEndpoint) return detached_manifest_fetcher();

  directory::UpdateEndpoint endpoint;
  endpoint.derived = true;
  endpoint.credentialHeader = config.credentialHeader;
  endpoint.origin = config.credentialOrigin;
  return [endpoint](const std::string& url, size_t maxBytes, std::string* document,
                    std::string* signatureHex, std::string* error) {
    // Judged against the url about to be fetched, not the one the snapshot was built from.
    const std::string header = directory::update_credential_allowed(endpoint, url)
                                   ? endpoint.credentialHeader
                                   : std::string();
    return envelope_fetch(url, maxBytes, header, document, signatureHex, error);
  };
}

'''

d = d[:start] + new + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok update_check.cpp')

# ---------------------------------------------------------------- header declarations
p = 'apps/native_poc/src/update_check.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''ManifestFetcher https_manifest_fetcher();'''
new = '''/** The fetcher for a url the directory gave us: one body carrying both fields. */
ManifestFetcher https_manifest_fetcher();

/**
 * The fetcher for an operator's own url: the document, then `url.sig` beside it, no credential.
 *
 * Both shapes exist because both are real. A static host publishes two files; our server answers
 * one object. Guessing between them from the response would turn a truncated answer of one kind
 * into a plausible answer of the other.
 */
ManifestFetcher detached_manifest_fetcher();

/** Picks the fetcher the config's endpoint kind calls for, and binds its credential to it. */
ManifestFetcher manifest_fetcher_for(const CheckConfig& config);'''
assert d.count(old) == 1
d = d.replace(old, new, 1)

old2 = '''struct CheckConfig {
  std::string manifestUrl;'''
new2 = '''struct CheckConfig {
  std::string manifestUrl;
  /**
   * Which wire shape this url answers with, and what may be sent to it.
   *
   * True means our directory's route: one body carrying the document and its signature, and a
   * credential this process is allowed to send there. False means an operator's own url: a
   * document with a detached `url.sig`, and nothing sent with it -- an override is somebody
   * else's server by definition, even when it points at the same host.
   */
  bool derivedEndpoint = false;
  /** "Name: value", no CRLF. Sent only when the fetch destination is `credentialOrigin`. */
  std::string credentialHeader;
  std::string credentialOrigin;'''
assert d.count(old2) == 1
d = d.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok update_check.hpp')
