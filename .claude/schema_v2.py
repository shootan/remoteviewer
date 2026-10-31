import io

# ---------------------------------------------------------------- header
p = 'apps/native_poc/src/update_manifest.hpp'
d = io.open(p, encoding='utf-8').read()

old = '''/** The fields a manifest carries. All of them are covered by the one signature. */
struct ManifestFields {'''
new = '''/**
 * One file in a release.
 *
 * Every field is covered by the manifest's single signature, so a name cannot be paired with a
 * different hash, and a hash cannot be paired with a different URL, without the signature failing.
 * That is why there is no archive: the manifest IS the container, and it contains identities
 * rather than bytes.
 *
 * What skipping an archive does NOT do is remove attack surface. There is no compression or
 * extraction code to get wrong, but the names and URLs in here are still attacker-chosen input if
 * anything upstream goes wrong, which is why both are checked (payload_name.hpp, update_http.hpp).
 */
struct ManifestArtifact {
  std::string name;    // destination, relative to the install directory
  uint64_t size = 0;
  std::string sha256;  // lowercase hex, 64 characters
  std::string url;     // https only
};

/** The fields a manifest carries. All of them are covered by the one signature. */
struct ManifestFields {'''
assert old in d, 'artifact struct anchor'
d = d.replace(old, new, 1)

old = '''  std::vector<std::string> payloadNames;
};'''
new = '''  std::vector<std::string> payloadNames;

  /**
   * The release this manifest describes, as one opaque identity.
   *
   * It exists so a set of files can be pinned to ONE release. Downloading each file against
   * whatever "latest" says at the moment it is fetched would let a release change underneath an
   * update in progress and produce an install that is half one build and half another. Everything
   * staged for one attempt carries this identity, and a mismatch abandons the attempt.
   */
  std::string releaseId;

  /** Architecture the artifacts are for. Part of the signed identity, like platform. */
  std::string arch;

  /** The files this release replaces. Empty is not allowed in schema 2. */
  std::vector<ManifestArtifact> artifacts;
};'''
assert old in d, 'fields anchor'
d = d.replace(old, new, 1)

old = '''ManifestResult load_manifest(const std::string& document,
                             const std::string& signatureHex,
                             const std::string& expectedPlatform,
                             const SignatureVerifier& verifier);'''
new = '''/** Limits on an artifact list, so a signed-but-absurd manifest cannot exhaust the machine. */
struct ManifestLimits {
  size_t maxArtifacts = 64;
  uint64_t maxArtifactBytes = 512ull * 1024 * 1024;
  uint64_t maxTotalBytes = 2048ull * 1024 * 1024;
};

ManifestResult load_manifest(const std::string& document,
                             const std::string& signatureHex,
                             const std::string& expectedPlatform,
                             const SignatureVerifier& verifier,
                             const std::string& expectedArch = "x64",
                             const ManifestLimits& limits = {});'''
assert old in d, 'load_manifest decl anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('header updated')

# ---------------------------------------------------------------- implementation
p = 'apps/native_poc/src/update_manifest.cpp'
d = io.open(p, encoding='utf-8').read()

d = d.replace('constexpr uint32_t kSupportedSchema = 1;',
              '''// Schema 1 named a single artifact and was never published. Schema 2 carries a release
// identity, an architecture and a list of files, which is what a real update needs.
constexpr uint32_t kSupportedSchema = 2;''', 1)

d = d.replace('#include "payload_name.hpp"',
              '#include "payload_name.hpp"\n#include "update_http.hpp"', 1)

# parse artifact= lines
old = '''    } else if (key == "payload") {'''
new = '''    } else if (key == "releaseId") {
      out->releaseId = value;
    } else if (key == "arch") {
      out->arch = value;
    } else if (key == "artifact" && value.find('|') != std::string::npos) {
      // name|size|sha256|url. Pipe-separated rather than nested syntax because the whole document
      // is signed as bytes -- a format with one obvious reading has nothing to disagree about.
      // A pipe cannot appear in a name (payload_name rejects it) or in a hash.
      std::vector<std::string> parts;
      size_t start = 0;
      for (;;) {
        const size_t bar = value.find('|', start);
        parts.push_back(value.substr(start, (bar == std::string::npos ? value.size() : bar) - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
      }
      if (parts.size() != 4) {
        *detail = "artifact line needs name|size|sha256|url";
        return false;
      }
      ManifestArtifact a;
      a.name = trim(parts[0]);
      if (!parse_u64(trim(parts[1]), &a.size)) {
        *detail = "artifact size is not a number";
        return false;
      }
      a.sha256 = trim(parts[2]);
      a.url = trim(parts[3]);
      out->artifacts.push_back(std::move(a));
    } else if (key == "payload") {'''
assert old in d, 'parse anchor'
d = d.replace(old, new, 1)

# validation
old = '''  // The lock goes on the door here: names that arrived in a manifest are checked before anything
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
'''
new = '''  if (fields.releaseId.empty()) {
    result.status = ManifestStatus::Malformed;
    result.detail = "missing releaseId";
    return result;
  }
  if (fields.arch.empty()) {
    result.status = ManifestStatus::Malformed;
    result.detail = "missing arch";
    return result;
  }
  if (!expectedArch.empty() && fields.arch != expectedArch) {
    // Not an error, the same way a platform mismatch is not: this release is simply not for this
    // machine.
    result.status = ManifestStatus::WrongPlatform;
    result.detail = "arch " + fields.arch;
    return result;
  }

  // ---- the artifact list. Every rule here is enforced BEFORE a VerifiedManifest exists, so
  // nothing downstream can be handed a name, a size or a URL that has not been through it.
  if (fields.artifacts.empty()) {
    result.status = ManifestStatus::Malformed;
    result.detail = "no artifacts listed";
    return result;
  }
  if (fields.artifacts.size() > limits.maxArtifacts) {
    result.status = ManifestStatus::Malformed;
    result.detail = "too many artifacts (" + std::to_string(fields.artifacts.size()) + ")";
    return result;
  }

  std::vector<std::string> artifactNames;
  artifactNames.reserve(fields.artifacts.size());
  uint64_t total = 0;
  for (size_t i = 0; i < fields.artifacts.size(); ++i) {
    const ManifestArtifact& a = fields.artifacts[i];
    const std::string where = "artifact " + std::to_string(i);

    if (a.size == 0 || a.size > limits.maxArtifactBytes) {
      result.status = ManifestStatus::Malformed;
      result.detail = where + " size out of range";
      return result;
    }
    // Checked as a running total rather than at the end, so an overflowing sum cannot wrap past
    // the limit on its way there.
    if (total > limits.maxTotalBytes - a.size) {
      result.status = ManifestStatus::Malformed;
      result.detail = "artifacts exceed the total size limit";
      return result;
    }
    total += a.size;

    if (!is_lowercase_sha256_hex(a.sha256)) {
      result.status = ManifestStatus::Malformed;
      result.detail = where + " sha256 is not 64 lowercase hex characters";
      return result;
    }
    // The transport boundary is enforced here too, not only at fetch time: a manifest naming an
    // http:// URL is refused outright rather than refused later by the client, so the rule cannot
    // be bypassed by a caller that fetches some other way.
    HttpsUrl parsed;
    std::string urlError;
    if (!parse_https_url(a.url, &parsed, &urlError)) {
      result.status = ManifestStatus::Malformed;
      result.detail = where + " url rejected: " + urlError;
      return result;
    }
    artifactNames.push_back(a.name);
  }

  // The lock on the door: names that arrived in a manifest are checked before anything can hold a
  // VerifiedManifest carrying them. A caller building an install config from a verified manifest
  // therefore cannot receive an unsafe name -- there is no path that produces one.
  {
    size_t bad = 0;
    PayloadNameVerdict verdict = PayloadNameVerdict::Ok;
    if (!check_payload_names_utf8(artifactNames, &bad, &verdict)) {
      result.status = ManifestStatus::Malformed;
      result.detail = "artifact name " + std::to_string(bad) + " rejected: " +
                      payload_name_verdict_name(verdict);
      return result;
    }
  }
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
'''
assert old in d, 'validation anchor'
d = d.replace(old, new, 1)

# signature of load_manifest
old = '''ManifestResult load_manifest(const std::string& document,
                             const std::string& signatureHex,
                             const std::string& expectedPlatform,
                             const SignatureVerifier& verifier) {'''
new = '''ManifestResult load_manifest(const std::string& document,
                             const std::string& signatureHex,
                             const std::string& expectedPlatform,
                             const SignatureVerifier& verifier,
                             const std::string& expectedArch,
                             const ManifestLimits& limits) {'''
assert old in d, 'definition anchor'
d = d.replace(old, new, 1)

# the old single-artifact requirement no longer applies
old = '''  if (fields.version.empty() || fields.artifact.empty() || fields.platform.empty()) {
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
  }'''
new = '''  if (fields.version.empty() || fields.platform.empty()) {
    result.status = ManifestStatus::Malformed;
    result.detail = "missing platform or version";
    return result;
  }'''
assert old in d, 'single-artifact anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('implementation updated')
