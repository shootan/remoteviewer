#pragma once

// ECDSA P-256 / SHA-256 signature verification for update manifests.
//
// The update path does not trust an artifact because it arrived over TLS -- TLS says the bytes
// came from the server we dialled, not that we are the ones who published them. So the manifest
// carries a detached signature, and the public key that checks it is compiled into the product.
// Both stay: TLS for the transport, this for provenance.
//
// Nothing here implements cryptography. The curve, the hash and the verification all come from
// BCrypt/CNG; this file only marshals a raw public point and a raw r||s signature into the shapes
// CNG expects.
//
// NOT Authenticode. This does not make Windows trust the publisher, and it does not affect the
// UAC prompt or SmartScreen -- those need a code-signing certificate, which is a separate matter.

#include <cstdint>
#include <string>
#include <vector>

namespace remote60::native_poc::update {

// A P-256 public key as the raw affine point: 32 bytes of X followed by 32 bytes of Y, with no
// leading 0x04 tag. This is what CNG's BCRYPT_ECCKEY_BLOB carries and what a JWK's x/y decode to.
inline constexpr size_t kP256PublicKeyBytes = 64;
// ECDSA over P-256 signs to two 32-byte integers. Raw r||s, the IEEE P1363 form -- not DER.
inline constexpr size_t kP256SignatureBytes = 64;

/**
 * True when `signature` is a valid ECDSA P-256/SHA-256 signature over `document` under
 * `publicKeyXY`.
 *
 * Returns false for every failure -- wrong signature, malformed key, CNG unavailable -- and never
 * throws. A caller cannot act differently on "invalid" versus "could not check", and treating
 * them alike is the safe direction: both mean the artifact is not known to be ours.
 */
bool verify_ecdsa_p256_sha256(const std::string& document,
                              const std::vector<uint8_t>& signature,
                              const std::vector<uint8_t>& publicKeyXY);

/** Decodes lowercase or uppercase hex. Returns false on odd length or a non-hex character. */
bool decode_hex(const std::string& text, std::vector<uint8_t>* out);

}  // namespace remote60::native_poc::update
