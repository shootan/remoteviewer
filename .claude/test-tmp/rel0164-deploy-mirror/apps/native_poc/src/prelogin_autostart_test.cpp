// Tests for the prelogin per-user autostart cleanup (prelogin_autostart.hpp). Non-elevated: it uses a
// THROWAWAY HKCU subkey, never the user's real ...\Windows\CurrentVersion\Run, so it cannot disturb a
// real autostart. Pins the gate (only remove after enrollment) and the idempotent, scoped value delete.

#include "prelogin_autostart.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}

const wchar_t* kTestSubkey = L"Software\\GNLinkPreloginTest\\Run";

bool value_exists(const wchar_t* sub, const wchar_t* name) {
  HKEY k = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
  const LSTATUS st = RegQueryValueExW(k, name, nullptr, nullptr, nullptr, nullptr);
  RegCloseKey(k);
  return st == ERROR_SUCCESS;
}
void set_value(const wchar_t* sub, const wchar_t* name, const wchar_t* data) {
  HKEY k = nullptr;
  RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr);
  RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(data),
                 static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t)));
  RegCloseKey(k);
}
}  // namespace

int main() {
  std::printf("prelogin per-user autostart cleanup\n");

  std::printf("[gate: remove only after enrollment is confirmed]\n");
  check(!PreloginServiceOwnsUserStartup(false, false), "feature off -> keep HKCU Run");
  check(!PreloginServiceOwnsUserStartup(true, false), "NEGATIVE: feature on but enrollment NOT confirmed -> keep Run");
  check(!PreloginServiceOwnsUserStartup(false, true), "enrollment confirmed but feature off -> keep Run");
  check(PreloginServiceOwnsUserStartup(true, true), "feature on + enrollment confirmed -> service owns startup (remove Run)");

  std::printf("[scoped idempotent removal in a throwaway subkey]\n");
  const wchar_t* names[] = {L"GNLink", L"remote60"};
  set_value(kTestSubkey, L"GNLink", L"\"C:\\x\\GNLinkHost.exe\" --tray");
  set_value(kTestSubkey, L"remote60", L"\"C:\\old\\remote60_host_app.exe\"");
  set_value(kTestSubkey, L"UnrelatedApp", L"\"C:\\other\\app.exe\"");
  check(value_exists(kTestSubkey, L"GNLink") && value_exists(kTestSubkey, L"remote60"), "seeded the test Run values");

  check(RemoveUserRunValues(HKEY_CURRENT_USER, kTestSubkey, names, 2), "remove the two GNLink Run values -> ok");
  check(!value_exists(kTestSubkey, L"GNLink") && !value_exists(kTestSubkey, L"remote60"),
        "both GNLink values gone");
  check(value_exists(kTestSubkey, L"UnrelatedApp"),
        "NEGATIVE: an unrelated Run value in the same key is untouched (scoped to named values)");

  check(RemoveUserRunValues(HKEY_CURRENT_USER, kTestSubkey, names, 2), "idempotent: removing again -> ok");
  check(RemoveUserRunValues(HKEY_CURRENT_USER, L"Software\\GNLinkPreloginTest\\DoesNotExist", names, 2),
        "a missing Run key -> ok (nothing to remove)");

  // Cleanup the throwaway tree.
  RegDeleteKeyW(HKEY_CURRENT_USER, kTestSubkey);
  RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\GNLinkPreloginTest");

  if (gFailures != 0) {
    std::printf("prelogin_autostart_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_autostart_test: PASS\n");
  return 0;
}
