#pragma once

// Plaintext characters to a LOCKED session (lockscreen-cred r1, Codex A6). Pure; no windows.h.
//
// The user's decision: a password reaches the lock screen only through the sealed "Windows 로그인" path --
// never as plaintext keys. A new PC viewer holds the characters back itself (prelogin_input_gate.hpp), but
// that is the viewer's courtesy, not the boundary: an older viewer, the Android app (APK 0.2.30, unchanged),
// a key queued before the lock, or a viewer that is simply wrong about the role would still send them. So the
// host refuses them where it injects, and the service refuses them again before its agent types on Winlogon.
//
// What is refused: a CHARACTER -- a key DOWN that is not a navigation key (prelogin_input_filter.hpp's list:
// Tab, Enter, Esc, Shift, the arrows, Home/End/PageUp/PageDown), and any text (typed, IME-composed, pasted).
// What passes: pointer and wheel; navigation keys; every key UP (releasing a key that went down before the
// lock is cleanup, not input -- refusing it would leave the key stuck).
//
// When: the session is KNOWN to be locked; or its lock state is unknown while the input desktop is not the
// user's own ("Default") -- an unknown state is not taken as "unlocked" (Codex contract 1). A UAC prompt on a
// session that is known NOT to be locked is unaffected: it is a secure desktop too, and typing there stays as
// it was. Never decided on "secure desktop" alone.

#include <cstdint>

#include "prelogin_input_filter.hpp"

namespace remote60::native_poc::lock_input {

constexpr uint16_t kKeyDown = 5;  // ControlInputEvent / SecureInputMessage eventKind
constexpr uint16_t kKeyUp = 6;

// The session is (or must be treated as) at its lock screen.
inline bool TreatAsLocked(bool lockKnown, bool locked, bool inputDesktopIsDefault) {
  if (lockKnown) return locked;
  return !inputDesktopIsDefault;
}

// A key event is a character for this purpose (a non-navigation key going down).
inline bool IsCharacterKey(uint16_t eventKind, uint32_t keyCode) {
  if (eventKind != kKeyDown) return false;
  return keyCode > 0xFFFF || !prelogin::IsPreloginNavKeyAllowed(static_cast<uint16_t>(keyCode));
}

// One input event (pointer, wheel or key) to a session in this state: refused?
inline bool RefuseEvent(bool treatAsLocked, uint16_t eventKind, uint32_t keyCode) {
  return treatAsLocked && IsCharacterKey(eventKind, keyCode);
}

// Text (InputText) to a session in this state: refused?
inline bool RefuseText(bool treatAsLocked) { return treatAsLocked; }

// A host-IME physical key (by its virtual key) to a session in this state: refused? A key up passes.
inline bool RefusePhysicalKey(bool treatAsLocked, bool down, uint16_t vk) {
  return treatAsLocked && down && !prelogin::IsPreloginNavKeyAllowed(vk);
}

}  // namespace remote60::native_poc::lock_input
