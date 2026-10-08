#pragma once

// The service's side of the sealed lock-screen unlock, per unlock-pipe connection (prelogin-ui r15, R14-2).
// Moved out of secure_input_service_main.cpp with its OS calls as primitives, so the decisions can be run
// against stand-ins.
//
// r14 made the secure-INPUT pipe serve several clients, each with its own requester. The unlock path still
// read a process-wide "requester session" -- whatever input connection had connected last -- for the
// challenge's session and account and for the last check before WTSConnectSession: with two hosts
// connected, host A's unlock was built for host B's session (or refused because of B's state), and B's
// input connection ending (or A's, in the same session) wiped the value for the other.
//
// Now the caller is the one this unlock pipe connection is with: the service reads its process (id,
// creation time) and session from the pipe when it connects (UnlockCaller), the challenge is issued for
// that session and account, and right before the irreversible WTSConnectSession the same process must
// still be the one on the pipe. Nothing an input connection does touches it. The topology fence stays --
// and is now moved only by real session events (WTS), not by input connections coming and going.

#include <windows.h>

#include <cstdint>
#include <functional>

#include "poc_protocol.hpp"
#include "sealed_unlock.hpp"
#include "secure_unlock_ipc.hpp"

namespace remote60::native_poc::sealed_unlock_service {

namespace su = remote60::native_poc::sealed_unlock;

inline constexpr uint32_t kNoSession = 0xffffffffu;

// Who is on this unlock pipe connection, as the service read it from the pipe at connect.
struct UnlockCaller {
  uint32_t processId = 0;
  uint64_t processStartTime = 0;
  uint32_t session = kNoSession;
  // r16 R15-3: the caller's process, opened (SYNCHRONIZE) when the connection was made and held for its
  // life -- whether THAT process is still running is asked of this handle, not of a pid that may by then
  // name another process (or of metadata that stays readable after it exited).
  HANDLE process = nullptr;
};

// r16 R15-3: the process behind `process` has not exited.
inline bool CallerProcessAlive(HANDLE process) {
  return process != nullptr && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

// The OS, as the decisions need it (the service binds the real calls; tests bind stand-ins).
struct UnlockPrimitives {
  std::function<bool(uint32_t session, bool* known)> sessionIsLocked;
  std::function<bool(uint32_t session, bool* known)> sessionIsActive;
  std::function<uint32_t()> consoleSession;
  std::function<uint64_t(uint32_t session, bool* known)> accountIdOf;
  std::function<uint64_t()> hostId;
  std::function<uint32_t()> topologyGeneration;  // bumped by real session events only
  std::function<uint64_t()> nowMs;
  // The irreversible call. True = accepted; *gle = GetLastError on failure.
  std::function<bool(uint32_t requester, uint32_t console, const wchar_t* password, DWORD* gle)> wtsConnect;
  std::function<bool()> running;
  std::function<void(uint32_t ms)> sleep;
  std::function<void(const char* line)> log;  // optional
};

// Per-connection unlock state, owned by the unlock thread.
struct UnlockSession {
  UnlockCaller caller;                      // r15 R14-2: this connection's caller -- never another's
  std::function<bool()> callerStillThere;   // the same process is still on this pipe
  su::EcdhKeyPair key;
  su::UnlockChallengeState challenge;
  su::UnlockContext ctx{};                  // the issued context (clientPub filled at seal time)
  uint8_t salt[su::kSaltBytes] = {};
  bool hasChallenge = false;
  uint32_t badAttempts = 0;                 // invalid-tag DoS guard
  uint32_t terminalRequestId = 0;           // cached terminal result (idempotent duplicate)
  bool hasTerminal = false;
  SecureUnlockResult terminal{};
  uint64_t lastChallengeMs = 0;             // rate-limit issuance (capability is advertised on the wire)
};

using Write = std::function<void(const void* data, size_t len)>;

void HandleChallengeRequest(UnlockSession& us, const SecureUnlockChallengeRequest& req, const UnlockPrimitives& p,
                            const Write& write);
void HandleSealedRequest(UnlockSession& us, const SecureUnlockSealedRequest& req, uint32_t jobId,
                         const UnlockPrimitives& p, const Write& write);

}  // namespace remote60::native_poc::sealed_unlock_service
