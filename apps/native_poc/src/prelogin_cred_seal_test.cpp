// Tests for the prelogin credential sealed purpose (sealed_unlock.hpp BuildPreloginCred*). The point is
// ISOLATION: a credential sealed for the pre-login LogonUI path must open ONLY under the prelogin context
// and never under the lock-screen Unlock context (and vice versa), which is exactly the mechanism by
// which an old, unlock-only peer rejects a prelogin blob. The existing Unlock round trip is unchanged
// (covered by sealed_unlock_test). (prelogin-service r3 §2.)

#include "sealed_unlock.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace remote60::native_poc::sealed_unlock;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}

UnlockContext ctx_with(const uint8_t hostPub[kPubKeyBytes], const uint8_t clientPub[kPubKeyBytes]) {
  UnlockContext c;
  c.protocolVersion = 1;
  c.hostId = 0x1122334455667788ull;
  c.clientSessionCookie = 0xA0A0A0A0u;
  for (int i = 0; i < static_cast<int>(kChallengeIdBytes); ++i) c.challengeId[i] = static_cast<uint8_t>(0x40 + i);
  c.requestId = 11;
  c.consoleSession = 1;
  c.accountId = 0xA0C0DE01ull;
  std::memcpy(c.hostPub, hostPub, kPubKeyBytes);
  std::memcpy(c.clientPub, clientPub, kPubKeyBytes);
  return c;
}
}  // namespace

int main() {
  std::printf("prelogin credential sealed purpose\n");

  EcdhKeyPair host, client;
  check(host.Generate() && client.Generate(), "keypairs generate");
  uint8_t hostPub[kPubKeyBytes], clientPub[kPubKeyBytes];
  host.ExportPublic(hostPub);
  client.ExportPublic(clientPub);
  UnlockContext ctx = ctx_with(hostPub, clientPub);

  std::printf("[the purpose labels diverge from the unlock labels]\n");
  check(BuildPreloginCredAad(ctx) != BuildAad(ctx), "prelogin AAD != unlock AAD");
  check(BuildPreloginCredKdfInfo(ctx) != BuildKdfInfo(ctx), "prelogin KDF info != unlock KDF info");

  std::printf("[prelogin credential round trip under its own context]\n");
  const auto info = BuildPreloginCredKdfInfo(ctx);
  const auto aad = BuildPreloginCredAad(ctx);
  uint8_t salt[kSaltBytes];
  RandomBytes(salt, kSaltBytes);
  uint8_t ck[kAesKeyBytes], hk[kAesKeyBytes];
  check(DeriveAesKey(client, hostPub, salt, info.data(), info.size(), ck) &&
            DeriveAesKey(host, clientPub, salt, info.data(), info.size(), hk) &&
            std::memcmp(ck, hk, kAesKeyBytes) == 0,
        "both sides derive the same prelogin key");

  const uint16_t secret[] = {L'P', 0x00E4 /*ä*/, 0xAC00 /*가*/, L'9'};
  uint8_t plain[kPlaintextBytes];
  check(PackPassword(secret, 4, plain), "pack credential");
  uint8_t nonce[kNonceBytes];
  RandomBytes(nonce, kNonceBytes);
  uint8_t cipher[kPlaintextBytes], tag[kTagBytes];
  check(AesGcmSeal(ck, nonce, aad.data(), aad.size(), plain, kPlaintextBytes, cipher, tag), "seal under prelogin AAD");

  uint8_t opened[kPlaintextBytes];
  check(AesGcmOpen(hk, nonce, aad.data(), aad.size(), cipher, kPlaintextBytes, tag, opened), "open under prelogin AAD");
  check(std::memcmp(opened, plain, kPlaintextBytes) == 0, "opened credential matches");

  std::printf("[NEGATIVE: cross-purpose and old-peer rejection]\n");
  // An unlock-context AAD (what a shipped unlock-only host reconstructs) must NOT open a prelogin blob.
  const auto unlockAad = BuildAad(ctx);
  uint8_t out[kPlaintextBytes];
  check(!AesGcmOpen(hk, nonce, unlockAad.data(), unlockAad.size(), cipher, kPlaintextBytes, tag, out),
        "a prelogin blob does NOT open under the unlock AAD (old peer rejects)");

  // And the key itself diverges: a key derived with the unlock KDF info cannot open the prelogin blob.
  const auto unlockInfo = BuildKdfInfo(ctx);
  uint8_t unlockKey[kAesKeyBytes];
  DeriveAesKey(host, clientPub, salt, unlockInfo.data(), unlockInfo.size(), unlockKey);
  check(std::memcmp(unlockKey, hk, kAesKeyBytes) != 0, "unlock-label key != prelogin-label key");
  check(!AesGcmOpen(unlockKey, nonce, unlockAad.data(), unlockAad.size(), cipher, kPlaintextBytes, tag, out),
        "unlock key + unlock AAD cannot open a prelogin blob");

  // Symmetric: a blob sealed under the UNLOCK context must not open under the prelogin context.
  uint8_t ucipher[kPlaintextBytes], utag[kTagBytes];
  check(AesGcmSeal(unlockKey, nonce, unlockAad.data(), unlockAad.size(), plain, kPlaintextBytes, ucipher, utag),
        "seal a blob under the unlock context");
  check(!AesGcmOpen(hk, nonce, aad.data(), aad.size(), ucipher, kPlaintextBytes, utag, out),
        "an unlock blob does NOT open under the prelogin context");

  // lockscreen-cred r1 (Codex A3): the third purpose, LockCred. Every pairing of the three: a blob sealed
  // for one opens under its own key + AAD and under NO other combination -- neither the other purpose's
  // key with its AAD, nor this key with the other AAD.
  std::printf("[lockscreen-cred: Unlock / PreloginCred / LockCred, every pairing]\n");
  struct Purpose {
    const char* name;
    std::vector<uint8_t> (*kdf)(const UnlockContext&);
    std::vector<uint8_t> (*aad)(const UnlockContext&);
  };
  const Purpose purposes[] = {{"Unlock", BuildKdfInfo, BuildAad},
                              {"PreloginCred", BuildPreloginCredKdfInfo, BuildPreloginCredAad},
                              {"LockCred", BuildLockCredKdfInfo, BuildLockCredAad}};
  check(BuildLockCredAad(ctx) != BuildAad(ctx) && BuildLockCredAad(ctx) != BuildPreloginCredAad(ctx),
        "lock-cred AAD differs from both others");
  check(BuildLockCredKdfInfo(ctx) != BuildKdfInfo(ctx) && BuildLockCredKdfInfo(ctx) != BuildPreloginCredKdfInfo(ctx),
        "lock-cred KDF info differs from both others");
  for (const Purpose& s : purposes) {
    const auto sInfo = s.kdf(ctx);
    const auto sAad = s.aad(ctx);
    uint8_t sKeyClient[kAesKeyBytes], sKeyHost[kAesKeyBytes];
    const bool derived = DeriveAesKey(client, hostPub, salt, sInfo.data(), sInfo.size(), sKeyClient) &&
                         DeriveAesKey(host, clientPub, salt, sInfo.data(), sInfo.size(), sKeyHost);
    uint8_t sCipher[kPlaintextBytes], sTag[kTagBytes];
    const bool sealed = derived && AesGcmSeal(sKeyClient, nonce, sAad.data(), sAad.size(), plain, kPlaintextBytes, sCipher, sTag);
    for (const Purpose& o : purposes) {
      const auto oInfo = o.kdf(ctx);
      const auto oAad = o.aad(ctx);
      uint8_t oKey[kAesKeyBytes];
      const bool oDerived = DeriveAesKey(host, clientPub, salt, oInfo.data(), oInfo.size(), oKey);
      char what[160];
      if (&s == &o) {
        std::snprintf(what, sizeof(what), "%s blob opens under %s key + AAD", s.name, o.name);
        check(sealed && oDerived && AesGcmOpen(oKey, nonce, oAad.data(), oAad.size(), sCipher, kPlaintextBytes, sTag, out) &&
                  std::memcmp(out, plain, kPlaintextBytes) == 0,
              what);
        continue;
      }
      std::snprintf(what, sizeof(what), "%s blob does NOT open under %s key + AAD", s.name, o.name);
      check(sealed && oDerived && !AesGcmOpen(oKey, nonce, oAad.data(), oAad.size(), sCipher, kPlaintextBytes, sTag, out), what);
      std::snprintf(what, sizeof(what), "%s blob does NOT open under its own key + %s AAD", s.name, o.name);
      check(sealed && !AesGcmOpen(sKeyHost, nonce, oAad.data(), oAad.size(), sCipher, kPlaintextBytes, sTag, out), what);
    }
  }

  if (gFailures != 0) {
    std::printf("prelogin_cred_seal_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_cred_seal_test: PASS\n");
  return 0;
}
