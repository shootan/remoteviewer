// Registration, under a scratch registry key and with every effectful operation recorded rather
// than performed.
//
// Nothing here touches the real uninstall key, the real GNLinkSecureInput service, the real
// firewall or the real Start menu. The registry root is HKCU under a per-process subkey; netsh
// and the service call go through a recording stand-in; shortcuts are recorded, not created.
//
// The assertion that matters most is that the version written is the version PASSED, not a
// constant. That is the entire reason this unit was extracted: after an update the GNLinkSetup.exe
// on disk is still the previous build, so registering through it would have written the old
// version into the key every later "is this newer" decision reads.

#include "install_registration.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace remote60::native_poc::install;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::string narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }

std::wstring scratch_subkey() {
  return L"Software\\GNLinkRegistrationTest\\" + std::to_wstring(GetCurrentProcessId());
}

std::wstring read_value(const std::wstring& subkey, const wchar_t* name) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return L"<no key>";
  }
  wchar_t buffer[1024]{};
  DWORD bytes = sizeof(buffer);
  DWORD type = 0;
  const LSTATUS status =
      RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &bytes);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS) return L"<no value>";
  return buffer;
}

bool key_exists(const std::wstring& subkey) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return false;
  }
  RegCloseKey(key);
  return true;
}

/** Records what would have been run or created. Nothing is actually performed. */
struct Recorder {
  std::vector<std::wstring> ran;
  std::vector<std::wstring> shortcuts;
  int processResult = 0;
  bool shortcutResult = true;
  // Makes a specific step fail, to check the reporting.
  std::wstring failProcessContaining;

  RegistrationOps ops() {
    return RegistrationOps{
        [this](const std::wstring& exe, const std::wstring& args) {
          ran.push_back(exe + L" " + args);
          if (!failProcessContaining.empty() &&
              (exe.find(failProcessContaining) != std::wstring::npos ||
               args.find(failProcessContaining) != std::wstring::npos)) {
            return 1;
          }
          return processResult;
        },
        [this](const std::wstring& target, const std::wstring& link, const std::wstring&,
               std::string* detail) {
          shortcuts.push_back(link + L" -> " + target);
          // A stub that fails still has to say something, or the caller's detail-plumbing is
          // never exercised by anything.
          if (!shortcutResult && detail) *detail = "stub refused";
          return shortcutResult;
        },
    };
  }

  bool ran_containing(const std::wstring& needle) const {
    for (const auto& r : ran) if (r.find(needle) != std::wstring::npos) return true;
    return false;
  }
};

RegistrationTarget base_target(const std::wstring& subkey, const std::wstring& version) {
  RegistrationTarget t;
  t.installDir = L"C:\\ScratchInstall\\GNLink";  // never created or touched -- only recorded
  t.setupPath = t.installDir + L"\\GNLinkSetup.exe";
  t.version = version;
  t.productName = L"GNLink Host (test)";
  t.clientShortcutName = L"GNLink (test)";
  t.publisher = L"GNLink";
  t.serviceName = L"GNLinkRegistrationTestService";   // NOT the real GNLinkSecureInput
  t.firewallRuleName = L"GNLink Registration Test";   // NOT the real rule
  t.uninstallRoot = HKEY_CURRENT_USER;                // NOT HKLM
  t.uninstallSubkey = subkey;
  t.hostExeName = L"GNLinkHost.exe";
  t.clientExeName = L"GNLinkClient.exe";
  t.serviceExeName = L"GNLinkInputService.exe";
  t.streamExeName = L"GNLinkStream.exe";
  return t;
}

}  // namespace

int main() {
  const std::wstring subkey = scratch_subkey();
  RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());

  // Guard the guard, the same way the effects test does.
  check("scratch key is under HKCU, not the real uninstall path",
        subkey.find(L"Microsoft\\Windows\\CurrentVersion\\Uninstall") == std::wstring::npos);

  // ---------------------------------------------------------------- validation

  {
    RegistrationTarget empty;
    std::string why;
    check("an empty target is invalid", !empty.validate(&why), why);

    RegistrationTarget t = base_target(subkey, L"1.2.3");
    check("a complete target is valid", t.validate(&why), why);

    // The field this unit exists for.
    RegistrationTarget noVersion = t;
    noVersion.version.clear();
    check("a target with no version is rejected", !noVersion.validate(&why), why);

    RegistrationTarget noRoot = t;
    noRoot.uninstallRoot = nullptr;
    check("a target with no registry root is rejected", !noRoot.validate(&why), why);
    RegistrationTarget noService = t;
    noService.serviceName.clear();
    check("a target with no service name is rejected", !noService.validate(&why), why);
  }

  {
    Recorder rec;
    RegistrationTarget t = base_target(subkey, L"1.2.3");
    t.version.clear();
    const RegistrationResult r = register_install(t, rec.ops());
    check("registration refuses an invalid target", !r.ok, r.detail);
    check("and does nothing at all", rec.ran.empty() && rec.shortcuts.empty());
    check("no key was created", !key_exists(subkey));
  }

  // ---------------------------------------------------------------- the happy path

  {
    Recorder rec;
    const RegistrationResult r = register_install(base_target(subkey, L"0.2.105"), rec.ops());
    check("registration succeeds", r.ok, r.detail);
    check("all four steps completed", r.completed.size() == 4, std::to_string(r.completed.size()));
    check("in order: service first",
          !r.completed.empty() && r.completed.front() == RegistrationStep::Service);
    check("in order: uninstall entry last",
          !r.completed.empty() && r.completed.back() == RegistrationStep::UninstallEntry);
    check("nothing is reported as failed", !r.failedAt.has_value());

    check("the service was asked to install itself",
          rec.ran_containing(L"--install-service"), narrow(rec.ran.empty() ? L"" : rec.ran[0]));
    check("netsh was asked for an inbound rule",
          rec.ran_containing(L"advfirewall firewall add rule"));
    check("the firewall rule names the stream executable",
          rec.ran_containing(L"GNLinkStream.exe"));
    check("two shortcuts were created", rec.shortcuts.size() == 2,
          std::to_string(rec.shortcuts.size()));

    // THE assertion. The version in the key is the one passed in.
    check("DisplayVersion is the version PASSED, not a constant",
          read_value(subkey, L"DisplayVersion") == L"0.2.105",
          narrow(read_value(subkey, L"DisplayVersion")));
    check("InstallLocation recorded", read_value(subkey, L"InstallLocation") == L"C:\\ScratchInstall\\GNLink");
    check("UninstallString points at the setup path",
          read_value(subkey, L"UninstallString").find(L"GNLinkSetup.exe") != std::wstring::npos,
          narrow(read_value(subkey, L"UninstallString")));
  }

  {
    // Registering again with a different version must move DisplayVersion. This is what an update
    // does, and it is the case re-running the OLD installer binary could not have got right.
    Recorder rec;
    const RegistrationResult r = register_install(base_target(subkey, L"0.3.0"), rec.ops());
    check("re-registering with a newer version succeeds", r.ok, r.detail);
    check("DisplayVersion moved to the new version",
          read_value(subkey, L"DisplayVersion") == L"0.3.0",
          narrow(read_value(subkey, L"DisplayVersion")));
  }

  // ---------------------------------------------------------------- partial failure is reported

  {
    Recorder rec;
    rec.failProcessContaining = L"--install-service";
    RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    const RegistrationResult r = register_install(base_target(subkey, L"9.9.9"), rec.ops());
    check("a failed service step fails the whole registration", !r.ok, r.detail);
    check("and says which step", r.failedAt.has_value() && *r.failedAt == RegistrationStep::Service,
          r.failedAt ? step_name(*r.failedAt) : "none");
    check("nothing after it was attempted", !rec.ran_containing(L"advfirewall"));
    check("no shortcuts were created", rec.shortcuts.empty());
    check("and no uninstall key was written", !key_exists(subkey),
          narrow(read_value(subkey, L"DisplayVersion")));
  }

  {
    Recorder rec;
    rec.failProcessContaining = L"advfirewall";
    RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    const RegistrationResult r = register_install(base_target(subkey, L"9.9.9"), rec.ops());
    check("a failed firewall step is reported as such", !r.ok && r.failedAt &&
                                                            *r.failedAt == RegistrationStep::Firewall,
          r.failedAt ? step_name(*r.failedAt) : "none");
    // The partial state is visible rather than hidden: the service DID register.
    check("the completed steps are still reported",
          r.completed_step(RegistrationStep::Service), std::to_string(r.completed.size()));
    check("no uninstall key was written", !key_exists(subkey));
  }

  {
    Recorder rec;
    rec.shortcutResult = false;
    RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    const RegistrationResult r = register_install(base_target(subkey, L"9.9.9"), rec.ops());
    check("a failed shortcut step is reported as such",
          !r.ok && r.failedAt && *r.failedAt == RegistrationStep::Shortcuts,
          r.failedAt ? step_name(*r.failedAt) : "none");
    check("service and firewall are reported as completed",
          r.completed_step(RegistrationStep::Service) && r.completed_step(RegistrationStep::Firewall));
  }

  // ---------------------------------------------------------------- snapshot and restore

  {
    RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    RegistrationSnapshot before;
    check("capturing an absent registration succeeds",
          capture_registration(HKEY_CURRENT_USER, subkey, &before));
    check("and records that nothing was there", !before.present);

    Recorder rec;
    check("register a first version", register_install(base_target(subkey, L"0.2.104"), rec.ops()).ok);

    RegistrationSnapshot old;
    check("capturing an existing registration succeeds",
          capture_registration(HKEY_CURRENT_USER, subkey, &old));
    check("it is marked present", old.present);
    check("it holds the OLD version", old.displayVersion == L"0.2.104", narrow(old.displayVersion));

    // Now an update registers the new version...
    check("register the new version", register_install(base_target(subkey, L"0.2.105"), rec.ops()).ok);
    check("the key now says the new version", read_value(subkey, L"DisplayVersion") == L"0.2.105");

    // ...and then rolls back. The registry must go back to the OLD version, because the files
    // just went back to the old build. Restoring the new version here would leave the registry
    // claiming something the disk does not support -- the same poisoning of the reference point
    // that ruled out re-running the old installer.
    check("restoring the captured snapshot succeeds",
          restore_registration(HKEY_CURRENT_USER, subkey, old));
    check("DisplayVersion is the PREVIOUS version again, not the new one",
          read_value(subkey, L"DisplayVersion") == L"0.2.104",
          narrow(read_value(subkey, L"DisplayVersion")));

    // And a rollback of a FIRST install removes the key rather than writing blanks.
    check("restoring an absent snapshot removes the key",
          restore_registration(HKEY_CURRENT_USER, subkey, before));
    check("the key is gone", !key_exists(subkey));
  }

  RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\GNLinkRegistrationTest");

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
