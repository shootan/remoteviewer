#include "update_endpoint.hpp"

#include "url_origin.hpp"

namespace remote60::native_poc::update {

bool credential_allowed(const UpdateEndpoint& endpoint, const std::string& targetUrl) {
  if (!endpoint.derived) return false;
  if (endpoint.credentialHeader.empty() || endpoint.origin.empty()) return false;
  if (targetUrl.empty()) return false;
  // The destination of the request about to be made, not the one this snapshot was built from.
  return url_origin_key(targetUrl) == endpoint.origin;
}


namespace {

/** One string field, with only the escapes a manifest actually contains decoded. */
bool extract_field(const std::string& body, const char* key, std::string* out) {
  const std::string needle = std::string("\"") + key + "\":\"";
  const size_t at = body.find(needle);
  if (at == std::string::npos) return false;
  size_t i = at + needle.size();
  out->clear();
  while (i < body.size() && body[i] != '"') {
    if (body[i] == '\\' && i + 1 < body.size()) {
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
  // An unterminated string is a truncated response, not a field.
  return i < body.size();
}

}  // namespace

bool parse_manifest_envelope(const std::string& body, std::string* document,
                             std::string* signatureHex, std::string* error) {
  if (!document || !signatureHex) return false;
  if (!extract_field(body, "manifest", document) ||
      !extract_field(body, "signature", signatureHex)) {
    if (error) *error = "response did not carry a manifest and a signature";
    return false;
  }
  return true;
}

}  // namespace remote60::native_poc::update
