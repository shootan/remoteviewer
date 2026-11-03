// lk-abcd D: old and new viewers against old and new hosts at a logon screen -- no combination closes the link,
// and each does what it should (the decision of 2026-11-03, seq18550: the four combinations fixed by a test).
//
// What is modelled, said once: the OLD side of each pair is the 0.2.163 rule, written out here as it shipped
// (prelogin_input_gate.hpp before lk-abcd: characters and text held back at any logon screen, release and
// navigation keys passing; the pre-login allowlist without ControlInputText; no 0x200000 in the pong). The NEW
// side is the product headers of this tree. A pre-login host closes the link on a control type outside its
// allowlist (host_control_session.cpp), so "the link stays up" is: every type the viewer sends is on the host's list.
//
// The phone's unlock sequence (MainActivity.sendUnlockSequence: Enter, 600 ms, the password as text, 250 ms, Enter)
// is replayed through the enter guard with its real timing: with the text held back (an older host, or the role
// not known yet) the second Enter is not delivered. Against an OLD APK nothing here can help: that APK holds the
// text back itself and sends the Enter -- the combination is written down as the known limit it is.
// Tags: pure-logic.

#include <cstdio>
#include <string>

#include "enter_after_drop_guard.hpp"
#include "lock_input_policy.hpp"
#include "prelogin_control_allowlist.hpp"
#include "prelogin_input_gate.hpp"

namespace n = remote60::native_poc;
namespace pl = remote60::native_poc::prelogin;
namespace ig = remote60::native_poc::input_guard;
using n::MessageType;

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

// ---- the 0.2.163 side, as shipped
bool OldAtLogon(uint32_t f) { return (f & (n::kCaptureFlagPreloginV1 | n::kCaptureFlagLockCredV1)) != 0; }
bool OldMaySendEvent(uint32_t f, uint16_t kind, uint32_t key) {
  if (!OldAtLogon(f)) return true;
  if (kind != 5 && kind != 6) return true;
  if (kind == 6) return true;
  return key <= 0xFFFF && pl::IsPreloginNavKeyAllowed(static_cast<uint16_t>(key));
}
bool OldMaySendText(uint32_t f) { return !OldAtLogon(f); }
bool OldAllowlist(MessageType t) { return t != MessageType::ControlInputText && pl::IsControlAllowedInPrelogin(t); }
bool OldHostDropsKey(uint16_t kind, uint32_t key) {  // the worker's payload filter (preloginBlockedKey)
  return (kind == 5 || kind == 6) && !pl::IsPreloginNavKeyAllowed(static_cast<uint16_t>(key));
}

// ---- the hosts' pongs at the logon screens
constexpr uint32_t kOldPrelogin = n::kCaptureFlagPreloginV1;
constexpr uint32_t kOldLock = n::kCaptureFlagLockCredV1 | n::kCaptureFlagSecureDesktopActive;
constexpr uint32_t kNewPrelogin = n::kCaptureFlagPreloginV1 | n::kCaptureFlagLogonPlaintextV1;
constexpr uint32_t kNewLock = n::kCaptureFlagLockCredV1 | n::kCaptureFlagSecureDesktopActive | n::kCaptureFlagLogonPlaintextV1;

constexpr uint32_t VK_RETURN_ = 0x0D;

struct Sent {
  bool text = false, letter = false, enter = false;
};
// What a NEW viewer sends of: a letter down, text, Enter down (in that order, the guard between them).
Sent NewViewer(uint32_t flags, uint64_t textMs, uint64_t enterMs) {
  Sent s;
  ig::EnterAfterDropGuard g;
  s.letter = pl::ViewerMaySendInputEvent(flags, 5, 'P');
  if (!s.letter) g.OnCharacterDropped(textMs);
  s.text = pl::ViewerMaySendText(flags);
  if (s.text) g.OnCharacterDelivered(); else g.OnCharacterDropped(textMs);
  s.enter = pl::ViewerMaySendInputEvent(flags, 5, VK_RETURN_) && !g.HoldEnterDown(enterMs);
  return s;
}
}  // namespace

int main() {
  std::printf("old and new viewers x old and new hosts at a logon screen (lk-abcd D)\n\n");

  std::printf("[the allowlist only grows: nothing an older viewer sends is newly refused]\n");
  int shrunk = 0, grown = 0;
  for (int t = 0; t < 512; ++t) {
    const auto mt = static_cast<MessageType>(t);
    if (OldAllowlist(mt) && !pl::IsControlAllowedInPrelogin(mt)) ++shrunk;
    if (!OldAllowlist(mt) && pl::IsControlAllowedInPrelogin(mt)) ++grown;
  }
  check("every type on the 0.2.163 pre-login allowlist is on the new one", shrunk == 0, std::to_string(shrunk));
  check("...and exactly one type was added (ControlInputText)", grown == 1 &&
                                                                    pl::IsControlAllowedInPrelogin(MessageType::ControlInputText),
        std::to_string(grown));
  check("ControlPhysicalKey and the host IME stay off it",
        !pl::IsControlAllowedInPrelogin(MessageType::ControlPhysicalKey) &&
            !pl::IsControlAllowedInPrelogin(MessageType::ControlImeStateRequest));

  std::printf("\n[1. NEW viewer -> OLD host (no 0x200000)]\n");
  for (const uint32_t f : {kOldPrelogin, kOldLock}) {
    const std::string at = f == kOldPrelogin ? "pre-login" : "lock screen";
    const Sent s = NewViewer(f, 600, 850);
    check("1 " + at + ": text and the letter are held back (as 0.2.163 did)", !s.text && !s.letter);
    check("1 " + at + ": THE ENTER AFTER THEM IS HELD BACK TOO (lk-abcd A) -- no empty submit", !s.enter);
    check("1 " + at + ": nothing it sends is outside the old host's allowlist (link stays up)",
          f != kOldPrelogin || (OldAllowlist(MessageType::ControlInputEvent) && !s.text));
  }

  std::printf("\n[2. OLD viewer -> NEW host]\n");
  for (const uint32_t f : {kNewPrelogin, kNewLock}) {
    const std::string at = f == kNewPrelogin ? "pre-login" : "lock screen";
    const bool letter = OldMaySendEvent(f, 5, 'P');
    const bool text = OldMaySendText(f);
    const bool enter = OldMaySendEvent(f, 5, 0x0D);
    check("2 " + at + ": the old viewer still holds characters back (it does not know 0x200000) and sends Enter",
          !letter && !text && enter);
    check("2 " + at + ": every type it sends is on the new host's allowlist (link stays up)",
          pl::IsControlAllowedInPrelogin(MessageType::ControlInputEvent) && pl::IsControlAllowedInPrelogin(MessageType::ControlPing));
  }

  std::printf("\n[3. OLD APK (0.2.30-0.2.32 shared session) -> NEW host: the known limit]\n");
  {
    // The phone's sequence at its real timing: Enter at 0, text at 600, Enter at 850.
    const bool wake = OldMaySendEvent(kNewLock, 5, 0x0D);
    const bool text = OldMaySendText(kNewLock);
    const bool submit = OldMaySendEvent(kNewLock, 5, 0x0D);
    // The new host never saw a dropped character: its guard is not armed, and nothing else holds the Enter.
    ig::EnterAfterDropGuard hostGuard;
    const bool hostDeliversWake = !hostGuard.HoldEnterDown(0);
    (void)hostGuard.HoldEnterUp();
    const bool hostDeliversSubmit = !hostGuard.HoldEnterDown(850);
    check("3 KNOWN LIMIT: the old APK holds the password text back itself and sends both Enters", wake && !text && submit);
    check("3 KNOWN LIMIT: ...the new host cannot tell -- both Enters are delivered (an empty submit); only an APK built "
          "from this tree fixes it on the phone",
          hostDeliversWake && hostDeliversSubmit);
    check("3: the link stays up (Enter is a ControlInputEvent, on every allowlist)",
          OldAllowlist(MessageType::ControlInputEvent) && pl::IsControlAllowedInPrelogin(MessageType::ControlInputEvent));
  }

  std::printf("\n[4. NEW viewer / NEW APK -> NEW host]\n");
  for (const uint32_t f : {kNewPrelogin, kNewLock}) {
    const std::string at = f == kNewPrelogin ? "pre-login" : "lock screen";
    const Sent s = NewViewer(f, 600, 850);
    check("4 " + at + ": the letter, the text and the Enter are all sent", s.letter && s.text && s.enter);
    check("4 " + at + ": every type is on the new host's allowlist (link stays up)",
          f != kNewPrelogin || (pl::IsControlAllowedInPrelogin(MessageType::ControlInputText) &&
                                pl::IsControlAllowedInPrelogin(MessageType::ControlInputEvent)));
    // The worker's payload filter is off (kAcceptAtPrelogin), the lock rule refuses nothing (kRefuse false).
    const bool workerDrops = !n::logon_plaintext::kAcceptAtPrelogin && OldHostDropsKey(5, 'P');
    const bool lockRefuses = n::lock_input::RefuseEvent(n::lock_input::kRefuse, true, 5, 'P') ||
                             n::lock_input::RefuseText(n::lock_input::kRefuse, true);
    check("4 " + at + ": the new host's own filters drop none of it (worker filter off, lock rule refuses nothing)",
          !workerDrops && !lockRefuses);
  }

  std::printf("\n[the phone's unlock sequence through the guard, real timing (Enter 0 / text 600 / Enter 850 ms)]\n");
  {
    ig::EnterAfterDropGuard g;  // an older host, or the role not known yet: the text is held back
    const bool wakeGoes = !g.HoldEnterDown(0);
    (void)g.HoldEnterUp();
    g.OnCharacterDropped(600);
    const bool submitHeld = g.HoldEnterDown(850);
    const bool submitUpHeld = g.HoldEnterUp();
    check("held text: the wake Enter goes; THE SUBMIT ENTER 250 ms LATER IS HELD BACK, and its up", wakeGoes && submitHeld && submitUpHeld);
  }
  {
    ig::EnterAfterDropGuard g;  // a new host: the text goes
    (void)g.HoldEnterDown(0);
    (void)g.HoldEnterUp();
    g.OnCharacterDelivered();
    check("delivered text: the submit Enter goes (the password is in the field before it)", !g.HoldEnterDown(850));
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
