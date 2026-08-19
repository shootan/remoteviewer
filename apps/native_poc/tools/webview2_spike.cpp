// Proves the WebView2 shell works here before anything is built on top of it.
//
// The Windows client is getting a real interface -- sign in, pick a PC, settings, macros -- and
// drawing that in raw Win32 means owning every pixel, font and DPI case by hand. WebView2 draws
// it from HTML instead, while the video keeps its existing path: UDP, Media Foundation, D3D11,
// untouched. That separation is the whole point, since the video path is where the months went.
//
// What this checks, in order of what would hurt most to discover late:
//   1. the SDK headers and the static loader link at all
//   2. the runtime is present and an environment can be created
//   3. HTML actually renders in a child window we control
//   4. JavaScript can call back into C++ -- the app is useless without it
//
// Run it, click the button, and the title bar reports the round trip.

#include <windows.h>
#include <wrl.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

ComPtr<ICoreWebView2Controller> gController;
ComPtr<ICoreWebView2> gWebView;
HWND gWindow = nullptr;
std::atomic<bool> gMessageFromJs{false};

// Inline rather than a file on disk: the spike should fail for one reason only, and "where does
// the html live once installed" is a question for the real thing.
const wchar_t* kPage = LR"HTML(
<!doctype html>
<html><head><meta charset="utf-8"><style>
  body { margin:0; font-family:'Segoe UI',sans-serif; background:#12161c; color:#e8eaed;
         display:flex; flex-direction:column; align-items:center; justify-content:center;
         height:100vh; gap:18px; }
  h1 { font-size:20px; font-weight:600; margin:0; }
  p { margin:0; color:#9aa3b2; font-size:13px; }
  button { padding:10px 22px; font-size:14px; border:0; border-radius:6px;
           background:#3b82f6; color:#fff; cursor:pointer; }
  button:hover { background:#2f6fd0; }
</style></head><body>
  <h1>WebView2 is rendering</h1>
  <p>Fonts, layout and colours came from CSS, not from GDI.</p>
  <button onclick="window.chrome.webview.postMessage('hello from javascript')">
    Call into C++
  </button>
</body></html>
)HTML";

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
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

}  // namespace

int wmain() {
  // The UI thread has to be STA; WebView2 refuses to initialise otherwise, and the failure is
  // an opaque HRESULT rather than anything that names the cause.
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
    std::printf("FAIL: CoInitializeEx\n");
    return 1;
  }
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"GNLinkWebView2Spike";
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  RegisterClassExW(&wc);

  gWindow = CreateWindowExW(0, wc.lpszClassName, L"GNLink WebView2 spike",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 460,
                            nullptr, nullptr, wc.hInstance, nullptr);
  if (!gWindow) {
    std::printf("FAIL: CreateWindowExW\n");
    return 2;
  }
  ShowWindow(gWindow, SW_SHOW);

  // User data goes beside the executable's temp area rather than into Program Files, which is
  // read-only for a normal user and is where the real client will have to be careful too.
  wchar_t userData[MAX_PATH]{};
  GetTempPathW(MAX_PATH, userData);
  wcscat_s(userData, L"GNLinkWebView2Spike");

  const HRESULT createEnv = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData, nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) {
              std::printf("FAIL: environment hr=0x%08lX\n", static_cast<unsigned long>(result));
              PostQuitMessage(3);
              return result;
            }
            env->CreateCoreWebView2Controller(
                gWindow,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(hr) || !controller) {
                        std::printf("FAIL: controller hr=0x%08lX\n",
                                    static_cast<unsigned long>(hr));
                        PostQuitMessage(4);
                        return hr;
                      }
                      gController = controller;
                      gController->get_CoreWebView2(&gWebView);
                      resize_webview();

                      // The bridge the real UI depends on: a click in HTML reaching C++.
                      EventRegistrationToken token{};
                      gWebView->add_WebMessageReceived(
                          Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                              [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                                  -> HRESULT {
                                LPWSTR message = nullptr;
                                if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
                                  std::wstring title = L"C++ received: ";
                                  title += message;
                                  SetWindowTextW(gWindow, title.c_str());
                                  gMessageFromJs.store(true);
                                  CoTaskMemFree(message);
                                }
                                return S_OK;
                              })
                              .Get(),
                          &token);

                      gWebView->NavigateToString(kPage);
                      std::printf("ok: webview created\n");
                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());

  if (FAILED(createEnv)) {
    std::printf("FAIL: CreateCoreWebView2EnvironmentWithOptions hr=0x%08lX\n",
                static_cast<unsigned long>(createEnv));
    return 5;
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  std::printf("closed; js->c++ bridge exercised=%d\n", gMessageFromJs.load() ? 1 : 0);
  return 0;
}
