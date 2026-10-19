// (2) input-path evidence for prelogin (prelogin-service r4 item 2): a product-equivalent demonstration
// that the key SendInput path reaches a focused window on the CURRENT (Default) desktop, and that the
// prelogin navigation filter (prelogin_input_filter.hpp) drops a character key so it never arrives. The
// only production difference is the TARGET desktop (Winlogon under SYSTEM = G); the injection mechanism
// and the filter decision are identical. This is an own-window SendInput test (NOT the shared viewer/
// clipboard e2e harness), so it is safe to run alongside other e2e. env-gated REMOTE60_ALLOW_UI_TEST.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "prelogin_input_filter.hpp"

using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
int gSkipped = 0;
void ok(bool c, const char* what) {
  std::printf(c ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!c) ++gFailures;
}

HWND g_main = nullptr, g_edit = nullptr;
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
bool make_window() {
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"PreloginInputE2EWnd";
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  if (!RegisterClassExW(&wc)) return false;
  g_main = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"PreloginInputE2E",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 400, 140,
                           nullptr, nullptr, wc.hInstance, nullptr);
  if (!g_main) return false;
  g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 10,
                           370, 26, g_main, reinterpret_cast<HMENU>(1), wc.hInstance, nullptr);
  return g_edit != nullptr;
}
bool focus_edit() {
  HWND fg = GetForegroundWindow();
  DWORD ft = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, me = GetCurrentThreadId();
  if (ft && ft != me) AttachThreadInput(me, ft, TRUE);
  ShowWindow(g_main, SW_SHOW);
  BringWindowToTop(g_main);
  SetForegroundWindow(g_main);
  SetFocus(g_edit);
  if (ft && ft != me) AttachThreadInput(me, ft, FALSE);
  MSG m;
  for (int i = 0; i < 50 && PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE); ++i) { TranslateMessage(&m); DispatchMessageW(&m); }
  return GetForegroundWindow() == g_main;
}
void pump() { MSG m; for (int i = 0; i < 200 && PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE); ++i) { TranslateMessage(&m); DispatchMessageW(&m); } }

// Inject a virtual-key press+release exactly as the host input path would (SendInput, wVk). This is the
// product injection mechanism (not KEYEVENTF_UNICODE).
void inject_vk(uint16_t vk) {
  INPUT in[2] = {};
  in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = vk;
  in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = vk; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, in, sizeof(INPUT));
}

// The prelogin host gate: inject only if the navigation filter permits (the host drops a filtered key).
void prelogin_inject(uint16_t vk) {
  if (IsPreloginNavKeyAllowed(vk)) inject_vk(vk);
}
std::wstring edit_text() { wchar_t b[128] = {}; GetWindowTextW(g_edit, b, 127); return b; }
}  // namespace

int main() {
  std::printf("prelogin input inject e2e (SendInput path + nav filter)\n");
  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_UI_TEST", nullptr, 0) == 0) {
    std::printf("  SKIP (set REMOTE60_ALLOW_UI_TEST=1 for the SendInput stage)\n");
    std::printf("prelogin_input_inject_e2e_test: SKIPPED\n");
    return 0;
  }
  if (!make_window() || !focus_edit()) {
    std::printf("  SKIP (no interactive foreground; display dependency)\n");
    std::printf("prelogin_input_inject_e2e_test: SKIPPED\n");
    if (g_main) DestroyWindow(g_main);
    return 0;
  }

  // Positive: the raw SendInput key path reaches the focused EDIT (a character key, injected directly).
  SetWindowTextW(g_edit, L"");
  pump();
  inject_vk('K');  // 0x4B -> 'k'
  for (int i = 0; i < 30; ++i) { pump(); Sleep(10); }
  ok(edit_text() == L"k", "the SendInput key path reaches the focused EDIT on the current desktop");

  // NEGATIVE: through the prelogin gate a character key is dropped -> nothing arrives.
  SetWindowTextW(g_edit, L"");
  pump();
  prelogin_inject('K');  // filtered out in prelogin
  for (int i = 0; i < 30; ++i) { pump(); Sleep(10); }
  ok(edit_text().empty(), "NEGATIVE: the prelogin gate drops a character key (nothing reaches the EDIT)");

  // A navigation key passes the gate (it would be injected). It produces no text in an EDIT, so assert
  // the gate decision + that injecting it does not add text (it is a caret move, not a character).
  ok(IsPreloginNavKeyAllowed(kVkRight), "a navigation key (Right) passes the prelogin gate");
  SetWindowTextW(g_edit, L"abc");
  pump();
  prelogin_inject(kVkRight);  // allowed -> injected; moves the caret, adds no character
  for (int i = 0; i < 20; ++i) { pump(); Sleep(10); }
  ok(edit_text() == L"abc", "the allowed navigation key injects without adding a character");

  DestroyWindow(g_main);
  std::printf("\nsummary: failures=%d skipped=%d\n", gFailures, gSkipped);
  if (gFailures != 0) { std::printf("prelogin_input_inject_e2e_test: FAIL\n"); return 1; }
  std::printf("prelogin_input_inject_e2e_test: PASS\n");
  return 0;
}
