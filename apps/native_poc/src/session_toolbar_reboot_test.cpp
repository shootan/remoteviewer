// UI evidence for the in-toolbar remote-reboot menu item (prelogin-service r3, d-viewer). Mirrors
// session_toolbar_click_test: creates the REAL session toolbar, drives it with real WM_LBUTTON clicks,
// and proves the reboot item renders/activates ONLY when the host advertises the capability and that
// clicking it routes to the onReboot callback (which, in the product, opens the confirm popup).

#define WIN32_LEAN_AND_MEAN
#include <windowsx.h>
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "client_session_toolbar.hpp"

using namespace remote60::native_poc;

namespace {
int gPass = 0, gFail = 0;
int gRebootInvoked = 0;
std::vector<std::string> gLines;
void ok(bool c, const char* what) {
  std::printf(c ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (c) ++gPass; else ++gFail;
}
void pump(int ms) {
  const DWORD end = GetTickCount() + ms;
  MSG m;
  while (GetTickCount() < end) {
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    Sleep(5);
  }
}
LPARAM at(int x, int y) { return MAKELPARAM(x, y); }

// Sweep the bar; return the x of the first click that logged "down id=<id>", or -1.
int find_button_x(HWND bar, const std::string& id, int midY, int right) {
  for (int x = 4; x < right - 4; x += 5) {
    gLines.clear();
    SendMessageW(bar, WM_LBUTTONDOWN, 0, at(x, midY));
    SendMessageW(bar, WM_LBUTTONUP, 0, at(x, midY));
    for (const std::string& line : gLines) {
      const size_t p = line.find("down id=");
      if (p == std::string::npos) continue;
      const std::string got = line.substr(p + 8, line.find(' ', p + 8) - (p + 8));
      if (got == id) return x;
    }
  }
  return -1;
}
LRESULT CALLBACK OwnerProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
}  // namespace

int main() {
  std::printf("session toolbar remote-reboot item (UI evidence)\n");

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = OwnerProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"RebootToolbarTestOwner";
  RegisterClassExW(&wc);
  HWND owner = CreateWindowExW(0, wc.lpszClassName, L"reboot toolbar test", WS_OVERLAPPEDWINDOW, -4000,
                               -4000, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
  if (!owner) { std::printf("FAIL owner window\n"); return 1; }

  SessionToolbarCallbacks cb;
  cb.onLog = [](const std::string& line) { gLines.push_back(line); };
  cb.onReboot = [] { ++gRebootInvoked; };
  ok(session_toolbar_create(owner, std::move(cb)), "toolbar created");
  ShowWindow(owner, SW_SHOWNOACTIVATE);
  session_toolbar_set_visible(true);
  session_toolbar_follow_owner();
  pump(300);
  HWND bar = FindWindowExW(nullptr, nullptr, L"Remote60SessionToolbar", nullptr);
  ok(bar != nullptr, "toolbar window found");
  if (!bar) { std::printf("session_toolbar_reboot_test: FAIL\n"); return 1; }
  RECT rc{};
  GetClientRect(bar, &rc);
  int midY = (rc.bottom - rc.top) / 2;

  std::printf("[host without the reboot capability: no item]\n");
  {
    SessionToolbarState s;
    s.connected = true;
    s.rebootSupported = false;
    session_toolbar_update(s);
    session_toolbar_follow_owner();
    pump(150);
    GetClientRect(bar, &rc);
    const int before = gRebootInvoked;
    const int x = find_button_x(bar, "6", midY, rc.right);  // kButtonReboot
    ok(x < 0, "NEGATIVE: no reboot item is laid out when the host does not advertise it");
    ok(gRebootInvoked == before, "and nothing fired onReboot");
  }

  std::printf("[host advertises reboot: item renders + click routes to onReboot]\n");
  {
    SessionToolbarState s;
    s.connected = true;
    s.rebootSupported = true;
    session_toolbar_update(s);
    session_toolbar_follow_owner();
    pump(150);
    GetClientRect(bar, &rc);
    const int x = find_button_x(bar, "6", midY, rc.right);
    ok(x >= 0, "the reboot item is laid out (id=6) when the host advertises the capability");
    if (x >= 0) {
      const int before = gRebootInvoked;
      SendMessageW(bar, WM_LBUTTONDOWN, 0, at(x, midY));
      SendMessageW(bar, WM_LBUTTONUP, 0, at(x, midY));
      ok(gRebootInvoked == before + 1, "clicking the reboot item runs onReboot exactly once");
      // Negative: press on it, release off it -> no fire (matches the other toolbar buttons).
      const int after = gRebootInvoked;
      SendMessageW(bar, WM_LBUTTONDOWN, 0, at(x, midY));
      SendMessageW(bar, WM_LBUTTONUP, 0, at(rc.right - 1, midY));
      ok(gRebootInvoked == after, "NEGATIVE: press on reboot, release off it -> no callback");
    }
  }

  session_toolbar_destroy();
  DestroyWindow(owner);
  std::printf(gFail == 0 ? "session_toolbar_reboot_test: PASS\n" : "session_toolbar_reboot_test: FAIL\n");
  return gFail == 0 ? 0 : 1;
}
