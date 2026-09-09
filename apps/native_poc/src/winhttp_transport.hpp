#pragma once

// Sending an HTTP request over WinHTTP, with one TLS posture written once.
//
// This exists because two parts of the product need to talk to a server over TLS and they must not
// each invent how. What they must NOT share is which schemes they accept:
//
//   * The updater takes https and refuses http, deliberately (update_http.hpp). The artifact it
//     fetches runs with administrator rights, so a cleartext hop is a place to swap it.
//   * The directory client takes http as well, because existing deployments are reached that way
//     and breaking them is not on the table.
//
// Folding those together in either direction is a defect. Making the directory https-only breaks
// every current install; relaxing the shared parser to allow http quietly removes the updater's
// first lock and leaves only the signature check behind it. So the transport is shared -- connect,
// send, read, status -- and the question of what a URL may be stays with each caller.

#include <cstdint>
#include <string>

namespace remote60::native_poc::net {

/**
 * The most a response may be, for either transport.
 *
 * One number, because there were three -- 64 KiB on the host agent's socket, 256 KiB on the
 * viewer's, 4 MiB here -- and all three answered an oversized response by silently returning a
 * truncated one. The caller then parsed a cut-off body and reported malformed json, which is a
 * true statement about the wrong thing. Past this, the exchange fails and says why.
 */
constexpr size_t kMaxHttpResponseBytes = 4u << 20;

/** What a request came back as. `status` is the HTTP code; 0 means it never got that far. */
struct HttpResult {
  bool sent = false;
  uint32_t status = 0;
  std::string body;
  std::string error;
};

/**
 * Performs one request and reads the whole response.
 *
 * `secure` chooses TLS; the caller decides that from its own URL rules, not this function. When it
 * is set, the session gets the same posture the updater uses: TLS 1.2 and above, redirects refused
 * the moment they would leave https, and certificate validation left alone -- there is no
 * parameter here that turns it off, because the way that gets turned off is someone adding one.
 *
 * `extraHeaders` is appended verbatim and must already be CRLF terminated -- the same contract
 * the raw-socket path has, so a caller can be moved between them without its headers changing
 * shape. `body` is sent when non-empty, with the caller's content type.
 *
 * Returns false only when the exchange did not happen at all; an HTTP error status is a completed
 * request and comes back in `status` for the caller to judge.
 */
bool http_exchange(const std::string& host, uint16_t port, bool secure, const char* method,
                   const std::string& path, const std::string& extraHeaders,
                   const std::string& body, const char* contentType, uint32_t timeoutMs,
                   HttpResult* out);

}  // namespace remote60::native_poc::net
