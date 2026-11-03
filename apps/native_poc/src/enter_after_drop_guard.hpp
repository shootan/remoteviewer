#pragma once

// "The characters were dropped, the Enter after them was not" (lk-abcd A). Pure; no windows.h.
//
// 2026-10-31: the phone's unlock sequence (Enter, the password as text, Enter) reached a locked PC as two
// Enters and nothing between them -- the text was refused at the lock screen (lockscreen-cred A6) while Enter,
// a navigation key, passed. Each try submitted an empty password field, and the account was locked out.
//
// Whatever rule drops a character (a lock/logon gate, an old host's allowlist, a secure-desktop physical-key
// drop, an injection that failed), the Enter that follows it in the same burst must not be delivered on its
// own: it would submit whatever the field holds without the characters the user meant. So every place that
// drops a character tells this guard, and asks it before it delivers an Enter:
//   * an Enter DOWN within kWindowMs of the latest dropped character is held back (not delivered);
//   * the UP of an Enter whose down was held is held back too (no up for a down that never went);
//   * a character that IS delivered ends the window (the user is typing again; the next Enter is theirs).
// One guard per input stream (a control session, a viewer session); single-threaded by its owner, or
// guarded by the owner's lock.

#include <cstdint>

#include "prelogin_input_filter.hpp"

namespace remote60::native_poc::input_guard {

constexpr uint16_t kVkReturn = 0x0D;

// A character for this purpose: a key DOWN that is not a navigation key (prelogin_input_filter.hpp's list).
inline bool IsCharacterDown(uint16_t eventKind, uint32_t keyCode) {
  return eventKind == 5 && (keyCode > 0xFFFF || !prelogin::IsPreloginNavKeyAllowed(static_cast<uint16_t>(keyCode)));
}

// The same on a queued ControlOutboundAction of the shared client core (a template, so this header stays free
// of it): text, a character key down, or a host-IME physical character down.
template <typename Action>
inline bool ActionIsCharacter(const Action& a) {
  using Kind = decltype(a.kind);
  if (a.kind == Kind::InputText) return true;
  if (a.kind == Kind::InputEvent) return IsCharacterDown(a.inputEvent.kind, a.inputEvent.keyCode);
  if (a.kind == Kind::PhysicalKey) return a.physicalKey.down != 0 && !prelogin::IsPreloginNavKeyAllowed(a.physicalKey.vk);
  return false;
}

class EnterAfterDropGuard {
 public:
  static constexpr uint64_t kWindowMs = 2000;

  // A character (a non-navigation key down, or text) was not delivered.
  void OnCharacterDropped(uint64_t nowMs) {
    armed_ = true;
    droppedAtMs_ = nowMs;
  }

  // A character was delivered: whatever was dropped before it is no longer what an Enter would submit.
  void OnCharacterDelivered() { armed_ = false; }

  // An Enter key down is about to be delivered: hold it back?
  bool HoldEnterDown(uint64_t nowMs) {
    if (!armed_ || nowMs < droppedAtMs_ || nowMs - droppedAtMs_ > kWindowMs) {
      armed_ = false;
      // This down goes -- so the next up is ITS release, whatever an earlier held down's lost up left behind.
      // (Holding it would leave Enter stuck down on the host.)
      heldDown_ = false;
      return false;
    }
    ++held_;
    heldDown_ = true;
    return true;
  }

  // An Enter key up is about to be delivered: hold it back (its down was held)?
  bool HoldEnterUp() {
    if (!heldDown_) return false;
    heldDown_ = false;
    return true;
  }

  uint64_t HeldTotal() const { return held_; }

 private:
  bool armed_ = false;
  bool heldDown_ = false;
  uint64_t droppedAtMs_ = 0;
  uint64_t held_ = 0;
};

}  // namespace remote60::native_poc::input_guard
