#pragma once

// The update manifest: what the server says is available, and the only thing that decides whether
// this machine is behind.
//
// The shape of this file is one design decision made structural. The manifest's signature has to
// be checked BEFORE anything in it is believed -- including the version, because a version that
// has not been authenticated must not even be compared. Rather than trusting every future caller
// to remember that order, the parsed fields are reachable only through VerifiedManifest, which
// nothing outside load_manifest() can construct. Getting the fields at all is proof the signature
// checked out.
//
// The document is a line-oriented `key=value` text, not JSON. Two reasons: the signature covers
// exact bytes, so a format with no canonicalisation step has nothing to get wrong between the
// signer and the verifier; and the same file has to be read by C++, JavaScript and Kotlin without
// dragging a parser dependency into any of them.
//
// Format and required fields are in apps/shared/update_manifest/README.txt.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace remote60::native_poc::update {

/** Why a manifest was rejected. Everything but Ok means "do not act on this". */
enum class ManifestStatus {
  Ok,
  // The signature did not verify, or could not be checked at all. Reported without looking at
  // the contents -- a caller cannot distinguish "forged" from "we could not tell", and both mean
  // the same thing.
  SignatureInvalid,
  // The signature was good but the document does not parse, or a required field is missing or
  // malformed. Distinct from SignatureInvalid on purpose: it means someone we trust published
  // something we do not understand, which is a different problem.
  Malformed,
  // Signed and well-formed, but not for us. Kept separate so "no update for this platform" does
  // not read as an error.
  WrongPlatform,
  // A schema version this build does not know how to read.
  UnsupportedSchema,
};

/** The fields a manifest carries. All of them are covered by the one signature. */
struct ManifestFields {
  uint32_t schema = 0;
  std::string platform;
  std::string version;
  std::string artifact;
  uint64_t size = 0;
  std::string sha256;          // lowercase hex, 64 characters
  uint64_t androidVersionCode = 0;  // 0 when absent; only meaningful for platform=android
  /**
   * Files the package replaces, relative to the install directory. One `payload=` line each.
   *
   * These become filesystem paths used with administrator rights, so every one is checked by
   * check_payload_names_utf8() during load_manifest -- a manifest carrying a traversal, an
   * absolute path or a reserved device name is Malformed and never produces a VerifiedManifest.
   * The signature says the bytes are ours; it does not say the names in them are safe.
   */
  std::vector<std::string> payloadNames;
};

/**
 * A manifest whose signature verified.
 *
 * The constructor is private and load_manifest() is its only friend, so an instance of this type
 * cannot exist unless the signature checked out. That is the "verify before you compare" rule
 * expressed as a type rather than as a comment someone can forget.
 */
class VerifiedManifest {
 public:
  const ManifestFields& fields() const { return fields_; }

  /**
   * True when this manifest is newer than `installedVersion`, by the shared version-comparison
   * contract (apps/shared/version_compare_vectors.txt).
   *
   * Reachable only from a verified manifest, which is the point.
   */
  bool is_newer_than(const std::string& installedVersion) const;

 private:
  friend struct ManifestLoader;
  VerifiedManifest() = default;
  ManifestFields fields_;
};

struct ManifestResult {
  ManifestStatus status = ManifestStatus::SignatureInvalid;
  // Present only when status == Ok.
  std::optional<VerifiedManifest> manifest;
  // A short reason for the log. Never contains the document or the signature.
  std::string detail;
};

/**
 * Checks a detached signature over `document` and, only if that passes, parses it.
 *
 * `verifier` is injectable so tests can drive the failure paths deterministically without keys;
 * production passes the CNG-backed default below. It is handed the exact document bytes and the
 * decoded signature, and returns true only for a good signature.
 */
using SignatureVerifier =
    std::function<bool(const std::string& document, const std::vector<uint8_t>& signature)>;

ManifestResult load_manifest(const std::string& document,
                             const std::string& signatureHex,
                             const std::string& expectedPlatform,
                             const SignatureVerifier& verifier);

/** The production verifier: ECDSA P-256/SHA-256 against the public key compiled into the build. */
SignatureVerifier default_verifier();

/**
 * The trusted public key, as raw X||Y hex.
 *
 * Empty in this build. A release key is a separate, approved decision -- generating, storing and
 * rotating it is not something this code gets to do on its own, and shipping a placeholder that
 * happened to verify something would be worse than shipping nothing. With it empty,
 * default_verifier() rejects everything, which is the safe direction: no update can be accepted
 * until a real key is deliberately put here.
 */
const char* trusted_public_key_hex();

}  // namespace remote60::native_poc::update
