#include "update_check.hpp"

#include <thread>

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

ManifestFetcher https_manifest_fetcher() {
  return [](const std::string& url, size_t maxBytes, std::string* document,
            std::string* signatureHex, std::string* error) {
    // The server answers with a small JSON object carrying both the document and its detached
    // signature. Parsed by hand rather than with a JSON library, because two string fields do not
    // justify a dependency in the product -- and because the document's exact bytes matter, so
    // the extraction has to be something that can be read and checked.
    std::string body;
    const FetchStatus status = https_get_text(url, maxBytes, &body, error);
    if (status != FetchStatus::Ok) {
      if (error && error->empty()) *error = fetch_status_name(status);
      return false;
    }

    const auto extract = [&body](const char* key, std::string* out) {
      const std::string needle = std::string("\"") + key + "\":\"";
      const size_t at = body.find(needle);
      if (at == std::string::npos) return false;
      size_t i = at + needle.size();
      out->clear();
      while (i < body.size() && body[i] != '"') {
        if (body[i] == '\\' && i + 1 < body.size()) {
          // Only the escapes a manifest actually contains: newlines, and a literal backslash or
          // quote. Anything else is left as-is rather than guessed at.
          const char next = body[i + 1];
          if (next == 'n') out->push_back('\n');
          else if (next == 'r') out->push_back('\r');
          else if (next == 't') out->push_back('\t');
          else out->push_back(next);
          i += 2;
          continue;
        }
        out->push_back(body[i++]);
      }
      return i < body.size();
    };

    if (!extract("manifest", document) || !extract("signature", signatureHex)) {
      if (error) *error = "response did not carry a manifest and a signature";
      return false;
    }
    return true;
  };
}

}  // namespace remote60::native_poc::update
