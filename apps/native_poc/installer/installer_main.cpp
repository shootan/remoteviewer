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

namespace {

constexpr wchar_t kProductName[] = L"GNLink Host";
constexpr wchar_t kInstallFolderName[] = L"GNLink";
constexpr wchar_t kServiceName[] = L"GNLinkSecureInput";
constexpr wchar_t kUninstallKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GNLink";
constexpr wchar_t kFirewallRuleName[] = L"GNLink Host";
constexpr wchar_t kSetupFileName[] = L"GNLinkSetup.exe";
constexpr wchar_t kProductVersion[] = L"0.2.5";

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
void add_firewall_rules(const std::wstring& directory) {
  const std::wstring hostExe = directory + L"\\GNLinkStream.exe";
  run_netsh(L"advfirewall firewall add rule name=\"" + std::wstring(kFirewallRuleName) +
            L"\" dir=in action=allow program=\"" + hostExe + L"\" enable=yes profile=any");
}

void remove_firewall_rules() {
  run_netsh(L"advfirewall firewall delete rule name=\"" + std::wstring(kFirewallRuleName) + L"\"");
}

bool create_start_menu_shortcut(const std::wstring& target, std::wstring* outPath) {
  PWSTR wide = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, KF_FLAG_CREATE, nullptr, &wide))) {
    return false;
  }
  std::wstring linkPath(wide);
  CoTaskMemFree(wide);
  linkPath += L"\\";
  linkPath += kProductName;
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
    (void)link->SetDescription(L"GNLink remote desktop host");
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

void write_uninstall_entry(const std::wstring& directory, const std::wstring& setupPath) {
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
    if (!extract_resource(file.resourceId, destination)) {
      report(std::wstring(L"Could not write ") + file.fileName + L" to " + directory, true);
      return 4;
    }
  }
  // Keep the installer itself alongside the product so Add/Remove Programs has something to run.
  const std::wstring setupPath = directory + L"\\" + kSetupFileName;
  const std::wstring self = current_executable_path();
  if (!self.empty() && _wcsicmp(self.c_str(), setupPath.c_str()) != 0) {
    (void)CopyFileW(self.c_str(), setupPath.c_str(), FALSE);
  }

  const std::wstring serviceExe = directory + L"\\GNLinkInputService.exe";
  const int serviceResult = run_and_wait(serviceExe, L"--install-service");
  if (serviceResult != 0) {
    report(L"Installed, but registering the secure input service failed (code " +
               std::to_wstring(serviceResult) +
               L").\nInput to elevated windows and the lock screen will not work.",
           true);
  }

  add_firewall_rules(directory);
  (void)create_start_menu_shortcut(directory + L"\\GNLinkHost.exe", nullptr);
  write_uninstall_entry(directory, setupPath);

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
  std::wstring linkPath(wide);
  CoTaskMemFree(wide);
  linkPath += L"\\";
  linkPath += kProductName;
  linkPath += L".lnk";
  (void)DeleteFileW(linkPath.c_str());
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

// Negative when a < b, 0 when equal, positive when a > b. Missing components count as zero, so
// "0.1" and "0.1.0" compare equal.
int compare_versions(const std::wstring& a, const std::wstring& b) {
  size_t i = 0;
  size_t j = 0;
  while (i < a.size() || j < b.size()) {
    unsigned long left = 0;
    while (i < a.size() && a[i] >= L'0' && a[i] <= L'9') left = left * 10 + (a[i++] - L'0');
    unsigned long right = 0;
    while (j < b.size() && b[j] >= L'0' && b[j] <= L'9') right = right * 10 + (b[j++] - L'0');
    if (left != right) return left < right ? -1 : 1;
    if (i < a.size() && a[i] == L'.') ++i;
    if (j < b.size() && b[j] == L'.') ++j;
    // Anything that is not a digit or a separator ends the comparison.
    if ((i < a.size() && (a[i] < L'0' || a[i] > L'9')) ||
        (j < b.size() && (b[j] < L'0' || b[j] > L'9'))) {
      break;
    }
  }
  return 0;
}

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
