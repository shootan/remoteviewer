// remote60 host application.
//
// The streaming host is a console program driven by flags, which is fine for development and
// wrong for the thing a person installs on their PC. This wraps it: sign in once, then it sits
// in the tray, keeps the streaming process alive, and shows whether the PC is reachable.
//
// It deliberately does not stream anything itself. Capture, encode and input injection stay in
// remote60_native_video_host_poc.exe, launched as a child process, so a crash there cannot take
// the sign-in state with it and the two can be developed independently.

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
  void ReadChildOutput(HANDLE readEnd) {
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
    const std::wstring exe = executable_dir() + L"\\remote60_native_video_host_poc.exe";
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

      if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                          CREATE_NO_WINDOW, nullptr, executable_dir().c_str(), &si, &pi)) {
        if (readEnd) CloseHandle(readEnd);
        if (writeEnd) CloseHandle(writeEnd);
        childAlive_.store(false, std::memory_order_relaxed);
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
      if (readEnd) reader = std::thread([this, readEnd] { ReadChildOutput(readEnd); });

      {
        std::lock_guard<std::mutex> lock(mu_);
        child_ = pi;
      }
      childAlive_.store(true, std::memory_order_relaxed);
      WaitForSingleObject(pi.hProcess, INFINITE);
      childAlive_.store(false, std::memory_order_relaxed);
      if (reader.joinable()) reader.join();
      if (readEnd) CloseHandle(readEnd);
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

struct AppState {
  HWND window = nullptr;
  HWND serverEdit = nullptr;
  HWND accountEdit = nullptr;
  HWND passwordEdit = nullptr;
  HWND hostNameEdit = nullptr;
  HWND signInButton = nullptr;
  HWND statusLabel = nullptr;
  HWND hintLabel = nullptr;
  HWND startWithWindowsCheck = nullptr;
  HWND createAccountCheck = nullptr;
  HWND signupKeyLabel = nullptr;
  HWND signupKeyEdit = nullptr;
  HWND signOutButton = nullptr;
  HWND switchAccountButton = nullptr;
  HFONT font = nullptr;
  NOTIFYICONDATAW tray{};
  bool trayAdded = false;
  bool signedIn = false;
  std::string cachePath;
  directory::HostCache cache;
  StreamingHostProcess streaming;
  std::atomic<bool> signInBusy{false};
};

AppState g;

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
  std::wstring tip = L"remote60";
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
  g.tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(g.tray.szTip, L"remote60");
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
  AppendMenuW(menu, MF_STRING, IdMenuOpen, L"Open remote60");
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
  const int hide = signedIn ? SW_HIDE : SW_SHOW;
  ShowWindow(g.serverEdit, hide);
  ShowWindow(g.accountEdit, hide);
  ShowWindow(g.passwordEdit, hide);
  ShowWindow(g.hostNameEdit, hide);
  ShowWindow(g.signInButton, hide);
  ShowWindow(g.hintLabel, hide);
  ShowWindow(g.createAccountCheck, hide);
  const bool creating =
      !signedIn && SendMessageW(g.createAccountCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
  ShowWindow(g.signupKeyLabel, creating ? SW_SHOW : SW_HIDE);
  ShowWindow(g.signupKeyEdit, creating ? SW_SHOW : SW_HIDE);
  ShowWindow(g.startWithWindowsCheck, signedIn ? SW_SHOW : SW_HIDE);
  // Signing out belonged in the window, not buried in the tray menu where nobody looks.
  ShowWindow(g.signOutButton, signedIn ? SW_SHOW : SW_HIDE);
  ShowWindow(g.switchAccountButton, signedIn ? SW_SHOW : SW_HIDE);
  update_tray_tip();
}

void start_streaming() {
  g.streaming.Configure(widen(g.cache.directoryUrl), widen(g.cache.accountId),
                        widen(g.cache.hostName));
  g.streaming.Start();
}

void perform_sign_in() {
  if (g.signInBusy.exchange(true)) return;

  const std::string url = narrow(window_text(g.serverEdit));
  const std::string account = narrow(window_text(g.accountEdit));
  const std::string password = narrow(window_text(g.passwordEdit));
  std::string hostName = narrow(window_text(g.hostNameEdit));
  if (hostName.empty()) hostName = narrow(default_host_name());

  if (url.empty()) {
    set_status(L"Enter the server address.");
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

HWND make_label(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
  HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr,
                                 nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  return control;
}

HWND make_edit(HWND parent, int id, int x, int y, int w, int h, DWORD extraStyle) {
  HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extraStyle,
                                 x, y, w, h, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr,
                                 nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  return control;
}

void build_controls(HWND window) {
  const int margin = 18;
  const int labelW = 110;
  const int fieldX = margin + labelW;
  const int fieldW = 300;
  const int rowH = 26;
  int y = margin;

  make_label(window, 0, L"Server", margin, y + 4, labelW, 20);
  g.serverEdit = make_edit(window, IdServer, fieldX, y, fieldW, rowH, 0);
  y += rowH + 10;

  make_label(window, 0, L"ID", margin, y + 4, labelW, 20);
  g.accountEdit = make_edit(window, IdAccount, fieldX, y, fieldW, rowH, 0);
  y += rowH + 10;

  make_label(window, 0, L"Password", margin, y + 4, labelW, 20);
  g.passwordEdit = make_edit(window, IdPassword, fieldX, y, fieldW, rowH, ES_PASSWORD);
  y += rowH + 10;

  make_label(window, 0, L"This PC's name", margin, y + 4, labelW, 20);
  g.hostNameEdit = make_edit(window, IdHostName, fieldX, y, fieldW, rowH, 0);
  y += rowH + 8;

  // Without this the only accounts that work are ones somebody created on the server by hand,
  // and picking your own id gets rejected as "not correct", which reads like a typo.
  g.createAccountCheck = CreateWindowExW(
      0, L"BUTTON", L"I don't have an account yet - create one",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, fieldX, y, fieldW, 22, window,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdCreateAccount)), nullptr, nullptr);
  SendMessageW(g.createAccountCheck, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  y += 26;

  g.signupKeyLabel =
      make_label(window, IdSignupKeyLabel, L"Signup key", margin, y + 4, labelW, 20);
  g.signupKeyEdit = make_edit(window, IdSignupKey, fieldX, y, fieldW, rowH, 0);
  ShowWindow(g.signupKeyLabel, SW_HIDE);
  ShowWindow(g.signupKeyEdit, SW_HIDE);
  y += rowH + 14;

  g.signInButton = CreateWindowExW(0, L"BUTTON", L"Sign in",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, fieldX,
                                   y, 120, 30, window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdSignIn)),
                                   nullptr, nullptr);
  SendMessageW(g.signInButton, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);

  // Occupy the same row as Sign in: only one of the two states is ever on screen.
  g.switchAccountButton = CreateWindowExW(
      0, L"BUTTON", L"Switch account", WS_CHILD | WS_TABSTOP, fieldX, y, 140, 30, window,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdSwitchAccount)), nullptr, nullptr);
  SendMessageW(g.switchAccountButton, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  g.signOutButton = CreateWindowExW(
      0, L"BUTTON", L"Sign out", WS_CHILD | WS_TABSTOP, fieldX + 150, y, 120, 30, window,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdSignOut)), nullptr, nullptr);
  SendMessageW(g.signOutButton, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  y += 40;

  g.statusLabel = make_label(window, IdStatus, L"", margin, y, fieldX + fieldW - margin, 40);
  y += 44;

  // Three lines: the account-creation explanation is the longest thing shown here.
  g.hintLabel = make_label(window, IdHint,
                           L"Sign in once. This PC then appears in the phone app "
                           L"wherever you are.",
                           margin, y, fieldX + fieldW - margin, 58);

  g.startWithWindowsCheck = CreateWindowExW(
      0, L"BUTTON", L"Start automatically when Windows starts",
      WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, margin, y, fieldX + fieldW - margin, 24, window,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdStartWithWindows)), nullptr, nullptr);
  SendMessageW(g.startWithWindowsCheck, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  SendMessageW(g.startWithWindowsCheck, BM_SETCHECK, autostart_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool needs_sign_in_again(const std::string& directoryStatus) {
  return directoryStatus.find("token rejected") != std::string::npos ||
         directoryStatus.find("registration needs") != std::string::npos;
}

void refresh_status_text() {
  if (!g.signedIn) return;
  const std::string directoryStatus = g.streaming.DirectoryStatus();

  std::wstring text = L"Signed in as " + widen(g.cache.accountId) + L"\n";
  text += L"This PC: " + widen(g.cache.hostName) + L"\n";
  if (!g.streaming.ChildAlive()) {
    text += L"Status: starting...";
  } else if (directoryStatus.rfind("online", 0) == 0) {
    text += L"Status: reachable from your phone";
  } else if (needs_sign_in_again(directoryStatus)) {
    // Saying "running" here would be a lie the user only discovers on the phone.
    text += L"Status: signed out by the server - use Change account to sign in again";
  } else {
    text += L"Status: " + widen(directoryStatus);
  }
  const uint32_t restarts = g.streaming.Restarts();
  if (restarts > 0) text += L"  (restarts: " + std::to_wstring(restarts) + L")";
  set_status(text);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      build_controls(window);
      add_tray_icon(window);
      SetTimer(window, kStatusTimer, kStatusIntervalMs, nullptr);
      return 0;

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

  g.font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kWindowClass;
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  RegisterClassExW(&wc);

  // Tall enough for the sign-in form with the account-creation row expanded.
  RECT desired{0, 0, 470, 400};
  AdjustWindowRect(&desired, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
  HWND window = CreateWindowExW(0, kWindowClass, L"remote60",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, desired.right - desired.left,
                                desired.bottom - desired.top, nullptr, nullptr, instance, nullptr);
  if (!window) return 1;
  g.window = window;

  g.cachePath = directory::default_host_cache_path();
  const bool haveToken = directory::load_host_cache(g.cachePath, &g.cache);
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
  ShowWindow(window, (haveToken && startHidden) ? SW_HIDE : SW_SHOW);
  UpdateWindow(window);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(window, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  if (g.font) DeleteObject(g.font);
  WSACleanup();
  return 0;
}
