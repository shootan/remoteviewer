import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

# ---------------------------------------------------------------- the builder takes an owner
edit('apps/native_poc/src/directory_client.hpp', [
 ('''update::UpdateEndpoint update_endpoint_for(const std::string& override_,
                                           const std::string& directoryUrl,
                                           const std::string& platform,
                                           const std::string& credentialHeader = {},
                                           uint64_t ownerEpoch = 0);''',
  '''update::UpdateEndpoint update_endpoint_for(const std::string& override_,
                                           const std::string& directoryUrl,
                                           const std::string& platform,
                                           const std::string& credentialHeader = {},
                                           const std::string& ownerKey = {},
                                           uint64_t ownerEpoch = 0);'''),
])

edit('apps/native_poc/src/directory_client.cpp', [
 ('''update::UpdateEndpoint update_endpoint_for(const std::string& override_,
                                          const std::string& directoryUrl,
                                          const std::string& platform,
                                          const std::string& credentialHeader,
                                          uint64_t ownerEpoch) {
  update::UpdateEndpoint endpoint;
  endpoint.ownerEpoch = ownerEpoch;''',
  '''update::UpdateEndpoint update_endpoint_for(const std::string& override_,
                                          const std::string& directoryUrl,
                                          const std::string& platform,
                                          const std::string& credentialHeader,
                                          const std::string& ownerKey, uint64_t ownerEpoch) {
  update::UpdateEndpoint endpoint;
  endpoint.ownerKey = ownerKey;
  endpoint.ownerEpoch = ownerEpoch;'''),
])

# ---------------------------------------------------------------- the worker checks the frame
edit('apps/native_poc/src/updater_main.cpp', [
 ('''  // What the worker may send, and where. Derived urls are on the directory's own origin by
  // construction, so that is the one origin this credential may reach; every artifact url is
  // judged against it separately.
  UpdateEndpoint endpoint;
  endpoint.url = options.manifestUrl;
  endpoint.derived = options.derivedEndpoint;
  if (options.derivedEndpoint && !credential.empty()) {
    endpoint.credentialHeader = credential;
    endpoint.origin = url_origin_key(options.manifestUrl);
  }''',
  '''  // What the worker may send, and where -- taken from the frame, not rebuilt from argv.
  //
  // The credential arrives over a channel that proved who was listening. If the origin it may be
  // sent to were rebuilt here from --manifest-url, that part would have proved nothing: anything
  // able to start this process with a different url could aim a real credential somewhere else.
  // So the frame carries url, origin, owner and shape, and the two are checked against each
  // other. Disagreeing is itself the answer.
  UpdateEndpoint endpoint;
  endpoint.url = options.manifestUrl;
  endpoint.derived = options.derivedEndpoint;
  if (!credential.empty()) {
    std::string descriptorError;
    UpdateEndpoint fromFrame;
    if (!decode_update_descriptor(credential, &fromFrame, &descriptorError)) {
      log_line("the credential frame did not parse: " + descriptorError);
      return 6;
    }
    if (fromFrame.url != options.manifestUrl) {
      log_line("the credential was issued for a different url than this run was given");
      return 6;
    }
    if (fromFrame.derived != options.derivedEndpoint) {
      log_line("the credential was issued for a different wire shape than this run was given");
      return 6;
    }
    if (fromFrame.derived && fromFrame.origin.empty()) {
      log_line("the credential frame names no origin");
      return 6;
    }
    endpoint = fromFrame;
    // Belt and braces: whatever the frame said, a derived url is on its own origin.
    if (endpoint.derived && endpoint.origin != url_origin_key(options.manifestUrl)) {
      log_line("the credential's origin is not the manifest url's own");
      return 6;
    }
  }'''),
])
