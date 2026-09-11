// Renders the production page in a real WebView2 and presses the button.
//
// This test exists because of what the previous round got wrong. `client_update_flow_test` read
// the source, found `start_update_check()` called from two places, and that was accepted as the
// Client's update path being wired. It was not. The notice reached the screen and there was
// nothing on the screen to press: `shell.html` had seven buttons and none of them was an update,
// while `client_shell_main.cpp` had handled a {"type":"update"} message all along that no page
// ever sent. A source grep cannot see that gap, because both halves are present -- what is
// missing is only the fact that nothing joins them.
//
// So nothing here is read from source. The real `ui/shell.html` is loaded into a real browser
// engine, the real message builders from `client_shell_bridge.cpp` are what gets posted to it,
// the click is a DOM click on the rendered element, and the assertion is that a message comes
// back out. A screenshot is written next to the results so a person can see what the user sees.
//
// What this does NOT prove, and is not claimed anywhere: that a live check against the real
// server produces this message. That needs an account and a network, and it is a separate step.
// This proves the half that was missing -- that when the message arrives, there is a button, and
// pressing it sends what the native side is waiting for.

#include <windows.h>
#include <shlwapi.h>
#include <wrl.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "WebView2.h"
#include "client_shell_bridge.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using remote60::native_poc::shell_message_type;
using remote60::native_poc::shell_update_available_json;
using remote60::native_poc::shell_update_busy_json;
using remote60::native_poc::shell_update_cleared_json;

namespace {

int gPass = 0;
int gFail = 0;

void ok(bool cond, const std::string& what, const std::string& detail = {}) {
  if (cond) {
    ++gPass;
    std::printf("PASS  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  } else {
    ++gFail;
    std::printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  }
}

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

/**
 * The page under test, which must be the one that ships.
 *
 * Looked for beside the executable first (that is the installed layout) and then up the build
 * tree at the repository copy. A test that fabricated its own HTML would pass forever while the
 * shipped page had no button on it -- which is exactly the failure this is here to catch.
 */
std::wstring production_page_path() {
  // The repository copy first, deliberately. The copy beside the executable is written by the
  // build and can lag the source by a whole edit -- it did on the first run of this test, which
  // would have meant asserting against a page nobody was shipping.
  const wchar_t* candidates[] = {
      L"\\..\\..\\..\\..\\apps\\native_poc\\ui\\shell.html",
      L"\\..\\..\\..\\apps\\native_poc\\ui\\shell.html",
  };
  for (const wchar_t* suffix : candidates) {
    wchar_t full[MAX_PATH]{};
    const std::wstring joined = executable_dir() + suffix;
    if (PathCanonicalizeW(full, joined.c_str()) && PathFileExistsW(full)) return full;
  }
  const std::wstring beside = executable_dir() + L"\\ui\\shell.html";
  if (PathFileExistsW(beside.c_str())) return beside;
  return {};
}

// ---------------------------------------------------------------------------- the harness

ComPtr<ICoreWebView2Controller> gController;
ComPtr<ICoreWebView2> gWebView;
HWND gWindow = nullptr;
std::vector<std::string> gFromPage;
std::atomic<bool> gReady{false};
std::atomic<bool> gFatal{false};

/** Runs the message loop until `done` or the budget runs out. Returns whether `done` happened. */
bool pump_until(const std::function<bool()>& done, DWORD budgetMs) {
  const DWORD deadline = GetTickCount() + budgetMs;
  while (!done()) {
    if (gFatal.load()) return false;
    if (GetTickCount() > deadline) return false;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Sleep(10);
  }
  return true;
}

/** Evaluates script in the page and returns its JSON result. Empty when it did not answer. */
std::string eval(const std::wstring& script, DWORD budgetMs = 5000) {
  std::string out;
  std::atomic<bool> answered{false};
  gWebView->ExecuteScript(
      script.c_str(),
      Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
          [&out, &answered](HRESULT, LPCWSTR result) -> HRESULT {
            if (result) out = narrow(result);
            answered = true;
            return S_OK;
          })
          .Get());
  pump_until([&answered]() { return answered.load(); }, budgetMs);
  return out;
}

bool page_sent(const std::string& type) {
  for (const std::string& message : gFromPage) {
    if (shell_message_type(message) == type) return true;
  }
  return false;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_SIZE && gController) {
    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    gController->put_Bounds(bounds);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void write_screenshot(const std::wstring& path) {
  ComPtr<IStream> stream;
  if (FAILED(SHCreateStreamOnFileEx(path.c_str(), STGM_CREATE | STGM_READWRITE, FILE_ATTRIBUTE_NORMAL,
                                    TRUE, nullptr, &stream))) {
    return;
  }
  std::atomic<bool> done{false};
  gWebView->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(),
                           Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                               [&done](HRESULT) -> HRESULT {
                                 done = true;
                                 return S_OK;
                               })
                               .Get());
  pump_until([&done]() { return done.load(); }, 8000);
}

}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // WebView2 refuses to build an environment without an apartment: the first run of this test
  // came back CO_E_NOTINITIALIZED (0x800401f0). Single-threaded, because the page and the
  // callbacks below all run on this one thread. S_FALSE still owes an uninitialise.
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
    std::printf("FAIL  CoInitializeEx  hr=0x%08lx\n", static_cast<unsigned long>(com));
    return 1;
  }

  const std::wstring page = production_page_path();
  if (page.empty()) {
    std::printf("FAIL  the production ui/shell.html could not be found\n");
    std::printf("client_update_ui_test: FAIL (1 failed)\n");
    return 1;
  }
  std::printf("page  %s\n", narrow(page).c_str());

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60UpdateUiTest";
  RegisterClassExW(&wc);
  // Off screen and never shown: this must not steal focus from whoever is at the machine.
  gWindow = CreateWindowExW(0, wc.lpszClassName, L"update ui test", WS_OVERLAPPEDWINDOW, -3000,
                            -3000, 520, 760, nullptr, nullptr, wc.hInstance, nullptr);
  if (!gWindow) {
    std::printf("FAIL  could not create the host window\n");
    return 1;
  }

  std::atomic<bool> created{false};
  // Its own user-data folder, under TEMP. Sharing the client's would have this test writing into
  // the profile of the program a person may be using at the time.
  wchar_t tempDir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, tempDir);
  const std::wstring userData = std::wstring(tempDir) + L"gnlink-update-ui-test";
  CreateDirectoryW(userData.c_str(), nullptr);

  const HRESULT env = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [&created](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(hr) || !environment) {
              std::printf("      environment hr=0x%08lx\n", static_cast<unsigned long>(hr));
              gFatal = true;
              return S_OK;
            }
            environment->CreateCoreWebView2Controller(
                gWindow,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [&created](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(hr2) || !controller) {
                        std::printf("      controller hr=0x%08lx\n",
                                    static_cast<unsigned long>(hr2));
                        gFatal = true;
                        return S_OK;
                      }
                      gController = controller;
                      gController->get_CoreWebView2(&gWebView);
                      RECT bounds{};
                      GetClientRect(gWindow, &bounds);
                      gController->put_Bounds(bounds);
                      EventRegistrationToken token;
                      gWebView->add_WebMessageReceived(
                          Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                              [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                                  -> HRESULT {
                                LPWSTR raw = nullptr;
                                if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                  const std::string text = narrow(raw);
                                  gFromPage.push_back(text);
                                  if (shell_message_type(text) == "ready") gReady = true;
                                  CoTaskMemFree(raw);
                                }
                                return S_OK;
                              })
                              .Get(),
                          &token);
                      created = true;
                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());
  if (FAILED(env)) {
    std::printf("FAIL  the WebView2 runtime is not available here  hr=0x%08lx\n",
                static_cast<unsigned long>(env));
    std::printf("client_update_ui_test: FAIL (1 failed)\n");
    return 1;
  }
  if (!pump_until([&created]() { return created.load(); }, 30000) || !gWebView) {
    std::printf("FAIL  the WebView2 controller never came up\n");
    std::printf("client_update_ui_test: FAIL (1 failed)\n");
    return 1;
  }

  gWebView->Navigate(page.c_str());
  ok(pump_until([]() { return gReady.load(); }, 30000),
     "the shipped page loads and reports ready");
  if (!gReady.load()) {
    std::printf("client_update_ui_test: FAIL (%d failed)\n", gFail);
    return 1;
  }

  // ------------------------------------------------------------------ negative: nothing offered yet
  ok(eval(L"document.getElementById('updateBar').classList.contains('hidden')") == "true",
     "before any check, there is no offer on screen");
  gFromPage.clear();
  eval(L"document.getElementById('updateNow').click()");
  ok(!page_sent("update"),
     "clicking the hidden button asks for nothing",
     "no update message was sent");

  // ------------------------------------------------------------------ the production message
  // The sentence comes from the production notice builder rather than being typed here. A test
  // that supplied its own wording would render something the user never sees.
  const remote60::native_poc::ShellUpdateNotice notice =
      remote60::native_poc::shell_update_notice("UpdateAvailable", "0.2.115", "");
  ok(notice.show, "the production notice builder says this outcome is worth showing");
  const std::string available = shell_update_available_json("0.2.115", notice.text);
  ok(available.find("\"type\":\"updateAvailable\"") != std::string::npos &&
         available.find("\"version\":\"0.2.115\"") != std::string::npos,
     "the version travels in a field of its own", available);

  gWebView->PostWebMessageAsString(widen(available).c_str());
  ok(pump_until(
         []() {
           return eval(L"document.getElementById('updateBar').classList.contains('hidden')") ==
                  "false";
         },
         8000),
     "the offer appears on the shipped page");
  ok(eval(L"document.getElementById('updateText').textContent").find("0.2.115") !=
         std::string::npos,
     "and it names the version");
  ok(eval(L"document.getElementById('updateNow').disabled") == "false",
     "the install button is usable");

  write_screenshot(executable_dir() + L"\\client_update_ui.png");
  std::printf("shot  %s\n", narrow(executable_dir() + L"\\client_update_ui.png").c_str());

  // ------------------------------------------------------------------ the click that was missing
  gFromPage.clear();
  eval(L"document.getElementById('updateNow').click()");
  ok(pump_until([]() { return page_sent("update"); }, 5000),
     "pressing it sends {\"type\":\"update\"} -- the message the native side has been waiting for");
  ok(eval(L"document.getElementById('updateNow').disabled") == "true",
     "and the button goes dead so one press is one updater");

  // ------------------------------------------------------------------ busy, then released
  gWebView->PostWebMessageAsString(widen(shell_update_busy_json(true)).c_str());
  ok(pump_until([]() { return eval(L"document.getElementById('updateNow').disabled") == "true"; },
                5000),
     "while an install is starting the button stays disabled");
  gWebView->PostWebMessageAsString(widen(shell_update_busy_json(false)).c_str());
  ok(pump_until([]() { return eval(L"document.getElementById('updateNow').disabled") == "false"; },
                5000),
     "a failed start hands the button back rather than leaving a dead end");

  // ------------------------------------------------------------------ 나중에 defers, nothing more
  eval(L"document.getElementById('updateLater').click()");
  ok(eval(L"document.getElementById('updateBar').classList.contains('hidden')") == "true",
     "나중에 takes the offer down");
  ok(eval(L"document.getElementById('signInCard').classList.contains('hidden')") == "false",
     "and leaves signing in exactly where it was");

  // ------------------------------------------------------------------ withdrawn
  gWebView->PostWebMessageAsString(widen(available).c_str());
  pump_until(
      []() {
        return eval(L"document.getElementById('updateBar').classList.contains('hidden')") ==
               "false";
      },
      8000);
  gWebView->PostWebMessageAsString(widen(shell_update_cleared_json()).c_str());
  ok(pump_until(
         []() {
           return eval(L"document.getElementById('updateBar').classList.contains('hidden')") ==
                  "true";
         },
         5000),
     "a check that finds nothing takes the offer back down");

  gWebView->PostWebMessageAsString(widen(available).c_str());
  pump_until(
      []() {
        return eval(L"document.getElementById('updateBar').classList.contains('hidden')") ==
               "false";
      },
      8000);
  gWebView->PostWebMessageAsString(L"{\"type\":\"signedOut\"}");
  ok(pump_until(
         []() {
           return eval(L"document.getElementById('updateBar').classList.contains('hidden')") ==
                  "true";
         },
         5000),
     "signing out withdraws an offer found for that session");

  gFromPage.clear();
  eval(L"document.getElementById('updateNow').click()");
  ok(!page_sent("update"), "and the withdrawn button asks for nothing");

  if (gController) gController->Close();
  DestroyWindow(gWindow);

  std::printf("client_update_ui_test: %s (%d passed, %d failed)\n", gFail == 0 ? "PASS" : "FAIL",
              gPass, gFail);
  return gFail == 0 ? 0 : 1;
}
