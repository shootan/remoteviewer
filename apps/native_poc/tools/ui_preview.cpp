// Renders the shipping page in each of its states and writes one PNG per state.
//
// A design discussion about screens nobody has looked at is a discussion about guesses. This
// loads `ui/shell.html` -- the file that ships, not a copy of it -- drives it with the same
// message builders the client uses, and photographs the result. What comes out is what the user
// would see.
//
// It is a preview tool, not a test: it asserts nothing and fails only if it cannot render. The
// assertions live in client_update_ui_test. Keeping them apart matters, because a tool that both
// produces the evidence and judges it is not evidence.
//
// Size presets cover the two things that break layouts here: a narrow window and a high-DPI
// scale. They are rendered at the pixel sizes those conditions produce, which is not the same as
// having run on such a display -- the product window, the shell scale factor and the monitor are
// not in this process. That limit is printed with the results rather than left for the reader to
// work out.

#include <windows.h>
#include <shlwapi.h>
#include <wrl.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "WebView2.h"
#include "client_shell_bridge.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

ComPtr<ICoreWebView2Controller> gController;
ComPtr<ICoreWebView2> gWebView;
HWND gWindow = nullptr;
std::atomic<bool> gReady{false};
std::atomic<bool> gFatal{false};

std::string narrow(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], size,
                      nullptr, nullptr);
  return out;
}

std::wstring widen(const std::string& text) {
  if (text.empty()) return {};
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], size);
  return out;
}

std::wstring executable_dir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring full(path);
  const size_t slash = full.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : full.substr(0, slash);
}

std::wstring repo_path(const wchar_t* relative) {
  const wchar_t* ups[] = {L"\\..\\..\\..\\..\\", L"\\..\\..\\..\\"};
  for (const wchar_t* up : ups) {
    wchar_t full[MAX_PATH]{};
    if (PathCanonicalizeW(full, (executable_dir() + up + relative).c_str()) &&
        PathFileExistsW(full)) {
      return full;
    }
  }
  return {};
}

bool pump_until(const std::function<bool()>& done, DWORD budgetMs) {
  const DWORD deadline = GetTickCount() + budgetMs;
  while (!done()) {
    if (gFatal.load() || GetTickCount() > deadline) return false;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Sleep(10);
  }
  return true;
}

/** Shared state, not captured references: a late completion must not write a dead frame. */
std::string eval(const std::wstring& script, DWORD budgetMs = 5000) {
  struct Answer {
    std::string text;
    std::atomic<bool> done{false};
  };
  auto answer = std::make_shared<Answer>();
  gWebView->ExecuteScript(script.c_str(),
                          Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                              [answer](HRESULT, LPCWSTR result) -> HRESULT {
                                if (result) answer->text = narrow(result);
                                answer->done = true;
                                return S_OK;
                              })
                              .Get());
  if (!pump_until([answer]() { return answer->done.load(); }, budgetMs)) return {};
  return answer->text;
}

bool shoot(const std::wstring& path) {
  ComPtr<IStream> stream;
  if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_READWRITE,
                                    FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &stream))) {
    return false;
  }
  auto done = std::make_shared<std::atomic<bool>>(false);
  auto hr = std::make_shared<HRESULT>(S_OK);
  gWebView->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(),
                           Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                               [done, hr](HRESULT result) -> HRESULT {
                                 *hr = result;
                                 *done = true;
                                 return S_OK;
                               })
                               .Get());
  if (!pump_until([done]() { return done->load(); }, 8000)) {
    std::printf("      (capture did not answer)\n");
    return false;
  }
  if (FAILED(*hr)) {
    std::printf("      (capture hr=0x%08lx)\n", static_cast<unsigned long>(*hr));
    return false;
  }
  return true;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_SIZE && gController) {
    RECT b{};
    GetClientRect(hwnd, &b);
    gController->put_Bounds(b);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, w, l);
}

void resize(int width, int height) {
  SetWindowPos(gWindow, nullptr, -3000, -3000, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
  RECT b{};
  GetClientRect(gWindow, &b);
  if (gController) gController->put_Bounds(b);
  Sleep(200);
}

struct Preset {
  const wchar_t* name;
  int width;
  int height;
};

}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
    std::printf("CoInitializeEx failed 0x%08lx\n", static_cast<unsigned long>(com));
    return 1;
  }

  const std::wstring page = repo_path(L"apps\\native_poc\\ui\\shell.html");
  if (page.empty()) {
    std::printf("could not find the shipping ui/shell.html\n");
    return 1;
  }
  const std::wstring outDir = repo_path(L".claude") + L"\\ui-preview";
  CreateDirectoryW(outDir.c_str(), nullptr);
  std::printf("page %s\nout  %s\n", narrow(page).c_str(), narrow(outDir).c_str());

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60UiPreview";
  RegisterClassExW(&wc);
  gWindow = CreateWindowExW(0, wc.lpszClassName, L"ui preview", WS_OVERLAPPEDWINDOW, -3000, -3000,
                            520, 780, nullptr, nullptr, wc.hInstance, nullptr);
  if (!gWindow) return 1;

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring userData = std::wstring(temp) + L"gnlink-ui-preview";
  CreateDirectoryW(userData.c_str(), nullptr);

  std::atomic<bool> created{false};
  CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [&created](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) {
              gFatal = true;
              return S_OK;
            }
            env->CreateCoreWebView2Controller(
                gWindow,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [&created](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
                      if (FAILED(hr2) || !c) {
                        gFatal = true;
                        return S_OK;
                      }
                      gController = c;
                      gController->get_CoreWebView2(&gWebView);
                      RECT b{};
                      GetClientRect(gWindow, &b);
                      gController->put_Bounds(b);
                      // CapturePreview photographs a composited view. With the controller hidden
                      // most captures came back empty; the window itself stays off-screen, so
                      // nothing appears in front of the user.
                      gController->put_IsVisible(TRUE);
                      EventRegistrationToken t;
                      gWebView->add_WebMessageReceived(
                          Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                              [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a)
                                  -> HRESULT {
                                LPWSTR raw = nullptr;
                                if (SUCCEEDED(a->TryGetWebMessageAsString(&raw)) && raw) {
                                  if (remote60::native_poc::shell_message_type(narrow(raw)) ==
                                      "ready") {
                                    gReady = true;
                                  }
                                  CoTaskMemFree(raw);
                                }
                                return S_OK;
                              })
                              .Get(),
                          &t);
                      created = true;
                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());
  if (!pump_until([&created]() { return created.load(); }, 30000) || !gWebView) {
    std::printf("the WebView2 controller never came up\n");
    return 1;
  }

  namespace np = remote60::native_poc;

  // Each entry is one row of docs/ui_state_table.md.
  struct Shot {
    const wchar_t* file;
    const char* what;
    std::function<void()> arrange;
  };

  const std::string hosts =
      "{\"type\":\"hosts\",\"accountId\":\"shotan\",\"hosts\":["
      "{\"hostId\":\"a\",\"hostName\":\"사무실 PC\",\"online\":true},"
      "{\"hostId\":\"b\",\"hostName\":\"집 데스크탑\",\"online\":true},"
      "{\"hostId\":\"c\",\"hostName\":\"노트북\",\"online\":false}]}";
  const std::string hostsEmpty =
      "{\"type\":\"hosts\",\"accountId\":\"shotan\",\"hosts\":[]}";

  const std::vector<Shot> shots = {
      {L"s1-signin-initial.png", "S1 login, initial", [] {}},
      {L"s1-signin-error.png", "S1 login, error",
       [&] {
         gWebView->PostWebMessageAsString(
             widen(np::shell_status_json("error", "아이디 또는 비밀번호가 올바르지 않습니다."))
                 .c_str());
       }},
      {L"s2-hosts-list.png", "S2 host list",
       [&] { gWebView->PostWebMessageAsString(widen(hosts).c_str()); }},
      {L"s2-hosts-empty.png", "S2 host list, empty",
       [&] { gWebView->PostWebMessageAsString(widen(hostsEmpty).c_str()); }},
      {L"s2-hosts-connecting.png", "S2 host list, connecting (real click)",
       [&] {
         gWebView->PostWebMessageAsString(widen(hosts).c_str());
         Sleep(250);
         // Clicked rather than arranged by posting a status. The connecting mark is set by the
         // page's own click handler, so a photograph staged with a message would show the state
         // this screen has when nobody pressed anything -- which is the state that was wrong.
         eval(L"document.querySelectorAll('.host')[0].click()");
       }},
      {L"s3-settings.png", "S3 settings",
       [&] {
         gWebView->PostWebMessageAsString(widen(hosts).c_str());
         eval(L"document.getElementById('openSettings').click()");
       }},
      {L"s4-update-offer.png", "S4 update offered",
       [&] {
         const np::ShellUpdateNotice n = np::shell_update_notice("UpdateAvailable", "0.2.120", "");
         gWebView->PostWebMessageAsString(
             widen(np::shell_update_available_json("0.2.120", n.text)).c_str());
       }},
      {L"s4-update-busy.png", "S4 update starting",
       [&] {
         const np::ShellUpdateNotice n = np::shell_update_notice("UpdateAvailable", "0.2.120", "");
         gWebView->PostWebMessageAsString(
             widen(np::shell_update_available_json("0.2.120", n.text)).c_str());
         gWebView->PostWebMessageAsString(widen(np::shell_update_busy_json(true)).c_str());
       }},
      {L"s4-update-failed.png", "S4 update failed, button handed back",
       [&] {
         const np::ShellUpdateNotice n = np::shell_update_notice("UpdateAvailable", "0.2.120", "");
         gWebView->PostWebMessageAsString(
             widen(np::shell_update_available_json("0.2.120", n.text)).c_str());
         gWebView->PostWebMessageAsString(widen(np::shell_update_busy_json(false)).c_str());
         gWebView->PostWebMessageAsString(
             widen(np::shell_status_json("error", "업데이트를 시작하지 못했습니다.")).c_str());
       }},
      {L"s4-update-cleared.png", "S4 nothing to install (bar must be gone)",
       [&] {
         const np::ShellUpdateNotice n = np::shell_update_notice("UpdateAvailable", "0.2.120", "");
         gWebView->PostWebMessageAsString(
             widen(np::shell_update_available_json("0.2.120", n.text)).c_str());
         Sleep(200);
         gWebView->PostWebMessageAsString(widen(np::shell_update_cleared_json()).c_str());
       }},
  };

  const Preset presets[] = {
      {L"w520", 520, 780},   // the normal window
      {L"w360", 360, 780},   // narrow
      {L"w1040", 1040, 900}, // 200% scale worth of pixels
  };

  int written = 0;
  for (const Preset& preset : presets) {
    for (const Shot& shot : shots) {
      gReady = false;
      gWebView->Navigate(page.c_str());
      if (!pump_until([]() { return gReady.load(); }, 20000)) {
        std::printf("  page did not report ready for %ls\n", shot.file);
        continue;
      }
      resize(preset.width, preset.height);
      shot.arrange();
      Sleep(400);
      const std::wstring out =
          outDir + L"\\" + preset.name + L"-" + shot.file;
      if (shoot(out)) {
        ++written;
        std::printf("  %-28ls %s\n", (std::wstring(preset.name) + L"-" + shot.file).c_str(),
                    shot.what);
      } else {
        std::printf("  FAILED %ls\n", shot.file);
      }
    }
  }

  std::printf("\n%d images written\n", written);
  std::printf("LIMIT: rendered at these pixel sizes in an off-screen WebView. Not the product\n"
              "       window, not a real high-DPI monitor, and not the GDI picker or toolbar --\n"
              "       those need the product running and are photographed separately.\n");

  if (gController) gController->Close();
  DestroyWindow(gWindow);
  return 0;
}
