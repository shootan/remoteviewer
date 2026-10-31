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
}  // namespace

int main() {
  std::printf("plaintext to a locked session (lockscreen-cred r1, A6)\n\n[the session state]\n");
  check("known locked (whatever the desktop): at the lock screen", li::TreatAsLocked(true, true, false) && li::TreatAsLocked(true, true, true));
  check("known NOT locked on a secure desktop (a UAC prompt): not the lock screen -- its typing stays",
        !li::TreatAsLocked(true, false, false));
  check("known not locked on Default: not the lock screen", !li::TreatAsLocked(true, false, true));
  check("unknown off the Default desktop: treated as the lock screen (unknown is not 'unlocked')", li::TreatAsLocked(false, false, false));
  check("unknown on the Default desktop: not the lock screen", !li::TreatAsLocked(false, false, true));

  std::printf("\n[what is a character there]\n");
  const bool locked = true;
  check("letter down: refused", li::RefuseEvent(locked, li::kKeyDown, kA));
  check("digit down: refused", li::RefuseEvent(locked, li::kKeyDown, kDigit1));
  check("space down: refused", li::RefuseEvent(locked, li::kKeyDown, kSpace));
  check("backspace down: refused (not on the navigation list)", li::RefuseEvent(locked, li::kKeyDown, kBack));
  check("Ctrl down: refused (chords are not navigation)", li::RefuseEvent(locked, li::kKeyDown, kCtrl));
  check("a key code beyond 16 bits: refused", li::RefuseEvent(locked, li::kKeyDown, 0x10041));
  check("Tab / Enter / Left / Shift down: pass (navigation, and Enter submits)",
        !li::RefuseEvent(locked, li::kKeyDown, kTab) && !li::RefuseEvent(locked, li::kKeyDown, kEnter) &&
            !li::RefuseEvent(locked, li::kKeyDown, kLeft) && !li::RefuseEvent(locked, li::kKeyDown, kShift));
  check("a letter's key UP: passes (releasing a key is never refused -- no stuck key)", !li::RefuseEvent(locked, li::kKeyUp, kA));
  check("pointer and wheel (kinds 1-4): pass", !li::RefuseEvent(locked, 1, 0) && !li::RefuseEvent(locked, 2, 1) &&
                                                   !li::RefuseEvent(locked, 3, 1) && !li::RefuseEvent(locked, 4, 0));
  check("text: refused", li::RefuseText(locked));
  check("host-IME physical letter down: refused; its up passes; Enter passes",
        li::RefusePhysicalKey(locked, true, 'A') && !li::RefusePhysicalKey(locked, false, 'A') &&
            !li::RefusePhysicalKey(locked, true, static_cast<uint16_t>(kEnter)));

  std::printf("\n[controls: not at the lock screen nothing is refused]\n");
  check("letter down, text, physical letter on an unlocked session: all pass",
        !li::RefuseEvent(false, li::kKeyDown, kA) && !li::RefuseText(false) && !li::RefusePhysicalKey(false, true, 'A'));

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
