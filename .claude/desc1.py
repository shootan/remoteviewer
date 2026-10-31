import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d' % (path, i)
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)

edit('apps/native_poc/src/update_endpoint.hpp', [
 ('''  /**
   * Who this snapshot belongs to. An answer that arrives after a sign-out or a server change
   * carries the epoch it was sent under, so it can be dropped rather than credited to the new
   * owner.
   */
  uint64_t ownerEpoch = 0;''',
  '''  /**
   * Who this snapshot belongs to.
   *
   * `ownerKey` is the account and machine it was built for; `ownerEpoch` counts sign-ins and
   * server changes in this process. Both are needed and neither is enough:
   *
   *   * the key alone lets the same account sign out and back in and inherit an attempt it never
   *     authorised -- the url is the same, the origin is the same, and the new session token is
   *     not empty, so a comparison of those three says nothing changed;
   *   * the epoch alone would not notice a different account on the same server if the counter
   *     happened to match.
   *
   * Comparing the url and the origin is not an owner check. That was the defect: it asked whether
   * the destination had changed, when the question is whether the person has.
   */
  std::string ownerKey;
  uint64_t ownerEpoch = 0;

  /** True when both identify the same owner. Neither field alone is asked. */
  bool same_owner_as(const UpdateEndpoint& other) const {
    return ownerKey == other.ownerKey && ownerEpoch == other.ownerEpoch;
  }'''),
 ('''bool parse_manifest_envelope(const std::string& body, std::string* document,
                             std::string* signatureHex, std::string* error);''',
  '''bool parse_manifest_envelope(const std::string& body, std::string* document,
                             std::string* signatureHex, std::string* error);

/**
 * The whole snapshot, as it travels over the credential channel.
 *
 * The credential used to be sent on its own, and the receiver rebuilt everything else from its
 * command line -- so the value arrived over a channel that had proved who was listening, and the
 * origin it would be sent to arrived in argv, which had proved nothing. Anything able to start
 * the worker with a different `--manifest-url` could point a real credential somewhere else.
 *
 * So url, origin, owner and wire shape travel WITH the credential, in the same frame, and the
 * receiver checks the frame against its arguments rather than trusting either alone. A mismatch
 * is a refusal: the two disagreeing is itself the signal.
 *
 * Line-based and explicit, because this is parsed by something running elevated: `key=value` per
 * line, unknown keys refused rather than ignored, and no value may contain a newline.
 */
std::string encode_update_descriptor(const UpdateEndpoint& endpoint);

bool decode_update_descriptor(const std::string& text, UpdateEndpoint* endpoint,
                              std::string* error);'''),
])
