// Deterministic checks for the update manifest: the signature really is checked by CNG, and it is
// checked BEFORE anything in the document is believed.
//
// The ordering property is the one worth being careful about, because it is the kind of thing that
// survives a refactor as a comment and dies as behaviour. It is tested two ways here: once
// structurally (a manifest's fields are only reachable through VerifiedManifest, which nothing
// outside load_manifest can construct), and once observably -- a document that is BOTH malformed
// and badly signed must report SignatureInvalid, because a parse-first implementation would say
// Malformed instead.
//
// Vectors come from apps/shared/update_manifest/, signed once with a throwaway key that exists
// only for this test. Path is baked in by CMake and can be overridden by argv[1].

#include "update_manifest.hpp"
#include "update_signature.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef REMOTE60_UPDATE_VECTORS_DIR
#define REMOTE60_UPDATE_VECTORS_DIR "apps/shared/update_manifest"
#endif

namespace {

using namespace remote60::native_poc::update;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

/** Reads a file as exact bytes -- the signature covers them, so no line-ending translation. */
bool read_file(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

std::string chomp(const std::string& s) {
  std::string out = s;
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

const char* status_name(ManifestStatus s) {
  switch (s) {
    case ManifestStatus::Ok: return "Ok";
    case ManifestStatus::SignatureInvalid: return "SignatureInvalid";
    case ManifestStatus::Malformed: return "Malformed";
    case ManifestStatus::WrongPlatform: return "WrongPlatform";
    case ManifestStatus::UnsupportedSchema: return "UnsupportedSchema";
  }
  return "?";
}

}  // namespace

int main(int argc, char** argv) {
  const std::string dir = (argc > 1) ? argv[1] : REMOTE60_UPDATE_VECTORS_DIR;

  std::string document;
  std::string sigHex;
  std::string keyHex;
  if (!read_file(dir + "/test_manifest.txt", &document) ||
      !read_file(dir + "/test_manifest.sig", &sigHex) ||
      !read_file(dir + "/test_public_key.txt", &keyHex)) {
    std::cerr << "FAIL  could not read vectors from " << dir << "\n";
    return 2;
  }
  sigHex = chomp(sigHex);
  keyHex = chomp(keyHex);

  std::vector<uint8_t> key;
  std::vector<uint8_t> sig;
  check("public key decodes", decode_hex(keyHex, &key) && key.size() == kP256PublicKeyBytes,
        std::to_string(key.size()) + " bytes");
  check("signature decodes", decode_hex(sigHex, &sig) && sig.size() == kP256SignatureBytes,
        std::to_string(sig.size()) + " bytes");

  // ---------------------------------------------------------------- real CNG verification

  check("CNG accepts the genuine signature",
        verify_ecdsa_p256_sha256(document, sig, key));

  {
    // One byte of the document changed. Everything else identical.
    std::string tampered = document;
    const size_t at = tampered.find("0.2.105");
    check("tamper point exists", at != std::string::npos);
    if (at != std::string::npos) tampered[at + 6] = '6';
    check("CNG rejects a tampered document",
          !verify_ecdsa_p256_sha256(tampered, sig, key));
  }
  {
    std::vector<uint8_t> flipped = sig;
    flipped[0] ^= 0x01;
    check("CNG rejects a tampered signature",
          !verify_ecdsa_p256_sha256(document, flipped, key));
  }
  {
    std::vector<uint8_t> otherKey = key;
    otherKey[0] ^= 0x01;
    check("CNG rejects the wrong key", !verify_ecdsa_p256_sha256(document, sig, otherKey));
  }
  check("CNG rejects a short signature",
        !verify_ecdsa_p256_sha256(document, std::vector<uint8_t>(63, 0), key));
  check("CNG rejects a short key",
        !verify_ecdsa_p256_sha256(document, sig, std::vector<uint8_t>(63, 0)));
  check("empty signature and key are rejected",
        !verify_ecdsa_p256_sha256(document, {}, {}));

  // ---------------------------------------------------------------- verify-then-parse ordering

  // A verifier that records whether it ran, so "the document was never parsed" can be stated as
  // "the parse result is unreachable" rather than inferred.
  int verifierCalls = 0;
  auto acceptingVerifier = [&verifierCalls](const std::string&, const std::vector<uint8_t>&) {
    ++verifierCalls;
    return true;
  };
  auto rejectingVerifier = [&verifierCalls](const std::string&, const std::vector<uint8_t>&) {
    ++verifierCalls;
    return false;
  };

  {
    verifierCalls = 0;
    const ManifestResult r = load_manifest(document, sigHex, "windows", acceptingVerifier);
    check("good signature -> Ok", r.status == ManifestStatus::Ok, status_name(r.status) + std::string(" ") + r.detail);
    check("verifier was called once", verifierCalls == 1, std::to_string(verifierCalls));
    check("fields reachable only via VerifiedManifest", r.manifest.has_value());
    if (r.manifest) {
      const ManifestFields& f = r.manifest->fields();
      check("version parsed", f.version == "0.2.105", f.version);
      check("artifact parsed", f.artifact == "GNLinkSetup-0.2.105.exe", f.artifact);
      check("size parsed", f.size == 3475968u, std::to_string(f.size));
      check("sha256 parsed", f.sha256.size() == 64, f.sha256);
      check("schema parsed", f.schema == 1u, std::to_string(f.schema));
      // The comparison lives on the verified object, so it cannot be reached without a signature.
      check("newer than an older install", r.manifest->is_newer_than("0.2.104"));
      check("not newer than itself", !r.manifest->is_newer_than("0.2.105"));
      check("not newer than a newer install", !r.manifest->is_newer_than("0.2.106"));
      // Numeric, not lexicographic -- the shared contract, reached through this path.
      check("0.2.105 is newer than 0.2.99", r.manifest->is_newer_than("0.2.99"));
    }
  }

  {
    verifierCalls = 0;
    const ManifestResult r = load_manifest(document, sigHex, "windows", rejectingVerifier);
    check("bad signature -> SignatureInvalid", r.status == ManifestStatus::SignatureInvalid,
          status_name(r.status));
    check("bad signature -> no manifest, so no comparison is possible", !r.manifest.has_value());
    check("verifier still ran once", verifierCalls == 1, std::to_string(verifierCalls));
  }

  {
    // THE ordering test. This document has no schema line and is not valid at all; if parsing ran
    // first the answer would be Malformed. It must be SignatureInvalid.
    verifierCalls = 0;
    const std::string garbage = "this is not a manifest at all\nno equals signs here\n";
    const ManifestResult r = load_manifest(garbage, sigHex, "windows", rejectingVerifier);
    check("malformed AND badly signed -> SignatureInvalid (not Malformed)",
          r.status == ManifestStatus::SignatureInvalid, status_name(r.status));
    check("nothing parsed from it", !r.manifest.has_value());
  }

  {
    // Same garbage, but the signature "passes". Now Malformed is the right answer -- which proves
    // the previous case was decided by the signature and not by the document being garbage.
    const ManifestResult r = load_manifest("no equals signs here\n", sigHex, "windows", acceptingVerifier);
    check("malformed with a good signature -> Malformed", r.status == ManifestStatus::Malformed,
          status_name(r.status) + std::string(" ") + r.detail);
  }

  {
    const ManifestResult r = load_manifest(document, "not-hex", "windows", acceptingVerifier);
    check("non-hex signature -> SignatureInvalid", r.status == ManifestStatus::SignatureInvalid,
          status_name(r.status));
  }
  {
    const ManifestResult r = load_manifest(document, sigHex, "windows", nullptr);
    check("absent verifier -> SignatureInvalid", r.status == ManifestStatus::SignatureInvalid,
          status_name(r.status));
  }

  // ---------------------------------------------------------------- field validation

  struct Case {
    const char* name;
    const char* doc;
    ManifestStatus expect;
  };
  const Case cases[] = {
      {"missing schema", "platform=windows\nversion=1\nartifact=a\nsize=1\n"
                         "sha256=0000000000000000000000000000000000000000000000000000000000000000\n",
       ManifestStatus::Malformed},
      {"unknown schema", "schema=99\nplatform=windows\nversion=1\nartifact=a\nsize=1\n"
                         "sha256=0000000000000000000000000000000000000000000000000000000000000000\n",
       ManifestStatus::UnsupportedSchema},
      {"wrong platform", "schema=1\nplatform=android\nversion=1\nartifact=a\nsize=1\n"
                         "sha256=0000000000000000000000000000000000000000000000000000000000000000\n",
       ManifestStatus::WrongPlatform},
      {"zero size", "schema=1\nplatform=windows\nversion=1\nartifact=a\nsize=0\n"
                    "sha256=0000000000000000000000000000000000000000000000000000000000000000\n",
       ManifestStatus::Malformed},
      {"uppercase sha256 rejected",
       "schema=1\nplatform=windows\nversion=1\nartifact=a\nsize=1\n"
       "sha256=ABCDEF0000000000000000000000000000000000000000000000000000000000\n",
       ManifestStatus::Malformed},
      {"short sha256 rejected", "schema=1\nplatform=windows\nversion=1\nartifact=a\nsize=1\n"
                                "sha256=abcdef\n",
       ManifestStatus::Malformed},
      {"missing version", "schema=1\nplatform=windows\nartifact=a\nsize=1\n"
                          "sha256=0000000000000000000000000000000000000000000000000000000000000000\n",
       ManifestStatus::Malformed},
      {"unknown key is ignored, not rejected",
       "schema=1\nplatform=windows\nversion=1\nartifact=a\nsize=1\nfutureField=whatever\n"
       "sha256=0000000000000000000000000000000000000000000000000000000000000000\n",
       ManifestStatus::Ok},
      {"comments and blank lines are ignored",
       "# a comment\n\nschema=1\nplatform=windows\nversion=1\nartifact=a\nsize=1\n"
       "sha256=0000000000000000000000000000000000000000000000000000000000000000\n",
       ManifestStatus::Ok},
  };
  for (const Case& c : cases) {
    const ManifestResult r = load_manifest(c.doc, sigHex, "windows", acceptingVerifier);
    check(std::string("field: ") + c.name, r.status == c.expect,
          std::string("expected ") + status_name(c.expect) + " got " + status_name(r.status) +
              " " + r.detail);
  }

  // ---------------------------------------------------------------- the lock on the door

  // The payload-name rules exist to stop a name from becoming a path outside the install
  // directory. Until now nothing connected them to a manifest, so they were a good lock not yet
  // fitted to a door. These cases are that connection: an unsafe name in a SIGNED manifest must
  // still be refused, and no VerifiedManifest carrying one can exist.
  {
    const std::string base =
        "schema=1\nplatform=windows\nversion=1\nartifact=a\nsize=1\n"
        "sha256=0000000000000000000000000000000000000000000000000000000000000000\n";

    struct Case {
      const char* name;
      const char* payloadLines;
      ManifestStatus expect;
    };
    const Case cases[] = {
        {"no payload lines is fine", "", ManifestStatus::Ok},
        {"ordinary names are accepted",
         "payload=GNLinkHost.exe\npayload=ui\\shell.html\npayload=GNLinkSetup.exe\n",
         ManifestStatus::Ok},
        // Each of these is signed by the accepting verifier, so the ONLY thing rejecting them is
        // the name check.
        {"a traversal in a signed manifest is refused", "payload=..\\evil.exe\n",
         ManifestStatus::Malformed},
        {"a mixed-separator traversal is refused", "payload=ui/../../evil.exe\n",
         ManifestStatus::Malformed},
        {"an absolute path is refused", "payload=C:\\Windows\\System32\\evil.dll\n",
         ManifestStatus::Malformed},
        {"a UNC path is refused", "payload=\\\\server\\share\\evil.exe\n",
         ManifestStatus::Malformed},
        {"an alternate data stream is refused", "payload=GNLinkHost.exe:hidden\n",
         ManifestStatus::Malformed},
        {"a reserved device name is refused", "payload=NUL\n", ManifestStatus::Malformed},
        {"a trailing dot is refused", "payload=GNLinkHost.exe.\n", ManifestStatus::Malformed},
        {"a duplicate is refused",
         "payload=GNLinkHost.exe\npayload=gnlinkhost.EXE\n", ManifestStatus::Malformed},
        {"a non-ASCII name is refused", "payload=\xed\x95\x9c.exe\n", ManifestStatus::Malformed},
    };

    for (const Case& c : cases) {
      const std::string doc = base + c.payloadLines;
      const ManifestResult r = load_manifest(doc, sigHex, "windows", acceptingVerifier);
      check(std::string("payload: ") + c.name, r.status == c.expect,
            std::string("expected ") + status_name(c.expect) + " got " + status_name(r.status) +
                " " + r.detail);
      if (c.expect != ManifestStatus::Ok) {
        // The point of the connection: nothing carrying an unsafe name can exist.
        check(std::string("payload: ") + c.name + " -> no VerifiedManifest", !r.manifest.has_value());
      }
    }
  }

  {
    // And the names that DO come through are the ones that were written, in order -- the swap
    // moves files aside in this order, so a reordering would change which backup a rollback used.
    const std::string doc =
        "schema=1\nplatform=windows\nversion=1\nartifact=a\nsize=1\n"
        "sha256=0000000000000000000000000000000000000000000000000000000000000000\n"
        "payload=GNLinkHost.exe\npayload=GNLinkStream.exe\npayload=ui\\shell.html\n";
    const ManifestResult r = load_manifest(doc, sigHex, "windows", acceptingVerifier);
    check("payload names survive verification", r.status == ManifestStatus::Ok, r.detail);
    if (r.manifest) {
      const auto& names = r.manifest->fields().payloadNames;
      check("three names", names.size() == 3, std::to_string(names.size()));
      check("in the order written",
            names.size() == 3 && names[0] == "GNLinkHost.exe" &&
                names[1] == "GNLinkStream.exe" && names[2] == "ui\\shell.html");
    }
  }

  {
    // A bad payload name with a BAD signature must still report SignatureInvalid -- the ordering
    // holds even for the newest check.
    const std::string doc =
        "schema=1\nplatform=windows\nversion=1\nartifact=a\nsize=1\n"
        "sha256=0000000000000000000000000000000000000000000000000000000000000000\n"
        "payload=..\\evil.exe\n";
    const ManifestResult r = load_manifest(doc, sigHex, "windows", rejectingVerifier);
    check("unsafe payload + bad signature -> SignatureInvalid (not Malformed)",
          r.status == ManifestStatus::SignatureInvalid, status_name(r.status));
  }

  // ---------------------------------------------------------------- the shipped key rejects all

  {
    // No release key is compiled in, on purpose. Until one is deliberately put there, the default
    // verifier must accept nothing -- including a signature that is genuinely valid under the
    // test key. This is the safe direction and it should fail loudly if someone drops in a
    // placeholder that happens to verify.
    const ManifestResult r = load_manifest(document, sigHex, "windows", default_verifier());
    check("default verifier rejects while no release key is compiled in",
          r.status == ManifestStatus::SignatureInvalid, status_name(r.status));
    check("trusted key is empty in this build",
          std::string(trusted_public_key_hex()).empty(), trusted_public_key_hex());
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed, vectors from " << dir << ")\n";
  return gFailures == 0 ? 0 : 1;
}
