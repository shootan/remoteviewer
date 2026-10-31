import io

BS = chr(92)

# ---------------------------------------------------------------- a parser for the option
p = 'apps/native_poc/src/updater_options.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''/**
 * The path the copy runs from, given the work directory and this process's image.'''
new = '''/**
 * Splits `HKLM@@Some@@Sub@@Key` into the hive and the rest.
 *
 * Exists because `--registry-root` was required, documented, and ignored: the assembly wrote
 * HKEY_LOCAL_MACHINE and a fixed subkey directly, so the option named something it did not
 * control. A required argument that changes nothing is worse than an absent one -- it reads like
 * a guarantee.
 *
 * `hive` receives HKEY_LOCAL_MACHINE or HKEY_CURRENT_USER as a void* (the header does not want
 * windows.h). Returns false for anything else, rather than guessing at a hive.
 */
bool split_registry_root(const std::wstring& text, void** hive, std::wstring* subkey);

/**
 * The path the copy runs from, given the work directory and this process's image.'''.replace('@@', BS * 2)
assert old in d, 'options header'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/updater_options.cpp'
d = io.open(p, encoding='utf-8').read()
d = d.replace('#include <cwchar>', '#include <windows.h>\n\n#include <cwchar>', 1)
old = '''std::wstring updater_copy_path('''
new = '''bool split_registry_root(const std::wstring& text, void** hive, std::wstring* subkey) {
  const size_t slash = text.find_first_of(L"@@/");
  if (slash == std::wstring::npos || slash == 0) return false;
  const std::wstring prefix = text.substr(0, slash);
  std::wstring rest = text.substr(slash + 1);
  if (rest.empty()) return false;

  // Only the two hives this product ever registers under. Anything else is refused rather than
  // mapped to a default -- an updater writing to a hive nobody asked for is the failure this
  // whole argument exists to prevent.
  if (_wcsicmp(prefix.c_str(), L"HKLM") == 0 ||
      _wcsicmp(prefix.c_str(), L"HKEY_LOCAL_MACHINE") == 0) {
    if (hive) *hive = HKEY_LOCAL_MACHINE;
  } else if (_wcsicmp(prefix.c_str(), L"HKCU") == 0 ||
             _wcsicmp(prefix.c_str(), L"HKEY_CURRENT_USER") == 0) {
    if (hive) *hive = HKEY_CURRENT_USER;
  } else {
    return false;
  }
  if (subkey) *subkey = rest;
  return true;
}

std::wstring updater_copy_path('''.replace('@@', BS * 2)
assert old in d, 'options impl'
d = d.replace(old, new, 1)

# validate() rejects a root it cannot read
old = '''  if (manifestUrl.rfind("https://", 0) != 0) return fail("--manifest-url must be https");'''
new = '''  if (manifestUrl.rfind("https://", 0) != 0) return fail("--manifest-url must be https");

  // Checked here so a root this build cannot use fails at the entry point rather than at the
  // registration stage, half way through an update.
  if (!split_registry_root(registryRoot, nullptr, nullptr)) {
    return fail("--registry-root must be HKLM\\\\... or HKCU\\\\...");
  }'''
assert old in d, 'validate'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the assembly uses it
p = 'apps/native_poc/src/updater_effects.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  target.uninstallRoot = HKEY_LOCAL_MACHINE;
  target.uninstallSubkey = L"SOFTWARE@@Microsoft@@Windows@@CurrentVersion@@Uninstall@@GNLink";'''.replace('@@', BS * 2)
new = '''  // From the option, not hardcoded. It was hardcoded, which made --registry-root a required
  // argument that named something it did not control.
  {
    void* hive = nullptr;
    std::wstring subkey;
    if (!split_registry_root(options_.registryRoot, &hive, &subkey)) {
      if (detail) *detail = "--registry-root is not a hive this build can use";
      return false;
    }
    target.uninstallRoot = static_cast<HKEY>(hive);
    target.uninstallSubkey = subkey;
  }'''
assert old in d, 'registration target'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('--registry-root now controls the registration')
