#include "update_endpoint.hpp"

#include <cstdlib>

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


namespace {

/** No value may contain a newline: the format is line-based, so one would forge a field. */
bool one_line(const std::string& value) {
  return value.find('\n') == std::string::npos && value.find('\r') == std::string::npos;
}

}  // namespace

std::string encode_update_descriptor(const UpdateEndpoint& endpoint) {
  // A newline in any value would forge a field, since the format is one field per line. Refused
  // by returning nothing rather than escaped: none of these values can legitimately contain one,
  // so a value that does is a sign something else is wrong.
  if (!one_line(endpoint.url) || !one_line(endpoint.origin) || !one_line(endpoint.ownerKey) ||
      !one_line(endpoint.credentialHeader)) {
    return {};
  }
  std::string out;
  out += "v=1";
  out += '\n';
  out += "url=" + endpoint.url;
  out += '\n';
  out += "origin=" + endpoint.origin;
  out += '\n';
  out += "owner=" + endpoint.ownerKey;
  out += '\n';
  out += "epoch=" + std::to_string(endpoint.ownerEpoch);
  out += '\n';
  out += std::string("envelope=") + (endpoint.derived ? "1" : "0");
  out += '\n';
  out += "cred=" + endpoint.credentialHeader;
  out += '\n';
  return out;
}

bool decode_update_descriptor(const std::string& text, UpdateEndpoint* endpoint,
                              std::string* error) {
  if (!endpoint) return false;
  *endpoint = UpdateEndpoint{};
  bool sawVersion = false;
  bool sawUrl = false;
  bool sawEnvelope = false;
  size_t at = 0;
  while (at < text.size()) {
    const size_t end = text.find('\n', at);
    const std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    at = end == std::string::npos ? text.size() : end + 1;
    if (line.empty()) continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      if (error) *error = "a line without a key";
      return false;
    }
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if (key == "v") {
      if (value != "1") {
        if (error) *error = "descriptor version " + value + " is not one this understands";
        return false;
      }
      sawVersion = true;
    } else if (key == "url") {
      endpoint->url = value;
      sawUrl = true;
    } else if (key == "origin") {
      endpoint->origin = value;
    } else if (key == "owner") {
      endpoint->ownerKey = value;
    } else if (key == "epoch") {
      endpoint->ownerEpoch = strtoull(value.c_str(), nullptr, 10);
    } else if (key == "envelope") {
      endpoint->derived = value == "1";
      sawEnvelope = true;
    } else if (key == "cred") {
      endpoint->credentialHeader = value;
    } else {
      // Refused rather than ignored. An unknown key means the sender and the receiver disagree
      // about what this frame is, and guessing which parts still apply is how the disagreement
      // gets buried.
      if (error) *error = "unknown field '" + key + "'";
      return false;
    }
  }
  if (!sawVersion || !sawUrl || !sawEnvelope) {
    if (error) *error = "the descriptor is missing a required field";
    return false;
  }
  return true;
}

}  // namespace remote60::native_poc::update
