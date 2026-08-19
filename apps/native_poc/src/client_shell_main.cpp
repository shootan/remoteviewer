// The window a person actually opens.
//
// It signs in, lists the PCs on the account and starts a session on the one they pick. The
// session itself runs in GNLinkViewer.exe as a child process -- the same arrangement the host
// side already uses, where GNLinkHost supervises GNLinkStream, and for the same reasons: the
// video path is four thousand lines of window loop and global state that would have to be
// restructured to embed, and a crash in it should not take the interface with it.
//
// The interface is a page in a WebView2. Drawing it in Win32 would mean owning every pixel, font
// and DPI case by hand for a result that still looks like 1998. Nothing about the video crosses
// into the page: it arrives on a UDP socket, decodes through Media Foundation and is drawn by
// D3D11, in the other process, untouched.

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Before windows.h, always: winsock2 redefines sockaddr and friends, and including them the
// other way round produces forty errors inside the SDK that name nothing about this file.
#include "native_socket.hpp"

#include <windows.h>
#include <shlobj.h>
#include <wrl.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "WebView2.h"
#include "client_shell_bridge.hpp"
#include "directory_session_client.hpp"
#include "json_profile.hpp"
#include "product_version.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using namespace remote60::native_poc;

namespace {

constexpr wchar_t kWindowClass[] = L"GNLinkClientShell";

ComPtr<ICoreWebView2Controller> gController;
ComPtr<ICoreWebView2> gWebView;
HWND gWindow = nullptr;

// Guards everything the worker threads write and the UI thread reads.
std::mutex gStateMu;
std::string gServerUrl;
std::string gAccountId;
std::string gSessionToken;
// Defaults chosen for a desktop rather than a phone: this is usually wired or on home Wi-Fi,
// where the picture is worth more than the bytes. The relay is the exception, and the interface
// says so where the number is set.
ShellRuntimeSettings gSettings{12000, 60, 0};

std::wstring widen(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring out(size > 0 ? size - 1 : 0, L'\0');
  if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
  return out;
}

std::string narrow(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(size > 0 ? size - 1 : 0, '\0');
  if (size > 0) {
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
  }
  return out;
}

std::wstring executable_dir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring full(path);
  const size_t slash = full.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : full.substr(0, slash);
}

/**
 * Where the server address and account id are remembered between runs.
 *
 * The password is not among them. Remembering it would save one field and hand anyone with the
 * user's profile a working login to every PC on the account.
 */
std::wstring settings_path() {
  wchar_t* roaming = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &roaming)) && roaming) {
    dir = std::wstring(roaming) + L"\\GNLink";
    CoTaskMemFree(roaming);
  } else {
    dir = executable_dir();
  }
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\client.txt";
}

void save_settings(const std::string& server, const std::string& accountId,
                   const ShellRuntimeSettings& settings) {
  std::ofstream file(settings_path(), std::ios::trunc);
  if (!file) return;
  // One value per line, in a fixed order. A settings file this small does not need a format.
  file << server << "\n"
       << accountId << "\n"
       << settings.bitrateKbps << "\n"
       << settings.fps << "\n"
       << settings.monitorId << "\n";
}

void load_settings(std::string* server, std::string* accountId, ShellRuntimeSettings* settings) {
  std::ifstream file(settings_path());
  if (!file) return;
  std::getline(file, *server);
  std::getline(file, *accountId);
  // A file touched by a text editor comes back with a byte order mark, and it would ride along
  // inside the url -- producing a message the page cannot parse and a screen that stays blank
  // for no visible reason.
  if (server->rfind("\xEF\xBB\xBF", 0) == 0) server->erase(0, 3);
  // Written on Windows, so a stray carriage return is likelier than not.
  for (std::string* line : {server, accountId}) {
    while (!line->empty() && (line->back() == '\r' || line->back() == '\n')) line->pop_back();
  }
  std::string line;
  auto read_u32 = [&](uint32_t* target) {
    if (!std::getline(file, line) || line.empty()) return;
    // A file edited by hand, or written by an older build, must not take the app down.
    try {
      *target = static_cast<uint32_t>(std::stoul(line));
    } catch (...) {
    }
  };
  if (settings) {
    read_u32(&settings->bitrateKbps);
    read_u32(&settings->fps);
    read_u32(&settings->monitorId);
  }
}

/**
 * A line in the client's log.
 *
 * The window can only show one short sentence, and when signing in fails that sentence is
 * whatever the server said -- which does not distinguish "the address is wrong" from "the
 * account is". This file does. It records what was attempted and what came back; never the
 * password, and never the session token, which is as good as one until it expires.
 */
void log_line(const std::string& text) {
  static std::mutex logMu;
  std::lock_guard<std::mutex> lock(logMu);
  std::wstring path = settings_path();
  const size_t slash = path.find_last_of(L'\\');
  path = (slash == std::wstring::npos ? L"." : path.substr(0, slash)) + L"\\client.log";

  std::ofstream file(path, std::ios::app);
  if (!file) return;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  char stamp[32]{};
  std::snprintf(stamp, sizeof(stamp), "%02d-%02d %02d:%02d:%02d ", now.wMonth, now.wDay,
                now.wHour, now.wMinute, now.wSecond);
  file << stamp << text << "\n";
}

/** Pushes one JSON message into the page. Safe to call from any thread. */
void post_to_page(const std::string& json) {
  if (!gWindow) return;
  // Marshalled onto the UI thread: WebView2 is apartment-threaded and calling it from a worker
  // fails in ways that look like the message was simply ignored.
  auto* payload = new std::string(json);
  PostMessageW(gWindow, WM_APP + 1, 0, reinterpret_cast<LPARAM>(payload));
}

void post_status(const std::string& state, const std::string& detail) {
  post_to_page(shell_status_json(state, detail));
}

/** Signing in and listing hosts both talk to the network, so they never run on the UI thread. */
void begin_login(std::string server, std::string accountId, std::string password) {
  log_line("login attempt server=" + server + " account=" + accountId);
  std::thread([server, accountId, password]() {
    std::string error;
    std::string token;
    if (!directory_login(server, accountId, password, &token, &error)) {
      log_line("login failed: " + error);
      post_status("error", error);
      return;
    }
    log_line("login ok");
    std::vector<DirectoryHostEntry> hosts;
    if (!directory_list_hosts(server, token, &hosts, &error)) {
      log_line("hosts failed: " + error);
      post_status("error", error);
      return;
    }
    log_line("hosts ok count=" + std::to_string(hosts.size()));
    {
      std::lock_guard<std::mutex> lock(gStateMu);
      gServerUrl = server;
      gAccountId = accountId;
      gSessionToken = token;
    }
    {
      std::lock_guard<std::mutex> lock(gStateMu);
      save_settings(server, accountId, gSettings);
    }

    std::string message = shell_hosts_json(hosts);
    // The page shows which account it is listing, so it travels with the list.
    const std::string suffix = ",\"accountId\":\"" + accountId + "\"}";
    message = message.substr(0, message.size() - 1) + suffix;
    post_to_page(message);
  }).detach();
}

void begin_refresh_hosts() {
  std::string server;
  std::string accountId;
  std::string token;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    server = gServerUrl;
    accountId = gAccountId;
    token = gSessionToken;
  }
  if (token.empty()) return;

  std::thread([server, accountId, token]() {
    std::string error;
    std::vector<DirectoryHostEntry> hosts;
    if (!directory_list_hosts(server, token, &hosts, &error)) {
      // A session that expired is the ordinary reason, and saying so beats an error the user
      // cannot act on.
      post_status("error", error + " — 다시 로그인해 주세요");
      return;
    }
    std::string message = shell_hosts_json(hosts);
    message = message.substr(0, message.size() - 1) + ",\"accountId\":\"" + accountId + "\"}";
    post_to_page(message);
  }).detach();
}

/**
 * Starts the session on the chosen PC.
 *
 * The session token is passed rather than the password: a command line can be read by any
 * process that cares to look, and a token expires where a password does not.
 */
void begin_session(const ShellConnectRequest& request) {
  std::string server;
  std::string token;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    server = gServerUrl;
    token = gSessionToken;
  }
  if (token.empty()) {
    post_status("error", "로그인이 필요합니다");
    return;
  }

  const std::wstring exe = executable_dir() + L"\\GNLinkViewer.exe";
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    post_status("error", "GNLinkViewer.exe 를 찾을 수 없습니다");
    return;
  }

  ShellRuntimeSettings settings;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    settings = gSettings;
  }

  std::wstringstream command;
  command << L"\"" << exe << L"\""
          << L" --transport udp --codec h264"
          << L" --directory-url \"" << widen(server) << L"\""
          << L" --directory-session \"" << widen(token) << L"\""
          << L" --directory-host-id \"" << widen(request.hostId) << L"\""
          // Bits per second on the wire; the interface talks in kbps because that is what the
          // numbers on a plan are quoted in.
          << L" --runtime-bitrate " << (settings.bitrateKbps * 1000u)
          << L" --runtime-fps " << settings.fps
          << L" --monitor " << settings.monitorId;

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring mutableCommand = command.str();
  if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      executable_dir().c_str(), &si, &pi)) {
    log_line("session launch failed err=" + std::to_string(GetLastError()));
    post_status("error", "세션을 시작하지 못했습니다");
    return;
  }
  log_line("session started host=" + request.hostId + " kbps=" +
           std::to_string(settings.bitrateKbps) + " fps=" + std::to_string(settings.fps) +
           " monitor=" + std::to_string(settings.monitorId));
  CloseHandle(pi.hThread);

  // Watched rather than forgotten: when the session window closes the list has to become usable
  // again, and if it exits immediately that is a failure the user should hear about.
  std::thread([handle = pi.hProcess, name = request.hostName]() {
    const DWORD waited = WaitForSingleObject(handle, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(handle, &exitCode);
    CloseHandle(handle);
    if (waited == WAIT_OBJECT_0 && exitCode != 0) {
      post_status("error", name + " 연결에 실패했습니다 (코드 " + std::to_string(exitCode) + ")");
    } else {
      post_status("idle", "");
    }
  }).detach();

  post_status("connecting", request.hostName + " 창을 여는 중");
}

void handle_page_message(const std::string& json) {
  const std::string type = shell_message_type(json);

  if (type == "ready") {
    std::string server;
    std::string accountId;
    ShellRuntimeSettings settings;
    {
      std::lock_guard<std::mutex> lock(gStateMu);
      settings = gSettings;
      load_settings(&server, &accountId, &settings);
      gSettings = settings;
    }
    post_to_page(shell_restore_json(server, accountId, settings));
    return;
  }
  if (type == "settings") {
    std::string server;
    std::string accountId;
    {
      std::lock_guard<std::mutex> lock(gStateMu);
      shell_parse_settings(json, &gSettings);
      server = gServerUrl;
      accountId = gAccountId;
      save_settings(server, accountId, gSettings);
    }
    // Applied to the next session rather than the running one: the child owns its own encoder
    // negotiation once started, and reaching into it from here would duplicate that logic.
    post_status("idle", "설정을 저장했습니다. 다음 연결부터 적용됩니다.");
    return;
  }
  if (type == "login") {
    std::string server;
    std::string accountId;
    std::string password;
    json_profile::json_get_string(json, "server", &server);
    json_profile::json_get_string(json, "accountId", &accountId);
    json_profile::json_get_string(json, "password", &password);
    begin_login(server, accountId, password);
    return;
  }
  if (type == "hosts") {
    begin_refresh_hosts();
    return;
  }
  if (type == "logout") {
    {
      std::lock_guard<std::mutex> lock(gStateMu);
      gSessionToken.clear();
    }
    post_to_page("{\"type\":\"signedOut\"}");
    return;
  }
  ShellConnectRequest connect{};
  if (shell_parse_connect(json, &connect)) {
    begin_session(connect);
  }
}

void resize_webview() {
  if (!gController || !gWindow) return;
  RECT bounds{};
  GetClientRect(gWindow, &bounds);
  gController->put_Bounds(bounds);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_SIZE:
      resize_webview();
      return 0;
    case WM_APP + 1: {
      // One message pushed from a worker thread, now on the thread WebView2 requires.
      std::unique_ptr<std::string> payload(reinterpret_cast<std::string*>(lParam));
      if (gWebView && payload) gWebView->PostWebMessageAsJson(widen(*payload).c_str());
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

/** The page, loaded from beside the executable so it can be edited without a rebuild. */
std::wstring shell_page_uri() {
  const std::wstring local = executable_dir() + L"\\ui\\shell.html";
  if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) return L"file:///" + local;
  // Running from a build tree, where the source layout still has it.
  return L"file:///" + executable_dir() + L"\\..\\..\\..\\..\\apps\\native_poc\\ui\\shell.html";
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  std::string socketError;
  if (!initialize_sockets(&socketError)) {
    MessageBoxW(nullptr, widen(socketError).c_str(), L"GNLink", MB_ICONERROR);
    return 1;
  }

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = instance;
  wc.lpszClassName = kWindowClass;
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = CreateSolidBrush(RGB(0x0f, 0x13, 0x19));
  RegisterClassExW(&wc);

  const std::wstring title = std::wstring(L"GNLink ") + kProductVersion;
  gWindow = CreateWindowExW(0, kWindowClass, title.c_str(),
                            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                            520, 720, nullptr, nullptr, instance, nullptr);
  if (!gWindow) return 2;
  ShowWindow(gWindow, SW_SHOW);

  wchar_t userData[MAX_PATH]{};
  GetTempPathW(MAX_PATH, userData);
  wcscat_s(userData, L"GNLinkClient");

  const HRESULT created = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData, nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) {
              // The runtime ships with Edge and is on any recent Windows, so its absence is worth
              // naming rather than failing silently.
              MessageBoxW(gWindow,
                          L"WebView2 런타임을 찾을 수 없습니다.\n"
                          L"Microsoft Edge WebView2 런타임을 설치한 뒤 다시 실행해 주세요.",
                          L"GNLink", MB_ICONERROR);
              PostQuitMessage(3);
              return result;
            }
            env->CreateCoreWebView2Controller(
                gWindow,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(hr) || !controller) {
                        PostQuitMessage(4);
                        return hr;
                      }
                      gController = controller;
                      gController->get_CoreWebView2(&gWebView);
                      resize_webview();

                      ComPtr<ICoreWebView2Settings> settings;
                      if (SUCCEEDED(gWebView->get_Settings(&settings)) && settings) {
                        // Nothing here is a browser: no dev tools, no context menu, no status
                        // bar. They would only advertise that this is a web view.
                        settings->put_AreDevToolsEnabled(FALSE);
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_IsStatusBarEnabled(FALSE);
                      }

                      EventRegistrationToken token{};
                      gWebView->add_WebMessageReceived(
                          Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                              [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                                  -> HRESULT {
                                LPWSTR raw = nullptr;
                                if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                  handle_page_message(narrow(raw));
                                  CoTaskMemFree(raw);
                                }
                                return S_OK;
                              })
                              .Get(),
                          &token);

                      gWebView->Navigate(shell_page_uri().c_str());
                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());
  if (FAILED(created)) {
    MessageBoxW(gWindow, L"WebView2 를 시작하지 못했습니다.", L"GNLink", MB_ICONERROR);
    return 5;
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}
