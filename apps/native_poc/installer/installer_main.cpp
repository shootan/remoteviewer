// GNLink Host installer / uninstaller.
//
// One executable that carries the whole product as embedded resources. It exists because the
// streaming host installs a LocalSystem service: when the product runs out of a user-writable
// folder, anyone who can write there can replace that service binary and get SYSTEM. Installing
// under %ProgramFiles% -- which only administrators can write -- is what closes that, so the
// installer's real job is to put the files somewhere safe and register the service exactly once
// against that location.
//
//   GNLinkSetup.exe               interactive install
//   GNLinkSetup.exe /S            silent install
//   GNLinkSetup.exe /uninstall    remove service, firewall rules, files and registration
//   GNLinkSetup.exe /uninstall /S silent uninstall

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>

#include <string>
#include <vector>

#include "installer_ids.h"
#include "product_version.hpp"
#include "version_compare.hpp"
#include "install_registration.hpp"

namespace {

using remote60::native_poc::kProductVersion;
// Registration is shared with the updater, so the version written into DisplayVersion is an
// argument rather than whatever constant the registering binary happens to carry.
namespace install = remote60::native_poc::install;

constexpr wchar_t kProductName[] = L"GNLink Host";
// The other half. Named apart from the host so the Start menu says which one is being opened:
// this machine can be the one you leave running, the one you sit at, or both.
constexpr wchar_t kClientShortcutName[] = L"GNLink";
constexpr wchar_t kInstallFolderName[] = L"GNLink";
constexpr wchar_t kServiceName[] = L"GNLinkSecureInput";
constexpr wchar_t kUninstallKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GNLink";
constexpr wchar_t kFirewallRuleName[] = L"GNLink Host";
constexpr wchar_t kSetupFileName[] = L"GNLinkSetup.exe";
/**
 * Where the updater runs from: a SIBLING of the install folder, not a child.
 *
 * A child would be inside the directory being replaced, which the updater refuses outright. And
 * it has to be somewhere only administrators can write, because what runs from here runs
 * elevated -- a user-writable location leaves a window between the copy and the launch in which
 * the binary can be swapped. Under %ProgramFiles% that is already true by the default ACL, so
 * there is nothing to set up and therefore nothing to forget. %TEMP% and %ProgramData% were
 * excluded for exactly that reason (design survey, history #427).
 */
constexpr wchar_t kUpdaterWorkFolderName[] = L"GNLink.update";

struct PayloadFile {
  int resourceId;
  const wchar_t* fileName;
};

// Order matters only for reporting; every file lands in the same flat directory because each
// executable locates its siblings next to its own module.
const PayloadFile kPayload[] = {
    {IDR_PAYLOAD_HOST_APP, L"GNLinkHost.exe"},
    {IDR_PAYLOAD_VIDEO_HOST, L"GNLinkStream.exe"},
    {IDR_PAYLOAD_SECURE_INPUT, L"GNLinkInputService.exe"},
    {IDR_PAYLOAD_GDI_WORKER, L"GNLinkCapture.exe"},
    {IDR_PAYLOAD_CLIENT_SHELL, L"GNLinkClient.exe"},
    {IDR_PAYLOAD_CLIENT_VIEWER, L"GNLinkViewer.exe"},
    // Installed here like anything else. It runs from a copy of itself in a sibling directory, so
    // the file sitting here is never the one executing and a later update can replace it. Putting
    // it outside the install directory instead would make it unreplaceable -- payload names are
    // relative and refuse traversal -- and uninstall would orphan it.
    {IDR_PAYLOAD_UPDATER, L"GNLinkUpdater.exe"},
    // Both load their interfaces from beside themselves, in a subdirectory that has to exist.
    {IDR_PAYLOAD_CLIENT_UI, L"ui\\shell.html"},
    {IDR_PAYLOAD_MACRO_UI, L"ui\\macro.html"},
};

bool gSilent = false;
// In the window the outcome is already visible in the status line, so only failures deserve to
// interrupt with a message box.
bool gDialogMode = false;

void report(const std::wstring& text, bool error) {
  if (gDialogMode && !error) return;
  if (gSilent) {
    HANDLE out = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) {
      const std::wstring line = text + L"\r\n";
      DWORD written = 0;
      (void)WriteConsoleW(out, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
    return;
  }
  MessageBoxW(nullptr, text.c_str(), kProductName,
              MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

std::wstring program_files_dir() {
  PWSTR wide = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_CREATE, nullptr, &wide))) {
    return {};
  }
  std::wstring result(wide);
  CoTaskMemFree(wide);
  return result;
}

std::wstring install_dir() {
  const std::wstring base = program_files_dir();
  if (base.empty()) return {};
  return base + L"\\" + kInstallFolderName;
}

std::wstring current_executable_path() {
  std::wstring path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  path.resize(length);
  return path;
}

bool write_file(const std::wstring& path, const void* data, DWORD bytes) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const auto* cursor = static_cast<const uint8_t*>(data);
  DWORD total = 0;
  bool ok = true;
  while (total < bytes) {
    DWORD written = 0;
    if (!WriteFile(file, cursor + total, bytes - total, &written, nullptr) || written == 0) {
      ok = false;
      break;
    }
    total += written;
  }
  CloseHandle(file);
  return ok;
}

bool extract_resource(int resourceId, const std::wstring& destination) {
  // RT_RCDATA is an integer atom declared for the ANSI entry points; reinterpret it for the W one.
  HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId),
                              reinterpret_cast<LPCWSTR>(RT_RCDATA));
  if (!found) return false;
  const DWORD size = SizeofResource(nullptr, found);
  HGLOBAL loaded = LoadResource(nullptr, found);
  if (!loaded || size == 0) return false;
  const void* data = LockResource(loaded);
  if (!data) return false;
  return write_file(destination, data, size);
}

// Blocks the installer until nothing is holding the files it is about to overwrite.
void stop_running_product() {
  // The service holds GNLinkInputService.exe open; ask the SCM to stop it first.
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (manager) {
    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service) {
      SERVICE_STATUS status{};
      (void)ControlService(service, SERVICE_CONTROL_STOP, &status);
      for (int i = 0; i < 50; ++i) {
        SERVICE_STATUS_PROCESS live{};
        DWORD bytes = 0;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<BYTE*>(&live), sizeof(live), &bytes)) {
          break;
        }
        if (live.dwCurrentState == SERVICE_STOPPED) break;
        Sleep(100);
      }
      CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
  }
  // The tray app and its children keep their own images locked.
  static const wchar_t* kImages[] = {L"GNLinkHost.exe", L"GNLinkStream.exe",
                                     L"GNLinkCapture.exe",
                                     L"GNLinkInputService.exe"};
  for (const wchar_t* image : kImages) {
    std::wstring command = L"taskkill /F /T /IM ";
    command += image;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &startup, &process)) {
      WaitForSingleObject(process.hProcess, 5000);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
    }
  }
}

int run_and_wait(const std::wstring& application, const std::wstring& arguments) {
  std::wstring command = L"\"" + application + L"\" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &startup, &process)) {
    return -1;
  }
  WaitForSingleObject(process.hProcess, 30000);
  DWORD exitCode = 0;
  (void)GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return static_cast<int>(exitCode);
}

void run_netsh(const std::wstring& arguments) {
  wchar_t system32[MAX_PATH]{};
  if (GetSystemDirectoryW(system32, MAX_PATH) == 0) return;
  (void)run_and_wait(std::wstring(system32) + L"\\netsh.exe", arguments);
}

// Without these the host binds its ports but every inbound datagram is dropped, so a freshly
// installed machine looks connected and never shows a picture.
[[maybe_unused]] void add_firewall_rules(const std::wstring& directory) {
  const std::wstring hostExe = directory + L"\\GNLinkStream.exe";
  run_netsh(L"advfirewall firewall add rule name=\"" + std::wstring(kFirewallRuleName) +
            L"\" dir=in action=allow program=\"" + hostExe + L"\" enable=yes profile=any");
}

void remove_firewall_rules() {
  run_netsh(L"advfirewall firewall delete rule name=\"" + std::wstring(kFirewallRuleName) + L"\"");
}

/** For rendering an ASCII step name into a message. Not a general converter. */
std::wstring widen_ascii(const char* text) {
  std::wstring out;
  for (const char* p = text; p && *p; ++p) out.push_back(static_cast<wchar_t>(*p));
  return out;
}

bool create_start_menu_shortcut(const std::wstring& target, const wchar_t* linkName,
                                const wchar_t* description, std::wstring* outPath) {
  PWSTR wide = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, KF_FLAG_CREATE, nullptr, &wide))) {
    return false;
  }
  std::wstring linkPath(wide);
  CoTaskMemFree(wide);
  linkPath += L"\\";
  linkPath += linkName;
  linkPath += L".lnk";

  IShellLinkW* link = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                              reinterpret_cast<void**>(&link)))) {
    return false;
  }
  bool ok = false;
  if (SUCCEEDED(link->SetPath(target.c_str()))) {
    const size_t slash = target.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
      (void)link->SetWorkingDirectory(target.substr(0, slash).c_str());
    }
    (void)link->SetDescription(description);
    IPersistFile* persist = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist)))) {
      ok = SUCCEEDED(persist->Save(linkPath.c_str(), TRUE));
      persist->Release();
    }
  }
  link->Release();
  if (ok && outPath) *outPath = linkPath;
  return ok;
}

// Superseded by install::register_install on the install path; kept only if some other caller
// needs it. Marked [[maybe_unused]] so removing the last caller does not become a build break.
[[maybe_unused]] void write_uninstall_entry(const std::wstring& directory,
                                            const std::wstring& setupPath) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return;
  }
  auto set = [&](const wchar_t* name, const std::wstring& value) {
    (void)RegSetValueExW(key, name, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(value.c_str()),
                         static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  };
  set(L"DisplayName", kProductName);
  set(L"DisplayVersion", kProductVersion);
  set(L"Publisher", L"GNLink");
  set(L"InstallLocation", directory);
  set(L"UninstallString", L"\"" + setupPath + L"\" /uninstall");
  set(L"QuietUninstallString", L"\"" + setupPath + L"\" /uninstall /S");
  set(L"DisplayIcon", directory + L"\\GNLinkHost.exe");
  const DWORD noModify = 1;
  (void)RegSetValueExW(key, L"NoModify", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&noModify), sizeof(noModify));
  (void)RegSetValueExW(key, L"NoRepair", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&noModify), sizeof(noModify));
  RegCloseKey(key);
}

/** %ProgramFiles%\\GNLink.update -- see kUpdaterWorkFolderName. Empty when it cannot be resolved. */
std::wstring updater_work_dir() {
  const std::wstring base = program_files_dir();
  if (base.empty()) return {};
  return base + L"\\" + kUpdaterWorkFolderName;
}

int do_install() {
  const std::wstring directory = install_dir();
  if (directory.empty()) {
    report(L"Could not resolve the Program Files folder.", true);
    return 2;
  }
  stop_running_product();
  if (!CreateDirectoryW(directory.c_str(), nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    report(L"Could not create " + directory + L"\n\nRun the installer as administrator.", true);
    return 3;
  }
  for (const PayloadFile& file : kPayload) {
    const std::wstring destination = directory + L"\\" + file.fileName;
    // One entry lives in a subdirectory, which has to exist before the write.
    const size_t slash = destination.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
      const std::wstring parent = destination.substr(0, slash);
      if (_wcsicmp(parent.c_str(), directory.c_str()) != 0) {
        CreateDirectoryW(parent.c_str(), nullptr);
      }
    }
    if (!extract_resource(file.resourceId, destination)) {
      report(std::wstring(L"Could not write ") + file.fileName + L" to " + directory, true);
      return 4;
    }
  }
  // Created now rather than by the updater on first use, so it exists with the ACL it inherits
  // from %ProgramFiles% before anything is ever written into it. Not fatal if it fails -- the
  // updater creates it too, and an update that cannot run is better than an install that stops.
  {
    const std::wstring workDir = updater_work_dir();
    if (!workDir.empty() && !CreateDirectoryW(workDir.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
      // Recorded, not reported: the user came here to install the product, and this folder only
      // matters the first time an update runs.
      OutputDebugStringW(L"GNLink: could not create the updater working directory\n");
    }
  }

  // Keep the installer itself alongside the product so Add/Remove Programs has something to run.
  const std::wstring setupPath = directory + L"\\" + kSetupFileName;
  const std::wstring self = current_executable_path();
  if (!self.empty() && _wcsicmp(self.c_str(), setupPath.c_str()) != 0) {
    (void)CopyFileW(self.c_str(), setupPath.c_str(), FALSE);
  }

  // Service, firewall, shortcuts and the uninstall key now go through the shared registration
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
               L").\nSome features may not work until this is repaired.",
           true);
  }

  report(L"GNLink Host was installed to\n" + directory +
             L"\n\nStart it from the Start menu. Because the files now live in an "
             L"administrator-only folder, the secure input service can no longer be redirected "
             L"to a writable copy.",
         false);
  return 0;
}

// Deleting a running executable is not possible, so the uninstaller relaunches itself from the
// temp folder and lets that copy remove the install directory.
bool relaunch_from_temp_for_uninstall() {
  const std::wstring self = current_executable_path();
  const std::wstring directory = install_dir();
  if (self.empty() || directory.empty()) return false;
  // Only needed when running from inside the directory that is about to be deleted.
  if (_wcsnicmp(self.c_str(), directory.c_str(), directory.size()) != 0) return false;

  wchar_t tempDir[MAX_PATH]{};
  if (GetTempPathW(MAX_PATH, tempDir) == 0) return false;
  const std::wstring copyPath = std::wstring(tempDir) + L"GNLinkUninstall.exe";
  if (!CopyFileW(self.c_str(), copyPath.c_str(), FALSE)) return false;

  std::wstring command = L"\"" + copyPath + L"\" /uninstall /fromtemp";
  if (gSilent) command += L" /S";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                      &startup, &process)) {
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

void remove_start_menu_shortcut() {
  PWSTR wide = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr, &wide))) return;
  const std::wstring folder(wide);
  CoTaskMemFree(wide);
  for (const wchar_t* name : {kProductName, kClientShortcutName}) {
    (void)DeleteFileW((folder + L"\\" + name + L".lnk").c_str());
  }
}

int do_uninstall(bool fromTemp) {
  const std::wstring directory = install_dir();
  if (directory.empty()) return 2;

  if (!fromTemp && relaunch_from_temp_for_uninstall()) return 0;

  const std::wstring serviceExe = directory + L"\\GNLinkInputService.exe";
  if (GetFileAttributesW(serviceExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
    (void)run_and_wait(serviceExe, L"--uninstall-service");
  }
  stop_running_product();
  remove_firewall_rules();
  remove_start_menu_shortcut();

  // The service can hold its image open briefly after DeleteService; retry before giving up.
  bool filesRemoved = true;
  for (int attempt = 0; attempt < 10; ++attempt) {
    filesRemoved = true;
    for (const PayloadFile& file : kPayload) {
      const std::wstring path = directory + L"\\" + file.fileName;
      if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
      if (!DeleteFileW(path.c_str())) filesRemoved = false;
    }
    if (filesRemoved) break;
    Sleep(300);
  }

  const std::wstring setupPath = directory + L"\\" + kSetupFileName;
  if (!DeleteFileW(setupPath.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
    // Still locked (we may be it): let the reboot finish the job rather than failing.
    (void)MoveFileExW(setupPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
  }
  (void)RemoveDirectoryW(directory.c_str());

  // The updater's working copy lives outside the install directory, so removing that directory
  // does not take it with it. An update running right now still holds its copy open, which is why
  // the same delete-after-reboot fallback is used here as for the installer's own image.
  {
    const std::wstring workDir = updater_work_dir();
    if (!workDir.empty()) {
      WIN32_FIND_DATAW found{};
      HANDLE search = FindFirstFileW((workDir + L"\\*").c_str(), &found);
      if (search != INVALID_HANDLE_VALUE) {
        do {
          const std::wstring name = found.cFileName;
          if (name == L"." || name == L"..") continue;
          const std::wstring path = workDir + L"\\" + name;
          if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            (void)MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
          }
        } while (FindNextFileW(search, &found));
        FindClose(search);
      }
      if (!RemoveDirectoryW(workDir.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
        (void)MoveFileExW(workDir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
      }
    }
  }

  (void)RegDeleteKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, KEY_WOW64_64KEY, 0);

  report(filesRemoved ? L"GNLink Host was removed."
                      : L"GNLink Host was removed. Some files are still in use and will be "
                        L"deleted after a restart.",
         false);
  return 0;
}

// ---------------------------------------------------------------- installed-state detection

std::wstring read_installed_version() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return {};
  }
  wchar_t buffer[128]{};
  DWORD bytes = sizeof(buffer);
  DWORD type = 0;
  const LSTATUS status =
      RegQueryValueExW(key, L"DisplayVersion", nullptr, &type, reinterpret_cast<BYTE*>(buffer),
                       &bytes);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || type != REG_SZ) return {};
  return buffer;
}

// The comparison itself now lives in version_compare.hpp, shared with the updater and covered by
// the vectors in apps/shared/version_compare_vectors.txt -- it used to be file-local here with no
// test at all, and the updater needs exactly the same answer the installer gives.
using remote60::native_poc::compare_versions;

enum class InstalledState { None, Same, Older, Newer };

InstalledState detect_state(std::wstring* outInstalledVersion) {
  const std::wstring installed = read_installed_version();
  if (outInstalledVersion) *outInstalledVersion = installed;
  if (installed.empty()) return InstalledState::None;
  const int cmp = compare_versions(installed, kProductVersion);
  if (cmp < 0) return InstalledState::Older;
  if (cmp > 0) return InstalledState::Newer;
  return InstalledState::Same;
}

void apply_state_to_dialog(HWND dialog) {
  std::wstring installed;
  const InstalledState state = detect_state(&installed);

  std::wstring title = std::wstring(kProductName) + L"  " + kProductVersion;
  std::wstring status;
  std::wstring primary;
  bool showUninstall = true;
  switch (state) {
    case InstalledState::None:
      status = L"Not installed on this computer.\n\nInstalls to Program Files and registers the "
               L"secure input service.";
      primary = L"Install";
      showUninstall = false;
      break;
    case InstalledState::Same:
      status = L"Version " + installed +
               L" is already installed.\n\nRepair rewrites the program files and re-registers "
               L"the service.";
      primary = L"Repair";
      break;
    case InstalledState::Older:
      status = L"Version " + installed + L" is installed. This setup contains " +
               kProductVersion + L".";
      primary = L"Update";
      break;
    case InstalledState::Newer:
      status = L"A newer version (" + installed + L") is installed. This setup contains " +
               kProductVersion + L".";
      primary = L"Reinstall";
      break;
  }
  SetDlgItemTextW(dialog, IDC_TITLE, title.c_str());
  SetDlgItemTextW(dialog, IDC_STATUS, status.c_str());
  SetDlgItemTextW(dialog, IDC_PRIMARY, primary.c_str());
  ShowWindow(GetDlgItem(dialog, IDC_UNINSTALL), showUninstall ? SW_SHOW : SW_HIDE);
}

void set_buttons_enabled(HWND dialog, bool enabled) {
  EnableWindow(GetDlgItem(dialog, IDC_PRIMARY), enabled);
  EnableWindow(GetDlgItem(dialog, IDC_UNINSTALL), enabled);
  EnableWindow(GetDlgItem(dialog, IDCANCEL), enabled);
}

INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wParam, LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_GNLINK));
      if (icon) {
        SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
      }
      apply_state_to_dialog(dialog);
      return TRUE;
    }
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case IDC_PRIMARY: {
          set_buttons_enabled(dialog, false);
          SetDlgItemTextW(dialog, IDC_STATUS, L"Installing...");
          // Let the label paint before the work blocks this thread.
          UpdateWindow(dialog);
          const int result = do_install();
          set_buttons_enabled(dialog, true);
          if (result == 0) {
            apply_state_to_dialog(dialog);
          } else {
            SetDlgItemTextW(dialog, IDC_STATUS, L"Installation failed.");
          }
          return TRUE;
        }
        case IDC_UNINSTALL: {
          set_buttons_enabled(dialog, false);
          SetDlgItemTextW(dialog, IDC_STATUS, L"Removing...");
          UpdateWindow(dialog);
          // Running from the install directory would delete this very file, so the uninstall
          // relaunches itself from temp and this instance simply exits.
          const std::wstring self = current_executable_path();
          const std::wstring directory = install_dir();
          const bool selfInInstallDir =
              !self.empty() && !directory.empty() &&
              _wcsnicmp(self.c_str(), directory.c_str(), directory.size()) == 0;
          (void)do_uninstall(false);
          if (selfInInstallDir) {
            EndDialog(dialog, 0);
            return TRUE;
          }
          set_buttons_enabled(dialog, true);
          apply_state_to_dialog(dialog);
          return TRUE;
        }
        case IDCANCEL:
          EndDialog(dialog, 0);
          return TRUE;
        default:
          break;
      }
      break;
    case WM_CLOSE:
      EndDialog(dialog, 0);
      return TRUE;
    default:
      break;
  }
  return FALSE;
}

bool has_flag(const std::wstring& commandLine, const wchar_t* flag) {
  std::wstring haystack = commandLine;
  for (wchar_t& c : haystack) c = static_cast<wchar_t>(towlower(c));
  std::wstring needle = flag;
  for (wchar_t& c : needle) c = static_cast<wchar_t>(towlower(c));
  return haystack.find(needle) != std::wstring::npos;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  const std::wstring commandLine = GetCommandLineW();
  gSilent = has_flag(commandLine, L"/s") || has_flag(commandLine, L"-s");
  if (gSilent) {
    // Give a console-launched silent run somewhere to print.
    (void)AttachConsole(ATTACH_PARENT_PROCESS);
  }

  (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  int result = 0;
  if (has_flag(commandLine, L"/uninstall")) {
    // Add/Remove Programs and the temp relaunch both come through here.
    result = do_uninstall(has_flag(commandLine, L"/fromtemp"));
  } else if (gSilent) {
    result = do_install();
  } else {
    // Interactive: show what is on the machine and offer the actions that make sense for it.
    gDialogMode = true;
    result = static_cast<int>(DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_MAIN),
                                              nullptr, dialog_proc, 0));
  }
  CoUninitialize();
  return result;
}
