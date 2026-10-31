# -*- coding: utf-8 -*-
"""The startup entry was still called remote60.

Task Manager's Startup tab shows the registry value's NAME, so this is a user-visible string --
the last one carrying the old project name. Renaming it alone would be worse than leaving it: the
old entry would keep launching the program at boot while `autostart_enabled()` reported false and
a later "off" removed only the new name.

So both names are handled. Reading accepts either, enabling writes the new one and removes the
old, disabling removes both. Nothing is written to the registry until the user toggles the
setting; this changes what happens then, not the machine now.
"""
import io

p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''const wchar_t kRunKey[] = L"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run";
const wchar_t kRunValue[] = L"remote60";

bool autostart_enabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
  wchar_t value[MAX_PATH * 2] = {};
  DWORD size = sizeof(value);
  const bool present =
      RegQueryValueExW(key, kRunValue, nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size) ==
      ERROR_SUCCESS;
  RegCloseKey(key);
  return present && value[0] != L'\\0';
}'''

new = '''const wchar_t kRunKey[] = L"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run";
/**
 * The name Task Manager's Startup tab shows, which is why it matters what it says.
 *
 * `kLegacyRunValue` is what every machine installed before this has. Both are read and both are
 * removed on the way out: renaming without that would leave the old entry launching the program
 * at boot while the app believed autostart was off.
 */
const wchar_t kRunValue[] = L"GNLink";
const wchar_t kLegacyRunValue[] = L"remote60";

bool run_value_present(HKEY key, const wchar_t* name) {
  wchar_t value[MAX_PATH * 2] = {};
  DWORD size = sizeof(value);
  return RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(value), &size) ==
             ERROR_SUCCESS &&
         value[0] != L'\\0';
}

bool autostart_enabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
  // Either name counts. A machine that was set up before the rename is still set to start.
  const bool present =
      run_value_present(key, kRunValue) || run_value_present(key, kLegacyRunValue);
  RegCloseKey(key);
  return present;
}'''
assert d.count(old) == 1, 'read anchor'
d = d.replace(old, new, 1)

old = '''    RegSetValueExW(key, kRunValue, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(command.c_str()),
                   static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, kRunValue);
  }'''
new = '''    RegSetValueExW(key, kRunValue, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(command.c_str()),
                   static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    // Migrate rather than accumulate: leaving the old name behind would start the program twice.
    RegDeleteValueW(key, kLegacyRunValue);
  } else {
    RegDeleteValueW(key, kRunValue);
    // Off has to mean off even on a machine that only ever had the old name.
    RegDeleteValueW(key, kLegacyRunValue);
  }'''
assert d.count(old) == 1, 'write anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
