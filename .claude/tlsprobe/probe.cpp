// Fetches a URL through the PRODUCT's WinHTTP transport, unmodified, and hashes what comes back.
//
// The point is not that the server answers -- curl can establish that. It is that the transport
// the product actually ships, with its own TLS posture and certificate handling, reaches the same
// bytes that were signed. Artifacts are binaries, so the body is hashed rather than printed.
// Nothing is sent but a GET and no credential is used.
#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <string>

#include "winhttp_transport.hpp"

int main(int argc, char** argv) {
  const std::string host = argc > 1 ? argv[1] : "rem.shotan.net";
  const std::string path = argc > 2 ? argv[2] : "/healthz";

  remote60::native_poc::net::HttpResult r;
  const bool ok = remote60::native_poc::net::http_exchange(
      host, 443, true, "GET", path, "", "", "", 60000, &r);

  std::printf("%-52s sent=%d status=%u bytes=%zu\n", path.c_str(), ok ? 1 : 0, r.status,
              r.body.size());
  if (!r.error.empty()) std::printf("  error: %s\n", r.error.c_str());

  if (!r.body.empty()) {
    unsigned char digest[32]{};
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
      BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(r.body.data())),
                 static_cast<ULONG>(r.body.size()), digest, sizeof(digest));
      BCryptCloseAlgorithmProvider(alg, 0);
    }
    std::printf("  sha256: ");
    for (unsigned char b : digest) std::printf("%02x", b);
    std::printf("\n");
    if (r.body.size() < 200) std::printf("  body: %s\n", r.body.c_str());
  }
  return ok && r.status == 200 ? 0 : 1;
}
