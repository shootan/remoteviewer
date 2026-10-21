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
#include "trusted_service_pipe.hpp"

namespace remote60::native_poc {

class HostUnlockRelay {
 public:
  HostUnlockRelay() = default;
  ~HostUnlockRelay();

  /**
   * TEST ONLY (secure-pipe-identity gate): point the REAL verified connect at a test pipe/service name
   * and an injected gatherer; shipping uses the production defaults with no runtime option to change
   * them. ProbeConnectForTest runs exactly the production ConnectPipe (verify included) once and reports
   * whether it yielded a trusted handle, closing it -- the relay's connect gate without the crypto.
   */
  void SetVerificationEndpointForTest(const wchar_t* pipeName, const wchar_t* serviceName,
                                      ServerIdentityGatherer gather) {
    testPipeName_ = pipeName;
    testServiceName_ = serviceName;
    testGatherer_ = gather;
  }
  bool ProbeConnectForTest();

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

  // --- lockscreen-cred r1 -------------------------------------------------------------------------
  // Whether the service on the other end can type a credential into the lock screen. Known only from its
  // own answer to CapabilitiesRequest on a verified connection; an older service, which drops the
  // connection on a kind it does not know, is Unsupported (asked again after a while -- it may have been
  // updated). Never sends that service anything else new.
  enum class LockCredSupport { Unknown, Supported, Unsupported };
  LockCredSupport LockCredSupportNow() const { return lockCredSupport_.load(std::memory_order_acquire); }
  // Any thread: ask the service, in the background, if that is not known (or due again). Never blocks.
  void ProbeLockCredAsync();
  // Control thread: a challenge for the lock-screen credential purpose -- sent only to a service that said
  // it supports it (otherwise `out` is a RejectedPolicy with no pipe traffic). `out->reserved` is the purpose
  // the service issued it for. Returns false if the service is unreachable / timed out.
  bool LockCredChallengeSync(const ControlUnlockChallengeRequestMessage& req, uint64_t clientSessionCookie,
                             ControlUnlockChallengeMessage* out, uint32_t timeoutMs = 3000);
  // Control thread: for a lock-credential job that is not terminal yet, ask the service where it is now
  // (bounded wait); the answer replaces the cached result PollResult reads. False for other requestIds.
  bool RefreshLockCredStatus(uint32_t requestId, uint32_t timeoutMs = 1000);

  void Stop();

 private:
  enum class Op { Challenge, LockCredChallenge, Sealed, Capabilities, Status };
  struct Command {
    Op op = Op::Challenge;
    uint32_t statusRequestId = 0;
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
  std::atomic<void*> workerThreadHandle_{nullptr};  // published so Stop() can CancelSynchronousIo it
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Command> queue_;

  // Challenge results, keyed by requestId, delivered back to the waiting control thread.
  std::map<uint32_t, ControlUnlockChallengeMessage> challengeResults_;
  // Sealed job results, keyed by requestId.
  std::map<uint32_t, ControlUnlockStatusResultMessage> sealedResults_;
  // lockscreen-cred r1: requestIds whose challenge was issued for the lock-screen credential, and the
  // status refreshes answered (requestId -> answered), for RefreshLockCredStatus's wait.
  std::map<uint32_t, bool> lockCredRequests_;
  std::map<uint32_t, uint64_t> statusAnswered_;
  uint64_t statusSeq_ = 0;
  std::atomic<LockCredSupport> lockCredSupport_{LockCredSupport::Unknown};
  bool capsInFlight_ = false;           // under mu_
  uint64_t capsRetryAfterMs_ = 0;       // under mu_: an Unsupported answer is asked again after this

  const wchar_t* testPipeName_ = nullptr;     // nullptr -> production kSecureUnlockPipeName
  const wchar_t* testServiceName_ = nullptr;  // nullptr -> production kSecureInputServiceName
  ServerIdentityGatherer testGatherer_ = nullptr;  // nullptr -> production GatherServerIdentity
};

}  // namespace remote60::native_poc
