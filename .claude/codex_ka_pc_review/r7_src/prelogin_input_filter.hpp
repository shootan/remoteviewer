#pragma once

// Prelogin plaintext-input filter (prelogin-service r4 item 2 / Codex review 3.1). The control allowlist
// (prelogin_control_allowlist.hpp) decides which message TYPES are accepted before login; this is the
// second, payload-level check the Codex review requires: within an accepted ControlInputEvent, only
// NAVIGATION keys may be injected as plaintext. Character/text entry (a letter, digit, symbol, space,
// IME, raw scan) must NOT travel the plaintext key path before login -- credentials come only through
// the sealed "Windows 로그인" path, and Ctrl+Alt+Del is an explicit SAS request, not raw keys.
//
// Pure (virtual-key codes only); no windows.h, so it is unit-testable off a real machine. The VK_*
// numeric values are the stable Win32 constants, mirrored here so the shared/test build needs no headers.

#include <cstdint>

namespace remote60::native_poc::prelogin {

// Win32 VK_* mirrors (stable OS constants).
constexpr uint16_t kVkTab = 0x09;
constexpr uint16_t kVkReturn = 0x0D;
constexpr uint16_t kVkEscape = 0x1B;
constexpr uint16_t kVkShift = 0x10, kVkLShift = 0xA0, kVkRShift = 0xA1;
constexpr uint16_t kVkPrior = 0x21, kVkNext = 0x22;  // PageUp / PageDown
constexpr uint16_t kVkEnd = 0x23, kVkHome = 0x24;
constexpr uint16_t kVkLeft = 0x25, kVkUp = 0x26, kVkRight = 0x27, kVkDown = 0x28;

// Is this virtual key permitted as a PLAINTEXT key event in prelogin? The set is exactly the
// navigation/selection keys a user needs to move around the login screen (Tab incl. Shift+Tab, the
// arrows, Home/End/PageUp/PageDown, Enter to submit, Esc to cancel). Everything else -- letters, digits,
// symbols, Space, Backspace, Delete, Ctrl/Alt (and thus chords), the Windows key, function keys, IME
// keys -- is rejected; those must not enter a credential field as cleartext before login.
inline bool IsPreloginNavKeyAllowed(uint16_t vk) {
  switch (vk) {
    case kVkTab:
    case kVkReturn:
    case kVkEscape:
    case kVkShift:
    case kVkLShift:
    case kVkRShift:
    case kVkPrior:
    case kVkNext:
    case kVkEnd:
    case kVkHome:
    case kVkLeft:
    case kVkUp:
    case kVkRight:
    case kVkDown:
      return true;
    default:
      return false;
  }
}

}  // namespace remote60::native_poc::prelogin
