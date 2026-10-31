// Checks a real release manifest with the verifier the product actually ships.
//
// Until now the C++ side of a release was only ever checked against a FIXTURE signed with the
// release key. That proves the compiled-in key accepts that key's signatures; it does not prove
// the product accepts the document being published, and the two are not the same claim. This
// closes that gap: same load_manifest(), same default_verifier(), same compiled-in key, pointed at
// the file that is about to go on the server.
//
// It also flips a byte, because acceptance on its own is equally true of a verifier that never
// looks at the signature.
//
//   remote60_verify_release <manifest> <sig> [platform]
//
// Exit 0 = the shipped verifier accepts it. Anything else = do not publish.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "update_manifest.hpp"

namespace upd = remote60::native_poc::update;

namespace {

bool read_file(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream os;
  os << in.rdbuf();
  *out = os.str();
  return true;
}

std::string chomp(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

const char* status_name(upd::ManifestStatus s) {
  switch (s) {
    case upd::ManifestStatus::Ok: return "Ok";
    case upd::ManifestStatus::SignatureInvalid: return "SignatureInvalid";
    case upd::ManifestStatus::Malformed: return "Malformed";
    case upd::ManifestStatus::WrongPlatform: return "WrongPlatform";
    case upd::ManifestStatus::UnsupportedSchema: return "UnsupportedSchema";
  }
  return "?";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: remote60_verify_release <manifest> <sig> [platform]\n");
    return 2;
  }
  const std::string platform = argc > 3 ? argv[3] : "windows";

  std::string document, sigHex;
  if (!read_file(argv[1], &document)) {
    std::fprintf(stderr, "FAIL  cannot read %s\n", argv[1]);
    return 2;
  }
  if (!read_file(argv[2], &sigHex)) {
    std::fprintf(stderr, "FAIL  cannot read %s\n", argv[2]);
    return 2;
  }
  sigHex = chomp(sigHex);

  int failures = 0;
  auto check = [&](const char* what, bool ok, const std::string& detail) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ",
                detail.c_str());
    if (!ok) ++failures;
  };

  // The key is not passed in. That is the point: this is the value compiled into the shipped
  // binaries, so a release that this refuses is a release no installed client could accept.
  check("a release key is compiled in",
        std::string(upd::trusted_public_key_hex()).size() == 128,
        std::to_string(std::string(upd::trusted_public_key_hex()).size()) + " hex characters");

  const upd::ManifestResult r =
      upd::load_manifest(document, sigHex, platform, upd::default_verifier());
  check("the shipped verifier accepts this document", r.status == upd::ManifestStatus::Ok,
        std::string(status_name(r.status)) + (r.detail.empty() ? "" : ": " + r.detail));

  if (r.manifest) {
    const auto& f = r.manifest->fields();
    std::printf("      version=%s releaseId=%s platform=%s arch=%s artifacts=%zu\n",
                f.version.c_str(), f.releaseId.c_str(), f.platform.c_str(), f.arch.c_str(),
                f.artifacts.size());
    for (const auto& a : f.artifacts) {
      std::printf("      %-24s %10llu  %.16s  %s\n", a.name.c_str(),
                  static_cast<unsigned long long>(a.size), a.sha256.c_str(), a.url.c_str());
    }
  }

  // The negative control. Written unconditionally rather than inside `if (r.manifest)`, because a
  // guard that is false skips its assertions silently -- which is how a verification passes by
  // never running.
  std::string tampered = document;
  if (!tampered.empty()) tampered[tampered.size() / 2] ^= 0x01;
  const upd::ManifestResult t =
      upd::load_manifest(tampered, sigHex, platform, upd::default_verifier());
  check("...and refuses the same document with one byte changed",
        t.status != upd::ManifestStatus::Ok, status_name(t.status));

  if (failures == 0) {
    std::printf("verify_release_manifest: PASS\n");
    return 0;
  }
  std::printf("verify_release_manifest: FAILED (%d)\n", failures);
  return 1;
}
