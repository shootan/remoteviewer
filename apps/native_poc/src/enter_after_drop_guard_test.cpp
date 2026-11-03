// lk-abcd A: "the characters were dropped, the Enter after them was not" (enter_after_drop_guard.hpp).
//
// The 2026-10-31 sequence, replayed against the guard: the phone sends Enter (wake), the password as text,
// Enter (submit). With the text dropped the submit Enter must not be delivered -- nor its up -- while the same
// sequence with the text delivered goes through whole. Plus the edges: the window ends, a delivered character
// ends it, a lost up does not leave the next Enter stuck, and the template on an outbound action.

#include <cstdio>

#include "enter_after_drop_guard.hpp"

namespace ig = remote60::native_poc::input_guard;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
}

// A stand-in for ControlOutboundAction: the same member names the template reads.
enum class Kind { InputEvent, InputText, PhysicalKey, Ping };
struct Action {
  Kind kind = Kind::Ping;
  struct { uint16_t kind = 0; uint32_t keyCode = 0; } inputEvent;
  struct { uint8_t down = 0; uint16_t vk = 0; } physicalKey;
};
}  // namespace

int main() {
  std::printf("the Enter after a dropped character (lk-abcd A)\n\n[the 2026-10-31 unlock sequence]\n");
  {
    ig::EnterAfterDropGuard g;
    // Enter (wake): nothing dropped yet -- delivered with its up.
    const bool wakeDown = !g.HoldEnterDown(1000);
    const bool wakeUp = !g.HoldEnterUp();
    g.OnCharacterDropped(1100);  // the password text, refused
    const bool submitDown = g.HoldEnterDown(1200);
    const bool submitUp = g.HoldEnterUp();
    check("wake Enter (before any drop): down and up delivered", wakeDown && wakeUp);
    check("submit Enter 100 ms after the text was dropped: down held back", submitDown);
    check("...and its up held back (no up for a down that never went)", submitUp);
    check("counted once", g.HeldTotal() == 1);
  }
  {
    ig::EnterAfterDropGuard g;
    (void)g.HoldEnterDown(1000);
    (void)g.HoldEnterUp();
    g.OnCharacterDelivered();  // the text went
    check("the same sequence with the text delivered: the submit Enter goes, down and up",
          !g.HoldEnterDown(1200) && !g.HoldEnterUp());
  }

  std::printf("\n[the window]\n");
  {
    ig::EnterAfterDropGuard g;
    g.OnCharacterDropped(5000);
    check("Enter exactly at the window's end (2000 ms): held", g.HoldEnterDown(5000 + ig::EnterAfterDropGuard::kWindowMs));
    (void)g.HoldEnterUp();
  }
  {
    ig::EnterAfterDropGuard g;
    g.OnCharacterDropped(5000);
    check("Enter after the window (2001 ms): delivered -- the user's own later Enter",
          !g.HoldEnterDown(5000 + ig::EnterAfterDropGuard::kWindowMs + 1));
    check("...and once past, a second Enter is delivered too (the window is over, not reopened)", !g.HoldEnterDown(5001));
  }
  {
    ig::EnterAfterDropGuard g;
    g.OnCharacterDropped(5000);
    check("every Enter inside the window is held (a phone's retry burst)", g.HoldEnterDown(5100) && g.HoldEnterDown(5300));
  }
  {
    ig::EnterAfterDropGuard g;
    g.OnCharacterDropped(5000);
    g.OnCharacterDelivered();
    check("a character delivered after the drop ends the window: the next Enter goes", !g.HoldEnterDown(5100));
  }
  {
    ig::EnterAfterDropGuard g;
    g.OnCharacterDropped(5000);
    check("a clock that went backwards is not inside the window", !g.HoldEnterDown(4000));
  }
  {
    ig::EnterAfterDropGuard g;
    check("nothing dropped: an up is not held", !g.HoldEnterUp());
  }

  std::printf("\n[a lost up never leaves the next Enter stuck]\n");
  {
    ig::EnterAfterDropGuard g;
    g.OnCharacterDropped(1000);
    (void)g.HoldEnterDown(1100);  // held; its up is lost on the way
    const bool nextDownGoes = !g.HoldEnterDown(9000);
    const bool nextUpGoes = !g.HoldEnterUp();
    check("the next Enter (window over): its down goes AND its up goes (the stale hold is gone)", nextDownGoes && nextUpGoes);
  }

  std::printf("\n[what counts as a character]\n");
  check("a letter down is a character", ig::IsCharacterDown(5, 'A'));
  check("a letter up is not", !ig::IsCharacterDown(6, 'A'));
  check("Enter, Tab, Shift, an arrow are not", !ig::IsCharacterDown(5, 0x0D) && !ig::IsCharacterDown(5, 0x09) &&
                                                   !ig::IsCharacterDown(5, 0x10) && !ig::IsCharacterDown(5, 0x25));
  check("Backspace and a key code beyond 16 bits are", ig::IsCharacterDown(5, 0x08) && ig::IsCharacterDown(5, 0x10041));
  check("pointer kinds are not", !ig::IsCharacterDown(1, 0) && !ig::IsCharacterDown(2, 1));
  Action text;
  text.kind = Kind::InputText;
  Action key;
  key.kind = Kind::InputEvent;
  key.inputEvent.kind = 5;
  key.inputEvent.keyCode = 'Q';
  Action enter = key;
  enter.inputEvent.keyCode = 0x0D;
  Action phys;
  phys.kind = Kind::PhysicalKey;
  phys.physicalKey.down = 1;
  phys.physicalKey.vk = 'Q';
  Action physUp = phys;
  physUp.physicalKey.down = 0;
  Action ping;
  check("on an action: text, a letter down and a physical letter down are characters",
        ig::ActionIsCharacter(text) && ig::ActionIsCharacter(key) && ig::ActionIsCharacter(phys));
  check("on an action: Enter, a physical up and a ping are not",
        !ig::ActionIsCharacter(enter) && !ig::ActionIsCharacter(physUp) && !ig::ActionIsCharacter(ping));

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
