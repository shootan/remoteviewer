#include "client_macro_window.hpp"

#include <shlobj.h>
#include <wrl.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "WebView2.h"
#include "macro_shell_bridge.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace remote60::native_poc {
namespace {

constexpr wchar_t kClassName[] = L"GNLinkMacroWindow";
constexpr UINT kPumpTimerId = 1;
// Playback is dispatched from this tick, so it bounds how closely a replayed gap can be
// honoured. One frame at 60 Hz is finer than any recorded delay worth reproducing.
constexpr UINT kPumpIntervalMs = 16;
constexpr UINT kMessageFromWorker = WM_APP + 1;

HWND gWindow = nullptr;
MacroWindowHooks gHooks;
ComPtr<ICoreWebView2Controller> gController;
ComPtr<ICoreWebView2> gWebView;
// The page is only redrawn when something it shows has changed; rebuilding the step list on
// every tick would fight the user's scroll and selection.
std::string gLastStateJson;

uint64_t now_ms() { return GetTickCount64(); }

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

std::wstring storage_dir() {
  PWSTR base = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &base)) && base) {
    dir = base;
    CoTaskMemFree(base);
  } else {
    dir = L".";
  }
  dir += L"\\GNLink";
  CreateDirectoryW(dir.c_str(), nullptr);
  dir += L"\\macros";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::vector<std::string> saved_macro_names() {
  std::vector<std::string> names;
  WIN32_FIND_DATAW found{};
  const std::wstring pattern = storage_dir() + L"\\*.gnmacro";
  HANDLE it = FindFirstFileW(pattern.c_str(), &found);
  if (it == INVALID_HANDLE_VALUE) return names;
  do {
    std::wstring name(found.cFileName);
    const size_t dot = name.rfind(L".gnmacro");
    if (dot != std::wstring::npos) name.erase(dot);
    if (!name.empty()) names.push_back(narrow(name));
  } while (FindNextFileW(it, &found));
  FindClose(it);
  std::sort(names.begin(), names.end());
  return names;
}

void post_to_page(const std::string& json) {
  if (gWebView) gWebView->PostWebMessageAsJson(widen(json).c_str());
}

void notify(const std::string& detail, bool isError) {
  post_to_page(macro_notice_json(detail, isError));
}

/** Sends the page everything it draws, but only when it differs from what it already has. */
void publish_state(bool force) {
  InputMacro* macro = gHooks.macro;
  if (!macro || !gWebView) return;

  MacroUiState state{};
  state.state = macro->state();
  state.paused = macro->IsPaused();
  state.steps = macro->Steps();
  state.stepCount = state.steps.size();
  state.playbackPosition = macro->PlaybackPosition();
  state.completedRepeats = macro->CompletedRepeats();
  state.savedNames = saved_macro_names();

  std::string json = macro_state_json(state);
  if (!force && json == gLastStateJson) return;
  gLastStateJson = json;
  post_to_page(json);
}

std::wstring macro_path(const std::string& name) {
  return storage_dir() + L"\\" + widen(name) + L".gnmacro";
}

void handle_save(const std::string& name) {
  InputMacro* macro = gHooks.macro;
  if (!macro) return;
  if (macro->StepCount() == 0) {
    notify("저장할 동작이 없습니다.", true);
    return;
  }
  // Refused rather than sanitised: quietly renaming what someone typed leaves them looking for a
  // macro under a name they never chose.
  if (!macro_name_is_valid(name)) {
    notify("이 이름은 쓸 수 없습니다. \\ / : * ? \" < > | 는 뺀 이름을 지어 주세요.", true);
    return;
  }
  std::ofstream file(macro_path(name), std::ios::trunc | std::ios::binary);
  if (!file) {
    notify("파일을 쓸 수 없습니다.", true);
    return;
  }
  file << macro->Serialize();
  if (!file) {
    notify("저장 중 오류가 났습니다.", true);
    return;
  }
  notify("'" + name + "' 으로 저장했습니다.", false);
  publish_state(true);
}

void handle_load(const std::string& name) {
  InputMacro* macro = gHooks.macro;
  if (!macro || !macro_name_is_valid(name)) return;
  std::ifstream file(macro_path(name), std::ios::binary);
  if (!file) {
    notify("파일을 열 수 없습니다.", true);
    return;
  }
  std::ostringstream text;
  text << file.rdbuf();
  if (!macro->LoadSerialized(text.str())) {
    notify("매크로 파일이 아니거나 손상되었습니다.", true);
    return;
  }
  notify("'" + name + "' 을 불러왔습니다.", false);
  publish_state(true);
}

void handle_delete(const std::string& name) {
  if (!macro_name_is_valid(name)) return;
  if (DeleteFileW(macro_path(name).c_str())) {
    notify("'" + name + "' 을 삭제했습니다.", false);
  } else {
    notify("삭제하지 못했습니다.", true);
  }
  publish_state(true);
}

void handle_message(const std::string& json) {
  InputMacro* macro = gHooks.macro;
  if (!macro) return;
  const std::string type = macro_message_type(json);

  if (type == "ready") {
    publish_state(true);
    return;
  }
  if (type == "record") {
    macro->StartRecording(now_ms());
  } else if (type == "stop") {
    // One button for both, because the state already says which is running.
    if (macro->IsRecording()) macro->StopRecording();
    else macro->StopPlayback();
  } else if (type == "pause") {
    macro->SetPaused(!macro->IsPaused(), now_ms());
  } else if (type == "clear") {
    macro->Clear();
  } else if (type == "play") {
    MacroPlaybackOptions options{};
    if (macro_parse_play(json, &options)) {
      if (!macro->StartPlayback(options, now_ms(), static_cast<uint32_t>(now_ms()))) {
        notify("재생할 동작이 없습니다.", true);
      }
    }
  } else {
    size_t index = 0;
    MacroStep edit{};
    if (macro_parse_edit(json, &index, &edit)) {
      // The index came from a list the page drew, which may be a redraw behind.
      if (!macro->UpdateStep(index, edit.x, edit.y, edit.delayMs)) {
        notify("녹화나 재생 중에는 수정할 수 없습니다.", true);
      }
    } else if (macro_parse_index(json, "deleteStep", &index)) {
      macro->RemoveStep(index);
    } else {
      std::string name;
      if (macro_parse_name(json, "save", &name)) handle_save(name);
      else if (macro_parse_name(json, "load", &name)) handle_load(name);
      else if (macro_parse_name(json, "delete", &name)) handle_delete(name);
    }
  }
  publish_state(false);
}

/** Dispatches whatever playback owes, then refreshes the page if anything moved. */
void pump(HWND hwnd) {
  InputMacro* macro = gHooks.macro;
  if (!macro) return;
  MacroStep due{};
  while (macro->PollDueStep(now_ms(), &due)) {
    if (gHooks.sendStep) gHooks.sendStep(due);
  }
  publish_state(false);
  (void)hwnd;
}

void resize_webview() {
  if (!gController || !gWindow) return;
  RECT bounds{};
  GetClientRect(gWindow, &bounds);
  gController->put_Bounds(bounds);
}

std::wstring macro_page_uri() {
  const std::wstring local = executable_dir() + L"\\ui\\macro.html";
  if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) return L"file:///" + local;
  return L"file:///" + executable_dir() + L"\\..\\..\\..\\..\\apps\\native_poc\\ui\\macro.html";
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_SIZE:
      resize_webview();
      return 0;
    case WM_TIMER:
      if (wParam == kPumpTimerId) pump(hwnd);
      return 0;
    case WM_CLOSE:
      // Hidden rather than destroyed: a recording in progress must survive the window being
      // dismissed, and rebuilding the WebView each time is slow enough to be noticed.
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, kPumpTimerId);
      gController.Reset();
      gWebView.Reset();
      gWindow = nullptr;
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

void create_window(HINSTANCE instance, HWND owner) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = instance;
  wc.lpszClassName = kClassName;
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = CreateSolidBrush(RGB(0x0f, 0x13, 0x19));
  RegisterClassExW(&wc);  // Refuses quietly if already registered.

  gWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"매크로",
                            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                            520, 760, owner, nullptr, instance, nullptr);
  if (!gWindow) return;

  wchar_t userData[MAX_PATH]{};
  GetTempPathW(MAX_PATH, userData);
  wcscat_s(userData, L"GNLinkMacro");

  const HRESULT created = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData, nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env || !gWindow) return result;
            env->CreateCoreWebView2Controller(
                gWindow,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(hr) || !controller) return hr;
                      gController = controller;
                      gController->get_CoreWebView2(&gWebView);
                      resize_webview();

                      ComPtr<ICoreWebView2Settings> settings;
                      if (SUCCEEDED(gWebView->get_Settings(&settings)) && settings) {
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
                                  handle_message(narrow(raw));
                                  CoTaskMemFree(raw);
                                }
                                return S_OK;
                              })
                              .Get(),
                          &token);

                      gWebView->Navigate(macro_page_uri().c_str());
                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());
  if (FAILED(created)) {
    MessageBoxW(gWindow,
                L"WebView2 런타임을 찾을 수 없어 매크로 창을 열 수 없습니다.",
                L"GNLink", MB_ICONERROR);
  }
  SetTimer(gWindow, kPumpTimerId, kPumpIntervalMs, nullptr);
}

}  // namespace

void macro_window_toggle(HINSTANCE instance, HWND owner, const MacroWindowHooks& hooks) {
  gHooks = hooks;
  if (!gWindow) {
    create_window(instance, owner);
    if (!gWindow) return;
  }
  if (IsWindowVisible(gWindow)) {
    ShowWindow(gWindow, SW_HIDE);
    return;
  }
  ShowWindow(gWindow, SW_SHOW);
  SetForegroundWindow(gWindow);
  // The page keeps its own copy of the state, and it may have been hidden through several
  // changes, so a fresh one is pushed rather than waiting for the next difference.
  publish_state(true);
}

bool macro_window_visible() { return gWindow && IsWindowVisible(gWindow); }

void macro_window_destroy() {
  if (gWindow) DestroyWindow(gWindow);
  gWindow = nullptr;
  gLastStateJson.clear();
}

}  // namespace remote60::native_poc
