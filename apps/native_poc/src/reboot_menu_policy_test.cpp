// Tests for the viewer reboot-menu / prelogin-credential policy (reboot_menu_policy.hpp). Pins R1 (the
// reboot warning keys off durable-enabled, not momentary prelogin) and the PC-first credential-entry
// decision, with their negative controls.

#include "reboot_menu_policy.hpp"

#include <cstdio>

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}
}  // namespace

int main() {
  std::printf("viewer reboot-menu / prelogin-credential policy\n");

  std::printf("[reboot menu + R1 warning]\n");
  {
    auto p = DecideRebootMenu(0);  // old host, no capabilities
    check(!p.menuEnabled && p.disabledBecauseOldHost && !p.showPreloginWarning,
          "old host (no reboot cap) -> menu disabled with reason, no warning");

    p = DecideRebootMenu(kCaptureFlagRemoteRebootV1);  // reboot, but prelogin NOT durable-enabled
    check(p.menuEnabled && !p.disabledBecauseOldHost && p.showPreloginWarning,
          "reboot cap, not durable-enabled -> menu on + 'no connect until login' warning");

    p = DecideRebootMenu(kCaptureFlagRemoteRebootV1 | kCaptureFlagPreloginEnabledV1);
    check(p.menuEnabled && !p.showPreloginWarning,
          "R1: reboot cap + DURABLE-enabled -> menu on, NO warning");

    // The momentary 'in prelogin now' bit must NOT drive the warning (a feature-on logged-in host that
    // is not currently in prelogin still advertises durable-enabled and must not be warned).
    p = DecideRebootMenu(kCaptureFlagRemoteRebootV1 | kCaptureFlagPreloginEnabledV1 | kCaptureFlagPreloginV1);
    check(p.menuEnabled && !p.showPreloginWarning,
          "NEGATIVE: in-prelogin-now does not add the warning when durable-enabled is set");

    p = DecideRebootMenu(kCaptureFlagRemoteRebootV1 | kCaptureFlagPreloginV1);  // in prelogin now, but not durable
    check(p.showPreloginWarning,
          "NEGATIVE: momentary prelogin alone (no durable bit) still warns -- the bit that matters is durable");
  }

  std::printf("[prelogin credential entry: PC first, Android note, no plaintext fallback]\n");
  {
    const uint32_t inPrelogin = kCaptureFlagPreloginV1;
    auto pc = DecidePreloginCred(ViewerPlatform::Pc, inPrelogin);
    check(pc.sealedEntryAvailable && !pc.showAndroidUpdateNote, "PC viewer + host in prelogin -> sealed entry available");

    auto andr = DecidePreloginCred(ViewerPlatform::Android, inPrelogin);
    check(!andr.sealedEntryAvailable && andr.showAndroidUpdateNote,
          "NEGATIVE: Android + host in prelogin -> NO sealed entry, show update note (no plaintext fallback)");

    auto pcNo = DecidePreloginCred(ViewerPlatform::Pc, 0);
    check(!pcNo.sealedEntryAvailable && !pcNo.showAndroidUpdateNote,
          "host NOT in prelogin -> nothing to enter (PC)");
    auto andrNo = DecidePreloginCred(ViewerPlatform::Android, 0);
    check(!andrNo.sealedEntryAvailable && !andrNo.showAndroidUpdateNote, "host NOT in prelogin -> no note (Android)");
  }

  if (gFailures != 0) {
    std::printf("reboot_menu_policy_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("reboot_menu_policy_test: PASS\n");
  return 0;
}
