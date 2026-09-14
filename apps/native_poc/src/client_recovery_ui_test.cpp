// Compile the actual shell entry/handlers into an isolated executable. Only its process entry
// is renamed: HTML, WebView, native bridge, workers, HTTP and owner adoption are production code.
#define wWinMain recovery_product_entry
#include "client_shell_main.cpp"
#undef wWinMain
#include <shlwapi.h>
#include <cstdio>
#include <stdexcept>
#include <filesystem>

namespace {
void recovery_pump(unsigned ms) {
  const uint64_t end = GetTickCount64() + ms;
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    Sleep(5);
  } while (GetTickCount64() < end);
}
bool recovery_wait(const std::function<bool()>& predicate, unsigned ms = 10000) {
  const uint64_t end = GetTickCount64() + ms;
  while (GetTickCount64() < end) { if (predicate()) return true; recovery_pump(20); }
  return predicate();
}
std::string recovery_eval(const std::wstring& script) {
  struct Reply { bool done=false; std::string text; };
  auto reply = std::make_shared<Reply>();
  if (!gWebView) return {};
  gWebView->ExecuteScript(script.c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
    [reply](HRESULT hr, LPCWSTR value) -> HRESULT {
      if (SUCCEEDED(hr) && value) reply->text = narrow(value);
      reply->done = true; return S_OK;
    }).Get());
  recovery_wait([&] { return reply->done; }, 3000);
  return reply->text;
}
void recovery_check(bool value, const char* label) {
  std::printf("%s %s\n", value ? "PASS" : "FAIL", label);
  if (!value) throw std::runtime_error(label);
}
bool recovery_dom(const std::wstring& script, unsigned timeout = 10000) {
  const uint64_t end = GetTickCount64() + timeout;
  while (GetTickCount64() < end) {
    if (recovery_eval(script) == "true") return true;
    recovery_pump(30);
  }
  return false;
}
}

int main(int argc, char** argv) {
  if (argc != 4) { std::puts("usage: client_recovery_ui_test <fixture-url> <delay-flag> <screenshot>"); return 2; }
  // Refuse the test before opening a WebView unless its profile is a private runner fixture.
  // In particular, never share the installed client's normal TEMP/GNLinkClient environment.
  const char* fixture = std::getenv("GNLINK_RECOVERY_TEST_ROOT");
  wchar_t temporary[MAX_PATH]{}; GetTempPathW(MAX_PATH, temporary);
  if (!fixture || !std::filesystem::exists(std::filesystem::path(fixture) / ".fixture") ||
      std::filesystem::weakly_canonical(temporary) !=
          std::filesystem::weakly_canonical(std::filesystem::path(fixture) / "profile") ||
      std::string(argv[1]).rfind("http://127.0.0.1:", 0) != 0) {
    std::puts("FAIL isolated fixture/profile required; no browser process was opened or terminated"); return 2;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  std::string socketError;
  if (!initialize_sockets(&socketError)) return 2;
  LogUploadShutdown uploader;
  int result = 0;
  try {
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=wnd_proc;
    wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=kWindowClass;
    wc.hCursor=LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    RegisterClassExW(&wc);
    gWindow=CreateWindowExW(0,kWindowClass,L"GNLink isolated recovery test",WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT,CW_USEDEFAULT,560,760,nullptr,nullptr,wc.hInstance,nullptr);
    ShowWindow(gWindow,SW_SHOW);
    recovery_check(SUCCEEDED(create_shell_webview()), "production WebView creation starts");
    recovery_check(recovery_dom(L"!!document.getElementById('signIn')"), "production login UI loads");
    const auto signIn = [&](const wchar_t* account) {
      const std::wstring script = L"document.getElementById('server').value='" + widen(argv[1]) +
        L"';document.getElementById('account').value='" + account +
        L"';document.getElementById('password').value='fixture-password';document.getElementById('signIn').click();true";
      recovery_eval(script);
      recovery_check(recovery_dom(L"!document.getElementById('hostsCard').classList.contains('hidden')"),
                     "DOM login crosses native handler and real HTTP into host list");
    };
    signIn(L"account-a");
    { std::ofstream flag(argv[2]); flag << '1'; }
    recovery_eval(L"document.getElementById('refresh').click();true");
    const std::wstring seen = widen(std::string(argv[2]) + ".seen");
    recovery_check(recovery_wait([&] { return GetFileAttributesW(seen.c_str()) != INVALID_FILE_ATTRIBUTES; }),
                   "real host-list request is delayed at server boundary");
    recovery_eval(L"document.getElementById('signOut').click();true");
    recovery_check(recovery_dom(L"!document.getElementById('signInCard').classList.contains('hidden')"), "logout returns to login UI");
    recovery_pump(1800);
    recovery_check(gSessionToken.empty() && recovery_eval(L"document.getElementById('hostsCard').classList.contains('hidden')") == "true",
                   "late refresh cannot restore signed-out UI or credentials");
    signIn(L"account-b");
    const std::string token = gSessionToken;
    UINT oldBrowser = 0;
    recovery_check(gWebView && SUCCEEDED(gWebView->get_BrowserProcessId(&oldBrowser)) && oldBrowser,
                   "identify browser belonging to isolated WebView environment");
    HANDLE browser = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, oldBrowser);
    recovery_check(browser != nullptr, "pin isolated browser process handle");
    const BOOL terminated = TerminateProcess(browser, 99);
    WaitForSingleObject(browser, 3000); CloseHandle(browser);
    recovery_check(terminated != FALSE, "inject failure only into this test browser");
    recovery_check(recovery_wait([&] {
      UINT current = 0;
      return gWebView && SUCCEEDED(gWebView->get_BrowserProcessId(&current)) && current && current != oldBrowser;
    }, 20000), "production ProcessFailed handler recreates WebView");
    recovery_check(recovery_dom(L"!document.getElementById('hostsCard').classList.contains('hidden') && document.getElementById('accountLabel').textContent === 'account-b'", 20000)
                   && gSessionToken == token, "browser recovery restores current account without another login");
    gReconnectRequest = ShellConnectRequest{};
    post_status("reconnecting", "재연결 시험");
    recovery_check(recovery_dom(L"!document.getElementById('cancelReconnect').classList.contains('hidden')"), "reconnect cancel control is visible");
    recovery_eval(L"document.getElementById('cancelReconnect').click();true");
    recovery_check(recovery_wait([&] { return !gReconnectRequest; }), "DOM reconnect cancel reaches native cancellation");
    recovery_eval(L"document.getElementById('openSettings').click();true");
    post_status("reconnecting", "fixture busy");
    recovery_check(recovery_dom(L"document.getElementById('signIn').disabled"), "busy state reaches UI while settings is open");
    post_status("error", "fixture recovery error");
    recovery_check(recovery_dom(L"!document.getElementById('signIn').disabled"), "failure in settings also clears global busy state");
    recovery_eval(L"document.getElementById('closeSettings').click();true");
    ComPtr<IStream> screenshot;
    recovery_check(SUCCEEDED(SHCreateStreamOnFileEx(widen(argv[3]).c_str(), STGM_CREATE|STGM_READWRITE,
                                                   FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &screenshot)), "open isolated screenshot output");
    auto captured = std::make_shared<std::atomic<bool>>(false);
    gWebView->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, screenshot.Get(),
      Callback<ICoreWebView2CapturePreviewCompletedHandler>([captured](HRESULT hr) -> HRESULT {
        captured->store(SUCCEEDED(hr)); return S_OK;
      }).Get());
    recovery_check(recovery_wait([&] { return captured->load(); }), "capture rendered production page");
    std::puts("client_recovery_ui_test: ALL PASS (real UI/native/HTTP; no updater installation)");
  } catch (const std::exception& error) { std::printf("client_recovery_ui_test: FAIL %s\n", error.what()); result=1; }
  if (gWindow && IsWindow(gWindow)) DestroyWindow(gWindow);
  gClosing=true; gWorkers.Shutdown();
  if (gController) gController->Close();
  gWebView.Reset(); gController.Reset();
  return result;
}
