#include "update_manifest.hpp"

#include "payload_name.hpp"
#include "update_signature.hpp"
#include "version_compare.hpp"

#include <cstdlib>

namespace remote60::native_poc::update {
namespace {

constexpr uint32_t kSupportedSchema = 1;

std::string trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

bool parse_u64(const std::string& text, uint64_t* out) {
  if (text.empty() || text.size() > 20) return false;
  uint64_t value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (value > (UINT64_MAX - digit) / 10u) return false;  // refuse rather than wrap
    value = value * 10u + digit;
  }
  *out = value;
  return true;
}

bool is_lowercase_sha256_hex(const std::string& s) {
  if (s.size() != 64) return false;
  for (char c : s) {
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    if (!digit && !lower) return false;
  }
  return true;
}

/** Splits the document into key/value pairs. Comments and blank lines are ignored. */
bool parse_fields(const std::string& document, ManifestFields* out, std::string* detail) {
  bool sawSchema = false;
  size_t pos = 0;
  while (pos <= document.size()) {
    const size_t nl = document.find('\n', pos);
    const std::string raw = document.substr(pos, (nl == std::string::npos ? document.size() : nl) - pos);
    pos = (nl == std::string::npos) ? document.size() + 1 : nl + 1;

    const std::string line = trim(raw);
    if (line.empty() || line[0] == '#') continue;

    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      *detail = "line without '='";
      return false;
    }
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key.empty()) {
      *detail = "empty key";
      return false;
    }

    if (key == "schema") {
      uint64_t v = 0;
      if (!parse_u64(value, &v) || v > UINT32_MAX) {
        *detail = "schema is not a number";
        return false;
      }
      out->schema = static_cast<uint32_t>(v);
      sawSchema = true;
    } else if (key == "platform") {
      out->platform = value;
    } else if (key == "version") {
      out->version = value;
    } else if (key == "artifact") {
      out->artifact = value;
    } else if (key == "size") {
      if (!parse_u64(value, &out->size)) {
        *detail = "size is not a number";
        return false;
      }
    } else if (key == "sha256") {
      out->sha256 = value;
    } else if (key == "payload") {
      // Repeatable. Order is kept because the swap moves files aside in it, and a stable order
      // makes a failure reproducible.
      out->payloadNames.push_back(value);
    } else if (key == "versionCode") {
      if (!parse_u64(value, &out->androidVersionCode)) {
        *detail = "versionCode is not a number";
        return false;
      }
    }
    // Unknown keys are ignored rather than rejected: a newer server adding a field must not brick
    // an older client. The schema number is what gates incompatible changes.
  }

  if (!sawSchema) {
    *detail = "missing schema";
    return false;
  }
  return true;
}

}  // namespace

/** Grants load_manifest() the private constructor without opening it to anyone else. */
struct ManifestLoader {
  static VerifiedManifest make(ManifestFields fields) {
    VerifiedManifest m;
    m.fields_ = std::move(fields);
    return m;
  }
};

bool VerifiedManifest::is_newer_than(const std::string& installedVersion) const {
  return compare_versions(fields_.version, installedVersion) > 0;
}

ManifestResult load_manifest(const std::string& document,
                             const std::string& signatureHex,
                             const std::string& expectedPlatform,
                             const SignatureVerifier& verifier) {
  ManifestResult result;

  // Step one, before the document is looked at in any way. A malformed document with a bad
  // signature is reported as SignatureInvalid, not Malformed -- that difference is observable,
  // and it is how a test can tell the two steps did not get reordered.
  if (!verifier) {
    result.status = ManifestStatus::SignatureInvalid;
    result.detail = "no verifier";
    return result;
  }
  std::vector<uint8_t> signature;
  if (!decode_hex(signatureHex, &signature)) {
    result.status = ManifestStatus::SignatureInvalid;
    result.detail = "signature is not hex";
    return result;
  }
  if (!verifier(document, signature)) {
    result.status = ManifestStatus::SignatureInvalid;
    result.detail = "signature did not verify";
    return result;
  }

  // Only now is anything in the document believed.
  ManifestFields fields;
  std::string detail;
  if (!parse_fields(document, &fields, &detail)) {
    result.status = ManifestStatus::Malformed;
    result.detail = detail;
    return result;
  }
  if (fields.schema != kSupportedSchema) {
    result.status = ManifestStatus::UnsupportedSchema;
    result.detail = "schema " + std::to_string(fields.schema);
    return result;
  }
  if (fields.version.empty() || fields.artifact.empty() || fields.platform.empty()) {
    result.status = ManifestStatus::Malformed;
    result.detail = "missing platform, version or artifact";
    return result;
  }
  if (fields.size == 0) {
    result.status = ManifestStatus::Malformed;
    result.detail = "missing or zero size";
    return result;
  }
  if (!is_lowercase_sha256_hex(fields.sha256)) {
    result.status = ManifestStatus::Malformed;
    result.detail = "sha256 is not 64 lowercase hex characters";
    return result;
  }
  // The lock goes on the door here: names that arrived in a manifest are checked before anything
  // can hold a VerifiedManifest carrying them. A caller building an install config from a
  // verified manifest therefore cannot receive an unsafe name -- there is no path that produces
  // one.
  if (!fields.payloadNames.empty()) {
    size_t bad = 0;
    PayloadNameVerdict verdict = PayloadNameVerdict::Ok;
    if (!check_payload_names_utf8(fields.payloadNames, &bad, &verdict)) {
      result.status = ManifestStatus::Malformed;
      result.detail = "payload name " + std::to_string(bad) + " rejected: " +
                      payload_name_verdict_name(verdict);
      return result;
    }
  }

  if (!expectedPlatform.empty() && fields.platform != expectedPlatform) {
    result.status = ManifestStatus::WrongPlatform;
    result.detail = fields.platform;
    return result;
  }

  result.status = ManifestStatus::Ok;
  result.manifest = ManifestLoader::make(std::move(fields));
  return result;
}

const char* trusted_public_key_hex() {
  // Deliberately empty. See the header: the release key is an approved decision this code does
  // not get to make, and an empty key means default_verifier() accepts nothing.
  return "";
}

SignatureVerifier default_verifier() {
  return [](const std::string& document, const std::vector<uint8_t>& signature) {
    std::vector<uint8_t> key;
    if (!decode_hex(trusted_public_key_hex(), &key)) return false;
    if (key.size() != kP256PublicKeyBytes) return false;  // empty key lands here: reject
    return verify_ecdsa_p256_sha256(document, signature, key);
  };
}

}  // namespace remote60::native_poc::update
