#pragma once

// Host-side relay between the viewer's control channel and the SYSTEM service's sealed-unlock pipe
// (\\.\pipe\GNLinkUnlock). The host never sees the password or any key material: it only shuttles the
// opaque challenge/sealed/result messages. A background worker owns the single duplex pipe so the
// control thread never blocks on WTSConnectSession (which can take seconds) -- the challenge round
// trip is fast and done synchronously; the sealed request is fire-and-poll. (Codex review #365/#366.)
//
// lockscreen-cred r2 (Codex L1/L5):
//   - The relay is shared by every control connection of this host, and viewers number their requests from
//     the same small start. So everything it keeps -- challenge answers, sealed results, which requests are
//     lock-screen ones, status answers -- belongs to a TRANSACTION: (the control session's cookie, its
//     requestId). Another connection's request 2 is a new request, never the old one's cached result; a
//     connection that ends takes its transactions with it.
//   - The service's unlock pipe has one instance. A transaction holds it from its challenge to its final
//     answer (bounded); meanwhile another connection's request is answered "busy" at once -- it never rides on
//     (and overwrites) the held connection's context.
//   - Every exchange on the pipe is overlapped and bounded by a deadline, and is cancelled when the relay
//     stops or the control session it belongs to ends; the worker that owns the pipe cancels the I/O, waits
//     for it, and closes the pipe itself. A silent service never holds the pipe past that.
//   - "The service can type into the lock screen" is an answer of a particular service process, kept for a
//     while: a new server process, a failed exchange or its age makes it unknown again and it is asked anew.
//
// lockscreen-cred r3 (Codex L1-R / L5-R / UX-R):
//   - Every transaction is numbered when it starts (its challenge). A result -- answer, failure or busy -- is
//     kept only while that same numbered transaction is alive: an exchange that completes after its session
//     ended (or after the session started a newer one) publishes nothing.
//   - The hold has an absolute end -- challenge 35 s, typed credential 130 s -- checked before every command,
//     however full the queue is; the held session's own exchanges get no more than what is left of it. When it
//     ends, the transaction ends with it (a typed one as UnlockUnconfirmed) and is never continued on another
//     pipe connection.
//   - "Busy" is its own stage (UnlockStage::Busy), never a general error, so the viewer can say what it is.

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
  // TEST ONLY: shorter exchange deadlines, so a test of a silent service does not wait the product's.
  void SetExchangeDeadlinesForTest(uint32_t shortMs, uint32_t sealedMs) {
    shortDeadlineMs_ = shortMs;
    sealedDeadlineMs_ = sealedMs;
  }
  // TEST ONLY (r3 L5-R): shorter hold ends (the product's are 35 s / 130 s).
  void SetHoldLimitsForTest(uint64_t challengeHoldMs, uint64_t typedHoldMs) {
    challengeHoldMs_ = challengeHoldMs;
    typedHoldMs_ = typedHoldMs;
  }
  // TEST ONLY (r3 L1-R): how many entries are kept for `clientSessionCookie` across every map.
  size_t KeptEntriesForTest(uint64_t clientSessionCookie) const;

  // Control thread: synchronous, fast (no WTSConnectSession here). Fills `out` with the service's
  // challenge (or a rejection stage). Returns false if the service is unreachable / timed out.
  bool ChallengeSync(const ControlUnlockChallengeRequestMessage& req, uint64_t clientSessionCookie,
                     ControlUnlockChallengeMessage* out, uint32_t timeoutMs = 3000);

  // Control thread: hand the sealed password to the worker and return immediately. The result is
  // polled with PollResult. A repeat for a transaction already final is a no-op.
  void SealedAsync(const ControlUnlockSealedRequestMessage& req, uint64_t clientSessionCookie);

  // Control thread: the latest result of THIS control session's request. False if nothing is known yet
  // (the viewer keeps polling until `terminal`).
  bool PollResult(uint64_t clientSessionCookie, uint32_t requestId, ControlUnlockStatusResultMessage* out);

  // --- lockscreen-cred ------------------------------------------------------------------------------
  // Whether the service on the other end can type a credential into the lock screen. Known only from its
  // own answer to CapabilitiesRequest on a verified connection, and only for a while (see the top); an older
  // service, which drops the connection on a kind it does not know, is Unsupported (asked again later).
  enum class LockCredSupport { Unknown, Supported, Unsupported };
  LockCredSupport LockCredSupportNow() const;
  // Any thread: ask the service, in the background, if that is not known (or due again). Never blocks.
  void ProbeLockCredAsync();
  // Control thread: a challenge for the lock-screen credential purpose -- sent only to a service that said
  // it supports it (otherwise `out` is a RejectedPolicy with no pipe traffic). `out->reserved` is the purpose
  // the service issued it for. Returns false if the service is unreachable / timed out.
  bool LockCredChallengeSync(const ControlUnlockChallengeRequestMessage& req, uint64_t clientSessionCookie,
                             ControlUnlockChallengeMessage* out, uint32_t timeoutMs = 3000);
  // Control thread: for THIS control session's lock-credential job that is not final yet, ask the service
  // where it is now (bounded wait); the answer replaces the result PollResult reads. False for others.
  bool RefreshLockCredStatus(uint64_t clientSessionCookie, uint32_t requestId, uint32_t timeoutMs = 1000);
  // The control session `clientSessionCookie` ended: its exchange in flight is cancelled, the pipe it holds
  // is let go, and everything kept for its transactions is dropped. Never blocks.
  void EndSession(uint64_t clientSessionCookie);

  void Stop();

 private:
  enum class Op { Challenge, LockCredChallenge, Sealed, Capabilities, Status, Release };
  struct Command {
    Op op = Op::Challenge;
    uint64_t cookie = 0;
    uint32_t statusRequestId = 0;
    uint64_t txGen = 0;  // r3 L1-R: the transaction (liveTx_) it was queued for; 0 = none
    ControlUnlockChallengeRequestMessage challenge{};
    ControlUnlockSealedRequestMessage seal{};
    SecureUnlockSealedRequest seal_ipc{};  // prebuilt pipe message (password ciphertext)
  };
  struct TxKey {
    uint64_t cookie = 0;
    uint32_t requestId = 0;
    bool operator<(const TxKey& o) const { return cookie != o.cookie ? cookie < o.cookie : requestId < o.requestId; }
  };
  enum class Io { Ok, Failed, Timeout, Cancelled };

  void EnsureWorker();
  void WorkerLoop();
  // Returns HANDLE or INVALID_HANDLE_VALUE (opened for overlapped I/O). `busy` (r3): it failed because the
  // pipe's one instance stayed taken -- another host's transaction holds it.
  void* ConnectPipe(bool* busy = nullptr);
  Io Exchange(void* pipe, const void* req, uint32_t reqSize, uint8_t* buf, uint32_t bufSize, uint32_t replySize,
              uint32_t deadlineMs);

  std::thread worker_;
  std::atomic<bool> running_{false};
  void* cancelEvent_ = nullptr;  // HANDLE: Stop() and EndSession() of the active transaction's session
  std::atomic<uint64_t> activeCookie_{0};  // the session whose exchange is on the pipe right now
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Command> queue_;

  std::map<TxKey, ControlUnlockChallengeMessage> challengeResults_;
  std::map<TxKey, ControlUnlockStatusResultMessage> sealedResults_;
  std::map<TxKey, bool> lockCredRequests_;
  std::map<TxKey, uint64_t> statusAnswered_;
  uint64_t statusSeq_ = 0;
  // r3 L1-R: the transactions alive now and their numbers -- started by a challenge, dropped by EndSession.
  std::map<TxKey, uint64_t> liveTx_;
  uint64_t txGenSeq_ = 0;
  bool IsLiveLocked(const Command& c, uint32_t requestId) const;  // under mu_

  // Capability: its answer, when, and of which server process (under mu_).
  LockCredSupport lockCredSupport_ = LockCredSupport::Unknown;
  uint64_t capsCheckedMs_ = 0;
  uint32_t capsServerPid_ = 0;
  bool capsInFlight_ = false;
  uint64_t capsRetryAfterMs_ = 0;

  uint32_t shortDeadlineMs_ = 3000;   // capability / challenge / status
  uint32_t sealedDeadlineMs_ = 25000; // the service's typing (child start, target wait, typing) or WTS connect
  uint64_t challengeHoldMs_ = 35000;  // an issued challenge holds the pipe for its seal (challenge: 30 s)
  uint64_t typedHoldMs_ = 130000;     // a typed lock-screen credential, for its unlock (service: 120 s)

  const wchar_t* testPipeName_ = nullptr;     // nullptr -> production kSecureUnlockPipeName
  const wchar_t* testServiceName_ = nullptr;  // nullptr -> production kSecureInputServiceName
  ServerIdentityGatherer testGatherer_ = nullptr;  // nullptr -> production GatherServerIdentity
};

}  // namespace remote60::native_poc
