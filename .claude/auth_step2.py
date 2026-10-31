import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

edit('apps/native_poc/src/directory_client.hpp', [
 ('''std::string directory_update_manifest_url(const std::string& directoryUrl,
                                          const std::string& platform);''',
  '''std::string directory_update_manifest_url(const std::string& directoryUrl,
                                          const std::string& platform);

/**
 * One update fetch, decided once, at the place that knows the answers.
 *
 * A snapshot rather than a set of loose values, because every field here is only correct together
 * with the others. Reading the url back out of global state later and deciding then whether to
 * attach a credential is the shape of the mistake: between the decision and the send, the account
 * can change, the server address can change, and the answer that arrives can belong to whoever
 * happens to be signed in when it lands.
 *
 * `derived` is not a hint. It selects the wire shape, because the two kinds of endpoint really do
 * publish differently and neither can be inferred from a response without guessing:
 *
 *   * derived  -- our directory's route, which answers {"manifest":..,"signature":..} in one body
 *   * override -- an operator's own url, which is a document with a detached `url.sig` beside it
 *
 * Sniffing the response to decide which one arrived would make a malformed answer from one look
 * like a valid answer of the other kind, so the kind is decided from where the url came from.
 *
 * `credentialHeader` may only be sent to `origin`, and only when `origin` is non-empty. An
 * override gets neither, even when it happens to point at the same host: the rule is about where
 * the url came from, not about what it looks like, and a rule about what it looks like is a
 * comparison that can be wrong.
 *
 * `ownerEpoch` is who this snapshot belongs to. An answer that arrives after a sign-out or a
 * server change carries the epoch it was sent under, so it can be dropped rather than attributed
 * to whoever is signed in now.
 */
struct UpdateEndpoint {
  std::string url;
  bool derived = false;
  std::string credentialHeader;  // "Name: value", no CRLF. Empty = send none.
  std::string origin;            // the only origin the credential may reach. Empty = nowhere.
  uint64_t ownerEpoch = 0;

  bool configured() const { return !url.empty(); }
};

/**
 * Builds that snapshot. The override wins and gets no credential; otherwise the url is derived
 * from the directory and carries one.
 *
 * `credentialHeader` is what this process holds -- `x-host-token: ...` on a host, an
 * `Authorization: Bearer ...` session elsewhere. Passing an empty one is normal: a client that is
 * not signed in yet has nothing to send, and the server will say so.
 */
UpdateEndpoint update_endpoint_for(const std::string& override_, const std::string& directoryUrl,
                                   const std::string& platform,
                                   const std::string& credentialHeader = {},
                                   uint64_t ownerEpoch = 0);

/**
 * Whether this snapshot's credential may go to `targetUrl`.
 *
 * Checked against the destination of the request actually about to be made -- the manifest url,
 * and separately every artifact url the manifest names. A signed manifest says its artifacts are
 * the right bytes; it does not say the credential for our directory belongs to whatever host the
 * artifacts are published on.
 */
bool update_credential_allowed(const UpdateEndpoint& endpoint, const std::string& targetUrl);'''),
])

edit('apps/native_poc/src/directory_client.cpp', [
 ('''std::string update_manifest_url_for(const std::string& override_, const std::string& directoryUrl,
                                    const std::string& platform) {
  if (!override_.empty()) return override_;
  return directory_update_manifest_url(directoryUrl, platform);
}''',
  '''std::string update_manifest_url_for(const std::string& override_, const std::string& directoryUrl,
                                    const std::string& platform) {
  if (!override_.empty()) return override_;
  return directory_update_manifest_url(directoryUrl, platform);
}

UpdateEndpoint update_endpoint_for(const std::string& override_, const std::string& directoryUrl,
                                   const std::string& platform,
                                   const std::string& credentialHeader, uint64_t ownerEpoch) {
  UpdateEndpoint endpoint;
  endpoint.ownerEpoch = ownerEpoch;
  if (!override_.empty()) {
    // An operator's own url. No credential, whatever it points at -- including our own host. What
    // decides is where the url came from, and an override did not come from here.
    endpoint.url = override_;
    endpoint.derived = false;
    return endpoint;
  }
  endpoint.url = directory_update_manifest_url(directoryUrl, platform);
  if (endpoint.url.empty()) return endpoint;  // http directory, or none at all
  endpoint.derived = true;
  endpoint.credentialHeader = credentialHeader;
  endpoint.origin = directory_origin_key(directoryUrl);
  return endpoint;
}

bool update_credential_allowed(const UpdateEndpoint& endpoint, const std::string& targetUrl) {
  if (endpoint.credentialHeader.empty() || endpoint.origin.empty()) return false;
  if (!endpoint.derived) return false;
  if (targetUrl.empty()) return false;
  // The destination of the request about to be made, not the one this snapshot was built for.
  // A manifest is free to name artifacts anywhere; the credential does not follow them there.
  return directory_origin_key(targetUrl) == endpoint.origin;
}'''),
])
