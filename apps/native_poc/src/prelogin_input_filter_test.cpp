// Tests for the prelogin plaintext-input filter (prelogin_input_filter.hpp). The negative controls are
// the point (Codex review 3.1): no character/text/chord key may be injected as cleartext before login.

#include "prelogin_input_filter.hpp"

#include <cstdio>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
void expect(uint16_t vk, bool want, const char* name) {
  const bool got = IsPreloginNavKeyAllowed(vk);
  if (got == want) {
    std::printf("  ok   %-26s %s\n", name, want ? "ALLOW" : "DENY");
  } else {
    std::printf("  FAIL %-26s expected %s got %s\n", name, want ? "ALLOW" : "DENY", got ? "ALLOW" : "DENY");
    ++gFailures;
  }
}
}  // namespace

int main() {
  std::printf("prelogin plaintext-input filter (navigation only)\n");

  std::printf("[allowed: navigation / selection]\n");
  expect(kVkTab, true, "Tab");
  expect(kVkShift, true, "Shift (Shift+Tab)");
  expect(kVkReturn, true, "Enter");
  expect(kVkEscape, true, "Esc");
  expect(kVkLeft, true, "Left");
  expect(kVkRight, true, "Right");
  expect(kVkUp, true, "Up");
  expect(kVkDown, true, "Down");
  expect(kVkHome, true, "Home");
  expect(kVkEnd, true, "End");
  expect(kVkPrior, true, "PageUp");
  expect(kVkNext, true, "PageDown");

  std::printf("[NEGATIVE: no cleartext characters / chords before login]\n");
  expect(0x41, false, "'A' letter");
  expect(0x5A, false, "'Z' letter");
  expect(0x30, false, "'0' digit");
  expect(0x39, false, "'9' digit");
  expect(0x20, false, "Space");
  expect(0x08, false, "Backspace");
  expect(0x2E, false, "Delete");
  expect(0xBA, false, "';' OEM symbol");
  expect(0x11, false, "Ctrl (chord)");
  expect(0x12, false, "Alt (chord)");
  expect(0x5B, false, "Left Windows key");
  expect(0x70, false, "F1");
  expect(0x15, false, "IME Hangul/Kana");
  expect(0xE5, false, "IME PROCESSKEY");

  if (gFailures != 0) {
    std::printf("prelogin_input_filter_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_input_filter_test: PASS\n");
  return 0;
}
