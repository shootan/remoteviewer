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

// r17 R16-2: THIS connection is still there -- the caller process alive (its held handle), the pipe's client
// still that process, AND the connection itself not broken: a client that closed only its pipe handle keeps
// its pid (GetNamedPipeClientProcessId still answers it) while the pipe is broken (PeekNamedPipe fails with
// ERROR_BROKEN_PIPE). Non-blocking; called on the thread that owns the pipe.
inline bool UnlockConnectionAlive(HANDLE pipe, const UnlockCaller& caller) {
  if (!CallerProcessAlive(caller.process)) return false;
  ULONG pid = 0;
  if (!GetNamedPipeClientProcessId(pipe, &pid) || pid != caller.processId) return false;
  DWORD avail = 0;
  return PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr) != FALSE;
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
  // lockscreen-cred r1: type `count` UTF-16 units into the lock screen's password field of `session` --
  // the service's own agent in that session, on its Winlogon desktop, after it has checked the target
  // (logon_ui_target.hpp). Returns Injected, TargetNotReady (nothing typed) or InjectFailed (*win32).
  // Unset: the feature is not offered (Capabilities lists nothing).
  // lockscreen-cred r2 (Codex L2): `stillValid` is the service's fence -- asked right before the field is touched
  // (after the agent found its target); false = everything it was issued for no longer holds -> nothing typed,
  // RejectedStaleTopology. The agent never decides that alone.
  std::function<UnlockStage(uint32_t session, const uint16_t* units, uint16_t count, uint32_t* win32,
                            const std::function<bool()>& stillValid)>
      typeLockCred;
};

// lockscreen-cred r1: how long a typed lock-screen credential waits for its session to unlock (the user
// presses Enter) before the job ends as UnlockUnconfirmed.
inline constexpr uint64_t kLockCredUnlockWaitMs = 120000;
// lockscreen-cred r2 (Codex L2): a consumed lock-screen credential must reach the field within this, or it is not typed.
inline constexpr uint64_t kLockCredCommitMs = 20000;

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
  uint64_t terminalCookie = 0;              // r2 (L1): ...of that control session only
  bool hasTerminal = false;
  SecureUnlockResult terminal{};
  uint64_t lastChallengeMs = 0;             // rate-limit issuance (capability is advertised on the wire)
  // lockscreen-cred r1: the purpose the outstanding challenge was ISSUED for (kUnlockPurpose*). The sealed
  // request is opened under this purpose only -- never by trying the others.
  uint16_t purpose = kUnlockPurposeUnlock;
  // A typed lock-screen credential waiting to be seen unlocking its session (the job is not terminal yet).
  bool lockCredWaiting = false;
  uint32_t lockCredRequestId = 0;
  uint64_t lockCredCookie = 0;     // r2 (L1): the control session it belongs to
  uint32_t lockCredJobId = 0;
  uint32_t lockCredTopology = 0;   // the topology generation when it was typed
  uint64_t lockCredTypedMs = 0;
  uint32_t rejectLogs = 0;         // refusals said so far on this connection (bounded)
};

using Write = std::function<void(const void* data, size_t len)>;

void HandleChallengeRequest(UnlockSession& us, const SecureUnlockChallengeRequest& req, const UnlockPrimitives& p,
                            const Write& write);
// lockscreen-cred r1: a challenge for the lock-screen credential -- only for the caller's own session while it
// is the console, ACTIVE and locked. The disconnected-session unlock above is untouched.
void HandleLockCredChallengeRequest(UnlockSession& us, const SecureUnlockChallengeRequest& req,
                                    const UnlockPrimitives& p, const Write& write);
// lockscreen-cred r1: the answer to CapabilitiesRequest.
void HandleCapabilities(const SecureUnlockQuery& q, const UnlockPrimitives& p, const Write& write);
// lockscreen-cred r1: where a lock-credential job is now (Injected while it waits; then SessionUnlocked or
// UnlockUnconfirmed, terminal and cached). Any other requestId: RejectedPolicy, terminal.
void HandleStatusRequest(UnlockSession& us, const SecureUnlockQuery& q, const UnlockPrimitives& p, const Write& write);
void HandleSealedRequest(UnlockSession& us, const SecureUnlockSealedRequest& req, uint32_t jobId,
                         const UnlockPrimitives& p, const Write& write);

}  // namespace remote60::native_poc::sealed_unlock_service
