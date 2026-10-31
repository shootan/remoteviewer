import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# --------------------------------------------- the updater learns which shape its url answers with
edit('apps/native_poc/src/updater_options.hpp', [
 ('''  std::string manifestUrl;''',
  '''  std::string manifestUrl;
  /**
   * Whether that url is our directory's route (one body carrying document and signature) or an
   * operator's own (a document with a detached `url.sig`).
   *
   * Passed in rather than guessed, because both shapes are real and a truncated answer of one
   * looks like a valid answer of the other. The worker used to assume the detached shape always,
   * which meant a derived url made it ask for `...?platform=windows.sig` -- an address the server
   * has never served. Nobody saw it because the url only ever came from an environment variable,
   * so that line never ran against the real server.
   */
  bool derivedEndpoint = false;'''),
])

edit('apps/native_poc/src/update_handoff.hpp', [
 ('''  std::string manifestUrl;''',
  '''  std::string manifestUrl;
  /** See UpdaterOptions::derivedEndpoint -- the wire shape travels with the url. */
  bool derivedEndpoint = false;'''),
])

edit('apps/native_poc/src/update_handoff.cpp', [
 ('''  add(L"--manifest-url", widen_ascii(spec.manifestUrl));''',
  '''  add(L"--manifest-url", widen_ascii(spec.manifestUrl));
  // The shape travels with the url. Without it the worker would have to decide from the response,
  // and that decision is exactly the one that cannot be made safely.
  if (spec.derivedEndpoint) args.push_back(L"--manifest-envelope");'''),
])

# --------------------------------------------------------------- the worker fetches the right shape
edit('apps/native_poc/src/updater_effects.cpp', [
 ('''  const std::string manifestUrl = options_.manifestUrl;''',
  '''  const std::string manifestUrl = options_.manifestUrl;
  const bool envelope = options_.derivedEndpoint;'''),
 ('''  config.fetchManifest = [manifestUrl, deps](std::string* document, std::string* signatureHex) {
    std::string error;
    if (!deps.fetchText(manifestUrl, 64 * 1024, document, &error)) {
      deps.log("manifest fetch failed: " + error);
      return false;
    }
    // The detached signature lives beside the document, fetched separately so the bytes verified
    // are exactly the bytes that arrived with nothing wrapping them.
    if (!deps.fetchText(manifestUrl + ".sig", 4 * 1024, signatureHex, &error)) {
      deps.log("signature fetch failed: " + error);
      return false;
    }''',
  '''  config.fetchManifest = [manifestUrl, envelope, deps](std::string* document,
                                                       std::string* signatureHex) {
    std::string error;
    std::string body;
    if (!deps.fetchText(manifestUrl, 64 * 1024, envelope ? &body : document, &error)) {
      deps.log("manifest fetch failed: " + error);
      return false;
    }
    if (envelope) {
      // Our directory answers one object carrying both. Asking for `url + ".sig"` here appended
      // to a query string -- `...?platform=windows.sig` -- an address the server has never
      // served. It never showed because the url only came from an environment variable, so this
      // line had never run against the real server.
      if (!update::parse_manifest_envelope(body, document, signatureHex, &error)) {
        deps.log("manifest response malformed: " + error);
        return false;
      }
      return true;
    }
    // An operator's own url: the detached signature lives beside the document, fetched separately
    // so the bytes verified are exactly the bytes that arrived with nothing wrapping them.
    if (!deps.fetchText(manifestUrl + ".sig", 4 * 1024, signatureHex, &error)) {
      deps.log("signature fetch failed: " + error);
      return false;
    }'''),
])
