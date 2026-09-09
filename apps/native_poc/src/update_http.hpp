#pragma once

// HTTPS for the update path, and only for the update path.
//
// The product's existing HTTP client cannot do this job: it is POST-only, it truncates responses
// at 64 KB, and it refuses https:// outright (directory_client.cpp:83-93). Rather than rework
// something the whole product depends on, the updater gets its own client -- WinHTTP, TLS on,
// used for two things: fetching a manifest and downloading an artifact.
//
// The security decisions live in parse_https_url() and redirect_is_allowed(), which are ordinary
// functions with no I/O. That is deliberate: "we do not accept http://" and "we do not follow a
// redirect down to cleartext" are the two rules worth being certain about, and rules buried in a
// WinHTTP callback can only be tested by standing up a server. Here they can be tested by calling
// them.
//
// Three things this never does, in any configuration:
//   * disable certificate validation (no SECURITY_FLAG_IGNORE_* anywhere)
//   * continue after a certificate error
//   * follow a redirect from https to http
//   * follow ANY redirect on a request that carries a credential -- see below
//
// Design: docs/업데이트_기능_설계.md 4.1.1.

#include <cstdint>
#include <string>

namespace remote60::native_poc::update {

struct HttpsUrl {
  std::wstring host;
  uint16_t port = 443;
  std::wstring path;  // includes the leading '/' and any query
};

/**
 * Parses an https:// URL. Anything else is refused, including http://.
 *
 * The refusal of http:// is not politeness about modern practice -- the artifact this URL leads
 * to gets executed with administrator rights, and a cleartext hop is a place to swap it. The
 * signature check (4.1.2) is the second lock on that door; this is the first.
 */
bool parse_https_url(const std::string& url, HttpsUrl* out, std::string* error);

/**
 * Whether a redirect from `from` to `to` may be followed.
 *
 * The rule is narrow on purpose: the destination must also be https. A redirect that downgrades
 * to cleartext is refused however it is dressed up, because an attacker who can inject a redirect
 * is exactly the attacker TLS was there to stop.
 */
bool redirect_is_allowed(const std::string& from, const std::string& to, std::string* error);

/** How a fetch ended. Any value but Ok means nothing was written. */
enum class FetchStatus {
  Ok,
  BadUrl,          // not https, or unparseable
  ConnectFailed,   // could not reach it, or TLS did not come up
  HttpError,       // reached it, and it said no
  TooLarge,        // the body exceeded the cap
  WriteFailed,     // could not store what arrived
};

const char* fetch_status_name(FetchStatus s);

/**
 * `credentialHeader`, when non-empty, is one header line ("Name: value", no CRLF) sent with the
 * request. Two things change when it is present:
 *
 *   * redirects are turned off entirely for that request. A redirect is a request to send this
 *     again somewhere else, and "somewhere else" is exactly where a credential must not go. The
 *     https-to-http policy is not enough on its own: https://evil is still https.
 *   * nothing else. There is no configuration in which a credential goes out over http, because
 *     there is no configuration in which this client speaks http at all.
 *
 * Deciding WHETHER to pass one is not this file's job: the caller knows which origin it obtained
 * the credential from and where it is about to send it. This only guarantees that if it is
 * passed, it goes to that one request and no other.
 */

/**
 * Fetches a small document into memory.
 *
 * `maxBytes` is a hard cap, not a hint: a manifest is a few hundred bytes and anything claiming
 * to be much larger is not one. Exceeding it aborts rather than truncating -- a truncated
 * manifest would fail its signature check anyway, and failing early says why.
 */
FetchStatus https_get_text(const std::string& url, size_t maxBytes, std::string* out,
                           std::string* error, const std::string& credentialHeader = {});

/**
 * Streams a larger response to a file.
 *
 * Writes to `destPath` only; the caller decides where that is. Deletes a partial file on any
 * failure, so nothing downstream can mistake a short write for a complete download.
 */
FetchStatus https_get_file(const std::string& url, const std::wstring& destPath, uint64_t maxBytes,
                           std::string* error, const std::string& credentialHeader = {});

}  // namespace remote60::native_poc::update
