import io

p = 'apps/native_poc/installer/installer_main.cpp'
d = io.open(p, encoding='utf-8').read()

# 1) include the shared unit
old = '#include "version_compare.hpp"'
new = old + '\n#include "install_registration.hpp"'
assert old in d, 'include anchor'
d = d.replace(old, new, 1)

# 2) replace the four inline registration calls with the shared unit
old = """  const std::wstring serviceExe = directory + L"\\\\GNLinkInputService.exe";
  const int serviceResult = run_and_wait(serviceExe, L"--install-service");
  if (serviceResult != 0) {
    report(L"Installed, but registering the secure input service failed (code " +
               std::to_wstring(serviceResult) +
               L").\\nInput to elevated windows and the lock screen will not work.",
           true);
  }

  add_firewall_rules(directory);
  // Two entries, because the machine can play either part and the names have to say which is
  // which -- "GNLink" alone would leave the user guessing which one they are opening.
  (void)create_start_menu_shortcut(directory + L"\\\\GNLinkHost.exe", kProductName,
                                   L"GNLink remote desktop host", nullptr);
  (void)create_start_menu_shortcut(directory + L"\\\\GNLinkClient.exe", kClientShortcutName,
                                   L"GNLink — connect to another PC", nullptr);
  write_uninstall_entry(directory, setupPath);"""

new = """  // Service, firewall, shortcuts and the uninstall key now go through the shared registration
  // unit, which the updater also uses. The version is passed in rather than read from a constant
  // inside the registering binary -- that is the whole reason the unit is shared, since after an
  // update the GNLinkSetup.exe on disk is still the previous build (it is not in kPayload) and
  // registering through it would write the old version into DisplayVersion.
  install::RegistrationTarget registration;
  registration.installDir = directory;
  registration.setupPath = setupPath;
  registration.version = kProductVersion;
  registration.productName = kProductName;
  registration.clientShortcutName = kClientShortcutName;
  registration.publisher = L"GNLink";
  registration.serviceName = kServiceName;
  registration.firewallRuleName = kFirewallRuleName;
  registration.uninstallRoot = HKEY_LOCAL_MACHINE;
  registration.uninstallSubkey = kUninstallKey;
  registration.hostExeName = L"GNLinkHost.exe";
  registration.clientExeName = L"GNLinkClient.exe";
  registration.serviceExeName = L"GNLinkInputService.exe";
  registration.streamExeName = L"GNLinkStream.exe";

  install::RegistrationOps ops;
  ops.runProcess = [](const std::wstring& exe, const std::wstring& args) {
    return run_and_wait(exe, args);
  };
  ops.createShortcut = [](const std::wstring& target, const std::wstring& linkName,
                          const std::wstring& description) {
    return create_start_menu_shortcut(target, linkName.c_str(), description.c_str(), nullptr);
  };

  const install::RegistrationResult registered = install::register_install(registration, ops);
  if (!registered.ok) {
    // Same user-visible behaviour as before -- the install is still reported as done and the
    // failure is named -- but now the step is named too, instead of only the service being
    // singled out.
    report(L"Installed, but registration did not finish (" +
               std::wstring(registered.failedAt
                                ? widen_ascii(install::step_name(*registered.failedAt))
                                : L"unknown step") +
               L").\\nSome features may not work until this is repaired.",
           true);
  }"""
assert old in d, 'registration block anchor'
d = d.replace(old, new, 1)

# 3) create_start_menu_shortcut currently takes const wchar_t*; make the first parameter a
#    std::wstring so the lambda above can pass one, and add the tiny ascii widener.
old = """bool create_start_menu_shortcut(const std::wstring& target, const wchar_t* linkName,
                                const wchar_t* description, std::wstring* outPath) {"""
new = """/** For rendering an ASCII step name into a message. Not a general converter. */
std::wstring widen_ascii(const char* text) {
  std::wstring out;
  for (const char* p = text; p && *p; ++p) out.push_back(static_cast<wchar_t>(*p));
  return out;
}

bool create_start_menu_shortcut(const std::wstring& target, const wchar_t* linkName,
                                const wchar_t* description, std::wstring* outPath) {"""
assert old in d, 'shortcut anchor'
d = d.replace(old, new, 1)

# 4) the now-unused inline helpers stay for the uninstall path; only write_uninstall_entry and
#    add_firewall_rules become unused on the install path. add_firewall_rules is still used by
#    nothing else, so leave it out to avoid an unused-function warning.
old = """void write_uninstall_entry(const std::wstring& directory, const std::wstring& setupPath) {"""
new = """// Superseded by install::register_install on the install path; kept only if some other caller
// needs it. Marked [[maybe_unused]] so removing the last caller does not become a build break.
[[maybe_unused]] void write_uninstall_entry(const std::wstring& directory,
                                            const std::wstring& setupPath) {"""
assert old in d, 'uninstall entry anchor'
d = d.replace(old, new, 1)

old = """void add_firewall_rules(const std::wstring& directory) {"""
new = """[[maybe_unused]] void add_firewall_rules(const std::wstring& directory) {"""
assert old in d, 'firewall anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('installer rewired')
