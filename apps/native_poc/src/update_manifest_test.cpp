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

/** A valid schema-2 head. Cases append or override, so each shows only what it is testing. */
std::string doc_head(const char* platform = "windows", const char* arch = "x64",
                     const char* version = "1.0.0") {
  return std::string("schema=2\nreleaseId=r-test\nplatform=") + platform + "\narch=" + arch +
         "\nversion=" + version + "\n";
}
const char* kGoodArtifact =
    "artifact=GNLinkHost.exe|24|0000000000000000000000000000000000000000000000000000000000000000|https://u.example/h.exe\n";

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
      check("schema parsed", f.schema == 2u, std::to_string(f.schema));
      check("release identity parsed", f.releaseId == "r-0.2.105-test", f.releaseId);
      check("arch parsed", f.arch == "x64", f.arch);
      // Three artifacts of DIFFERENT sizes and contents, so nothing can pass by treating them as
      // interchangeable -- and GNLinkSetup.exe is among them, because the installer travels in
      // the package it installs.
      check("three artifacts from the shared vectors", f.artifacts.size() == 3,
            std::to_string(f.artifacts.size()));
      if (f.artifacts.size() == 3) {
        check("first is the host", f.artifacts[0].name == "GNLinkHost.exe", f.artifacts[0].name);
        check("the Setup is a member", f.artifacts[2].name == "GNLinkSetup.exe",
              f.artifacts[2].name);
        check("sizes differ", f.artifacts[0].size != f.artifacts[1].size &&
                                  f.artifacts[1].size != f.artifacts[2].size);
        check("hashes differ", f.artifacts[0].sha256 != f.artifacts[2].sha256);
        check("every URL is https",
              f.artifacts[0].url.rfind("https://", 0) == 0 &&
                  f.artifacts[2].url.rfind("https://", 0) == 0);
      }
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
    std::string doc;
    ManifestStatus expect;
  };
  const Case cases[] = {
      {"a well formed manifest", doc_head() + kGoodArtifact, ManifestStatus::Ok},
      {"missing schema", std::string("releaseId=r\nplatform=windows\narch=x64\nversion=1\n") +
                             kGoodArtifact,
       ManifestStatus::Malformed},
      {"unknown schema", std::string("schema=99\nreleaseId=r\nplatform=windows\narch=x64\n"
                                     "version=1\n") + kGoodArtifact,
       ManifestStatus::UnsupportedSchema},
      {"schema 1 is no longer accepted",
       std::string("schema=1\nreleaseId=r\nplatform=windows\narch=x64\nversion=1\n") +
           kGoodArtifact,
       ManifestStatus::UnsupportedSchema},
      {"missing releaseId",
       std::string("schema=2\nplatform=windows\narch=x64\nversion=1\n") + kGoodArtifact,
       ManifestStatus::Malformed},
      {"missing arch",
       std::string("schema=2\nreleaseId=r\nplatform=windows\nversion=1\n") + kGoodArtifact,
       ManifestStatus::Malformed},
      // Not for this machine is not an error, the same way a platform mismatch is not.
      {"a different arch", doc_head("windows", "arm64") + kGoodArtifact,
       ManifestStatus::WrongPlatform},
      {"a different platform", doc_head("android") + kGoodArtifact, ManifestStatus::WrongPlatform},
      {"missing version", std::string("schema=2\nreleaseId=r\nplatform=windows\narch=x64\n") +
                              kGoodArtifact,
       ManifestStatus::Malformed},
      {"no artifacts at all", doc_head(), ManifestStatus::Malformed},
      {"zero size", doc_head() + "artifact=a.exe|0|" + std::string(64, '0') +
                        "|https://u.example/a\n",
       ManifestStatus::Malformed},
      {"an artifact larger than the per-file limit",
       doc_head() + "artifact=a.exe|999999999999|" + std::string(64, '0') +
           "|https://u.example/a\n",
       ManifestStatus::Malformed},
      {"uppercase sha256 rejected",
       doc_head() + "artifact=a.exe|1|ABCDEF" + std::string(58, '0') + "|https://u.example/a\n",
       ManifestStatus::Malformed},
      {"short sha256 rejected", doc_head() + "artifact=a.exe|1|abcdef|https://u.example/a\n",
       ManifestStatus::Malformed},
      // The transport boundary is enforced at parse time, not only at fetch time -- so a caller
      // that fetched some other way could not bypass it.
      {"an http:// artifact URL is refused",
       doc_head() + "artifact=a.exe|1|" + std::string(64, '0') + "|http://u.example/a\n",
       ManifestStatus::Malformed},
      {"a URL with credentials is refused",
       doc_head() + "artifact=a.exe|1|" + std::string(64, '0') +
           "|https://evil@u.example/a\n",
       ManifestStatus::Malformed},
      {"a malformed artifact line is refused",
       doc_head() + "artifact=a.exe|1|onlythree\n", ManifestStatus::Malformed},
      {"a non-numeric size is refused",
       doc_head() + "artifact=a.exe|big|" + std::string(64, '0') + "|https://u.example/a\n",
       ManifestStatus::Malformed},
      {"unknown key is ignored, not rejected",
       doc_head() + "futureField=whatever\n" + kGoodArtifact, ManifestStatus::Ok},
      {"comments and blank lines are ignored",
       std::string("# a comment\n\n") + doc_head() + kGoodArtifact, ManifestStatus::Ok},
  };
  for (const Case& c : cases) {
    const ManifestResult r = load_manifest(c.doc, sigHex, "windows", acceptingVerifier);
    check(std::string("field: ") + c.name, r.status == c.expect,
          std::string("expected ") + status_name(c.expect) + " got " + status_name(r.status) +
              " " + r.detail);
  }

  // ---------------------------------------------------------------- the lock on the door

  // Names in a manifest become filesystem paths used with administrator rights. Each case here is
  // SIGNED by the accepting verifier, so the only thing rejecting it is the name check.
  {
    struct NameCase { const char* name; std::string artifactLine; ManifestStatus expect; };
    const auto line = [&](const std::string& name) {
      return "artifact=" + name + "|1|" + std::string(64, '0') + "|https://u.example/a\n";
    };
    const NameCase cases[] = {
        {"ordinary names are accepted", line("GNLinkHost.exe") + line("ui\\shell.html"),
         ManifestStatus::Ok},
        {"a traversal in a signed manifest is refused", line("..\\evil.exe"),
         ManifestStatus::Malformed},
        {"a mixed-separator traversal is refused", line("ui/../../evil.exe"),
         ManifestStatus::Malformed},
        {"an absolute path is refused", line("C:\\Windows\\evil.dll"), ManifestStatus::Malformed},
        {"a UNC path is refused", line("\\\\server\\share\\evil.exe"), ManifestStatus::Malformed},
        {"an alternate data stream is refused", line("GNLinkHost.exe:hidden"),
         ManifestStatus::Malformed},
        {"a reserved device name is refused", line("NUL"), ManifestStatus::Malformed},
        {"a trailing dot is refused", line("GNLinkHost.exe."), ManifestStatus::Malformed},
        {"a duplicate is refused", line("GNLinkHost.exe") + line("gnlinkhost.EXE"),
         ManifestStatus::Malformed},
        {"a non-ASCII name is refused", line("\xed\x95\x9c.exe"), ManifestStatus::Malformed},
    };
    for (const NameCase& c : cases) {
      const std::string doc = doc_head() + c.artifactLine;
      const ManifestResult r = load_manifest(doc, sigHex, "windows", acceptingVerifier);
      check(std::string("artifact name: ") + c.name, r.status == c.expect,
            std::string("expected ") + status_name(c.expect) + " got " + status_name(r.status) +
                " " + r.detail);
      if (c.expect != ManifestStatus::Ok) {
        check(std::string("artifact name: ") + c.name + " -> no VerifiedManifest",
              !r.manifest.has_value());
      }
    }
  }

  {
    // Order is kept, because the swap moves files aside in it and a reordering would change which
    // backup a rollback restores from.
    const std::string doc = doc_head() +
        "artifact=GNLinkHost.exe|1|" + std::string(64, '0') + "|https://u.example/1\n"
        "artifact=GNLinkStream.exe|2|" + std::string(64, '0') + "|https://u.example/2\n"
        "artifact=ui\\shell.html|3|" + std::string(64, '0') + "|https://u.example/3\n";
    const ManifestResult r = load_manifest(doc, sigHex, "windows", acceptingVerifier);
    check("artifact list survives verification", r.status == ManifestStatus::Ok, r.detail);
    if (r.manifest) {
      const auto& a = r.manifest->fields().artifacts;
      check("three artifacts", a.size() == 3, std::to_string(a.size()));
      check("in the order written",
            a.size() == 3 && a[0].name == "GNLinkHost.exe" && a[1].name == "GNLinkStream.exe" &&
                a[2].name == "ui\\shell.html");
      check("sizes are per-artifact, not shared",
            a.size() == 3 && a[0].size == 1 && a[1].size == 2 && a[2].size == 3);
      check("release identity is carried", r.manifest->fields().releaseId == "r-test",
            r.manifest->fields().releaseId);
    }
  }

  {
    // A bad name with a BAD signature must still report SignatureInvalid -- the ordering holds
    // even for the newest check.
    const std::string doc = doc_head() + "artifact=..\evil.exe|1|" + std::string(64, '0') +
                            "|https://u.example/a\n";
    const ManifestResult r = load_manifest(doc, sigHex, "windows", rejectingVerifier);
    check("unsafe artifact name + bad signature -> SignatureInvalid (not Malformed)",
          r.status == ManifestStatus::SignatureInvalid, status_name(r.status));
  }

  // ---------------------------------------------------------------- the shipped key

  {
    // The release key is compiled in now. What this asserts is the same safety property as
    // before, from the other side: the shipped verifier must refuse a document signed by any
    // OTHER key -- and the shared test vectors are signed by exactly such a key, so they are the
    // right thing to hand it.
    //
    // Before the key existed, this block asserted the key was empty. That assertion was about a
    // temporary state, and keeping it would now mean asserting the product cannot check updates.
    // Which answer is correct depends on whose key signed the vectors, and the test knows: the
    // fixture carries its own public key. Run against the shared TEST vectors this must refuse;
    // run against a fixture signed with the release key it must accept. Asserting only the
    // refusal would leave "the shipped key can verify anything at all" untested -- and a verifier
    // that refuses everything passes a refusal test perfectly.
    const bool signedByShippedKey = keyHex == trusted_public_key_hex();
    const ManifestResult r = load_manifest(document, sigHex, "windows", default_verifier());
    if (signedByShippedKey) {
      check("the shipped verifier accepts a document signed by the release key",
            r.status == ManifestStatus::Ok, status_name(r.status));

      // And still refuses it once a byte moves. Acceptance on its own would also be true of a
      // verifier that never looks.
      std::string tampered = document;
      tampered[tampered.size() / 2] ^= 0x01;
      const ManifestResult t = load_manifest(tampered, sigHex, "windows", default_verifier());
      check("...and refuses the same document with one byte changed",
            t.status == ManifestStatus::SignatureInvalid || t.status == ManifestStatus::Malformed,
            status_name(t.status));
    } else {
      check("the shipped verifier rejects a document signed by another key",
            r.status == ManifestStatus::SignatureInvalid, status_name(r.status));
    }

    // The two encodings of the same key are easy to swap and the mistake is silent: the SPKI form
    // decodes fine and then fails the length check inside the verifier, so every update is
    // refused with no error anywhere.
    const std::string key = trusted_public_key_hex();
    check("a release key is compiled in", !key.empty());
    check("...as raw X||Y, which is 64 bytes", key.size() == 128, std::to_string(key.size()));
    check("...in lower-case hex and nothing else",
          key.find_first_not_of("0123456789abcdef") == std::string::npos, key);
    // The SPKI DER form of a P-256 key is 91 bytes: this is the length that would silently
    // disable every check if it were pasted here instead.
    check("...and not the 91-byte SPKI form", key.size() != 182, std::to_string(key.size()));
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed, vectors from " << dir << ")\n";
  return gFailures == 0 ? 0 : 1;
}
