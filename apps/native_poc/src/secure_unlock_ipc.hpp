#pragma once

// Host (GNLinkStream) <-> secure-input service (GNLinkInputService, LocalSystem) IPC for sealed
// unlock. This is a SEPARATE, duplex pipe from the high-rate input pipe (which stays inbound-only,
// fire-and-forget) so the request/response unlock exchange never perturbs input latency, and so the
// service can return a real result (Codex review #365: the input pipe cannot).
//
// The service is the crypto + WTS authority: it generates the ephemeral ECDH key and challenge (so
// the password is only ever decrypted inside LocalSystem), snapshots the session topology, and runs
// WTSConnectSession. The host only relays these between the viewer's control channel and this pipe.
//
// Fixed pack(1) structs; sizes are pinned by static_assert. Array sizes mirror sealed_unlock.hpp.

#include <cstdint>

namespace remote60::native_poc {

constexpr uint32_t kSecureUnlockMagic = 0x554E4C4Bu;  // "UNLK"
constexpr wchar_t kSecureUnlockPipeName[] = L"\\\\.\\pipe\\GNLinkUnlock";

enum class SecureUnlockKind : uint16_t {
  ChallengeRequest = 1,   // host -> service: user pressed Unlock
  ChallengeResponse = 2,  // service -> host: one-shot challenge (or rejection in `stage`)
  SealedRequest = 3,      // host -> service: the sealed password for the challenge
  Result = 4,             // service -> host: job stage (terminal results are cached/idempotent)
};

#pragma pack(push, 1)

// host -> service. clientSessionCookie identifies the control session the request belongs to; the
// service binds the challenge to it so a sealed request from a different session is rejected.
struct SecureUnlockChallengeRequest {
  uint32_t magic = kSecureUnlockMagic;
  uint16_t size = static_cast<uint16_t>(sizeof(SecureUnlockChallengeRequest));
  uint16_t kind = static_cast<uint16_t>(SecureUnlockKind::ChallengeRequest);
  uint32_t requestId = 0;
  uint32_t reserved = 0;
  uint64_t clientSessionCookie = 0;
};

// service -> host. `stage` == ChallengeIssued on success; any Rejected* stage means no challenge was
// issued (host relays the reason to the viewer, shows nothing sealable).
struct SecureUnlockChallengeResponse {
  uint32_t magic = kSecureUnlockMagic;
  uint16_t size = static_cast<uint16_t>(sizeof(SecureUnlockChallengeResponse));
  uint16_t kind = static_cast<uint16_t>(SecureUnlockKind::ChallengeResponse);
  uint32_t requestId = 0;
  uint16_t stage = 0;  // UnlockStage
  uint16_t reserved = 0;
  uint8_t challengeId[16] = {};
  uint8_t hostPub[64] = {};
  uint8_t salt[32] = {};
  uint64_t hostId = 0;
  uint64_t clientSessionCookie = 0;
  uint64_t accountId = 0;
  uint32_t requesterSession = 0;
  uint32_t consoleSession = 0;
  uint32_t lockGeneration = 0;
  uint32_t topologyGeneration = 0;
  uint64_t issuedMs = 0;
  uint64_t expiresMs = 0;
};

// host -> service.
struct SecureUnlockSealedRequest {
  uint32_t magic = kSecureUnlockMagic;
  uint16_t size = static_cast<uint16_t>(sizeof(SecureUnlockSealedRequest));
  uint16_t kind = static_cast<uint16_t>(SecureUnlockKind::SealedRequest);
  uint32_t requestId = 0;
  uint32_t reserved = 0;
  uint64_t clientSessionCookie = 0;
  uint8_t challengeId[16] = {};
  uint8_t clientPub[64] = {};
  uint8_t nonce[12] = {};
  uint8_t tag[16] = {};
  uint8_t cipher[258] = {};
};

// service -> host. Terminal once `terminal` == 1; the service caches the terminal result per
// requestId so a duplicate SealedRequest never re-runs WTSConnectSession.
struct SecureUnlockResult {
  uint32_t magic = kSecureUnlockMagic;
  uint16_t size = static_cast<uint16_t>(sizeof(SecureUnlockResult));
  uint16_t kind = static_cast<uint16_t>(SecureUnlockKind::Result);
  uint32_t requestId = 0;
  uint32_t jobId = 0;
  uint16_t stage = 0;     // UnlockStage
  uint16_t terminal = 0;  // 1 once final
  uint32_t win32Error = 0;
};

#pragma pack(pop)

static_assert(sizeof(SecureUnlockChallengeRequest) == 24, "unlock ipc challenge-req drift");
static_assert(sizeof(SecureUnlockChallengeResponse) == 184, "unlock ipc challenge-resp drift");
static_assert(sizeof(SecureUnlockSealedRequest) == 390, "unlock ipc sealed-req drift");
static_assert(sizeof(SecureUnlockResult) == 24, "unlock ipc result drift");

}  // namespace remote60::native_poc
