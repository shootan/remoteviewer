#pragma once

// Registering an installation: the service, the firewall rule, the Start menu entries and the
// uninstall key. Shared by the installer and the updater so there is one copy of it.
//
// It exists because of one fact about the alternative. The updater could have re-run the
// installed GNLinkSetup.exe with a "register only" flag -- but GNLinkSetup.exe is not in the
// installer's payload list (installer_main.cpp:48-57) and is copied separately (`:314`), so after
// an update the copy on disk is still the OLD binary. Its DisplayVersion is a compile-time
// constant (`:269`), so registering through it would write the OLD version into the key that
// read_installed_version() reads (`:436`) -- and every later "is this newer" decision would be
// measured against a lie. Passing the version as an argument is the whole point of this unit.
//
// What this is NOT: a transaction. Replacing several files and then registering four separate
// system facts is not atomic and cannot be made atomic here. The file swap has all-or-nothing
// behaviour of its own (update_effects.cpp), and this reports which registration step failed so
// a caller can undo deliberately -- but "the update either happened or it did not" is a contract
// built out of recoverable steps, not a single indivisible one.
//
// Design: docs/업데이트_기능_설계.md 3.1 (Register stage), 3.6 (completion conditions).

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace remote60::native_poc::install {

/** The four things registering an installation consists of, in the order they are done. */
enum class RegistrationStep {
  Service,        // the LocalSystem secure-input service
  Firewall,       // inbound rule for the streaming host
  Shortcuts,      // Start menu entries
  UninstallEntry, // the Add/Remove Programs key, including DisplayVersion
};

const char* step_name(RegistrationStep step);

/**
 * Everything registration needs, with nothing defaulted to a real machine value.
 *
 * The registry root and the service name are here rather than baked in for the same reason they
 * are required in UpdateEffectsConfig: a test that supplies scratch values must not be able to
 * reach the real uninstall key or the real GNLinkSecureInput service.
 */
struct RegistrationTarget {
  std::wstring installDir;
  std::wstring setupPath;      // what UninstallString will point at
  std::wstring version;        // NOT a constant -- the caller says which version this is
  std::wstring productName;
  std::wstring clientShortcutName;
  std::wstring publisher;

  std::wstring serviceName;
  std::wstring firewallRuleName;
  HKEY uninstallRoot = nullptr;      // HKEY_LOCAL_MACHINE in production; HKCU in tests
  std::wstring uninstallSubkey;

  // File names inside installDir, so nothing here assumes a layout.
  std::wstring hostExeName;
  std::wstring clientExeName;
  std::wstring serviceExeName;
  std::wstring streamExeName;

  bool validate(std::string* detail = nullptr) const;
};

/**
 * The effectful bits, injected.
 *
 * `runProcess` covers both netsh and the service registration call; `createShortcut` covers the
 * Start menu. A test supplies recording stand-ins, so no test creates a firewall rule, touches
 * the SCM, or writes a .lnk into the real Start menu.
 */
struct RegistrationOps {
  /** Returns the process exit code, or a negative value when it could not be started. */
  std::function<int(const std::wstring& exePath, const std::wstring& arguments)> runProcess;
  /**
   * target, link name, description, and where the failure was when it fails.
   *
   * `detail` may be null. It exists because the only thing the caller could previously report was
   * "could not create the host shortcut" -- true, and indistinguishable from every other reason a
   * shell link does not get written. The one that actually happened in the field was COM not being
   * initialised, and that is an HRESULT nobody could see.
   */
  std::function<bool(const std::wstring&, const std::wstring&, const std::wstring&,
                     std::string* detail)>
      createShortcut;
};

struct RegistrationResult {
  bool ok = false;
  /** Steps that completed, in order. Present even on failure -- that is the partial state. */
  std::vector<RegistrationStep> completed;
  /** Which step failed, when one did. */
  std::optional<RegistrationStep> failedAt;
  std::string detail;

  bool completed_step(RegistrationStep step) const;
};

/**
 * Registers an installation.
 *
 * Stops at the first failure and says where. The installer today carries on after a failed
 * service registration, showing a message box and still reporting success -- that behaviour is
 * deliberately not reproduced here, because a caller that cannot tell a full registration from a
 * partial one cannot decide whether to roll back.
 */
RegistrationResult register_install(const RegistrationTarget& target, const RegistrationOps& ops);

/**
 * What is currently registered, so a rollback can put it back.
 *
 * Captured BEFORE a swap. Restoring with the new version would leave old files claiming to be the
 * new version -- the same poisoning of the reference point that made re-running the old installer
 * unacceptable, arrived at from the other direction.
 */
struct RegistrationSnapshot {
  bool present = false;   // false when nothing was registered before
  std::wstring displayName;
  std::wstring displayVersion;
  std::wstring publisher;
  std::wstring installLocation;
  std::wstring uninstallString;
  std::wstring quietUninstallString;
  std::wstring displayIcon;
};

bool capture_registration(HKEY root, const std::wstring& subkey, RegistrationSnapshot* out);

/**
 * Puts a captured registration back.
 *
 * A snapshot with `present == false` means there was nothing registered before, so restoring it
 * removes the key rather than writing blanks.
 */
bool restore_registration(HKEY root, const std::wstring& subkey, const RegistrationSnapshot& snapshot);

}  // namespace remote60::native_poc::install
