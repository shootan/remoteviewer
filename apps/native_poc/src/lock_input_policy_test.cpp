// lockscreen-cred r1 (Codex A6): the one rule the host and the service both apply to plaintext input bound for
// a locked session (lock_input_policy.hpp). The table: which session states count as "at the lock screen",
// and which input is a character there. The point of the controls is that a UAC prompt on an unlocked session
// -- a secure desktop too -- keeps its typing, and that releasing a key is never refused.

#include <cstdio>
#include <string>

#include "lock_input_policy.hpp"

namespace li = remote60::native_poc::lock_input;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
}
constexpr uint32_t kA = 'A', kDigit1 = '1', kSpace = 0x20, kBack = 0x08, kTab = 0x09, kEnter = 0x0D, kLeft = 0x25,
                   kShift = 0x10, kCtrl = 0x11;
constexpr bool kOld = true;  // the lockscreen-cred A6 rule (refuse), as it was before lk-abcd D
}  // namespace

int main() {
  std::printf("plaintext to a locked session (lockscreen-cred r1, A6)\n\n[the session state]\n");
  check("known locked (whatever the desktop): at the lock screen", li::TreatAsLocked(true, true, false) && li::TreatAsLocked(true, true, true));
  check("known NOT locked on a secure desktop (a UAC prompt): not the lock screen -- its typing stays",
        !li::TreatAsLocked(true, false, false));
  check("known not locked on Default: not the lock screen", !li::TreatAsLocked(true, false, true));
  check("unknown off the Default desktop: treated as the lock screen (unknown is not 'unlocked')", li::TreatAsLocked(false, false, false));
  check("unknown on the Default desktop: not the lock screen", !li::TreatAsLocked(false, false, true));

  std::printf("\n[what is a character there -- the rule as it was (refuse=true), kept for a policy turned back]\n");
  const bool locked = true;
  check("letter down: refused", li::RefuseEvent(kOld, locked, li::kKeyDown, kA));
  check("digit down: refused", li::RefuseEvent(kOld, locked, li::kKeyDown, kDigit1));
  check("space down: refused", li::RefuseEvent(kOld, locked, li::kKeyDown, kSpace));
  check("backspace down: refused (not on the navigation list)", li::RefuseEvent(kOld, locked, li::kKeyDown, kBack));
  check("Ctrl down: refused (chords are not navigation)", li::RefuseEvent(kOld, locked, li::kKeyDown, kCtrl));
  check("a key code beyond 16 bits: refused", li::RefuseEvent(kOld, locked, li::kKeyDown, 0x10041));
  check("Tab / Enter / Left / Shift down: pass (navigation, and Enter submits)",
        !li::RefuseEvent(kOld, locked, li::kKeyDown, kTab) && !li::RefuseEvent(kOld, locked, li::kKeyDown, kEnter) &&
            !li::RefuseEvent(kOld, locked, li::kKeyDown, kLeft) && !li::RefuseEvent(kOld, locked, li::kKeyDown, kShift));
  check("a letter's key UP: passes (releasing a key is never refused -- no stuck key)", !li::RefuseEvent(kOld, locked, li::kKeyUp, kA));
  check("pointer and wheel (kinds 1-4): pass", !li::RefuseEvent(kOld, locked, 1, 0) && !li::RefuseEvent(kOld, locked, 2, 1) &&
                                                   !li::RefuseEvent(kOld, locked, 3, 1) && !li::RefuseEvent(kOld, locked, 4, 0));
  check("text: refused", li::RefuseText(kOld, locked));
  check("host-IME physical letter down: refused; its up passes; Enter passes",
        li::RefusePhysicalKey(kOld, locked, true, 'A') && !li::RefusePhysicalKey(kOld, locked, false, 'A') &&
            !li::RefusePhysicalKey(kOld, locked, true, static_cast<uint16_t>(kEnter)));

  std::printf("\n[controls: not at the lock screen nothing is refused]\n");
  check("letter down, text, physical letter on an unlocked session: all pass",
        !li::RefuseEvent(kOld, false, li::kKeyDown, kA) && !li::RefuseText(kOld, false) && !li::RefusePhysicalKey(kOld, false, true, 'A'));

  std::printf("\n[lk-abcd D: the product policy (user decision 2026-11-03) refuses nothing at the lock screen]\n");
  check("the product policy is 'accept' (kRefuse is false)", !li::kRefuse);
  check("at a locked session under the product policy: letter, digit, space, backspace and Ctrl downs pass",
        !li::RefuseEvent(li::kRefuse, locked, li::kKeyDown, kA) && !li::RefuseEvent(li::kRefuse, locked, li::kKeyDown, kDigit1) &&
            !li::RefuseEvent(li::kRefuse, locked, li::kKeyDown, kSpace) &&
            !li::RefuseEvent(li::kRefuse, locked, li::kKeyDown, kBack) && !li::RefuseEvent(li::kRefuse, locked, li::kKeyDown, kCtrl));
  check("...text passes", !li::RefuseText(li::kRefuse, locked));
  check("...a host-IME physical letter passes", !li::RefusePhysicalKey(li::kRefuse, locked, true, 'A'));
  check("...and unknown-off-Default (treated as locked) passes too",
        !li::RefuseText(li::kRefuse, li::TreatAsLocked(false, false, false)));

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
