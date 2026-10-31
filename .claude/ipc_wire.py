import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ---------------------------------------------------------------- the option and the argument
edit('apps/native_poc/src/updater_options.hpp', [
 ('''  bool derivedEndpoint = false;''',
  '''  bool derivedEndpoint = false;
  /**
   * The pipe this process reads its credential from, when there is one.
   *
   * A NAME, not the credential: the name is worthless to anyone who cannot also prove they are
   * the process that was started, which is what the channel checks before writing. The credential
   * itself never appears in an argument, an environment block, a file or a log.
   *
   * Empty means no credential is coming, which is the ordinary case for an override endpoint --
   * somebody else's server gets nothing from us.
   */
  std::wstring credentialPipeName;'''),
])

edit('apps/native_poc/src/updater_options.cpp', [
 ('''    } else if (arg == L"--manifest-envelope") {''',
  '''    } else if (arg == L"--credential-pipe") {
      if (!(value = need_value(i, L"--credential-pipe"))) return result;
      result.options.credentialPipeName = *value;
    } else if (arg == L"--manifest-envelope") {'''),
])

edit('apps/native_poc/src/update_handoff.hpp', [
 ('''  /** See UpdaterOptions::derivedEndpoint -- the wire shape travels with the url. */
  bool derivedEndpoint = false;''',
  '''  /** See UpdaterOptions::derivedEndpoint -- the wire shape travels with the url. */
  bool derivedEndpoint = false;
  /** The name of the pipe the credential will be served on. A name, never the credential. */
  std::wstring credentialPipeName;'''),
])

edit('apps/native_poc/src/update_handoff.cpp', [
 ('''  if (spec.derivedEndpoint) args.push_back(L"--manifest-envelope");''',
  '''  if (spec.derivedEndpoint) args.push_back(L"--manifest-envelope");
  // The name only. Whoever reads it still has to prove it is the process we started before
  // anything is written to it.
  if (!spec.credentialPipeName.empty()) {
    add(L"--credential-pipe", spec.credentialPipeName);
  }'''),
])

# ---------------------------------------------------------------- the worker's fetchers
edit('apps/native_poc/src/updater_effects.hpp', [
 ('''UpdaterDeps production_updater_deps(std::function<void(const std::string&)> log);''',
  '''/**
 * `endpoint` decides what may be attached to which request. An endpoint with no credential -- an
 * override, or a run that never received one -- produces fetchers that send nothing, which is the
 * behaviour this had before there was a credential at all.
 */
UpdaterDeps production_updater_deps(std::function<void(const std::string&)> log,
                                    const UpdateEndpoint& endpoint = {});'''),
])

edit('apps/native_poc/src/updater_effects.cpp', [
 ('''UpdaterDeps production_updater_deps(std::function<void(const std::string&)> log) {
  UpdaterDeps deps;
  deps.log = log ? std::move(log) : [](const std::string&) {};

  deps.fetchText = [](const std::string& url, size_t maxBytes, std::string* body,
                      std::string* error) {
    return https_get_text(url, maxBytes, body, error) == FetchStatus::Ok;
  };
  deps.fetchFile = [](const std::string& url, const std::wstring& destPath, uint64_t maxBytes,
                      std::string* error) {
    return https_get_file(url, destPath, maxBytes, error) == FetchStatus::Ok;
  };''',
  '''UpdaterDeps production_updater_deps(std::function<void(const std::string&)> log,
                                   const UpdateEndpoint& endpoint) {
  UpdaterDeps deps;
  deps.log = log ? std::move(log) : [](const std::string&) {};

  // Judged per request, against the url about to be fetched. The manifest may name artifacts on
  // any host; a signature over it says those bytes are the right bytes, not that our directory's
  // credential belongs to whoever serves them.
  deps.fetchText = [endpoint](const std::string& url, size_t maxBytes, std::string* body,
                              std::string* error) {
    const std::string credential =
        credential_allowed(endpoint, url) ? endpoint.credentialHeader : std::string();
    return https_get_text(url, maxBytes, body, error, credential) == FetchStatus::Ok;
  };
  deps.fetchFile = [endpoint](const std::string& url, const std::wstring& destPath,
                              uint64_t maxBytes, std::string* error) {
    const std::string credential =
        credential_allowed(endpoint, url) ? endpoint.credentialHeader : std::string();
    return https_get_file(url, destPath, maxBytes, error, credential) == FetchStatus::Ok;
  };'''),
])
