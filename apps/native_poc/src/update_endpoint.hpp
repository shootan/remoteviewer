#pragma once

// One update fetch, decided once, at the place that knows the answers.
//
// A snapshot rather than a set of loose values, because every field here is only correct together
// with the others. Reading the url back out of global state later and deciding *then* whether to
// attach a credential is the shape of the mistake: between the decision and the send, the account
// can change, the server address can change, and an answer that arrives late can be attributed to
// whoever happens to be signed in when it lands.
//
// Shared by the product and the updater, which are otherwise deliberately separate trees.

#include <cstdint>
#include <string>

namespace remote60::native_poc::update {

struct UpdateEndpoint {
  /** Where to fetch. Empty means this build has no endpoint -- not configured, not failed. */
  std::string url;

  /**
   * Which wire shape this url answers with. Not a hint, and not inferred from a response.
   *
   *   * true  -- our directory's route: {"manifest":..,"signature":..} in one body
   *   * false -- an operator's own url: a document with a detached `url.sig` beside it
   *
   * Both are real: a static host publishes two files, our server answers one object. Choosing
   * between them by looking at what came back would turn a truncated answer of one kind into a
   * plausible answer of the other, so the kind comes from where the url came from.
   */
  bool derived = false;

  /** "Name: value", no CRLF. Empty means send none. */
  std::string credentialHeader;

  /** The only origin the credential may reach. Empty means nowhere. */
  std::string origin;

  /**
   * Who this snapshot belongs to. An answer that arrives after a sign-out or a server change
   * carries the epoch it was sent under, so it can be dropped rather than credited to the new
   * owner.
   */
  uint64_t ownerEpoch = 0;

  bool configured() const { return !url.empty(); }
};

/**
 * Whether this snapshot's credential may go to `targetUrl`.
 *
 * Checked against the destination of the request actually about to be made -- the manifest url,
 * and separately every artifact url the manifest names. A signed manifest says its artifacts are
 * the bytes that were signed for; it says nothing about whether the credential for our directory
 * belongs to whatever host those artifacts are published on.
 *
 * An override never qualifies, even when it points at the same host. What decides is where the
 * url came from, not what it looks like -- a rule about what it looks like is a comparison, and a
 * comparison can be wrong on the day it matters.
 */
bool credential_allowed(const UpdateEndpoint& endpoint, const std::string& targetUrl);

/**
 * Pulls the document and its signature out of our directory's one-object answer.
 *
 * Parsed by hand rather than with a JSON library: two string fields do not justify a dependency,
 * and the document's exact bytes decide whether its signature verifies, so the extraction has to
 * be something that can be read and checked. Only the escapes a manifest actually contains are
 * decoded; an unterminated string is a truncated response and not a field.
 *
 * Shared so the UI check and the elevated worker read the same shape the same way.
 */
bool parse_manifest_envelope(const std::string& body, std::string* document,
                             std::string* signatureHex, std::string* error);

}  // namespace remote60::native_poc::update
