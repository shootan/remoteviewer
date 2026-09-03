#pragma once

// The single source of truth for mapping the sealed-unlock challenge wire message to the crypto
// UnlockContext. Host and client both go through here so their AAD / KDF-info bytes are identical --
// the mapping being duplicated (and drifting) on the two sides would silently break every open.
// (Codex review #365: "requestId/challengeId/topologyGeneration/account/session binding must be the
// same on the AAD and the wire on both sides.")

#include <cstring>

#include "poc_protocol.hpp"
#include "sealed_unlock.hpp"

namespace remote60::native_poc {

// Host side, once it has picked the ephemeral key + salt + binding: fills the wire challenge from the
// context it will also use for AAD/KDF. (hostPub/salt/challengeId already set into ctx by the host.)
inline void FillChallengeFromContext(const sealed_unlock::UnlockContext& c, uint32_t requestId,
                                     const uint8_t salt[sealed_unlock::kSaltBytes],
                                     ControlUnlockChallengeMessage* m) {
  m->header.type = static_cast<uint16_t>(MessageType::ControlUnlockChallenge);
  m->header.size = static_cast<uint16_t>(sizeof(ControlUnlockChallengeMessage));
  m->requestId = requestId;
  m->status = 0;
  std::memcpy(m->challengeId, c.challengeId, sealed_unlock::kChallengeIdBytes);
  std::memcpy(m->hostPub, c.hostPub, sealed_unlock::kPubKeyBytes);
  if (salt) std::memcpy(m->salt, salt, sealed_unlock::kSaltBytes);
  m->hostId = c.hostId;
  m->clientSessionCookie = c.clientSessionCookie;
  m->accountId = c.accountId;
  m->requesterSession = c.requesterSession;
  m->consoleSession = c.consoleSession;
  m->lockGeneration = c.lockGeneration;
  m->topologyGeneration = c.topologyGeneration;
  m->issuedMs = c.issuedMs;
  m->expiresMs = c.expiresMs;
}

// Both sides: rebuild the exact context the AAD/KDF is derived from. The client passes its own public
// point (which the host learns from the sealed request); the host passes the clientPub it received.
inline sealed_unlock::UnlockContext ContextFromChallenge(
    const ControlUnlockChallengeMessage& m, const uint8_t clientPub[sealed_unlock::kPubKeyBytes]) {
  sealed_unlock::UnlockContext c;
  c.protocolVersion = 1;
  c.hostId = m.hostId;
  c.clientSessionCookie = m.clientSessionCookie;
  std::memcpy(c.challengeId, m.challengeId, sealed_unlock::kChallengeIdBytes);
  c.requestId = m.requestId;
  c.requesterSession = m.requesterSession;
  c.consoleSession = m.consoleSession;
  c.lockGeneration = m.lockGeneration;
  c.topologyGeneration = m.topologyGeneration;
  c.issuedMs = m.issuedMs;
  c.expiresMs = m.expiresMs;
  c.accountId = m.accountId;
  std::memcpy(c.hostPub, m.hostPub, sealed_unlock::kPubKeyBytes);
  if (clientPub) std::memcpy(c.clientPub, clientPub, sealed_unlock::kPubKeyBytes);
  return c;
}

}  // namespace remote60::native_poc
