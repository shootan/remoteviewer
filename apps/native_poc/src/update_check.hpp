#pragma once

// "Is there an update?" -- the question behind the tray menu item and the client's start-up
// check, with the answer separated from the asking.
//
// Two properties matter more than the mechanics.
//
// The first is that this can never block the thing that called it. A tray menu that freezes while
// a socket times out, or a client that will not show its sign-in window until a server answers,
// is worse than one that never checks at all. So the synchronous form is a plain function a test
// can call, and the asynchronous form hands the answer back on a thread the caller does not wait
// for.
//
// The second is that "we could not tell" is a distinct answer from "no update". An unreachable
// server, an unconfigured build and an up-to-date install are three different situations, and
// collapsing them into one boolean is how a product ends up reporting "you are up to date" to a
// machine that has not successfully checked in a month.
//
// Design: docs/업데이트_기능_설계.md 3.5 (entry points), 4.1 (transport and provenance).

#include <functional>
#include <string>

#include "update_manifest.hpp"

namespace remote60::native_poc::update {

enum class CheckOutcome {
  // This build has no trusted key or no update URL compiled in, so it cannot check at all.
  // Reported separately from a failure because nothing is wrong -- the feature is simply not
  // switched on, and saying "update check failed" would be misleading.
  NotConfigured,
  // The server could not be reached, or TLS did not come up. Explicitly NOT "up to date".
  Unreachable,
  // Something answered, but what it said did not verify or did not parse.
  Rejected,
  UpToDate,
  UpdateAvailable,
};

const char* check_outcome_name(CheckOutcome outcome);

struct CheckResult {
  CheckOutcome outcome = CheckOutcome::NotConfigured;
  /** The available version, when there is one. Empty otherwise. */
  std::string availableVersion;
  /** Short reason for the log and for the UI's secondary line. Never a manifest or a signature. */
  std::string detail;

  bool actionable() const { return outcome == CheckOutcome::UpdateAvailable; }
};

/** What a check needs. Empty url or key means NotConfigured -- there is no default endpoint. */
struct CheckConfig {
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
  std::string credentialOrigin;
  std::string trustedPublicKeyHex;
  std::string platform;
  std::string installedVersion;
  /** Hard cap on the manifest body. A manifest is a few hundred bytes. */
  size_t maxManifestBytes = 64 * 1024;
};

/**
 * Fetches, verifies and compares. Blocks; call it from somewhere that can afford to.
 *
 * `fetch` is injectable so tests do not open sockets; production passes the WinHTTP client.
 */
using ManifestFetcher =
    std::function<bool(const std::string& url, size_t maxBytes, std::string* document,
                       std::string* signatureHex, std::string* error)>;

CheckResult check_for_update(const CheckConfig& config, const ManifestFetcher& fetch,
                             const SignatureVerifier& verifier);

/**
 * Runs the check on a detached thread and delivers the result to `onResult`.
 *
 * The callback arrives on that thread, not the caller's. A UI caller has to marshal it back
 * itself -- which is stated here rather than hidden, because doing it silently is how a worker
 * thread ends up touching windows it does not own.
 */
void check_for_update_async(CheckConfig config, ManifestFetcher fetch, SignatureVerifier verifier,
                            std::function<void(CheckResult)> onResult);

/** The production fetcher, backed by the update path's HTTPS client. */
/** The fetcher for a url the directory gave us: one body carrying both fields. */
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
ManifestFetcher manifest_fetcher_for(const CheckConfig& config);

}  // namespace remote60::native_poc::update
