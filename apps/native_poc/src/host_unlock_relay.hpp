#pragma once

// Host-side relay between the viewer's control channel and the SYSTEM service's sealed-unlock pipe
// (\\.\pipe\GNLinkUnlock). The host never sees the password or any key material: it only shuttles the
// opaque challenge/sealed/result messages. A background worker owns the single duplex pipe so the
// control thread never blocks on WTSConnectSession (which can take seconds) -- the challenge round
// trip is fast and done synchronously; the sealed request is fire-and-poll. (Codex review #365/#366.)

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "poc_protocol.hpp"
#include "secure_unlock_ipc.hpp"

namespace remote60::native_poc {

class HostUnlockRelay {
 public:
  HostUnlockRelay() = default;
  ~HostUnlockRelay();

  // Control thread: synchronous, fast (no WTSConnectSession here). Fills `out` with the service's
  // challenge (or a rejection stage). Returns false if the service is unreachable / timed out.
  bool ChallengeSync(const ControlUnlockChallengeRequestMessage& req, uint64_t clientSessionCookie,
                     ControlUnlockChallengeMessage* out, uint32_t timeoutMs = 3000);

  // Control thread: hand the sealed password to the worker and return immediately. The result is
  // polled with PollResult. Safe to call once per requestId.
  void SealedAsync(const ControlUnlockSealedRequestMessage& req, uint64_t clientSessionCookie);

  // Control thread: read the latest result for a requestId. Returns false if nothing is known yet
  // (the viewer keeps polling until `terminal`).
  bool PollResult(uint32_t requestId, ControlUnlockStatusResultMessage* out);

  void Stop();

 private:
  struct Command {
    bool sealed = false;
    ControlUnlockChallengeRequestMessage challenge{};
    ControlUnlockSealedRequestMessage seal{};
    SecureUnlockSealedRequest seal_ipc{};  // prebuilt pipe message (password ciphertext)
    uint64_t cookie = 0;
  };

  void EnsureWorker();
  void WorkerLoop();
  void* ConnectPipe();  // returns HANDLE or INVALID_HANDLE_VALUE

  std::thread worker_;
  std::atomic<bool> running_{false};
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Command> queue_;

  // Challenge results, keyed by requestId, delivered back to the waiting control thread.
  std::map<uint32_t, ControlUnlockChallengeMessage> challengeResults_;
  // Sealed job results, keyed by requestId.
  std::map<uint32_t, ControlUnlockStatusResultMessage> sealedResults_;
};

}  // namespace remote60::native_poc
