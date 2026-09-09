#include "update_check.hpp"

#include <thread>

#include "update_endpoint.hpp"
#include "update_http.hpp"
#include "version_compare.hpp"

namespace remote60::native_poc::update {

const char* check_outcome_name(CheckOutcome outcome) {
  switch (outcome) {
    case CheckOutcome::NotConfigured: return "NotConfigured";
    case CheckOutcome::Unreachable: return "Unreachable";
    case CheckOutcome::Rejected: return "Rejected";
    case CheckOutcome::UpToDate: return "UpToDate";
    case CheckOutcome::UpdateAvailable: return "UpdateAvailable";
  }
  return "?";
}

CheckResult check_for_update(const CheckConfig& config, const ManifestFetcher& fetch,
                             const SignatureVerifier& verifier) {
  CheckResult result;

  // Not configured is answered first and without touching the network. A build with no trusted
  // key cannot accept any manifest, so asking for one would be theatre -- and the failure it
  // produced would read as "the update server is broken" rather than "this build does not do
  // updates yet".
  if (config.trustedPublicKeyHex.empty()) {
    result.outcome = CheckOutcome::NotConfigured;
    result.detail = "no trusted key is compiled into this build";
    return result;
  }
  if (config.manifestUrl.empty()) {
    result.outcome = CheckOutcome::NotConfigured;
    result.detail = "no update URL is configured";
    return result;
  }
  if (!fetch || !verifier) {
    result.outcome = CheckOutcome::NotConfigured;
    result.detail = "no fetcher or verifier supplied";
    return result;
  }

  std::string document;
  std::string signatureHex;
  std::string error;
  if (!fetch(config.manifestUrl, config.maxManifestBytes, &document, &signatureHex, &error)) {
    // Deliberately not UpToDate. A machine that has failed to check for a month must not be
    // telling its user it is current.
    result.outcome = CheckOutcome::Unreachable;
    result.detail = error.empty() ? "could not reach the update server" : error;
    return result;
  }

  const ManifestResult manifest =
      load_manifest(document, signatureHex, config.platform, verifier);
  if (manifest.status != ManifestStatus::Ok || !manifest.manifest) {
    result.outcome = CheckOutcome::Rejected;
    result.detail = manifest.detail.empty() ? "manifest was rejected" : manifest.detail;
    return result;
  }

  const ManifestFields& fields = manifest.manifest->fields();
  if (!manifest.manifest->is_newer_than(config.installedVersion)) {
    result.outcome = CheckOutcome::UpToDate;
    result.availableVersion = fields.version;
    return result;
  }

  result.outcome = CheckOutcome::UpdateAvailable;
  result.availableVersion = fields.version;
  return result;
}

void check_for_update_async(CheckConfig config, ManifestFetcher fetch, SignatureVerifier verifier,
                            std::function<void(CheckResult)> onResult) {
  if (!onResult) return;
  // Detached on purpose. Nothing in the UI should be able to wait on this, and there is no state
  // to join back into -- the whole answer is the CheckResult handed to the callback.
  std::thread([config = std::move(config), fetch = std::move(fetch),
               verifier = std::move(verifier), onResult = std::move(onResult)]() mutable {
    onResult(check_for_update(config, fetch, verifier));
  }).detach();
}

namespace {

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
  return parse_manifest_envelope(body, document, signatureHex, error);
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
         (signatureHex->back() == '\n' || signatureHex->back() == '\r' ||
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

  UpdateEndpoint endpoint;
  endpoint.derived = true;
  endpoint.credentialHeader = config.credentialHeader;
  endpoint.origin = config.credentialOrigin;
  return [endpoint](const std::string& url, size_t maxBytes, std::string* document,
                    std::string* signatureHex, std::string* error) {
    // Judged against the url about to be fetched, not the one the snapshot was built from.
    const std::string header = credential_allowed(endpoint, url)
                                   ? endpoint.credentialHeader
                                   : std::string();
    return envelope_fetch(url, maxBytes, header, document, signatureHex, error);
  };
}

}  // namespace remote60::native_poc::update
