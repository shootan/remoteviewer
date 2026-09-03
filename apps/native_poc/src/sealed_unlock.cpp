// See sealed_unlock.hpp. Windows CNG (bcrypt) implementation of the sealed-unlock primitives.

#include "sealed_unlock.hpp"

#include <windows.h>

#include <bcrypt.h>
#include <winternl.h>

#include <cstring>

#pragma comment(lib, "bcrypt.lib")

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_AUTH_TAG_MISMATCH
#define STATUS_AUTH_TAG_MISMATCH ((NTSTATUS)0xC000A002L)
#endif

namespace remote60::native_poc::sealed_unlock {
namespace {

// BCRYPT_ECCKEY_BLOB header is { ULONG dwMagic; ULONG cbKey; } then X and Y, cbKey bytes each.
constexpr ULONG kEccHeaderBytes = 8;
constexpr ULONG kP256Coord = 32;

struct AlgProvider {
  BCRYPT_ALG_HANDLE h = nullptr;
  ~AlgProvider() {
    if (h) BCryptCloseAlgorithmProvider(h, 0);
  }
};

void append_u32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xff));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xff));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xff));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xff));
}
void append_u64(std::vector<uint8_t>& v, uint64_t x) {
  for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xff));
}
void append_bytes(std::vector<uint8_t>& v, const uint8_t* p, size_t n) {
  v.insert(v.end(), p, p + n);
}

// Both AAD and KDF-info are the same canonical context bytes; the info additionally carries a label
// so the two derivations of the shared secret in different roles can never collide.
std::vector<uint8_t> serialize_context(const UnlockContext& c) {
  std::vector<uint8_t> v;
  v.reserve(256);
  append_u32(v, c.protocolVersion);
  append_u64(v, c.hostId);
  append_u64(v, c.clientSessionCookie);
  append_bytes(v, c.challengeId, kChallengeIdBytes);
  append_u32(v, c.requestId);
  append_u32(v, c.requesterSession);
  append_u32(v, c.consoleSession);
  append_u32(v, c.lockGeneration);
  append_u64(v, c.issuedMs);
  append_u64(v, c.expiresMs);
  append_u64(v, c.accountId);
  append_bytes(v, c.hostPub, kPubKeyBytes);
  append_bytes(v, c.clientPub, kPubKeyBytes);
  return v;
}


// HMAC-SHA256 (CNG). Empty key is allowed (keyLen==0). out must hold 32 bytes.
bool hmac_sha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen,
                 uint8_t out[32]) {
  AlgProvider alg;
  if (BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  BCRYPT_ALG_HANDLE_HMAC_FLAG) != STATUS_SUCCESS) {
    return false;
  }
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptCreateHash(alg.h, &hash, nullptr, 0, const_cast<PUCHAR>(key),
                       static_cast<ULONG>(keyLen), 0) != STATUS_SUCCESS) {
    return false;
  }
  bool ok = false;
  if (BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(dataLen), 0) ==
          STATUS_SUCCESS &&
      BCryptFinishHash(hash, out, 32, 0) == STATUS_SUCCESS) {
    ok = true;
  }
  BCryptDestroyHash(hash);
  return ok;
}

// HKDF-SHA256 producing exactly 32 bytes (one expand block). RFC 5869. Implemented over HMAC rather
// than the CNG HKDF-on-secret-handle path, which did not derive on this environment. (Codex #364.)
bool hkdf_sha256_32(const uint8_t* ikm, size_t ikmLen, const uint8_t* salt, size_t saltLen,
                    const uint8_t* info, size_t infoLen, uint8_t out[32]) {
  uint8_t prk[32];
  if (!hmac_sha256(salt, saltLen, ikm, ikmLen, prk)) return false;
  std::vector<uint8_t> t1;
  t1.reserve(infoLen + 1);
  if (infoLen) t1.insert(t1.end(), info, info + infoLen);
  t1.push_back(0x01);
  const bool ok = hmac_sha256(prk, 32, t1.data(), t1.size(), out);
  SecureZeroMemory(prk, sizeof(prk));
  return ok;
}

}  // namespace

bool RandomBytes(uint8_t* out, size_t len) {
  if (!out) return false;
  return BCryptGenRandom(nullptr, out, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) ==
         STATUS_SUCCESS;
}

void SecureZero(void* p, size_t len) {
  if (p && len) SecureZeroMemory(p, len);
}

bool Hkdf_Sha256_32(const uint8_t* ikm, size_t ikmLen, const uint8_t* salt, size_t saltLen,
                    const uint8_t* info, size_t infoLen, uint8_t out[32]) {
  if (!out || (ikmLen && !ikm) || (saltLen && !salt) || (infoLen && !info)) return false;
  return hkdf_sha256_32(ikm, ikmLen, salt, saltLen, info, infoLen, out);
}

// --- EcdhKeyPair --------------------------------------------------------------------------------

EcdhKeyPair::~EcdhKeyPair() { Reset(); }

EcdhKeyPair::EcdhKeyPair(EcdhKeyPair&& other) noexcept : key_(other.key_) { other.key_ = nullptr; }

EcdhKeyPair& EcdhKeyPair::operator=(EcdhKeyPair&& other) noexcept {
  if (this != &other) {
    Reset();
    key_ = other.key_;
    other.key_ = nullptr;
  }
  return *this;
}

void EcdhKeyPair::Reset() {
  if (key_) {
    BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(key_));
    key_ = nullptr;
  }
}

bool EcdhKeyPair::Generate() {
  Reset();
  AlgProvider alg;
  if (BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) {
    return false;
  }
  BCRYPT_KEY_HANDLE key = nullptr;
  if (BCryptGenerateKeyPair(alg.h, &key, 256, 0) != STATUS_SUCCESS) return false;
  if (BCryptFinalizeKeyPair(key, 0) != STATUS_SUCCESS) {
    BCryptDestroyKey(key);
    return false;
  }
  key_ = key;
  return true;
}

bool EcdhKeyPair::ExportPublic(uint8_t out[kPubKeyBytes]) const {
  if (!key_ || !out) return false;
  ULONG needed = 0;
  if (BCryptExportKey(static_cast<BCRYPT_KEY_HANDLE>(key_), nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0,
                      &needed, 0) != STATUS_SUCCESS) {
    return false;
  }
  if (needed != kEccHeaderBytes + 2 * kP256Coord) return false;
  uint8_t blob[kEccHeaderBytes + 2 * kP256Coord] = {};
  ULONG written = 0;
  if (BCryptExportKey(static_cast<BCRYPT_KEY_HANDLE>(key_), nullptr, BCRYPT_ECCPUBLIC_BLOB, blob,
                      sizeof(blob), &written, 0) != STATUS_SUCCESS ||
      written != sizeof(blob)) {
    return false;
  }
  std::memcpy(out, blob + kEccHeaderBytes, 2 * kP256Coord);
  return true;
}

namespace {

// Imports a peer public point (X||Y) into a CNG key handle. Caller destroys the handle.
bool import_peer_public(const uint8_t peerPub[kPubKeyBytes], BCRYPT_KEY_HANDLE* outKey) {
  *outKey = nullptr;
  AlgProvider alg;
  if (BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) {
    return false;
  }
  uint8_t blob[kEccHeaderBytes + 2 * kP256Coord] = {};
  // dwMagic (LE) then cbKey (LE) then X, Y.
  const uint32_t magic = BCRYPT_ECDH_PUBLIC_P256_MAGIC;
  std::memcpy(blob + 0, &magic, 4);
  const uint32_t cbKey = kP256Coord;
  std::memcpy(blob + 4, &cbKey, 4);
  std::memcpy(blob + kEccHeaderBytes, peerPub, 2 * kP256Coord);
  BCRYPT_KEY_HANDLE key = nullptr;
  // BCryptImportKeyPair validates that the point is on the curve.
  if (BCryptImportKeyPair(alg.h, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob, sizeof(blob), 0) !=
      STATUS_SUCCESS) {
    return false;
  }
  *outKey = key;
  return true;
}

}  // namespace

bool DeriveAesKey(const EcdhKeyPair& mine, const uint8_t peerPub[kPubKeyBytes],
                  const uint8_t salt[kSaltBytes], const uint8_t* info, size_t infoLen,
                  uint8_t outKey[kAesKeyBytes]) {
  if (outKey) std::memset(outKey, 0, kAesKeyBytes);
  if (!mine.valid() || !peerPub || !salt || !outKey || (infoLen && !info)) return false;

  BCRYPT_KEY_HANDLE peer = nullptr;
  if (!import_peer_public(peerPub, &peer)) return false;

  bool ok = false;
  BCRYPT_SECRET_HANDLE secret = nullptr;
  if (BCryptSecretAgreement(static_cast<BCRYPT_KEY_HANDLE>(mine.handle()), peer, &secret, 0) ==
      STATUS_SUCCESS) {
    // Extract the raw ECDH shared secret, then run HKDF-SHA256 ourselves. CNG returns the raw secret
    // in the same byte order on both peers, so the derivation matches. (Codex #364.)
    ULONG rawLen = 0;
    if (BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, nullptr, 0, &rawLen, 0) ==
            STATUS_SUCCESS &&
        rawLen > 0 && rawLen <= 64) {
      uint8_t rawSecret[64] = {};
      ULONG written = 0;
      if (BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, rawSecret, rawLen, &written, 0) ==
              STATUS_SUCCESS &&
          written == rawLen) {
        ok = hkdf_sha256_32(rawSecret, rawLen, salt, kSaltBytes, info, infoLen, outKey);
      }
      SecureZeroMemory(rawSecret, sizeof(rawSecret));
    }
    BCryptDestroySecret(secret);
  }
  BCryptDestroyKey(peer);
  if (!ok) std::memset(outKey, 0, kAesKeyBytes);
  return ok;
}

namespace {

// Builds an AES-256-GCM key handle from raw key bytes. Caller destroys.
bool make_gcm_key(const uint8_t key[kAesKeyBytes], AlgProvider* alg, BCRYPT_KEY_HANDLE* outKey) {
  *outKey = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg->h, BCRYPT_AES_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) {
    return false;
  }
  if (BCryptSetProperty(alg->h, BCRYPT_CHAINING_MODE,
                        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                        sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != STATUS_SUCCESS) {
    return false;
  }
  BCRYPT_KEY_HANDLE k = nullptr;
  if (BCryptGenerateSymmetricKey(alg->h, &k, nullptr, 0, const_cast<PUCHAR>(key),
                                 static_cast<ULONG>(kAesKeyBytes), 0) != STATUS_SUCCESS) {
    return false;
  }
  *outKey = k;
  return true;
}

}  // namespace

bool AesGcmSeal(const uint8_t key[kAesKeyBytes], const uint8_t nonce[kNonceBytes], const uint8_t* aad,
                size_t aadLen, const uint8_t* plain, size_t plainLen, uint8_t* cipherOut,
                uint8_t tagOut[kTagBytes]) {
  if (!key || !nonce || !plain || !cipherOut || !tagOut || (aadLen && !aad)) return false;
  AlgProvider alg;
  BCRYPT_KEY_HANDLE k = nullptr;
  if (!make_gcm_key(key, &alg, &k)) return false;

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
  BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
  authInfo.pbNonce = const_cast<PUCHAR>(nonce);
  authInfo.cbNonce = static_cast<ULONG>(kNonceBytes);
  authInfo.pbAuthData = const_cast<PUCHAR>(aad);
  authInfo.cbAuthData = static_cast<ULONG>(aadLen);
  authInfo.pbTag = tagOut;
  authInfo.cbTag = static_cast<ULONG>(kTagBytes);

  ULONG written = 0;
  const NTSTATUS st =
      BCryptEncrypt(k, const_cast<PUCHAR>(plain), static_cast<ULONG>(plainLen), &authInfo, nullptr, 0,
                    cipherOut, static_cast<ULONG>(plainLen), &written, 0);
  BCryptDestroyKey(k);
  return st == STATUS_SUCCESS && written == plainLen;
}

bool AesGcmOpen(const uint8_t key[kAesKeyBytes], const uint8_t nonce[kNonceBytes], const uint8_t* aad,
                size_t aadLen, const uint8_t* cipher, size_t cipherLen, const uint8_t tag[kTagBytes],
                uint8_t* plainOut) {
  if (plainOut && cipherLen) std::memset(plainOut, 0, cipherLen);
  if (!key || !nonce || !cipher || !tag || !plainOut || (aadLen && !aad)) return false;
  AlgProvider alg;
  BCRYPT_KEY_HANDLE k = nullptr;
  if (!make_gcm_key(key, &alg, &k)) return false;

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
  BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
  authInfo.pbNonce = const_cast<PUCHAR>(nonce);
  authInfo.cbNonce = static_cast<ULONG>(kNonceBytes);
  authInfo.pbAuthData = const_cast<PUCHAR>(aad);
  authInfo.cbAuthData = static_cast<ULONG>(aadLen);
  authInfo.pbTag = const_cast<PUCHAR>(tag);
  authInfo.cbTag = static_cast<ULONG>(kTagBytes);

  ULONG written = 0;
  const NTSTATUS st =
      BCryptDecrypt(k, const_cast<PUCHAR>(cipher), static_cast<ULONG>(cipherLen), &authInfo, nullptr,
                    0, plainOut, static_cast<ULONG>(cipherLen), &written, 0);
  BCryptDestroyKey(k);
  if (st != STATUS_SUCCESS) {
    std::memset(plainOut, 0, cipherLen);  // never expose unverified plaintext
    return false;
  }
  return written == cipherLen;
}

// --- plaintext padding --------------------------------------------------------------------------

bool PackPassword(const uint16_t* utf16, uint16_t utf16Count, uint8_t out[kPlaintextBytes]) {
  if (!out || utf16Count > kMaxPasswordUtf16 || (utf16Count && !utf16)) return false;
  // Random pad first so unused tail is not a constant.
  if (!RandomBytes(out, kPlaintextBytes)) return false;
  out[0] = static_cast<uint8_t>(utf16Count & 0xff);
  out[1] = static_cast<uint8_t>((utf16Count >> 8) & 0xff);
  for (uint16_t i = 0; i < utf16Count; ++i) {
    out[2 + i * 2] = static_cast<uint8_t>(utf16[i] & 0xff);
    out[2 + i * 2 + 1] = static_cast<uint8_t>((utf16[i] >> 8) & 0xff);
  }
  return true;
}

bool UnpackPassword(const uint8_t block[kPlaintextBytes], uint16_t* outUtf16, uint16_t* outCount) {
  if (!block || !outUtf16 || !outCount) return false;
  const uint16_t count = static_cast<uint16_t>(block[0] | (block[1] << 8));
  if (count > kMaxPasswordUtf16) {
    *outCount = 0;
    return false;
  }
  for (uint16_t i = 0; i < count; ++i) {
    outUtf16[i] = static_cast<uint16_t>(block[2 + i * 2] | (block[2 + i * 2 + 1] << 8));
  }
  *outCount = count;
  return true;
}

// --- canonical serialization --------------------------------------------------------------------

std::vector<uint8_t> BuildAad(const UnlockContext& ctx) {
  std::vector<uint8_t> v;
  static const char kLabel[] = "GNLink-Unlock-Sealed-v1-AAD";
  v.insert(v.end(), kLabel, kLabel + sizeof(kLabel) - 1);
  const auto body = serialize_context(ctx);
  v.insert(v.end(), body.begin(), body.end());
  return v;
}

std::vector<uint8_t> BuildKdfInfo(const UnlockContext& ctx) {
  std::vector<uint8_t> v;
  static const char kLabel[] = "GNLink-Unlock-Sealed-v1-KDF";
  v.insert(v.end(), kLabel, kLabel + sizeof(kLabel) - 1);
  const auto body = serialize_context(ctx);
  v.insert(v.end(), body.begin(), body.end());
  return v;
}

// --- replay / challenge state machine -----------------------------------------------------------

void UnlockChallengeState::Issue(const uint8_t challengeId[kChallengeIdBytes],
                                 uint64_t clientSessionCookie, uint32_t lockGeneration, uint64_t nowMs,
                                 uint64_t expiresMs) {
  (void)nowMs;
  hasOutstanding_ = true;
  consumed_ = false;
  std::memcpy(outstandingId_, challengeId, kChallengeIdBytes);
  clientSessionCookie_ = clientSessionCookie;
  lockGeneration_ = lockGeneration;
  expiresMs_ = expiresMs;
}

ChallengeVerdict UnlockChallengeState::Verify(const uint8_t challengeId[kChallengeIdBytes],
                                              uint64_t clientSessionCookie, uint32_t lockGeneration,
                                              uint64_t nowMs) const {
  // A late duplicate of the just-consumed challenge reports AlreadyConsumed so the caller returns the
  // cached result instead of re-running the unlock.
  if (hasConsumedId_ && std::memcmp(challengeId, consumedId_, kChallengeIdBytes) == 0) {
    return ChallengeVerdict::AlreadyConsumed;
  }
  if (!hasOutstanding_ ||
      std::memcmp(challengeId, outstandingId_, kChallengeIdBytes) != 0 ||
      clientSessionCookie != clientSessionCookie_ || lockGeneration != lockGeneration_) {
    return ChallengeVerdict::Unknown;
  }
  if (consumed_) return ChallengeVerdict::AlreadyConsumed;
  if (nowMs > expiresMs_) return ChallengeVerdict::Expired;
  return ChallengeVerdict::Valid;
}

void UnlockChallengeState::Consume(const uint8_t challengeId[kChallengeIdBytes]) {
  if (!hasOutstanding_ || std::memcmp(challengeId, outstandingId_, kChallengeIdBytes) != 0) return;
  consumed_ = true;
  hasConsumedId_ = true;
  std::memcpy(consumedId_, outstandingId_, kChallengeIdBytes);
}

void UnlockChallengeState::ClearOutstanding() {
  hasOutstanding_ = false;
  consumed_ = false;
  std::memset(outstandingId_, 0, kChallengeIdBytes);
  clientSessionCookie_ = 0;
  lockGeneration_ = 0;
  expiresMs_ = 0;
}

}  // namespace remote60::native_poc::sealed_unlock
