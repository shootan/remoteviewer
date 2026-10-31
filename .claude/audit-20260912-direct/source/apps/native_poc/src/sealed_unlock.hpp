#pragma once

// Sealed unlock v1 -- the crypto and replay primitives for delivering the lock-screen password from
// the viewer to the host without exposing it to passive network capture. The name is deliberate:
// this is an UNAUTHENTICATED sealed channel. It defeats eavesdropping (ECDH + AES-GCM), but with no
// host identity it does NOT defeat an active man-in-the-middle -- a decision the user made explicitly
// (TOFU host pinning skipped). Everything security-relevant is centralised here so the wire code and
// the service never touch raw key material directly.
//
// Design (Codex review #364):
//   - P-256 ECDH (ephemeral, one keypair per unlock challenge). The raw shared secret is extracted
//     and run through HKDF-SHA256 (RFC 5869, implemented over HMAC) with a per-challenge salt and an
//     info label -> a 32-byte AES key. (CNG's HKDF-on-secret-handle path did not derive on the target
//     environment; the HMAC implementation is pinned by a known-answer test.)
//   - AES-256-GCM with a fresh 12-byte random nonce and a 16-byte tag; the AAD binds the ciphertext
//     to the challenge/session context so it cannot be replayed into a different unlock.
//   - One challenge -> one derived key -> one encryption, so a GCM nonce can never repeat for a key.
//   - A separate, pure replay state machine (no CNG) tracks the single outstanding challenge and the
//     consumed result so a duplicate sealed request executes the unlock exactly once.
//
// Thread: the crypto calls are self-contained and may be called from any single thread; handles are
// RAII and never shared. UnlockChallengeState is not internally synchronised -- the service owns it
// on one thread.
//
// Platform: Windows CNG (bcrypt). Definitions in sealed_unlock.cpp. The replay state machine and the
// byte layouts are portable and unit-tested; the CNG paths are exercised by the Windows test build.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace remote60::native_poc::sealed_unlock {

// --- fixed sizes (wire + crypto) ----------------------------------------------------------------
constexpr size_t kPubKeyBytes = 64;   // P-256 public point, X||Y, 32 bytes each (no CNG struct on the wire)
constexpr size_t kNonceBytes = 12;    // AES-GCM standard nonce
constexpr size_t kTagBytes = 16;      // AES-GCM tag
constexpr size_t kAesKeyBytes = 32;   // AES-256
constexpr size_t kSaltBytes = 32;     // HKDF salt (host random, per challenge)
constexpr size_t kChallengeIdBytes = 16;

// The plaintext is fixed-length padded so the ciphertext length does not leak the password length.
// Layout inside the padded block: uint16 utf16Count, then utf16Count UTF-16 units, then random pad.
constexpr size_t kMaxPasswordUtf16 = 128;
constexpr size_t kPlaintextBytes = 2 + kMaxPasswordUtf16 * 2;  // 258

// --- randomness / zeroing -----------------------------------------------------------------------
bool RandomBytes(uint8_t* out, size_t len);
void SecureZero(void* p, size_t len);

// --- ECDH keypair (RAII over the CNG key handle) ------------------------------------------------
class EcdhKeyPair {
 public:
  EcdhKeyPair() = default;
  ~EcdhKeyPair();
  EcdhKeyPair(const EcdhKeyPair&) = delete;
  EcdhKeyPair& operator=(const EcdhKeyPair&) = delete;
  EcdhKeyPair(EcdhKeyPair&& other) noexcept;
  EcdhKeyPair& operator=(EcdhKeyPair&& other) noexcept;

  // Generates a fresh ephemeral P-256 keypair. Returns false and leaves the object empty on failure.
  bool Generate();
  bool valid() const { return key_ != nullptr; }

  // Writes this keypair's public point (X||Y) to `out`. Returns false if there is no key.
  bool ExportPublic(uint8_t out[kPubKeyBytes]) const;

  // Handle for DeriveAesKey. Opaque to callers.
  void* handle() const { return key_; }

  void Reset();

 private:
  void* key_ = nullptr;  // BCRYPT_KEY_HANDLE
};

// Derives the shared AES-256 key from our private key and the peer's public point, mixing in the
// host salt (HKDF extract) and the caller-supplied info label (HKDF expand). `info` must be built
// identically on both sides (see BuildKdfInfo). Returns false on any CNG failure or bad input; on
// failure `outKey` is zeroed.
bool DeriveAesKey(const EcdhKeyPair& mine, const uint8_t peerPub[kPubKeyBytes],
                  const uint8_t salt[kSaltBytes], const uint8_t* info, size_t infoLen,
                  uint8_t outKey[kAesKeyBytes]);

// AES-256-GCM. `aad` binds context (challenge id, sessions, key hashes...) -- identical AAD is
// required to open. `nonce` must be unique per key (guaranteed by one-key-per-challenge). `cipherOut`
// must hold `plainLen` bytes. Returns false on failure.
bool AesGcmSeal(const uint8_t key[kAesKeyBytes], const uint8_t nonce[kNonceBytes], const uint8_t* aad,
                size_t aadLen, const uint8_t* plain, size_t plainLen, uint8_t* cipherOut,
                uint8_t tagOut[kTagBytes]);

// Opens/verifies. Returns false when the tag does not verify (tampered / wrong key / wrong AAD /
// replayed into a different context); on failure `plainOut` is zeroed and must not be used.
bool AesGcmOpen(const uint8_t key[kAesKeyBytes], const uint8_t nonce[kNonceBytes], const uint8_t* aad,
                size_t aadLen, const uint8_t* cipher, size_t cipherLen, const uint8_t tag[kTagBytes],
                uint8_t* plainOut);

// HKDF-SHA256 producing exactly 32 bytes (RFC 5869, one expand block). Exposed so a known-answer
// test can pin the hand-rolled implementation against the RFC vectors. out must hold 32 bytes.
bool Hkdf_Sha256_32(const uint8_t* ikm, size_t ikmLen, const uint8_t* salt, size_t saltLen,
                    const uint8_t* info, size_t infoLen, uint8_t out[32]);

// --- plaintext padding (hides password length) --------------------------------------------------
// Packs a UTF-16 password into the fixed kPlaintextBytes block with random padding. Returns false if
// utf16Count > kMaxPasswordUtf16.
bool PackPassword(const uint16_t* utf16, uint16_t utf16Count, uint8_t out[kPlaintextBytes]);
// Unpacks; validates the length field. Writes up to kMaxPasswordUtf16 units. Returns false on a bad
// length field. The caller must SecureZero both the block and the unpacked buffer after use.
bool UnpackPassword(const uint8_t block[kPlaintextBytes], uint16_t* outUtf16, uint16_t* outCount);

// --- canonical AAD / KDF-info serialization -----------------------------------------------------
// Both sides must serialize these identically (no struct memcpy -- explicit little-endian bytes).
// Public context only; never secrets. peer IP/port and streamGeneration are deliberately excluded.
struct UnlockContext {
  uint32_t protocolVersion = 1;
  uint64_t hostId = 0;               // stable per host install
  uint64_t clientSessionCookie = 0;  // this control session
  uint8_t challengeId[kChallengeIdBytes] = {};
  uint32_t requestId = 0;
  uint32_t requesterSession = 0;
  uint32_t consoleSession = 0;
  uint32_t lockGeneration = 0;
  uint32_t topologyGeneration = 0;  // service session-topology counter, bound so a mid-flight session change invalidates
  uint64_t issuedMs = 0;
  uint64_t expiresMs = 0;
  uint64_t accountId = 0;            // hash of the account the credential belongs to
  uint8_t hostPub[kPubKeyBytes] = {};
  uint8_t clientPub[kPubKeyBytes] = {};
};
std::vector<uint8_t> BuildAad(const UnlockContext& ctx);
std::vector<uint8_t> BuildKdfInfo(const UnlockContext& ctx);

// --- replay / challenge state machine (pure; no CNG) --------------------------------------------
enum class ChallengeVerdict {
  Valid,           // matches the one outstanding challenge, not expired, not consumed
  Unknown,         // no such outstanding challenge (or superseded by a newer one)
  Expired,         // matched id but past expiry
  AlreadyConsumed  // matched id but already consumed -- the caller returns the cached result
};

// Tracks exactly one outstanding challenge and the last consumed challenge id, so a retransmitted
// sealed request runs the unlock once and a stale/expired/foreign challenge is rejected before any
// decryption or WTS call. Not thread-safe; the service owns it on its worker thread.
class UnlockChallengeState {
 public:
  // Issues (and thereby invalidates any previous outstanding) a challenge. Records the binding.
  void Issue(const uint8_t challengeId[kChallengeIdBytes], uint64_t clientSessionCookie,
             uint32_t lockGeneration, uint64_t nowMs, uint64_t expiresMs);

  // Checks a sealed request's challenge id + binding + clock BEFORE any crypto. Does not mutate.
  ChallengeVerdict Verify(const uint8_t challengeId[kChallengeIdBytes], uint64_t clientSessionCookie,
                          uint32_t lockGeneration, uint64_t nowMs) const;

  // Marks the outstanding challenge consumed. Call only after the AEAD tag verified and before the
  // unlock executes. After this, Verify() of the same id returns AlreadyConsumed.
  void Consume(const uint8_t challengeId[kChallengeIdBytes]);

  // Clears the outstanding challenge (session change / rollover / shutdown). Consumed-id memory is
  // kept so a late duplicate of the just-consumed challenge still reports AlreadyConsumed.
  void ClearOutstanding();

 private:
  bool hasOutstanding_ = false;
  bool consumed_ = false;
  uint8_t outstandingId_[kChallengeIdBytes] = {};
  uint64_t clientSessionCookie_ = 0;
  uint32_t lockGeneration_ = 0;
  uint64_t expiresMs_ = 0;
  bool hasConsumedId_ = false;
  uint8_t consumedId_[kChallengeIdBytes] = {};
  uint64_t consumedCookie_ = 0;
  uint32_t consumedLockGeneration_ = 0;
};

}  // namespace remote60::native_poc::sealed_unlock
