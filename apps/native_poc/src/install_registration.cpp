#include "install_registration.hpp"

#include <algorithm>

namespace remote60::native_poc::install {
namespace {

std::string to_utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<size_t>(need), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), need, nullptr,
                      nullptr);
  return out;
}

bool set_string(HKEY key, const wchar_t* name, const std::wstring& value) {
  return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

std::wstring read_string(HKEY key, const wchar_t* name) {
  wchar_t buffer[1024]{};
  DWORD bytes = sizeof(buffer);
  DWORD type = 0;
  if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &bytes) !=
          ERROR_SUCCESS ||
      type != REG_SZ) {
    return {};
  }
  return buffer;
}

}  // namespace

const char* step_name(RegistrationStep step) {
  switch (step) {
    case RegistrationStep::Service: return "Service";
    case RegistrationStep::Firewall: return "Firewall";
    case RegistrationStep::Shortcuts: return "Shortcuts";
    case RegistrationStep::UninstallEntry: return "UninstallEntry";
  }
  return "?";
}

bool RegistrationResult::completed_step(RegistrationStep step) const {
  return std::find(completed.begin(), completed.end(), step) != completed.end();
}

bool RegistrationTarget::validate(std::string* detail) const {
  const auto fail = [detail](const char* why) {
    if (detail) *detail = why;
    return false;
  };
  if (installDir.empty()) return fail("installDir not set");
  if (setupPath.empty()) return fail("setupPath not set");
  // The reason this unit exists. An empty version would write an empty DisplayVersion, and
  // read_installed_version() would then report "not installed" for a machine that is.
  if (version.empty()) return fail("version not set");
  if (productName.empty()) return fail("productName not set");
  if (clientShortcutName.empty()) return fail("clientShortcutName not set");
  if (publisher.empty()) return fail("publisher not set");
  if (serviceName.empty()) return fail("serviceName not set");
  if (firewallRuleName.empty()) return fail("firewallRuleName not set");
  if (!uninstallRoot) return fail("uninstallRoot not set");
  if (uninstallSubkey.empty()) return fail("uninstallSubkey not set");
  if (hostExeName.empty()) return fail("hostExeName not set");
  if (clientExeName.empty()) return fail("clientExeName not set");
  if (serviceExeName.empty()) return fail("serviceExeName not set");
  if (streamExeName.empty()) return fail("streamExeName not set");
  return true;
}

RegistrationResult register_install(const RegistrationTarget& target, const RegistrationOps& ops) {
  RegistrationResult result;

  std::string why;
  if (!target.validate(&why)) {
    result.detail = "target invalid: " + why;
    return result;
  }
  if (!ops.runProcess || !ops.createShortcut) {
    result.detail = "operations not supplied";
    return result;
  }

  const auto fail = [&result](RegistrationStep step, const std::string& detail) {
    result.failedAt = step;
    result.detail = detail;
    result.ok = false;
    return result;
  };

  // ---- service. Registered against the install directory, so the path recorded in the SCM is
  // the one the files actually live at.
  {
    const std::wstring serviceExe = target.installDir + L"\\" + target.serviceExeName;
    const int code = ops.runProcess(serviceExe, L"--install-service");
    if (code != 0) {
      // The installer today reports overall success after this fails, having only shown a message
      // box. That is not reproduced: a caller that cannot tell a complete registration from a
      // partial one has no basis for deciding whether to roll back.
      return fail(RegistrationStep::Service, "service registration returned " + std::to_string(code));
    }
    result.completed.push_back(RegistrationStep::Service);
  }

  // ---- firewall. Without it the host binds its ports and every inbound datagram is dropped.
  {
    wchar_t system32[MAX_PATH]{};
    if (GetSystemDirectoryW(system32, MAX_PATH) == 0) {
      return fail(RegistrationStep::Firewall, "could not locate the system directory");
    }
    const std::wstring netsh = std::wstring(system32) + L"\\netsh.exe";
    const std::wstring program = target.installDir + L"\\" + target.streamExeName;
    const std::wstring args = L"advfirewall firewall add rule name=\"" + target.firewallRuleName +
                              L"\" dir=in action=allow program=\"" + program +
                              L"\" enable=yes profile=any";
    const int code = ops.runProcess(netsh, args);
    if (code != 0) return fail(RegistrationStep::Firewall, "netsh returned " + std::to_string(code));
    result.completed.push_back(RegistrationStep::Firewall);
  }

  // ---- shortcuts. Two, because this machine can play either part and the names have to say
  // which one is being opened.
  {
    if (!ops.createShortcut(target.installDir + L"\\" + target.hostExeName, target.productName,
                            L"GNLink remote desktop host")) {
      return fail(RegistrationStep::Shortcuts, "could not create the host shortcut");
    }
    if (!ops.createShortcut(target.installDir + L"\\" + target.clientExeName,
                            target.clientShortcutName, L"GNLink - connect to another PC")) {
      return fail(RegistrationStep::Shortcuts, "could not create the client shortcut");
    }
    result.completed.push_back(RegistrationStep::Shortcuts);
  }

  // ---- uninstall entry. DisplayVersion comes from target.version, which is why this unit is
  // shared instead of the updater re-running an old installer binary.
  {
    HKEY key = nullptr;
    if (RegCreateKeyExW(target.uninstallRoot, target.uninstallSubkey.c_str(), 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
      return fail(RegistrationStep::UninstallEntry, "could not open the uninstall key");
    }
    bool ok = true;
    ok = set_string(key, L"DisplayName", target.productName) && ok;
    ok = set_string(key, L"DisplayVersion", target.version) && ok;
    ok = set_string(key, L"Publisher", target.publisher) && ok;
    ok = set_string(key, L"InstallLocation", target.installDir) && ok;
    ok = set_string(key, L"UninstallString", L"\"" + target.setupPath + L"\" /uninstall") && ok;
    ok = set_string(key, L"QuietUninstallString", L"\"" + target.setupPath + L"\" /uninstall /S") && ok;
    ok = set_string(key, L"DisplayIcon", target.installDir + L"\\" + target.hostExeName) && ok;
    const DWORD one = 1;
    ok = (RegSetValueExW(key, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one),
                         sizeof(one)) == ERROR_SUCCESS) && ok;
    ok = (RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one),
                         sizeof(one)) == ERROR_SUCCESS) && ok;
    RegCloseKey(key);
    if (!ok) return fail(RegistrationStep::UninstallEntry, "could not write every value");
    result.completed.push_back(RegistrationStep::UninstallEntry);
  }

  result.ok = true;
  return result;
}

bool capture_registration(HKEY root, const std::wstring& subkey, RegistrationSnapshot* out) {
  if (!out) return false;
  *out = RegistrationSnapshot{};

  HKEY key = nullptr;
  if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
    // Nothing registered. Recorded as a fact rather than an error: a first install has nothing to
    // capture, and a rollback in that case has to REMOVE the key, not restore blanks.
    out->present = false;
    return true;
  }
  out->present = true;
  out->displayName = read_string(key, L"DisplayName");
  out->displayVersion = read_string(key, L"DisplayVersion");
  out->publisher = read_string(key, L"Publisher");
  out->installLocation = read_string(key, L"InstallLocation");
  out->uninstallString = read_string(key, L"UninstallString");
  out->quietUninstallString = read_string(key, L"QuietUninstallString");
  out->displayIcon = read_string(key, L"DisplayIcon");
  RegCloseKey(key);
  return true;
}

bool restore_registration(HKEY root, const std::wstring& subkey,
                          const RegistrationSnapshot& snapshot) {
  if (!snapshot.present) {
    const LSTATUS deleted = RegDeleteTreeW(root, subkey.c_str());
    return deleted == ERROR_SUCCESS || deleted == ERROR_FILE_NOT_FOUND;
  }

  HKEY key = nullptr;
  if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    return false;
  }
  bool ok = true;
  ok = set_string(key, L"DisplayName", snapshot.displayName) && ok;
  // The whole reason a snapshot exists: after a rollback the files are the previous version, so
  // the version recorded here has to be the previous one too. Writing the new version would leave
  // the registry claiming something the disk does not support.
  ok = set_string(key, L"DisplayVersion", snapshot.displayVersion) && ok;
  ok = set_string(key, L"Publisher", snapshot.publisher) && ok;
  ok = set_string(key, L"InstallLocation", snapshot.installLocation) && ok;
  ok = set_string(key, L"UninstallString", snapshot.uninstallString) && ok;
  ok = set_string(key, L"QuietUninstallString", snapshot.quietUninstallString) && ok;
  ok = set_string(key, L"DisplayIcon", snapshot.displayIcon) && ok;
  RegCloseKey(key);
  return ok;
}

}  // namespace remote60::native_poc::install
