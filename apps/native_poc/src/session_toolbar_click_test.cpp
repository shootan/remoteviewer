// Proves the toolbar actually says what it did with a click.
//
// The diagnostics added for "대상 선택 does nothing" are only worth having if an empty log means
// "nobody pressed anything". Without this test an empty log has two readings -- the user did not
// click, or the logging was never wired -- and those are the two readings that have cost the most
// time on this project. So a real toolbar window is created and real WM_LBUTTONDOWN/UP messages
// are sent to it, and each of the three outcomes has to produce its own line.
//
// The button's position is discovered rather than assumed: the test sweeps x until the window
// reports a hit, so a layout change moves the test with it instead of breaking it.
//
// What this does NOT show: that a user's click reaches this window at all. That is the part that
// needs a live session, and it is the reason these lines exist.

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "client_session_toolbar.hpp"

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

std::vector<std::string> gLines;
int gTargetsInvoked = 0;

void pump(int ms) {
  const DWORD until = GetTickCount() + static_cast<DWORD>(ms);
  MSG msg;
  while (GetTickCount() < until) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Sleep(5);
  }
}

bool said(const std::string& needle) {
  for (const std::string& line : gLines) {
    if (line.find(needle) != std::string::npos) return true;
  }
  return false;
}

std::string last_line() { return gLines.empty() ? std::string() : gLines.back(); }

LRESULT CALLBACK owner_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  return DefWindowProcW(h, m, w, l);
}

/** LPARAM for a mouse message, in the toolbar's own client coordinates. */
LPARAM at(int x, int y) {
  return MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(y));
}

}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = owner_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60ToolbarClickTestOwner";
  RegisterClassExW(&wc);
  // Off screen and never shown: this must not appear in front of whoever is at the machine.
  HWND owner = CreateWindowExW(0, wc.lpszClassName, L"toolbar click test", WS_OVERLAPPEDWINDOW,
                               -4000, -4000, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
  if (!owner) {
    std::printf("FAIL  could not create the owner window\n");
    return 1;
  }

  remote60::native_poc::SessionToolbarCallbacks callbacks;
  callbacks.onLog = [](const std::string& line) { gLines.push_back(line); };
  callbacks.onTargets = [] { ++gTargetsInvoked; };
  ok(remote60::native_poc::session_toolbar_create(owner, std::move(callbacks)),
     "the toolbar window is created");
  // Shown, but at -4000,-4000: the bar only lays out its buttons when the owner is visible and
  // not minimised (reposition()), and a hidden owner leaves it 10x10 with nothing in it. Off
  // screen means Windows treats it as visible while the person at the machine sees nothing.
  ShowWindow(owner, SW_SHOWNOACTIVATE);
  remote60::native_poc::session_toolbar_set_visible(true);
  remote60::native_poc::session_toolbar_follow_owner();
  pump(400);

  HWND bar = FindWindowExW(nullptr, nullptr, L"Remote60SessionToolbar", nullptr);
  ok(bar != nullptr, "and can be found by its class");
  if (!bar) {
    std::printf("session_toolbar_click_test: FAIL (%d passed, %d failed)\n", gPass, gFail);
    return 1;
  }

  RECT bounds{};
  GetClientRect(bar, &bounds);
  std::printf("bar   client=%ldx%ld visible=%d\n", bounds.right - bounds.left,
              bounds.bottom - bounds.top, IsWindowVisible(bar) ? 1 : 0);
  ok(bounds.right - bounds.left > 0 && bounds.bottom - bounds.top > 0,
     "the bar has a size to click in",
     std::to_string(bounds.right - bounds.left) + "x" + std::to_string(bounds.bottom - bounds.top));
  const int midY = (bounds.bottom - bounds.top) / 2;

  // ------------------------------------------------------------------ find a button
  //
  // Swept rather than hard-coded. The ids come back in the log, which is also the first thing
  // this test proves: that pressing produces a line at all.
  int hitX = -1;
  std::string hitId;
  for (int x = 4; x < bounds.right - 4 && hitX < 0; x += 6) {
    gLines.clear();
    SendMessageW(bar, WM_LBUTTONDOWN, 0, at(x, midY));
    SendMessageW(bar, WM_LBUTTONUP, 0, at(x, midY));
    for (const std::string& line : gLines) {
      const size_t at_id = line.find("down id=");
      if (at_id == std::string::npos) continue;
      const std::string id = line.substr(at_id + 8, line.find(' ', at_id + 8) - at_id - 8);
      if (id != "0") {   // kButtonNone
        hitX = x;
        hitId = id;
      }
    }
  }
  ok(hitX >= 0, "a press on the bar reports which button it hit", "x=" + std::to_string(hitX) +
                                                                      " id=" + hitId);
  if (hitX < 0) {
    remote60::native_poc::session_toolbar_destroy();
    DestroyWindow(owner);
    std::printf("session_toolbar_click_test: FAIL (%d passed, %d failed)\n", gPass, gFail);
    return 1;
  }

  // ------------------------------------------------------------------ the ordinary press
  gLines.clear();
  const int invokedBefore = gTargetsInvoked;
  SendMessageW(bar, WM_LBUTTONDOWN, 0, at(hitX, midY));
  SendMessageW(bar, WM_LBUTTONUP, 0, at(hitX, midY));
  ok(said("[toolbar] down id=" + hitId), "pressing says which button went down", last_line());
  ok(!said("up ignored"), "and releasing on the same button is not ignored");
  if (hitId == "1") {  // kButtonTargets
    ok(said("targets clicked, callback present"), "the targets callback is reported as present");
    ok(gTargetsInvoked == invokedBefore + 1, "and it actually ran",
       "invoked=" + std::to_string(gTargetsInvoked));
  }

  // ------------------------------------------------------------------ 1a: no press recorded
  gLines.clear();
  SendMessageW(bar, WM_LBUTTONUP, 0, at(hitX, midY));
  ok(said("up ignored: no press recorded"),
     "a release with no press behind it says so rather than going quiet", last_line());

  // ------------------------------------------------------------------ 1b: released elsewhere
  //
  // The branch that looks exactly like a dead button from outside. Released far to the right, off
  // the end of the bar, so the hit test cannot land on the same id.
  gLines.clear();
  SendMessageW(bar, WM_LBUTTONDOWN, 0, at(hitX, midY));
  SendMessageW(bar, WM_LBUTTONUP, 0, at(bounds.right - 1, midY));
  ok(said("up ignored: released over"),
     "releasing off the button says the press was cancelled", last_line());
  ok(said("but pressed " + hitId), "and names the button that had been pressed", last_line());

  // ------------------------------------------------------------------ what the bar says about the path
  //
  // `relay` is a bool and the bar printed `relay ? 릴레이 : 직접`, so before anything had answered
  // it claimed a direct connection. A default is not a measurement. Asserted through the
  // product's own composer rather than a copy of its rules.
  auto line = [](bool connected, bool known, bool relay) {
    remote60::native_poc::SessionToolbarState s;
    s.connected = connected;
    s.inputOn = true;
    s.pathKnown = known;
    s.relay = relay;
    return remote60::native_poc::session_toolbar_status_text(s);
  };

  const std::wstring unknown = line(false, false, false);
  const std::wstring direct = line(true, true, false);
  const std::wstring relay = line(true, true, true);

  ok(unknown.find(L"직접") == std::wstring::npos,
     "an undecided path is NOT shown as a direct connection");
  ok(unknown.find(L"확인 중") != std::wstring::npos, "and says it is still being worked out");
  ok(relay.find(L"릴레이") != std::wstring::npos, "a relayed path says so");
  ok(direct.find(L"직접") != std::wstring::npos, "and a direct one says so");
  ok(unknown != direct && direct != relay && unknown != relay,
     "the three readings differ from each other in text, not only in colour");

  // ------------------------------------------------------------------ auto-hide while pressed
  //
  // Observed before being changed. The worry was that the bar hides itself out from under a
  // finger that is already on a button; `collapse_toolbar` has a guard for exactly that, and
  // this checks the guard rather than trusting the comment beside it.
  //
  // The mouse is reported far from the summon band, which is what arms the collapse timer, and
  // the loop runs well past the 800ms delay.
  SendMessageW(bar, WM_LBUTTONDOWN, 0, at(hitX, midY));
  remote60::native_poc::session_toolbar_notify_mouse(10, 400, 1280);
  pump(1200);
  ok(IsWindowVisible(bar) != 0,
     "the bar does not hide itself while a button is held down",
     IsWindowVisible(bar) ? std::string("still up after 1200ms") : std::string("vanished"));
  // Release it so the press does not leak into the next case.
  SendMessageW(bar, WM_LBUTTONUP, 0, at(hitX, midY));

  // And the other half: with nothing pressed or hovered, moving away DOES collapse it. Without
  // this the guard above would pass just as well on a bar that never hides at all.
  gLines.clear();
  remote60::native_poc::session_toolbar_notify_mouse(10, 400, 1280);
  pump(1500);
  ok(IsWindowVisible(bar) == 0,
     "and it does hide once nothing is pressed or hovered",
     IsWindowVisible(bar) ? std::string("still up") : std::string("hidden"));
  // Bring it back for whatever runs after this.
  remote60::native_poc::session_toolbar_set_visible(true);
  remote60::native_poc::session_toolbar_follow_owner();
  pump(300);

  // ------------------------------------------------------------------ 1b, seen and not only logged
  //
  // The three log branches above are covered, but a log is not what the user gets. 1b is the
  // hypothesis that the press was cancelled by releasing off the button: correct behaviour, and
  // without a visible pressed state it is indistinguishable from a button that does nothing --
  // which is exactly the report being chased.
  //
  // So the button is photographed rather than reasoned about. PrintWindow renders the bar into a
  // DIB and one pixel at the button's centre is read back.
  {
    auto shot = [&](int x, int y) -> COLORREF {
      RECT rc{};
      GetClientRect(bar, &rc);
      const int w = rc.right - rc.left;
      const int h = rc.bottom - rc.top;
      if (w <= 0 || h <= 0) return CLR_INVALID;
      HDC screen = GetDC(nullptr);
      HDC mem = CreateCompatibleDC(screen);
      BITMAPINFO bi{};
      bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
      bi.bmiHeader.biWidth = w;
      bi.bmiHeader.biHeight = -h;  // top-down, so y is a row index
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;
      void* bits = nullptr;
      HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
      HGDIOBJ old = SelectObject(mem, dib);
      const BOOL printed = PrintWindow(bar, mem, PW_RENDERFULLCONTENT);
      COLORREF out = CLR_INVALID;
      if (printed && bits && x >= 0 && x < w && y >= 0 && y < h) {
        const unsigned char* p =
            static_cast<const unsigned char*>(bits) + (static_cast<size_t>(y) * w + x) * 4;
        out = RGB(p[2], p[1], p[0]);
      }
      SelectObject(mem, old);
      DeleteObject(dib);
      DeleteDC(mem);
      ReleaseDC(nullptr, screen);
      return out;
    };
    auto hex = [](COLORREF c) {
      char buf[32];
      if (c == CLR_INVALID) return std::string("(no capture)");
      std::snprintf(buf, sizeof(buf), "%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
      return std::string(buf);
    };

    pump(120);
    const COLORREF idle = shot(hitX, midY);
    ok(idle != CLR_INVALID, "the bar can be photographed at all", hex(idle));
    std::printf("      probe visible=%d  bg=%s  edge=%s  btn=%s\n", IsWindowVisible(bar) ? 1 : 0,
                hex(shot(1, 1)).c_str(), hex(shot(hitX, 1)).c_str(), hex(shot(hitX, midY)).c_str());

    // (a) Pressed with the pointer never having moved over the bar. The bar summons itself under
    // a cursor that may be sitting still, and a still cursor generates no WM_MOUSEMOVE, so this is
    // a state a real user reaches. It used to draw nothing: the pressed look required a hover, and
    // hover is cleared by WM_MOUSELEAVE the moment the pointer is reported off the bar.
    SendMessageW(bar, WM_LBUTTONDOWN, 0, at(hitX, midY));
    pump(120);
    const COLORREF held = shot(hitX, midY);
    ok(held != idle, "a press shows on screen even if the pointer never moved over the bar",
       hex(idle) + " -> " + hex(held));

    // (b) Still held, pointer dragged off the button. This is the moment 1b turns on: the press is
    // about to be cancelled, and the question is whether the screen says so before the release.
    SendMessageW(bar, WM_MOUSEMOVE, 0, at(bounds.right - 1, midY));
    pump(120);
    const COLORREF draggedOff = shot(hitX, midY);
    ok(draggedOff == idle, "dragging off the button lifts the pressed look before the release",
       hex(held) + " -> " + hex(draggedOff));

    // (c) Dragged back on. The press is live again and looks it, so the two halves of the gesture
    // are legible in both directions rather than only on the way out.
    SendMessageW(bar, WM_MOUSEMOVE, 0, at(hitX, midY));
    pump(120);
    const COLORREF heldAgain = shot(hitX, midY);
    ok(heldAgain == held, "and comes back when the pointer returns",
       hex(draggedOff) + " -> " + hex(heldAgain));

    // (d) Released off the button: nothing runs, and the button is back to resting. This is the
    // whole of hypothesis 1b, end to end, with what the user sees at each step.
    gLines.clear();
    const int invokedBeforeCancel = gTargetsInvoked;
    SendMessageW(bar, WM_MOUSEMOVE, 0, at(bounds.right - 1, midY));
    SendMessageW(bar, WM_LBUTTONUP, 0, at(bounds.right - 1, midY));
    pump(120);
    ok(gTargetsInvoked == invokedBeforeCancel, "a press released off the button invokes nothing",
       "invoked=" + std::to_string(gTargetsInvoked));
    ok(shot(hitX, midY) == idle, "and the button returns to its resting look");

    // The camera's own control: a sample taken off the buttons has to differ from one taken on
    // them, or every comparison above is reading the same patch of background.
    ok(shot(1, 1) != idle, "the samples come from the button and not from the bar behind it",
       hex(shot(1, 1)) + " vs " + hex(idle));
  }

  // ------------------------------------------------------------------ the negative control
  //
  // If this ever passes while the assertions above also pass, the lines are being produced by
  // something other than the clicks.
  gLines.clear();
  pump(200);
  ok(gLines.empty(), "nothing is logged when nothing is clicked",
     std::to_string(gLines.size()) + " lines");

  remote60::native_poc::session_toolbar_destroy();
  DestroyWindow(owner);

  std::printf("session_toolbar_click_test: %s (%d passed, %d failed)\n", gFail == 0 ? "PASS" : "FAIL",
              gPass, gFail);
  return gFail == 0 ? 0 : 1;
}
