// Tests for the sealed-unlock crypto + replay primitives (sealed_unlock.hpp). These pin the two
// things that are catastrophic to get wrong: (1) both sides derive the same AES key and the GCM tag
// rejects any tampering / wrong context, so the password is confidential and bound to its challenge;
// (2) the replay state machine runs an unlock exactly once and rejects stale/expired/foreign or
// duplicate requests before any decryption. (Codex review #364.)

#include "sealed_unlock.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace remote60::native_poc::sealed_unlock;

namespace {

int gFailures = 0;

void check(bool cond, const char* what) {
  if (cond) {
    std::printf("  ok   %s\n", what);
  } else {
    std::printf("  FAIL %s\n", what);
    ++gFailures;
  }
}

UnlockContext make_ctx() {
  UnlockContext c;
  c.protocolVersion = 1;
  c.hostId = 0x1122334455667788ull;
  c.clientSessionCookie = 0xabcdef01u;
  for (int i = 0; i < static_cast<int>(kChallengeIdBytes); ++i) c.challengeId[i] = static_cast<uint8_t>(i + 1);
  c.requestId = 7;
  c.requesterSession = 1;
  c.consoleSession = 13;
  c.lockGeneration = 4;
  c.issuedMs = 1000;
  c.expiresMs = 31000;
  c.accountId = 0xdeadbeefull;
  return c;
}

// The end-to-end shape: host and client each hold an ephemeral keypair, exchange public points, and
// must derive an identical AES key; a message sealed by the client opens on the host and not under
// any altered context.
void TestEcdhRoundTrip() {
  std::printf("ECDH agreement + AES-GCM seal/open round trip\n");
  EcdhKeyPair host, client;
  check(host.Generate() && client.Generate(), "both keypairs generate");

  uint8_t hostPub[kPubKeyBytes], clientPub[kPubKeyBytes];
  check(host.ExportPublic(hostPub) && client.ExportPublic(clientPub), "export both public points");

  UnlockContext ctx = make_ctx();
  std::memcpy(ctx.hostPub, hostPub, kPubKeyBytes);
  std::memcpy(ctx.clientPub, clientPub, kPubKeyBytes);
  const auto info = BuildKdfInfo(ctx);
  const auto aad = BuildAad(ctx);

  uint8_t salt[kSaltBytes];
  check(RandomBytes(salt, kSaltBytes), "salt generated");

  uint8_t hostKey[kAesKeyBytes], clientKey[kAesKeyBytes];
  check(DeriveAesKey(client, hostPub, salt, info.data(), info.size(), clientKey),
        "client derives key from host public");
  check(DeriveAesKey(host, clientPub, salt, info.data(), info.size(), hostKey),
        "host derives key from client public");
  check(std::memcmp(hostKey, clientKey, kAesKeyBytes) == 0, "both sides derived the SAME key");

  // Client seals a password; host opens it.
  const uint16_t pw[] = {L'P', L'a', L's', L's', 0xAC00 /*가*/, L'!'};
  uint8_t plain[kPlaintextBytes];
  check(PackPassword(pw, 6, plain), "pack password");

  uint8_t nonce[kNonceBytes];
  check(RandomBytes(nonce, kNonceBytes), "nonce generated");
  uint8_t cipher[kPlaintextBytes], tag[kTagBytes];
  check(AesGcmSeal(clientKey, nonce, aad.data(), aad.size(), plain, kPlaintextBytes, cipher, tag),
        "client seals");

  uint8_t opened[kPlaintextBytes];
  check(AesGcmOpen(hostKey, nonce, aad.data(), aad.size(), cipher, kPlaintextBytes, tag, opened),
        "host opens with correct key/aad/tag");
  check(std::memcmp(opened, plain, kPlaintextBytes) == 0, "opened plaintext matches");

  uint16_t outPw[kMaxPasswordUtf16];
  uint16_t outCount = 0;
  check(UnpackPassword(opened, outPw, &outCount) && outCount == 6 &&
            std::memcmp(outPw, pw, 6 * sizeof(uint16_t)) == 0,
        "unpacked password matches (incl. Hangul unit)");
}

// Any tampering or context mismatch must fail the tag -- confidentiality is worthless if a modified
// or replayed-into-another-context blob still opens.
void TestTamperAndContextRejected() {
  std::printf("tampered ciphertext / wrong AAD / wrong key are rejected\n");
  EcdhKeyPair host, client;
  host.Generate();
  client.Generate();
  uint8_t hostPub[kPubKeyBytes], clientPub[kPubKeyBytes];
  host.ExportPublic(hostPub);
  client.ExportPublic(clientPub);
  UnlockContext ctx = make_ctx();
  std::memcpy(ctx.hostPub, hostPub, kPubKeyBytes);
  std::memcpy(ctx.clientPub, clientPub, kPubKeyBytes);
  const auto info = BuildKdfInfo(ctx);
  const auto aad = BuildAad(ctx);
  uint8_t salt[kSaltBytes];
  RandomBytes(salt, kSaltBytes);
  uint8_t key[kAesKeyBytes];
  DeriveAesKey(client, hostPub, salt, info.data(), info.size(), key);

  uint8_t plain[kPlaintextBytes];
  const uint16_t pw[] = {L'x'};
  PackPassword(pw, 1, plain);
  uint8_t nonce[kNonceBytes];
  RandomBytes(nonce, kNonceBytes);
  uint8_t cipher[kPlaintextBytes], tag[kTagBytes];
  AesGcmSeal(key, nonce, aad.data(), aad.size(), plain, kPlaintextBytes, cipher, tag);

  uint8_t out[kPlaintextBytes];
  uint8_t badCipher[kPlaintextBytes];
  std::memcpy(badCipher, cipher, kPlaintextBytes);
  badCipher[0] ^= 0x01;
  check(!AesGcmOpen(key, nonce, aad.data(), aad.size(), badCipher, kPlaintextBytes, tag, out),
        "flipped ciphertext byte -> reject");

  uint8_t badTag[kTagBytes];
  std::memcpy(badTag, tag, kTagBytes);
  badTag[0] ^= 0x01;
  check(!AesGcmOpen(key, nonce, aad.data(), aad.size(), cipher, kPlaintextBytes, badTag, out),
        "flipped tag byte -> reject");

  // Different AAD: change the challengeId (i.e., a blob replayed under a new challenge).
  UnlockContext ctx2 = ctx;
  ctx2.challengeId[0] ^= 0xff;
  const auto aad2 = BuildAad(ctx2);
  check(!AesGcmOpen(key, nonce, aad2.data(), aad2.size(), cipher, kPlaintextBytes, tag, out),
        "AAD from a different challenge -> reject");

  // Wrong key: derive under a different salt.
  uint8_t salt2[kSaltBytes];
  RandomBytes(salt2, kSaltBytes);
  uint8_t key2[kAesKeyBytes];
  DeriveAesKey(host, clientPub, salt2, info.data(), info.size(), key2);
  check(std::memcmp(key, key2, kAesKeyBytes) != 0, "different salt -> different key");
  check(!AesGcmOpen(key2, nonce, aad.data(), aad.size(), cipher, kPlaintextBytes, tag, out),
        "wrong key -> reject");
}

void TestMalformedPublicKeyRejected() {
  std::printf("a malformed peer public key is rejected\n");
  EcdhKeyPair mine;
  mine.Generate();
  uint8_t salt[kSaltBytes];
  RandomBytes(salt, kSaltBytes);
  uint8_t badPub[kPubKeyBytes];
  std::memset(badPub, 0xff, kPubKeyBytes);  // 0xff..ff is not a valid curve point
  const char info[] = "info";
  uint8_t key[kAesKeyBytes];
  check(!DeriveAesKey(mine, badPub, salt, reinterpret_cast<const uint8_t*>(info), 4, key),
        "not-on-curve public point -> derive fails");
}

void TestPackBounds() {
  std::printf("password packing validates length\n");
  uint8_t block[kPlaintextBytes];
  std::vector<uint16_t> tooLong(kMaxPasswordUtf16 + 1, L'a');
  check(!PackPassword(tooLong.data(), static_cast<uint16_t>(tooLong.size()), block),
        "over-length password -> pack fails");
  std::vector<uint16_t> maxLen(kMaxPasswordUtf16, L'a');
  check(PackPassword(maxLen.data(), kMaxPasswordUtf16, block), "max-length password -> pack ok");
  uint16_t out[kMaxPasswordUtf16];
  uint16_t count = 0;
  check(UnpackPassword(block, out, &count) && count == kMaxPasswordUtf16, "unpack max-length ok");
  // Corrupt the length field beyond the max.
  block[0] = 0xff;
  block[1] = 0xff;
  check(!UnpackPassword(block, out, &count), "bogus length field -> unpack fails");
}

// RFC 5869 Test Case 1 (SHA-256), first 32 bytes of OKM. Pins the hand-rolled HKDF against the
// standard so a subtle HMAC/extract/expand mistake cannot silently ship. (Codex review #365.)
void TestHkdfRfc5869() {
  std::printf("HKDF-SHA256 matches RFC 5869 test vector 1\n");
  uint8_t ikm[22];
  std::memset(ikm, 0x0b, sizeof(ikm));
  uint8_t salt[13];
  for (int i = 0; i < 13; ++i) salt[i] = static_cast<uint8_t>(i);  // 00 01 ... 0c
  const uint8_t info[10] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
  const uint8_t wantOkm32[32] = {0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f,
                                 0x64, 0xd0, 0x36, 0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a,
                                 0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf};
  uint8_t out[32];
  check(Hkdf_Sha256_32(ikm, sizeof(ikm), salt, sizeof(salt), info, sizeof(info), out),
        "HKDF derives");
  check(std::memcmp(out, wantOkm32, 32) == 0, "HKDF output equals RFC 5869 OKM[0:32]");
}

// The replay state machine: exactly-once execution and rejection of stale/foreign/expired/duplicate.
void TestReplayStateMachine() {
  std::printf("challenge/replay state machine\n");
  UnlockChallengeState st;
  uint8_t id[kChallengeIdBytes];
  for (int i = 0; i < static_cast<int>(kChallengeIdBytes); ++i) id[i] = static_cast<uint8_t>(i);
  const uint64_t cookie = 42, gen = 3;

  check(st.Verify(id, cookie, gen, 1000) == ChallengeVerdict::Unknown, "unknown before Issue");
  st.Issue(id, cookie, gen, 1000, 31000);
  check(st.Verify(id, cookie, gen, 2000) == ChallengeVerdict::Valid, "valid within window");
  check(st.Verify(id, cookie + 1, gen, 2000) == ChallengeVerdict::Unknown, "wrong session cookie -> unknown");
  check(st.Verify(id, cookie, gen + 1, 2000) == ChallengeVerdict::Unknown, "wrong lock generation -> unknown");
  check(st.Verify(id, cookie, gen, 40000) == ChallengeVerdict::Expired, "past expiry -> expired");

  uint8_t other[kChallengeIdBytes];
  std::memset(other, 0xAA, kChallengeIdBytes);
  check(st.Verify(other, cookie, gen, 2000) == ChallengeVerdict::Unknown, "different id -> unknown");

  // Consume once; a duplicate is AlreadyConsumed (caller returns cached result, does not re-run).
  st.Consume(id);
  check(st.Verify(id, cookie, gen, 2000) == ChallengeVerdict::AlreadyConsumed, "after consume -> already consumed");

  // A newer challenge supersedes the old outstanding one.
  uint8_t id2[kChallengeIdBytes];
  std::memset(id2, 0x5, kChallengeIdBytes);
  st.Issue(id2, cookie, gen, 50000, 80000);
  check(st.Verify(id2, cookie, gen, 51000) == ChallengeVerdict::Valid, "new challenge valid");
  check(st.Verify(id, cookie, gen, 51000) == ChallengeVerdict::AlreadyConsumed,
        "the just-consumed id is still remembered as consumed (same context)");
  check(st.Verify(id, cookie + 99, gen, 51000) != ChallengeVerdict::AlreadyConsumed,
        "consumed id under a different cookie is NOT treated as consumed");

  st.ClearOutstanding();
  check(st.Verify(id2, cookie, gen, 51000) == ChallengeVerdict::Unknown, "cleared outstanding -> unknown");
}

}  // namespace

int main() {
  TestEcdhRoundTrip();
  TestTamperAndContextRejected();
  TestMalformedPublicKeyRejected();
  TestPackBounds();
  TestHkdfRfc5869();
  TestReplayStateMachine();

  if (gFailures != 0) {
    std::printf("sealed_unlock_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("sealed_unlock_test: PASS\n");
  return 0;
}
