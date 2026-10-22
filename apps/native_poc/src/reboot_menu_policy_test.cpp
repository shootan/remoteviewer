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

  std::printf("[lockscreen-cred r1: one 'Windows 로그인' for both screens]\n");
  {
    const auto lock = DecidePreloginCred(ViewerPlatform::Pc, kCaptureFlagLockCredV1 | kCaptureFlagSecureDesktopActive);
    check(lock.sealedEntryAvailable && lock.role == LogonScreenRole::LockScreen,
          "lock screen (0x40000) -> PC entry offered, role LockScreen");
    const auto pre = DecidePreloginCred(ViewerPlatform::Pc, kCaptureFlagPreloginV1);
    check(pre.sealedEntryAvailable && pre.role == LogonScreenRole::Prelogin, "pre-login (0x2000) -> offered, role Prelogin");
    const auto uac = DecidePreloginCred(ViewerPlatform::Pc, kCaptureFlagSecureDesktopActive | kCaptureFlagUnlockSealedV1);
    check(!uac.sealedEntryAvailable && uac.role == LogonScreenRole::None,
          "NEGATIVE: a UAC prompt alone (secure desktop, sealed-unlock cap) -> no entry");
    const auto none = DecidePreloginCred(ViewerPlatform::Pc, 0);
    check(!none.sealedEntryAvailable && none.role == LogonScreenRole::None, "NEGATIVE: logged-in desktop -> no entry");
    const auto android = DecidePreloginCred(ViewerPlatform::Android, kCaptureFlagLockCredV1);
    check(!android.sealedEntryAvailable && android.showAndroidUpdateNote,
          "Android at the lock screen -> no sealed entry, the update note only (APK unchanged)");
  }

  if (gFailures != 0) {
    std::printf("reboot_menu_policy_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("reboot_menu_policy_test: PASS\n");
  return 0;
}
