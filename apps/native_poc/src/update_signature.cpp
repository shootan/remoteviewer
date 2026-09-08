#include "update_signature.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <cstring>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace remote60::native_poc::update {
namespace {

/** Closes what it was given, so no early return can leak a provider or a key. */
struct AlgHandle {
  BCRYPT_ALG_HANDLE h = nullptr;
  ~AlgHandle() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
};
struct KeyHandle {
  BCRYPT_KEY_HANDLE h = nullptr;
  ~KeyHandle() { if (h) BCryptDestroyKey(h); }
};

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

bool decode_hex(const std::string& text, std::vector<uint8_t>* out) {
  if (!out) return false;
  out->clear();
  if (text.size() % 2 != 0) return false;
  out->reserve(text.size() / 2);
  for (size_t i = 0; i < text.size(); i += 2) {
    const int hi = hex_value(text[i]);
    const int lo = hex_value(text[i + 1]);
    if (hi < 0 || lo < 0) {
      out->clear();
      return false;
    }
    out->push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return true;
}

bool verify_ecdsa_p256_sha256(const std::string& document,
                              const std::vector<uint8_t>& signature,
                              const std::vector<uint8_t>& publicKeyXY) {
  // Sizes are fixed for this curve, so anything else is malformed input rather than a key that
  // merely fails to match. Checked before touching CNG so the failure is deterministic.
  if (signature.size() != kP256SignatureBytes) return false;
  if (publicKeyXY.size() != kP256PublicKeyBytes) return false;

  // SHA-256 of the exact document bytes. The signature covers what is on the wire, byte for byte,
  // which is why the manifest format has no canonicalisation step to get wrong.
  uint8_t digest[32]{};
  {
    AlgHandle hashAlg;
    if (BCryptOpenAlgorithmProvider(&hashAlg.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) {
      return false;
    }
    const NTSTATUS hashed = BCryptHash(
        hashAlg.h, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(document.data())),
        static_cast<ULONG>(document.size()), digest, sizeof(digest));
    if (hashed != STATUS_SUCCESS) return false;
  }

  AlgHandle ecdsa;
  if (BCryptOpenAlgorithmProvider(&ecdsa.h, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) {
    return false;
  }

  // BCRYPT_ECCKEY_BLOB is the header followed by X then Y, each cbKey bytes.
  std::vector<uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + kP256PublicKeyBytes);
  auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
  header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
  header->cbKey = 32;
  std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), publicKeyXY.data(), kP256PublicKeyBytes);

  KeyHandle key;
  if (BCryptImportKeyPair(ecdsa.h, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key.h, blob.data(),
                          static_cast<ULONG>(blob.size()), 0) != STATUS_SUCCESS) {
    return false;
  }

  const NTSTATUS verified = BCryptVerifySignature(
      key.h, nullptr, digest, sizeof(digest),
      const_cast<PUCHAR>(signature.data()), static_cast<ULONG>(signature.size()), 0);
  return verified == STATUS_SUCCESS;
}

}  // namespace remote60::native_poc::update
