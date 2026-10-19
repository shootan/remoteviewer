#pragma once

// "Windows 로그인" -- the pre-login worker's end of the sealed credential (prelogin-ui r3, item 5 of
// prelogin-service; r5 G7).
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
// Bound:  (r5 G7) the opened password waits for the logon UI only while everything it was sent for still
//         holds: its control session (EndSession drops it), its challenge (a new challenge, or the
//         challenge's expiry, drops it), and the console session and input desktop it was opened under
//         (the environment identity). Any change: it is zeroed and nothing is typed. The target is
//         identified, not just recognised -- its identity is taken again immediately before the typing
//         (one SendInput for the whole password) and after it; a target that changed before is not
//         typed into, one that changed during is reported as a failure.
// Result: Injected (8) means "typed", NOT "logged in"; the viewer shows the two differently.
// Thread: the control thread calls Challenge/Sealed/PollResult/EndSession; the waiting and typing run on
//         one worker thread; the lock guards both.

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include "poc_protocol.hpp"
#include "sealed_unlock.hpp"

namespace remote60::native_poc::prelogin {

class PreloginCredReceiver {
 public:
  // Types `count` UTF-16 units in one go; false when the OS refused (win32 in *err).
  using InjectFn = std::function<bool(const uint16_t* units, uint16_t count, uint32_t* err)>;
  // The identity of the logon UI that holds the input now; 0 when the input is not at it.
  using TargetFn = std::function<uint64_t()>;
  // The identity of where the worker is: the console session and the input desktop.
  using EnvironmentFn = std::function<uint64_t()>;

  PreloginCredReceiver(InjectFn inject, TargetFn target, EnvironmentFn environment, uint64_t hostId,
                       uint32_t targetWaitMs = 3000, uint32_t challengeLifetimeMs = 30000)
      : inject_(std::move(inject)),
        target_(std::move(target)),
        environment_(std::move(environment)),
        hostId_(hostId),
        targetWaitMs_(targetWaitMs),
        challengeLifetimeMs_(challengeLifetimeMs) {}
  ~PreloginCredReceiver();
  PreloginCredReceiver(const PreloginCredReceiver&) = delete;
  PreloginCredReceiver& operator=(const PreloginCredReceiver&) = delete;

  // 40 -> 41. A rejected request answers with its stage (not ChallengeIssued). A new challenge drops
  // a credential still waiting from the previous one.
  ControlUnlockChallengeMessage Challenge(const ControlUnlockChallengeRequestMessage& req, uint64_t cookie);
  // 42: open now (a failure is the result at once); the wait for the logon UI and the typing run on
  // a worker thread, so the control thread answers 43 without waiting. The result is kept for 44.
  void Sealed(const ControlUnlockSealedRequestMessage& req, uint64_t cookie);
  // 44 -> 45: false while there is no result for `requestId` (still waiting for the target / typing).
  bool PollResult(uint32_t requestId, ControlUnlockStatusResultMessage* out);
  // The control session with this cookie ended: a credential of it still waiting is dropped, untyped.
  void EndSession(uint64_t cookie);

  // Told every terminal result (request id, stage, win32) -- never the credential. Set before use.
  std::function<void(uint32_t requestId, uint16_t stage, uint32_t win32)> onResult;
  // r17 R16-1: false once this worker has left the logon desktop (PreloginLeftLatch): no challenge is
  // issued, no sealed credential is opened, and one already waiting is not typed. Unset: always serving.
  std::function<bool()> stillServing;

  uint64_t injections() {  // how many credentials were typed (tests, diag)
    std::lock_guard<std::mutex> lock(mu_);
    return injections_;
  }

 private:
  // The worker: owns and frees `units`.
  void Deliver(uint16_t* units, uint16_t count, uint32_t requestId, uint64_t environment, uint64_t expiresMs);
  void Finish(uint32_t requestId, UnlockStage stage, uint32_t win32);  // under mu_

  std::mutex mu_;
  // r7 H5: the last look (still valid? the same target?) and the typing are one step under this lock, and
  // EndSession takes it before it cancels -- so once EndSession has returned, no typing can begin.
  std::mutex commitMu_;
  std::thread worker_;
  bool inflight_ = false;
  uint64_t inflightCookie_ = 0;
  std::atomic<bool> cancel_{false};  // set by EndSession / a new Challenge for the one in flight
  InjectFn inject_;
  TargetFn target_;
  EnvironmentFn environment_;
  uint64_t hostId_;
  uint32_t targetWaitMs_;
  uint32_t challengeLifetimeMs_;
  sealed_unlock::EcdhKeyPair key_;
  uint8_t salt_[sealed_unlock::kSaltBytes] = {};
  ControlUnlockChallengeMessage challenge_{};
  bool haveChallenge_ = false;
  uint64_t challengeExpiresMs_ = 0;
  uint64_t challengeCookie_ = 0;
  sealed_unlock::UnlockChallengeState state_;
  uint32_t generation_ = 0;
  uint64_t lastChallengeMs_ = 0;
  bool haveResult_ = false;
  ControlUnlockStatusResultMessage result_{};
  uint64_t injections_ = 0;
};

// The product's typing, target and environment: one SendInput of Unicode units; the target is the
// foreground window of %SystemRoot%\System32\LogonUI.exe running as LocalSystem in this worker's session
// while the input desktop is Winlogon -- AND (r7 H5) the element that has the keyboard focus is a password
// field of that same process (UI Automation IsPassword): a user-name box of the same window is not a
// target. (A test build of the host, REMOTE60_STREAM_TEST_SEAM, may name a test window's title instead of
// LogonUI -- GNLINK_STREAM_TEST_CRED_TARGET_TITLE; the password-field rule is the same.) The environment
// is the console session and the input desktop's name. And the host id the challenge carries.
PreloginCredReceiver::InjectFn ProductCredInject();
PreloginCredReceiver::TargetFn ProductCredTarget();
PreloginCredReceiver::EnvironmentFn ProductCredEnvironment();
// r18 R17-1: the same two with what they look at named -- the input desktop's name, the logon UI's password
// field (identity, 0 = none) and the desktop served. Every look at the input desktop that finds a desktop
// OTHER than the served one is the worker's permanent leave (PreloginSawDesktop: the latch + exit 14), not
// just "no target now"; the served desktop with no password field yet is the ordinary wait. The product
// functions above are these with the real sources.
PreloginCredReceiver::TargetFn ProductCredTargetWith(std::function<std::wstring()> desktopName,
                                                     std::function<uint64_t()> logonUiPasswordField, std::wstring served);
PreloginCredReceiver::EnvironmentFn ProductCredEnvironmentWith(std::function<std::wstring()> desktopName, std::wstring served);
uint64_t ProductCredHostId();
// How long the worker waits for the logon UI (3 s; a test build may set GNLINK_STREAM_TEST_CRED_WAIT_MS).
uint32_t ProductCredTargetWaitMs();

}  // namespace remote60::native_poc::prelogin
