// Pins the sealed-unlock wire messages and the challenge<->context mapping. The static_asserts in
// poc_protocol.hpp pin the byte layout; this test pins that a challenge round-trips through raw bytes
// unchanged and that both peers rebuild an identical crypto context (hence identical AAD/KDF) from it.
// (Codex review #365.)

#include <cstdio>
#include <cstring>
#include <vector>

#include "poc_protocol.hpp"
#include "sealed_unlock.hpp"
#include "secure_unlock_ipc.hpp"
#include "unlock_wire.hpp"

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}

// A packed control struct must survive a memcpy to a byte buffer and back byte-for-byte -- that is
// the "serialization" for these pack(1) messages, and how they travel the control channel.
void TestWireRoundTrip() {
  std::printf("challenge message round-trips through raw bytes\n");
  ControlUnlockChallengeMessage m{};
  m.header.type = static_cast<uint16_t>(MessageType::ControlUnlockChallenge);
  m.header.size = static_cast<uint16_t>(sizeof(m));
  m.requestId = 0x11223344;
  m.status = 0;
  for (int i = 0; i < 16; ++i) m.challengeId[i] = static_cast<uint8_t>(0xA0 + i);
  for (int i = 0; i < 64; ++i) m.hostPub[i] = static_cast<uint8_t>(i);
  for (int i = 0; i < 32; ++i) m.salt[i] = static_cast<uint8_t>(0x40 + i);
  m.hostId = 0xdeadbeefcafef00dull;
  m.clientSessionCookie = 0x0102030405060708ull;
  m.accountId = 0x99887766ull;
  m.requesterSession = 1;
  m.consoleSession = 13;
  m.lockGeneration = 5;
  m.topologyGeneration = 9;
  m.issuedMs = 1000;
  m.expiresMs = 31000;

  std::vector<uint8_t> bytes(sizeof(m));
  std::memcpy(bytes.data(), &m, sizeof(m));
  ControlUnlockChallengeMessage back{};
  std::memcpy(&back, bytes.data(), sizeof(back));

  check(std::memcmp(&m, &back, sizeof(m)) == 0, "byte round-trip is identical");
  check(back.topologyGeneration == 9 && back.consoleSession == 13 && back.hostId == m.hostId,
        "key binding fields survive");
}

// Both peers must build the same AAD from the same challenge (+ the client's public point), or the
// GCM tag will never verify. This is the drift Codex flagged; unlock_wire.hpp is the single mapping.
void TestContextMappingIdentical() {
  std::printf("host and client derive an identical context/AAD from the challenge\n");
  ControlUnlockChallengeMessage m{};
  m.requestId = 7;
  for (int i = 0; i < 16; ++i) m.challengeId[i] = static_cast<uint8_t>(i + 1);
  for (int i = 0; i < 64; ++i) m.hostPub[i] = static_cast<uint8_t>(0x10 + i);
  m.hostId = 0x1111222233334444ull;
  m.clientSessionCookie = 0xAABBCCDDull;
  m.accountId = 0x5555ull;
  m.requesterSession = 1;
  m.consoleSession = 13;
  m.lockGeneration = 2;
  m.topologyGeneration = 4;
  m.issuedMs = 500;
  m.expiresMs = 30500;

  uint8_t clientPub[sealed_unlock::kPubKeyBytes];
  for (int i = 0; i < static_cast<int>(sealed_unlock::kPubKeyBytes); ++i)
    clientPub[i] = static_cast<uint8_t>(0x80 + i);

  const auto hostCtx = ContextFromChallenge(m, clientPub);
  const auto clientCtx = ContextFromChallenge(m, clientPub);
  const auto aadHost = sealed_unlock::BuildAad(hostCtx);
  const auto aadClient = sealed_unlock::BuildAad(clientCtx);
  const auto kdfHost = sealed_unlock::BuildKdfInfo(hostCtx);
  const auto kdfClient = sealed_unlock::BuildKdfInfo(clientCtx);
  check(aadHost == aadClient, "AAD identical on both sides");
  check(kdfHost == kdfClient, "KDF info identical on both sides");
  check(hostCtx.topologyGeneration == 4 && hostCtx.consoleSession == 13,
        "binding fields carried into the context");

  // A different challenge id must change the AAD (so a blob cannot be replayed under a new challenge).
  ControlUnlockChallengeMessage m2 = m;
  m2.challengeId[0] ^= 0xff;
  const auto aad2 = sealed_unlock::BuildAad(ContextFromChallenge(m2, clientPub));
  check(aad2 != aadHost, "different challenge id -> different AAD");
}

// FillChallengeFromContext (host side) must produce a message that ContextFromChallenge maps back to
// the same context -- the two halves of the mapping are inverses over the bound fields.
void TestFillRoundTrip() {
  std::printf("FillChallengeFromContext and ContextFromChallenge agree\n");
  sealed_unlock::UnlockContext c;
  c.hostId = 0x1234ull;
  c.clientSessionCookie = 0x5678ull;
  for (int i = 0; i < 16; ++i) c.challengeId[i] = static_cast<uint8_t>(i * 3 + 1);
  c.requestId = 42;
  c.requesterSession = 1;
  c.consoleSession = 13;
  c.lockGeneration = 6;
  c.topologyGeneration = 11;
  c.issuedMs = 100;
  c.expiresMs = 30100;
  c.accountId = 0xF00Dull;
  for (int i = 0; i < 64; ++i) c.hostPub[i] = static_cast<uint8_t>(0x20 + i);
  uint8_t salt[sealed_unlock::kSaltBytes];
  for (int i = 0; i < static_cast<int>(sealed_unlock::kSaltBytes); ++i) salt[i] = static_cast<uint8_t>(i);

  ControlUnlockChallengeMessage m{};
  FillChallengeFromContext(c, c.requestId, salt, &m);
  uint8_t clientPub[sealed_unlock::kPubKeyBytes] = {};
  const auto c2 = ContextFromChallenge(m, clientPub);
  check(c2.hostId == c.hostId && c2.topologyGeneration == c.topologyGeneration &&
            c2.requestId == c.requestId && c2.consoleSession == c.consoleSession &&
            std::memcmp(c2.challengeId, c.challengeId, sealed_unlock::kChallengeIdBytes) == 0 &&
            std::memcmp(c2.hostPub, c.hostPub, sealed_unlock::kPubKeyBytes) == 0,
        "context -> challenge -> context preserves bound fields");
  check(m.header.type == static_cast<uint16_t>(MessageType::ControlUnlockChallenge), "type set");
  check(std::memcmp(m.salt, salt, sealed_unlock::kSaltBytes) == 0, "salt carried onto the wire");
}

}  // namespace

int main() {
  TestWireRoundTrip();
  TestContextMappingIdentical();
  TestFillRoundTrip();
  if (gFailures != 0) {
    std::printf("unlock_protocol_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("unlock_protocol_test: PASS\n");
  return 0;
}
