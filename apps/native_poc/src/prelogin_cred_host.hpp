#pragma once

// "Windows 로그인" -- the pre-login worker's end of the sealed credential (prelogin-ui r3, item 5 of
// prelogin-service).
//
// Role:   in --prelogin mode the host is the SYSTEM worker on the console's Winlogon desktop; there is no
//         user session for the service's lock-screen unlock to connect. The same sealed exchange
//         (ControlUnlockChallengeRequest 40 / Challenge 41 / SealedRequest 42 / Accepted 43 / Status
//         44-45, wire unchanged) is answered HERE: a fresh ephemeral key per challenge, the challenge
//         bound to this control session, and the credential opened ONLY under the prelogin purpose
//         (sealed_unlock BuildPreloginCred*; an Unlock-purpose blob fails at the tag). The password is
//         typed as Unicode keystrokes into the logon UI -- only when the input target is the one this
//         worker types into (LogonUI.exe in the product), never into whatever else has focus -- and
//         zeroed at once. Nothing is stored or logged.
// Result: Injected (8) means "typed", NOT "logged in"; the viewer shows the two differently.
// Thread: one control thread at a time per host; the lock only guards against a second dispatcher.

#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include "poc_protocol.hpp"
#include "sealed_unlock.hpp"

namespace remote60::native_poc::prelogin {

class PreloginCredReceiver {
 public:
  // Types `count` UTF-16 units; false when the OS refused (win32 in *err).
  using InjectFn = std::function<bool(const uint16_t* units, uint16_t count, uint32_t* err)>;
  // True when the input target is the logon UI this worker may type into.
  using TargetFn = std::function<bool()>;

  PreloginCredReceiver(InjectFn inject, TargetFn target, uint64_t hostId, uint32_t targetWaitMs = 3000)
      : inject_(std::move(inject)), target_(std::move(target)), hostId_(hostId), targetWaitMs_(targetWaitMs) {}
  ~PreloginCredReceiver();
  PreloginCredReceiver(const PreloginCredReceiver&) = delete;
  PreloginCredReceiver& operator=(const PreloginCredReceiver&) = delete;

  // 40 -> 41. A rejected request answers with its stage (not ChallengeIssued).
  ControlUnlockChallengeMessage Challenge(const ControlUnlockChallengeRequestMessage& req, uint64_t cookie);
  // 42: open now (a failure is the result at once); the wait for the logon UI and the typing run on
  // a worker thread, so the control thread answers 43 without waiting. The result is kept for 44.
  void Sealed(const ControlUnlockSealedRequestMessage& req, uint64_t cookie);
  // 44 -> 45: false while there is no result for `requestId` (still waiting for the target / typing).
  bool PollResult(uint32_t requestId, ControlUnlockStatusResultMessage* out);

  // Told every terminal result (request id, stage, win32) -- never the credential. Set before use.
  std::function<void(uint32_t requestId, uint16_t stage, uint32_t win32)> onResult;

  uint64_t injections() {  // how many credentials were typed (tests, diag)
    std::lock_guard<std::mutex> lock(mu_);
    return injections_;
  }

 private:
  void Deliver(uint16_t* units, uint16_t count, uint32_t requestId);  // the worker: owns and frees `units`

  std::mutex mu_;
  std::thread worker_;
  bool inflight_ = false;
  uint32_t inflightId_ = 0;
  InjectFn inject_;
  TargetFn target_;
  uint64_t hostId_;
  uint32_t targetWaitMs_;
  sealed_unlock::EcdhKeyPair key_;
  uint8_t salt_[sealed_unlock::kSaltBytes] = {};
  ControlUnlockChallengeMessage challenge_{};
  bool haveChallenge_ = false;
  sealed_unlock::UnlockChallengeState state_;
  uint32_t generation_ = 0;
  uint64_t lastChallengeMs_ = 0;
  bool haveResult_ = false;
  ControlUnlockStatusResultMessage result_{};
  uint64_t injections_ = 0;
};

// The product's typing and target: SendInput Unicode units; the target is the foreground window of
// LogonUI.exe (a test build of the host, REMOTE60_STREAM_TEST_SEAM, may name a test window's title
// instead -- GNLINK_STREAM_TEST_CRED_TARGET_TITLE). And the host id the challenge carries.
PreloginCredReceiver::InjectFn ProductCredInject();
PreloginCredReceiver::TargetFn ProductCredTarget();
uint64_t ProductCredHostId();

}  // namespace remote60::native_poc::prelogin
