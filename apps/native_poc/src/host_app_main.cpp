// GNLink host application.
//
// The streaming host is a console program driven by flags, which is fine for development and
// wrong for the thing a person installs on their PC. This wraps it: sign in once, then it sits
// in the tray, keeps the streaming process alive, and shows whether the PC is reachable.
//
// It deliberately does not stream anything itself. Capture, encode and input injection stay in
// GNLinkStream.exe, launched as a child process, so a crash there cannot take
// the sign-in state with it and the two can be developed independently.
//
// The window has exactly two states with their own layouts: signed out is a sign-in form,
// signed in is a small status card. Every control lives in AppState so a state switch can hide
// all of one set and show the other; an anonymous label that nothing owns cannot be hidden,
// which is how sign-in labels used to bleed into the signed-in screen.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// winsock2 must precede windows.h: windows.h otherwise pulls in the original winsock, whose
// declarations collide with it.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <shellapi.h>
#include <shlwapi.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "directory_client.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace {

namespace directory = remote60::native_poc::directory;

constexpr wchar_t kWindowClass[] = L"Remote60HostApp";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kStatusTimer = 1;
constexpr UINT kStatusIntervalMs = 2000;
// Matches IDI_GNLINK in host_app.rc.
constexpr int kIconResource = 101;
// What the user sees. Internal identifiers (window class, Run value, cache folder) keep the
// remote60 name so existing installs migrate without special handling.
constexpr wchar_t kProductName[] = L"GNLink Host";

enum ControlId : int {
  IdServer = 1001,
  IdAccount,
  IdPassword,
  IdHostName,
  IdSignIn,
  IdStatus,
  IdHint,
  IdStartWithWindows,
  IdCreateAccount,
  IdSignupKey,
  IdSignupKeyLabel,
  IdSignOut,
  IdSwitchAccount,
  IdTitle,
  IdServerLabel,
  IdAccountLabel,
  IdPasswordLabel,
  IdHostNameLabel,
  IdAdvanced,
  IdSignedAccount,
  IdSignedHost,
  IdBadge,
  IdOpenLog,
};

enum MenuId : int {
  IdMenuOpen = 2001,
  IdMenuChangeAccount,
  IdMenuSignOut,
  IdMenuExit,
};

std::wstring widen(const std::string& value) {
  if (value.empty()) return {};
  const int need = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (need <= 1) return {};
  std::wstring out(static_cast<size_t>(need - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), need);
  return out;
}

std::string narrow(const std::wstring& value) {
  if (value.empty()) return {};
  const int need = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (need <= 1) return {};
  std::string out(static_cast<size_t>(need - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), need, nullptr, nullptr);
  return out;
}

std::wstring window_text(HWND control) {
  if (!control) return {};
  const int length = GetWindowTextLengthW(control);
  if (length <= 0) return {};
  // Sized for the terminator as well, then trimmed to what was actually copied: writing the
  // terminator into the last usable slot of an exactly-sized buffer is not something to rely on.
  std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(control, buffer.data(), length + 1);
  if (copied <= 0) return {};
  return std::wstring(buffer.data(), static_cast<size_t>(copied));
}

std::wstring executable_dir() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  PathRemoveFileSpecW(path);
  return path;
}

std::wstring own_executable_path() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

std::wstring default_host_name() {
  wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD length = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameW(name, &length) && name[0] != L'\0') return name;
  return L"PC";
}

/**
 * %LOCALAPPDATA%\GNLink\host_app.log -- the child's stdout, which is otherwise invisible in a
 * windowed app. When the status card says "not reachable" this is the only place with the why.
 * Credentials never appear here: the child is handed a token, not a password.
 */
std::wstring log_file_path() {
  wchar_t base[MAX_PATH] = {};
  const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};
  std::wstring dir = std::wstring(base) + L"\\GNLink";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\host_app.log";
}

// ---------------------------------------------------------------- streaming child

/**
 * Owns the streaming process. Restarting it is the correct response to almost any failure it
 * can hit -- a lost capture device, a driver reset -- so the supervisor simply relaunches, with
 * a delay so a process that dies instantly cannot spin.
 */
class StreamingHostProcess {
 public:
  void Configure(const std::wstring& directoryUrl, const std::wstring& accountId,
                 const std::wstring& hostName) {
    directoryUrl_ = directoryUrl;
    accountId_ = accountId;
    hostName_ = hostName;
  }

  void Start() {
    if (running_.exchange(true)) return;
    RotateLog();
    supervisor_ = std::thread([this] { Supervise(); });
  }

  void Stop() {
    if (!running_.exchange(false)) return;
    TerminateChild();
    if (supervisor_.joinable()) supervisor_.join();
  }

  bool Running() const { return running_.load(std::memory_order_relaxed); }
  uint32_t Restarts() const { return restarts_.load(std::memory_order_relaxed); }
  bool ChildAlive() const { return childAlive_.load(std::memory_order_relaxed); }

  /**
   * Last thing the streaming host said about the directory, verbatim.
   *
   * Without this the window reported "running" whether or not the PC was actually reachable,
   * which is the worst possible answer: the user believes they are set up and only finds out
   * when the phone shows nothing.
   */
  std::string DirectoryStatus() const {
    std::lock_guard<std::mutex> lock(statusMu_);
    return directoryStatus_;
  }

 private:
  void RotateLog() {
    const std::wstring path = log_file_path();
    if (path.empty()) return;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        info.nFileSizeLow > 2u * 1024 * 1024) {
      const std::wstring old = path + L".old";
      MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
  }

  void AppendLogLine(FILE* log, const std::string& line) {
    if (!log) return;
    std::fputs(line.c_str(), log);
    std::fputc('\n', log);
    std::fflush(log);
  }

  FILE* OpenLog() {
    const std::wstring path = log_file_path();
    if (path.empty()) return nullptr;
    // Shared open: the whole point of this file is reading it while the host runs, and the
    // Open log button (or a curious tail) must not be locked out by our writer handle.
    return _wfsopen(path.c_str(), L"ab", _SH_DENYNO);
  }

  void ReadChildOutput(HANDLE readEnd, FILE* log) {
    std::string pending;
    char buffer[512];
    DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
      pending.append(buffer, read);
      size_t newline;
      while ((newline = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        AppendLogLine(log, line);
        const size_t marker = line.find("directory ");
        if (marker != std::string::npos) {
          std::lock_guard<std::mutex> lock(statusMu_);
          directoryStatus_ = line.substr(marker + 10);
        }
      }
      if (pending.size() > 4096) pending.clear();
    }
  }

  void TerminateChild() {
    std::lock_guard<std::mutex> lock(mu_);
    if (child_.hProcess) {
      TerminateProcess(child_.hProcess, 0);
      CloseHandle(child_.hProcess);
      CloseHandle(child_.hThread);
      child_ = PROCESS_INFORMATION{};
    }
  }

  void Supervise() {
    const std::wstring exe = executable_dir() + L"\\GNLinkStream.exe";
    while (running_.load(std::memory_order_relaxed)) {
      // The control port serves clients on the same network that dial this PC directly. One
      // arriving through the directory tunnels control over the media socket instead, but with
      // no port open a LAN connection showed a picture that could not be controlled.
      std::wstring command = L"\"" + exe + L"\"" +
                             L" --transport udp --codec h264 --bind-port 43000" +
                             L" --control-port 43001" +
                             L" --directory-url \"" + directoryUrl_ + L"\"" +
                             L" --directory-id \"" + accountId_ + L"\"" +
                             L" --host-name \"" + hostName_ + L"\"";

      // The child's stdout is the only place the directory result is reported, so it is piped
      // back here rather than discarded.
      SECURITY_ATTRIBUTES sa{};
      sa.nLength = sizeof(sa);
      sa.bInheritHandle = TRUE;
      HANDLE readEnd = nullptr;
      HANDLE writeEnd = nullptr;
      if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        readEnd = writeEnd = nullptr;
      } else {
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
      }

      STARTUPINFOW si{};
      si.cb = sizeof(si);
      si.dwFlags = STARTF_USESHOWWINDOW | (writeEnd ? STARTF_USESTDHANDLES : 0);
      si.wShowWindow = SW_HIDE;
      si.hStdOutput = writeEnd;
      si.hStdError = writeEnd;
      PROCESS_INFORMATION pi{};
      std::vector<wchar_t> mutableCommand(command.begin(), command.end());
      mutableCommand.push_back(L'\0');

      // H.264 is still behind a build-time experiment switch in the streaming host, and a
      // build without it refuses --codec h264 and exits immediately. The product has no other
      // path, so the switch is turned on for the child rather than left to how it was built.
      SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
      // The product encoder preset, stated instead of inherited: whatever happens to be in
      // the parent environment must not silently change how every session encodes. Chosen by
      // A/B on 1080p30 scroll (2026-07-31): stable_text bought no decoded fps and doubled
      // latency p95; text legibility is already protected by the peak-constrained VBR policy.
      SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODER_TUNE_MODE", L"low_latency");

      FILE* log = OpenLog();
      AppendLogLine(log, "[host-app] starting the streaming host (tune=low_latency)");

      if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                          CREATE_NO_WINDOW, nullptr, executable_dir().c_str(), &si, &pi)) {
        if (readEnd) CloseHandle(readEnd);
        if (writeEnd) CloseHandle(writeEnd);
        childAlive_.store(false, std::memory_order_relaxed);
        AppendLogLine(log, "[host-app] cannot start the streaming host");
        if (log) std::fclose(log);
        {
          std::lock_guard<std::mutex> lock(statusMu_);
          directoryStatus_ = "cannot start the streaming host";
        }
        for (int i = 0; i < 30 && running_.load(std::memory_order_relaxed); ++i) {
          Sleep(100);
        }
        continue;
      }

      // Ours must close or the reader never sees end-of-file when the child exits.
      if (writeEnd) CloseHandle(writeEnd);
      std::thread reader;
      if (readEnd) reader = std::thread([this, readEnd, log] { ReadChildOutput(readEnd, log); });

      {
        std::lock_guard<std::mutex> lock(mu_);
        child_ = pi;
      }
      childAlive_.store(true, std::memory_order_relaxed);
      WaitForSingleObject(pi.hProcess, INFINITE);
      childAlive_.store(false, std::memory_order_relaxed);
      if (reader.joinable()) reader.join();
      if (readEnd) CloseHandle(readEnd);
      AppendLogLine(log, "[host-app] the streaming host exited");
      if (log) std::fclose(log);
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (child_.hProcess) {
          CloseHandle(child_.hProcess);
          CloseHandle(child_.hThread);
          child_ = PROCESS_INFORMATION{};
        }
      }
      if (!running_.load(std::memory_order_relaxed)) break;
      restarts_.fetch_add(1, std::memory_order_relaxed);
      for (int i = 0; i < 20 && running_.load(std::memory_order_relaxed); ++i) Sleep(100);
    }
  }

  std::wstring directoryUrl_;
  std::wstring accountId_;
  std::wstring hostName_;
  std::mutex mu_;
  mutable std::mutex statusMu_;
  std::string directoryStatus_ = "starting";
  PROCESS_INFORMATION child_{};
  std::thread supervisor_;
  std::atomic<bool> running_{false};
  std::atomic<bool> childAlive_{false};
  std::atomic<uint32_t> restarts_{0};
};

// ---------------------------------------------------------------- app state

enum class BadgeState { Starting, Reachable, SignInAgain, Error };

struct AppState {
  HWND window = nullptr;

  // Sign-in form. Labels are owned like everything else: an anonymous label cannot be hidden
  // when the state flips, and a stray "Password" caption on the signed-in card is exactly the
  // bug this design replaces.
  HWND titleLabel = nullptr;
  HWND hintLabel = nullptr;
  HWND accountLabel = nullptr;
  HWND accountEdit = nullptr;
  HWND passwordLabel = nullptr;
  HWND passwordEdit = nullptr;
  HWND hostNameLabel = nullptr;
  HWND hostNameEdit = nullptr;
  HWND advancedToggle = nullptr;
  HWND serverLabel = nullptr;
  HWND serverEdit = nullptr;
  HWND createAccountCheck = nullptr;
  HWND signupKeyLabel = nullptr;
  HWND signupKeyEdit = nullptr;
  HWND signInButton = nullptr;

  // Status card, shown once signed in.
  HWND signedAccountLabel = nullptr;
  HWND signedHostLabel = nullptr;
  HWND statusBadge = nullptr;
  HWND startWithWindowsCheck = nullptr;
  HWND switchAccountButton = nullptr;
  HWND signOutButton = nullptr;
  HWND openLogButton = nullptr;

  // Both states.
  HWND statusLabel = nullptr;

  HFONT font = nullptr;
  HFONT titleFont = nullptr;
  HBRUSH badgeBrushes[4] = {};
  BadgeState badgeState = BadgeState::Starting;
  UINT dpi = 96;
  bool advancedOpen = false;
  NOTIFYICONDATAW tray{};
  bool trayAdded = false;
  bool signedIn = false;
  bool uiPreview = false;
  std::string cachePath;
  directory::HostCache cache;
  StreamingHostProcess streaming;
  std::atomic<bool> signInBusy{false};
};

AppState g;

void relayout();

int sc(int value) { return MulDiv(value, static_cast<int>(g.dpi), 96); }

void set_status(const std::wstring& text) {
  if (g.statusLabel) SetWindowTextW(g.statusLabel, text.c_str());
}

// ---------------------------------------------------------------- autostart

const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
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
  return present && value[0] != L'\0';
}

void set_autostart(bool enabled) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return;
  }
  if (enabled) {
    // Quoted: the install path routinely contains spaces, and an unquoted entry silently
    // launches the wrong thing.
    const std::wstring command = L"\"" + own_executable_path() + L"\" --tray";
    RegSetValueExW(key, kRunValue, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(command.c_str()),
                   static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, kRunValue);
  }
  RegCloseKey(key);
}

// ---------------------------------------------------------------- tray

/** True when the directory has stopped accepting us and only a password can fix it. */
bool needs_sign_in_again(const std::string& directoryStatus);

void update_tray_tip() {
  if (!g.trayAdded) return;
  std::wstring tip = kProductName;
  if (g.signedIn) {
    const std::string directoryStatus = g.streaming.DirectoryStatus();
    tip += L" - " + widen(g.cache.hostName);
    if (!g.streaming.ChildAlive()) {
      tip += L" (starting)";
    } else if (directoryStatus.rfind("online", 0) == 0) {
      tip += L" (online)";
    } else if (needs_sign_in_again(directoryStatus)) {
      tip += L" (sign in again)";
    } else {
      tip += L" (not reachable)";
    }
  } else {
    tip += L" - signed out";
  }
  wcsncpy_s(g.tray.szTip, tip.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &g.tray);
}

void add_tray_icon(HWND window) {
  g.tray = {};
  g.tray.cbSize = sizeof(g.tray);
  g.tray.hWnd = window;
  g.tray.uID = 1;
  g.tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g.tray.uCallbackMessage = kTrayMessage;
  HICON smallIcon = static_cast<HICON>(
      LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconResource), IMAGE_ICON,
                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
  g.tray.hIcon = smallIcon ? smallIcon : LoadIconW(GetModuleHandleW(nullptr),
                                                   MAKEINTRESOURCEW(kIconResource));
  wcscpy_s(g.tray.szTip, kProductName);
  g.trayAdded = Shell_NotifyIconW(NIM_ADD, &g.tray) != FALSE;
  update_tray_tip();
}

void remove_tray_icon() {
  if (!g.trayAdded) return;
  Shell_NotifyIconW(NIM_DELETE, &g.tray);
  g.trayAdded = false;
}

void show_tray_menu(HWND window) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, IdMenuOpen, L"Open GNLink Host");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (g.signedIn ? 0 : MF_GRAYED), IdMenuChangeAccount,
              L"Change account");
  AppendMenuW(menu, MF_STRING | (g.signedIn ? 0 : MF_GRAYED), IdMenuSignOut, L"Sign out");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IdMenuExit, L"Exit");

  POINT cursor{};
  GetCursorPos(&cursor);
  // Required so the menu closes when the user clicks elsewhere.
  SetForegroundWindow(window);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
  PostMessageW(window, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

// ---------------------------------------------------------------- sign in

void apply_signed_in_ui(bool signedIn) {
  g.signedIn = signedIn;
  const int form = signedIn ? SW_HIDE : SW_SHOW;
  const int card = signedIn ? SW_SHOW : SW_HIDE;

  ShowWindow(g.hintLabel, form);
  ShowWindow(g.accountLabel, form);
  ShowWindow(g.accountEdit, form);
  ShowWindow(g.passwordLabel, form);
  ShowWindow(g.passwordEdit, form);
  ShowWindow(g.hostNameLabel, form);
  ShowWindow(g.hostNameEdit, form);
  ShowWindow(g.advancedToggle, form);
  const bool serverVisible = !signedIn && g.advancedOpen;
  ShowWindow(g.serverLabel, serverVisible ? SW_SHOW : SW_HIDE);
  ShowWindow(g.serverEdit, serverVisible ? SW_SHOW : SW_HIDE);
  ShowWindow(g.createAccountCheck, form);
  const bool creating =
      !signedIn && SendMessageW(g.createAccountCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
  ShowWindow(g.signupKeyLabel, creating ? SW_SHOW : SW_HIDE);
  ShowWindow(g.signupKeyEdit, creating ? SW_SHOW : SW_HIDE);
  ShowWindow(g.signInButton, form);

  ShowWindow(g.signedAccountLabel, card);
  ShowWindow(g.signedHostLabel, card);
  ShowWindow(g.statusBadge, card);
  ShowWindow(g.startWithWindowsCheck, card);
  // Signing out belonged in the window, not buried in the tray menu where nobody looks.
  ShowWindow(g.switchAccountButton, card);
  ShowWindow(g.signOutButton, card);
  ShowWindow(g.openLogButton, card);

  update_tray_tip();
  relayout();
}

void start_streaming() {
  if (g.uiPreview) return;
  g.streaming.Configure(widen(g.cache.directoryUrl), widen(g.cache.accountId),
                        widen(g.cache.hostName));
  g.streaming.Start();
}

void open_advanced_settings() {
  g.advancedOpen = true;
  SendMessageW(g.advancedToggle, BM_SETCHECK, BST_CHECKED, 0);
  ShowWindow(g.serverLabel, SW_SHOW);
  ShowWindow(g.serverEdit, SW_SHOW);
  relayout();
}

void perform_sign_in() {
  if (g.signInBusy.exchange(true)) return;

  const std::string url = narrow(window_text(g.serverEdit));
  const std::string account = narrow(window_text(g.accountEdit));
  const std::string password = narrow(window_text(g.passwordEdit));
  std::string hostName = narrow(window_text(g.hostNameEdit));
  if (hostName.empty()) hostName = narrow(default_host_name());

  if (url.empty()) {
    // The field lives behind the advanced toggle; asking for it while keeping it hidden
    // would send the user hunting.
    if (!g.advancedOpen) open_advanced_settings();
    set_status(L"Enter the server address under Advanced settings.");
    SetFocus(g.serverEdit);
    g.signInBusy.store(false);
    return;
  }
  if (account.empty() || password.empty()) {
    set_status(L"Enter your ID and password.");
    g.signInBusy.store(false);
    return;
  }

  const bool creating =
      SendMessageW(g.createAccountCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
  const std::string signupKey = creating ? narrow(window_text(g.signupKeyEdit)) : std::string();
  if (creating && signupKey.empty()) {
    set_status(L"Enter the signup key from whoever set up the server.");
    g.signInBusy.store(false);
    return;
  }

  // Remember where they were signing in to before knowing whether it worked; retyping the
  // server address after every failed attempt is needless. The token goes with the account it
  // was issued for, so it is dropped when either the account or the server changes.
  if (g.cache.accountId != account || g.cache.directoryUrl != url) {
    g.cache.hostToken.clear();
    g.cache.hostId.clear();
  }
  g.cache.directoryUrl = url;
  g.cache.accountId = account;
  g.cache.hostName = hostName;
  g.cache.machineId = directory::machine_id();
  (void)directory::save_host_cache(g.cachePath, g.cache);

  set_status(creating ? L"Creating the account..." : L"Signing in...");
  EnableWindow(g.signInButton, FALSE);

  // Off the UI thread: this makes a network round trip and the window must stay responsive.
  std::thread([url, account, password, hostName, creating, signupKey]() {
    std::string hostId, token, error;
    if (creating && !directory::create_account(url, account, password, signupKey, &error)) {
      PostMessageW(g.window, WM_APP + 2, 0,
                   reinterpret_cast<LPARAM>(new std::wstring(widen(error))));
      return;
    }
    const bool ok = directory::register_host(url, account, password, hostName,
                                             directory::machine_id(), &hostId, &token, &error);
    if (ok) {
      g.cache.directoryUrl = url;
      g.cache.accountId = account;
      g.cache.machineId = directory::machine_id();
      g.cache.hostName = hostName;
      g.cache.hostId = hostId;
      g.cache.hostToken = token;
      // Only the token is written; the password never reaches disk.
      (void)directory::save_host_cache(g.cachePath, g.cache);
    }
    // Hand the result back to the UI thread rather than touching windows from here.
    PostMessageW(g.window, WM_APP + 2, ok ? 1 : 0,
                 reinterpret_cast<LPARAM>(new std::wstring(widen(error))));
  }).detach();
}

void sign_out(bool keepAccount) {
  g.streaming.Stop();
  g.cache.hostToken.clear();
  g.cache.hostId.clear();
  (void)directory::save_host_cache(g.cachePath, g.cache);
  apply_signed_in_ui(false);
  SetWindowTextW(g.passwordEdit, L"");
  if (!keepAccount) SetWindowTextW(g.accountEdit, L"");
  set_status(L"Signed out.");
}

// ---------------------------------------------------------------- window

void create_fonts() {
  if (g.font) DeleteObject(g.font);
  if (g.titleFont) DeleteObject(g.titleFont);
  g.font = CreateFontW(-sc(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  g.titleFont = CreateFontW(-sc(21), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void apply_fonts() {
  const HWND bodyControls[] = {
      g.hintLabel,       g.accountLabel,       g.accountEdit,      g.passwordLabel,
      g.passwordEdit,    g.hostNameLabel,      g.hostNameEdit,     g.advancedToggle,
      g.serverLabel,     g.serverEdit,         g.createAccountCheck, g.signupKeyLabel,
      g.signupKeyEdit,   g.signInButton,       g.signedAccountLabel, g.signedHostLabel,
      g.statusBadge,     g.startWithWindowsCheck, g.switchAccountButton, g.signOutButton,
      g.openLogButton,   g.statusLabel,
  };
  for (HWND control : bodyControls) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  }
  if (g.titleLabel) {
    SendMessageW(g.titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g.titleFont), TRUE);
  }
}

HWND make_label(HWND parent, int id, const wchar_t* text) {
  HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr,
                                 nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  return control;
}

HWND make_edit(HWND parent, int id, DWORD extraStyle) {
  HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extraStyle,
                                 0, 0, 10, 10, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr,
                                 nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  return control;
}

HWND make_button(HWND parent, int id, const wchar_t* text, DWORD style) {
  HWND control = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | style, 0, 0, 10, 10,
                                 parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 nullptr, nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  return control;
}

// Creation order is tab order; the layout functions only assign positions.
void build_controls(HWND window) {
  g.titleLabel = make_label(window, IdTitle, kProductName);
  SendMessageW(g.titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(g.titleFont), TRUE);
  g.hintLabel = make_label(window, IdHint,
                           L"Sign in once. This PC then appears in the phone app "
                           L"wherever you are.");

  g.accountLabel = make_label(window, IdAccountLabel, L"ID");
  g.accountEdit = make_edit(window, IdAccount, 0);
  g.passwordLabel = make_label(window, IdPasswordLabel, L"Password");
  g.passwordEdit = make_edit(window, IdPassword, ES_PASSWORD);
  g.hostNameLabel = make_label(window, IdHostNameLabel, L"This PC's name");
  g.hostNameEdit = make_edit(window, IdHostName, 0);

  // The server address is real configuration, but 99% of sign-ins reuse the cached one, so it
  // hides behind a toggle instead of being the first thing on the form.
  g.advancedToggle =
      make_button(window, IdAdvanced, L"Advanced settings (server address)",
                  WS_VISIBLE | BS_AUTOCHECKBOX);
  g.serverLabel = make_label(window, IdServerLabel, L"Server");
  g.serverEdit = make_edit(window, IdServer, 0);
  ShowWindow(g.serverLabel, SW_HIDE);
  ShowWindow(g.serverEdit, SW_HIDE);

  // Without this the only accounts that work are ones somebody created on the server by hand,
  // and picking your own id gets rejected as "not correct", which reads like a typo.
  g.createAccountCheck =
      make_button(window, IdCreateAccount, L"I don't have an account yet - create one",
                  WS_VISIBLE | BS_AUTOCHECKBOX);
  g.signupKeyLabel = make_label(window, IdSignupKeyLabel, L"Signup key");
  g.signupKeyEdit = make_edit(window, IdSignupKey, 0);
  ShowWindow(g.signupKeyLabel, SW_HIDE);
  ShowWindow(g.signupKeyEdit, SW_HIDE);

  g.signInButton = make_button(window, IdSignIn, L"Sign in", WS_VISIBLE | BS_DEFPUSHBUTTON);

  g.signedAccountLabel = make_label(window, IdSignedAccount, L"");
  g.signedHostLabel = make_label(window, IdSignedHost, L"");
  g.statusBadge = CreateWindowExW(0, L"STATIC", L"STARTING",
                                  WS_CHILD | SS_CENTER | SS_CENTERIMAGE, 0, 0, 10, 10, window,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdBadge)), nullptr,
                                  nullptr);
  SendMessageW(g.statusBadge, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);

  g.startWithWindowsCheck = make_button(
      // Says "sign in" rather than "starts": this is an HKCU\Run entry, so it fires when this
      // user logs on, not at boot. Promising boot-time start would be a lie -- reaching a PC
      // sitting at the lock screen needs a service, which does not exist yet.
      window, IdStartWithWindows, L"Start automatically when I sign in to Windows",
      BS_AUTOCHECKBOX);
  SendMessageW(g.startWithWindowsCheck, BM_SETCHECK,
               autostart_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
  g.switchAccountButton = make_button(window, IdSwitchAccount, L"Change account", 0);
  g.signOutButton = make_button(window, IdSignOut, L"Sign out", 0);
  g.openLogButton = make_button(window, IdOpenLog, L"Open log", 0);

  g.statusLabel = make_label(window, IdStatus, L"");
}

// ---------------------------------------------------------------- layout

void place(HWND control, int x, int y, int w, int h) {
  if (control) MoveWindow(control, x, y, w, h, TRUE);
}

/** Positions the sign-in form and returns the required client height. */
int layout_signed_out() {
  const int margin = sc(18);
  const int labelW = sc(110);
  const int fieldX = margin + labelW;
  const int fieldW = sc(300);
  const int rowH = sc(26);
  const int labelH = sc(20);
  const int contentW = fieldX + fieldW - margin;
  int y = sc(14);

  place(g.titleLabel, margin, y, contentW, sc(30));
  y += sc(36);
  // Three lines: the account-creation explanation is the longest thing shown here.
  place(g.hintLabel, margin, y, contentW, sc(58));
  y += sc(66);

  place(g.accountLabel, margin, y + sc(4), labelW, labelH);
  place(g.accountEdit, fieldX, y, fieldW, rowH);
  y += rowH + sc(10);
  place(g.passwordLabel, margin, y + sc(4), labelW, labelH);
  place(g.passwordEdit, fieldX, y, fieldW, rowH);
  y += rowH + sc(10);
  place(g.hostNameLabel, margin, y + sc(4), labelW, labelH);
  place(g.hostNameEdit, fieldX, y, fieldW, rowH);
  y += rowH + sc(10);

  place(g.advancedToggle, margin, y, contentW, sc(22));
  y += sc(28);
  if (g.advancedOpen) {
    place(g.serverLabel, margin, y + sc(4), labelW, labelH);
    place(g.serverEdit, fieldX, y, fieldW, rowH);
    y += rowH + sc(10);
  }

  place(g.createAccountCheck, margin, y, contentW, sc(22));
  y += sc(28);
  const bool creating = SendMessageW(g.createAccountCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (creating) {
    place(g.signupKeyLabel, margin, y + sc(4), labelW, labelH);
    place(g.signupKeyEdit, fieldX, y, fieldW, rowH);
    y += rowH + sc(10);
  }
  y += sc(6);

  place(g.signInButton, fieldX, y, sc(150), sc(30));
  y += sc(40);

  // Three lines minimum: sign-in errors from the server are sentences, not words.
  place(g.statusLabel, margin, y, contentW, sc(62));
  y += sc(70);

  return y;
}

/** Positions the signed-in status card and returns the required client height. */
int layout_signed_in() {
  const int margin = sc(18);
  const int contentW = sc(470) - margin * 2;
  int y = sc(14);

  place(g.titleLabel, margin, y, contentW, sc(30));
  y += sc(40);

  place(g.signedAccountLabel, margin, y, contentW, sc(22));
  y += sc(26);
  place(g.signedHostLabel, margin, y, contentW, sc(22));
  y += sc(30);

  place(g.statusBadge, margin, y, sc(150), sc(26));
  y += sc(34);

  place(g.statusLabel, margin, y, contentW, sc(62));
  y += sc(70);

  place(g.startWithWindowsCheck, margin, y, contentW, sc(24));
  y += sc(34);

  place(g.switchAccountButton, margin, y, sc(140), sc(30));
  place(g.signOutButton, margin + sc(150), y, sc(110), sc(30));
  place(g.openLogButton, margin + sc(270), y, sc(110), sc(30));
  y += sc(44);

  return y;
}

void relayout() {
  if (!g.window) return;
  const int clientH = g.signedIn ? layout_signed_in() : layout_signed_out();
  RECT desired{0, 0, sc(470), clientH};
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRectExForDpi(&desired, style, FALSE, 0, g.dpi);
  SetWindowPos(g.window, nullptr, 0, 0, desired.right - desired.left,
               desired.bottom - desired.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  InvalidateRect(g.window, nullptr, TRUE);
}

// ---------------------------------------------------------------- status card

bool needs_sign_in_again(const std::string& directoryStatus) {
  return directoryStatus.find("token rejected") != std::string::npos ||
         directoryStatus.find("registration needs") != std::string::npos;
}

COLORREF badge_color(BadgeState state) {
  switch (state) {
    case BadgeState::Starting: return RGB(96, 96, 96);
    case BadgeState::Reachable: return RGB(22, 128, 58);
    case BadgeState::SignInAgain: return RGB(178, 108, 0);
    case BadgeState::Error: return RGB(178, 32, 32);
  }
  return RGB(96, 96, 96);
}

HBRUSH badge_brush(BadgeState state) {
  const int index = static_cast<int>(state);
  if (!g.badgeBrushes[index]) g.badgeBrushes[index] = CreateSolidBrush(badge_color(state));
  return g.badgeBrushes[index];
}

void set_badge(BadgeState state, const wchar_t* text) {
  // The words carry the state on their own; the color is reinforcement, so high-contrast
  // themes and monochrome screenshots still read correctly.
  if (g.badgeState != state || window_text(g.statusBadge) != text) {
    g.badgeState = state;
    SetWindowTextW(g.statusBadge, text);
    InvalidateRect(g.statusBadge, nullptr, TRUE);
  }
}

void refresh_status_text() {
  if (!g.signedIn) return;
  const std::string directoryStatus = g.streaming.DirectoryStatus();

  SetWindowTextW(g.signedAccountLabel, (L"Account:  " + widen(g.cache.accountId)).c_str());
  SetWindowTextW(g.signedHostLabel, (L"This PC:  " + widen(g.cache.hostName)).c_str());

  std::wstring detail;
  if (g.uiPreview) {
    set_badge(BadgeState::Starting, L"STARTING");
    detail = L"UI preview - the streaming host is not running.";
  } else if (!g.streaming.ChildAlive()) {
    set_badge(BadgeState::Starting, L"STARTING");
    detail = L"The streaming host is starting...";
  } else if (directoryStatus.rfind("online", 0) == 0) {
    set_badge(BadgeState::Reachable, L"REACHABLE");
    detail = L"This PC is reachable from your phone.";
  } else if (needs_sign_in_again(directoryStatus)) {
    // Saying "running" here would be a lie the user only discovers on the phone.
    set_badge(BadgeState::SignInAgain, L"SIGN IN AGAIN");
    detail = L"The server signed this PC out.\nUse Change account to sign in again.";
  } else {
    set_badge(BadgeState::Error, L"NOT REACHABLE");
    detail = L"Directory: " + widen(directoryStatus);
  }
  const uint32_t restarts = g.streaming.Restarts();
  if (restarts > 0) detail += L"\nStreaming host restarts: " + std::to_wstring(restarts);
  set_status(detail);
}

void open_log_file() {
  const std::wstring path = log_file_path();
  if (path.empty()) return;
  // Ensure it exists so the shell opens an editor instead of erroring.
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      g.window = window;
      g.dpi = GetDpiForWindow(window);
      create_fonts();
      build_controls(window);
      add_tray_icon(window);
      SetTimer(window, kStatusTimer, kStatusIntervalMs, nullptr);
      return 0;

    case WM_DPICHANGED: {
      g.dpi = HIWORD(wParam);
      create_fonts();
      apply_fonts();
      // Take the suggested position, then let relayout set the size the content needs at the
      // new DPI -- this window is fixed-size, so the suggested size is not authoritative.
      const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
      SetWindowPos(window, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      relayout();
      return 0;
    }

    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      if (reinterpret_cast<HWND>(lParam) == g.statusBadge) {
        SetBkColor(dc, badge_color(g.badgeState));
        SetTextColor(dc, RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(badge_brush(g.badgeState));
      }
      // Labels and checkboxes otherwise paint on the grey dialog color, which reads as
      // stripes against this window's white background.
      SetBkMode(dc, TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }

    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      if (id == IdSignIn) {
        perform_sign_in();
        return 0;
      }
      if (id == IdStartWithWindows) {
        const bool checked =
            SendMessageW(g.startWithWindowsCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        set_autostart(checked);
        return 0;
      }
      if (id == IdAdvanced) {
        g.advancedOpen = SendMessageW(g.advancedToggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
        ShowWindow(g.serverLabel, g.advancedOpen ? SW_SHOW : SW_HIDE);
        ShowWindow(g.serverEdit, g.advancedOpen ? SW_SHOW : SW_HIDE);
        relayout();
        return 0;
      }
      if (id == IdCreateAccount) {
        const bool checked =
            SendMessageW(g.createAccountCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        ShowWindow(g.signupKeyLabel, checked ? SW_SHOW : SW_HIDE);
        ShowWindow(g.signupKeyEdit, checked ? SW_SHOW : SW_HIDE);
        SetWindowTextW(g.signInButton, checked ? L"Create and sign in" : L"Sign in");
        // Spelling out which field is which: "Signup key" on its own reads like somewhere to
        // put the account details, and there is no second chance to explain it.
        SetWindowTextW(g.hintLabel,
                       checked ? L"The ID and password above become your new account - pick "
                                 L"anything you like. The signup key is a separate password "
                                 L"for the server itself."
                               : L"Sign in once. This PC then appears in the phone app "
                                 L"wherever you are.");
        set_status(checked ? L"" : L"Sign in to make this PC reachable.");
        relayout();
        return 0;
      }
      if (id == IdOpenLog) {
        open_log_file();
        return 0;
      }
      if (id == IdMenuOpen) {
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        return 0;
      }
      if (id == IdSwitchAccount || id == IdMenuChangeAccount) {
        sign_out(/*keepAccount=*/true);
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        SetFocus(g.passwordEdit);
        return 0;
      }
      if (id == IdSignOut || id == IdMenuSignOut) {
        sign_out(/*keepAccount=*/false);
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        return 0;
      }
      if (id == IdMenuExit) {
        DestroyWindow(window);
        return 0;
      }
      break;
    }

    case WM_APP + 2: {
      // Sign-in result, marshalled back from the worker thread.
      std::wstring* error = reinterpret_cast<std::wstring*>(lParam);
      g.signInBusy.store(false);
      EnableWindow(g.signInButton, TRUE);
      if (wParam == 1) {
        SetWindowTextW(g.passwordEdit, L"");
        apply_signed_in_ui(true);
        start_streaming();
        refresh_status_text();
      } else {
        set_status(error && !error->empty() ? *error : L"Sign in failed.");
      }
      delete error;
      return 0;
    }

    case kTrayMessage:
      if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
      } else if (LOWORD(lParam) == WM_RBUTTONUP) {
        show_tray_menu(window);
      }
      return 0;

    case WM_TIMER:
      if (wParam == kStatusTimer) {
        refresh_status_text();
        update_tray_tip();
      }
      return 0;

    // Closing the window means "get out of my way", not "stop sharing this PC"; the tray icon
    // is what actually represents the app running.
    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      return 0;

    case WM_DESTROY:
      KillTimer(window, kStatusTimer);
      g.streaming.Stop();
      remove_tray_icon();
      for (HBRUSH& brush : g.badgeBrushes) {
        if (brush) {
          DeleteObject(brush);
          brush = nullptr;
        }
      }
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kWindowClass;
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kIconResource));
  wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kIconResource),
                                             IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), 0));
  RegisterClassExW(&wc);

  HWND window = CreateWindowExW(0, kWindowClass, kProductName,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr,
                                instance, nullptr);
  if (!window) return 1;

  // Layout verification without touching the cache or launching the streaming child:
  //   --ui-preview            the sign-in form with sample data
  //   --ui-preview=signedin   the status card with sample data
  const bool uiPreview = commandLine && wcsstr(commandLine, L"--ui-preview") != nullptr;
  const bool previewSignedIn = commandLine && wcsstr(commandLine, L"--ui-preview=signedin") != nullptr;

  bool haveToken = false;
  if (uiPreview) {
    g.uiPreview = true;
    g.cache.accountId = "preview-account";
    g.cache.hostName = "PREVIEW-PC";
    g.cache.directoryUrl = "http://127.0.0.1:8080";
    haveToken = previewSignedIn;
  } else {
    g.cachePath = directory::default_host_cache_path();
    haveToken = directory::load_host_cache(g.cachePath, &g.cache);
  }
  SetWindowTextW(g.serverEdit, widen(g.cache.directoryUrl).c_str());
  SetWindowTextW(g.accountEdit, widen(g.cache.accountId).c_str());
  SetWindowTextW(g.hostNameEdit,
                 g.cache.hostName.empty() ? default_host_name().c_str()
                                          : widen(g.cache.hostName).c_str());

  // Launched by the autostart entry: come up in the tray rather than in the user's face.
  const bool startHidden = commandLine && wcsstr(commandLine, L"--tray") != nullptr;

  if (haveToken) {
    apply_signed_in_ui(true);
    start_streaming();
    refresh_status_text();
  } else {
    apply_signed_in_ui(false);
    set_status(L"Sign in to make this PC reachable.");
  }
  ShowWindow(window, (haveToken && startHidden && !uiPreview) ? SW_HIDE : SW_SHOW);
  UpdateWindow(window);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(window, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  if (g.font) DeleteObject(g.font);
  if (g.titleFont) DeleteObject(g.titleFont);
  WSACleanup();
  return 0;
}
