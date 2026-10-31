// The mouse-button vocabulary, and the host's window-mode injection against a window this process
// owns. (mouse-xbutton r1)
//
// Part 1 is pure: wire bit <-> virtual key <-> MK_* / XBUTTON / MOUSEEVENTF_*. The value that
// matters most is the one that is easy to get wrong -- the wire's X1 is 0x8 and X2 0x10, Win32's
// MK_XBUTTON1 is 0x20 and MK_XBUTTON2 0x40, and they must never be passed through as if equal. The
// SendInput mapping (dwFlags + mouseData) is checked here rather than by sending it: the desktop
// path drives the real cursor and there is no isolated desktop to point it at.
//
// Part 2 runs the product's inject_background_input_event in WINDOW mode with captureTargetHwnd
// set to a window of this process, which confines it to PostMessage into that window and excludes
// every SendInput path by construction (the desktopMode branch is never entered). What is checked
// is what the window RECEIVED: WM_XBUTTONDOWN naming XBUTTON1 in its HIWORD with MK_XBUTTON1 in
// its LOWORD, the up without that bit, a left click still a left click -- and, for a key that is
// not a button at all, NO message. That last one is the bug: the pre-r1 default branch turned an
// unknown key into WM_LBUTTONDOWN.
//
// Build: remote60_mouse_button_map_test (CMake). Needs no host and no network.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include "host_input_inject.hpp"
#include "host_window_enum.hpp"
#include "mouse_button_map.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
}

std::string hex(unsigned long long v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%llx", v);
  return buf;
}

// ------------------------------------------------------------------------------ part 1: pure

void test_wire_and_vk() {
  std::printf("\n--- the wire bits and the virtual keys ---\n");
  check("VK_LBUTTON -> wire bit0", mouse_vk_to_wire(VK_LBUTTON) == kMouseWireLeft);
  check("VK_RBUTTON -> wire bit1", mouse_vk_to_wire(VK_RBUTTON) == kMouseWireRight);
  check("VK_MBUTTON -> wire bit2", mouse_vk_to_wire(VK_MBUTTON) == kMouseWireMiddle);
  check("VK_XBUTTON1 -> wire bit3 (0x8)", mouse_vk_to_wire(VK_XBUTTON1) == 0x8u);
  check("VK_XBUTTON2 -> wire bit4 (0x10)", mouse_vk_to_wire(VK_XBUTTON2) == 0x10u);
  check("0x07 (no such button) -> no wire bit", mouse_vk_to_wire(0x07) == 0);
  check("0 -> no wire bit", mouse_vk_to_wire(0) == 0);
  check("'A' -> no wire bit", mouse_vk_to_wire('A') == 0);
  check("the legacy mask is the low three bits", kMouseWireLegacyMask == 0x7u);
  check("the full mask is five bits", kMouseWireMask == 0x1Fu && kMouseWireXMask == 0x18u);
  check("only X1 / X2 are X buttons",
        mouse_vk_is_xbutton(VK_XBUTTON1) && mouse_vk_is_xbutton(VK_XBUTTON2) &&
            !mouse_vk_is_xbutton(VK_LBUTTON) && !mouse_vk_is_xbutton(0x07));
  check("XBUTTON1 <-> VK_XBUTTON1, XBUTTON2 <-> VK_XBUTTON2, else 0",
        mouse_xbutton_to_vk(XBUTTON1) == VK_XBUTTON1 && mouse_xbutton_to_vk(XBUTTON2) == VK_XBUTTON2 &&
            mouse_xbutton_to_vk(0) == 0 && mouse_xbutton_to_vk(3) == 0 &&
            mouse_vk_to_xbutton(VK_XBUTTON1) == XBUTTON1 && mouse_vk_to_xbutton(VK_XBUTTON2) == XBUTTON2 &&
            mouse_vk_to_xbutton(VK_LBUTTON) == 0);
}

void test_mk_state() {
  std::printf("\n--- wire bits -> the MK_* state word (the values differ) ---\n");
  check("wire X1 (0x8) -> MK_XBUTTON1 (0x20), not 0x8",
        mouse_wire_to_mk(kMouseWireX1) == MK_XBUTTON1 && MK_XBUTTON1 == 0x20,
        hex(mouse_wire_to_mk(kMouseWireX1)));
  check("wire X2 (0x10) -> MK_XBUTTON2 (0x40), not 0x10",
        mouse_wire_to_mk(kMouseWireX2) == MK_XBUTTON2 && MK_XBUTTON2 == 0x40,
        hex(mouse_wire_to_mk(kMouseWireX2)));
  check("L/R/M keep their MK values",
        mouse_wire_to_mk(kMouseWireLeft) == MK_LBUTTON && mouse_wire_to_mk(kMouseWireRight) == MK_RBUTTON &&
            mouse_wire_to_mk(kMouseWireMiddle) == MK_MBUTTON);
  check("all five together", mouse_wire_to_mk(kMouseWireMask) ==
                                 (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2));
  check("bits outside the wire mask are ignored", mouse_wire_to_mk(0xFFE0u) == 0);
  check("nothing held -> 0", mouse_wire_to_mk(0) == 0);
}

void test_messages() {
  std::printf("\n--- button edge -> window message + wParam ---\n");
  check("X1 down -> WM_XBUTTONDOWN", mouse_vk_to_message(2, VK_XBUTTON1) == WM_XBUTTONDOWN);
  check("X2 down -> WM_XBUTTONDOWN", mouse_vk_to_message(2, VK_XBUTTON2) == WM_XBUTTONDOWN);
  check("X1 up -> WM_XBUTTONUP", mouse_vk_to_message(3, VK_XBUTTON1) == WM_XBUTTONUP);
  check("L/R/M down/up unchanged",
        mouse_vk_to_message(2, VK_LBUTTON) == WM_LBUTTONDOWN && mouse_vk_to_message(3, VK_LBUTTON) == WM_LBUTTONUP &&
            mouse_vk_to_message(2, VK_RBUTTON) == WM_RBUTTONDOWN && mouse_vk_to_message(3, VK_RBUTTON) == WM_RBUTTONUP &&
            mouse_vk_to_message(2, VK_MBUTTON) == WM_MBUTTONDOWN && mouse_vk_to_message(3, VK_MBUTTON) == WM_MBUTTONUP);
  check("AN UNKNOWN KEY IS NO MESSAGE (it used to be WM_LBUTTONDOWN)",
        mouse_vk_to_message(2, 0x07) == 0 && mouse_vk_to_message(3, 0x07) == 0 &&
            mouse_vk_to_message(2, 0) == 0 && mouse_vk_to_message(2, 'A') == 0 &&
            mouse_vk_to_message(2, 0x1234) == 0);
  check("a kind that is not an edge is no message",
        mouse_vk_to_message(1, VK_LBUTTON) == 0 && mouse_vk_to_message(4, VK_XBUTTON1) == 0 &&
            mouse_vk_to_message(5, VK_LBUTTON) == 0);

  // The wParam: LOWORD = MK_* of everything held AFTER the edge, HIWORD = which X button.
  const WPARAM x1Down = mouse_button_message_wparam(2, VK_XBUTTON1, 0);
  check("X1 down from nothing: LOWORD MK_XBUTTON1, HIWORD XBUTTON1",
        LOWORD(x1Down) == MK_XBUTTON1 && HIWORD(x1Down) == XBUTTON1, hex(x1Down));
  const WPARAM x1Up = mouse_button_message_wparam(3, VK_XBUTTON1, kMouseWireX1);
  check("X1 up while X1 held: LOWORD without MK_XBUTTON1, HIWORD XBUTTON1",
        LOWORD(x1Up) == 0 && HIWORD(x1Up) == XBUTTON1, hex(x1Up));
  const WPARAM x2DownLeftHeld = mouse_button_message_wparam(2, VK_XBUTTON2, kMouseWireLeft);
  check("X2 down while left held: LOWORD MK_LBUTTON|MK_XBUTTON2, HIWORD XBUTTON2",
        LOWORD(x2DownLeftHeld) == (MK_LBUTTON | MK_XBUTTON2) && HIWORD(x2DownLeftHeld) == XBUTTON2,
        hex(x2DownLeftHeld));
  const WPARAM lDownX1Held = mouse_button_message_wparam(2, VK_LBUTTON, kMouseWireX1);
  check("left down while X1 held: LOWORD MK_LBUTTON|MK_XBUTTON1, HIWORD 0",
        LOWORD(lDownX1Held) == (MK_LBUTTON | MK_XBUTTON1) && HIWORD(lDownX1Held) == 0, hex(lDownX1Held));
  const WPARAM lUp = mouse_button_message_wparam(3, VK_LBUTTON, kMouseWireLeft | kMouseWireRight);
  check("left up while left+right held: LOWORD MK_RBUTTON only", LOWORD(lUp) == MK_RBUTTON && HIWORD(lUp) == 0,
        hex(lUp));
  const WPARAM junk = mouse_button_message_wparam(2, VK_LBUTTON, 0xFFFFu);
  check("held bits outside the wire mask never reach the wParam",
        LOWORD(junk) == (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2), hex(junk));
}

void test_sendinput() {
  std::printf("\n--- button edge -> SendInput dwFlags + mouseData ---\n");
  const MouseSendInput x1d = mouse_vk_to_sendinput(2, VK_XBUTTON1);
  check("X1 down -> MOUSEEVENTF_XDOWN, mouseData XBUTTON1",
        x1d.flags == MOUSEEVENTF_XDOWN && x1d.mouseData == XBUTTON1);
  const MouseSendInput x1u = mouse_vk_to_sendinput(3, VK_XBUTTON1);
  check("X1 up -> MOUSEEVENTF_XUP, mouseData XBUTTON1", x1u.flags == MOUSEEVENTF_XUP && x1u.mouseData == XBUTTON1);
  const MouseSendInput x2d = mouse_vk_to_sendinput(2, VK_XBUTTON2);
  const MouseSendInput x2u = mouse_vk_to_sendinput(3, VK_XBUTTON2);
  check("X2 -> XDOWN / XUP with mouseData XBUTTON2",
        x2d.flags == MOUSEEVENTF_XDOWN && x2d.mouseData == XBUTTON2 && x2u.flags == MOUSEEVENTF_XUP &&
            x2u.mouseData == XBUTTON2);
  check("L/R/M unchanged, mouseData 0",
        mouse_vk_to_sendinput(2, VK_LBUTTON).flags == MOUSEEVENTF_LEFTDOWN &&
            mouse_vk_to_sendinput(3, VK_LBUTTON).flags == MOUSEEVENTF_LEFTUP &&
            mouse_vk_to_sendinput(2, VK_RBUTTON).flags == MOUSEEVENTF_RIGHTDOWN &&
            mouse_vk_to_sendinput(3, VK_RBUTTON).flags == MOUSEEVENTF_RIGHTUP &&
            mouse_vk_to_sendinput(2, VK_MBUTTON).flags == MOUSEEVENTF_MIDDLEDOWN &&
            mouse_vk_to_sendinput(3, VK_MBUTTON).flags == MOUSEEVENTF_MIDDLEUP &&
            mouse_vk_to_sendinput(2, VK_LBUTTON).mouseData == 0);
  check("AN UNKNOWN KEY IS NO SENDINPUT (it used to be MOUSEEVENTF_LEFTDOWN)",
        mouse_vk_to_sendinput(2, 0x07).flags == 0 && mouse_vk_to_sendinput(3, 0x07).flags == 0 &&
            mouse_vk_to_sendinput(2, 0).flags == 0 && mouse_vk_to_sendinput(3, 'A').flags == 0);
  check("a kind that is not an edge is no SendInput",
        mouse_vk_to_sendinput(1, VK_XBUTTON1).flags == 0 && mouse_vk_to_sendinput(4, VK_LBUTTON).flags == 0);
}

// ------------------------------------------------------- part 2: the host's window-mode path

struct Seen {
  UINT msg = 0;
  WPARAM wp = 0;
  LPARAM lp = 0;
};

std::vector<Seen> gSeen;

LRESULT CALLBACK TargetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  const bool mouse = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
  if (mouse) {
    gSeen.push_back({msg, wp, lp});
    if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) return TRUE;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void pump() {
  MSG m;
  while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}

const char* result_name(InputInjectResult r) {
  switch (r) {
    case InputInjectResult::Injected: return "injected";
    case InputInjectResult::IgnoredMove: return "ignored-move";
    case InputInjectResult::NoTarget: return "no-target";
    case InputInjectResult::Unsupported: return "unsupported";
    case InputInjectResult::Failed: return "failed";
  }
  return "?";
}

// One event through the product function; what the window then received.
struct Outcome {
  InputInjectResult result = InputInjectResult::Failed;
  std::vector<Seen> messages;
};

Outcome inject(HWND target, uint16_t kind, uint32_t keyCode, uint16_t buttons, int32_t x, int32_t y,
               int32_t wheel = 0) {
  static const CaptureWindowCriteria noExplicitTarget{};
  static DesktopInputState state;
  std::atomic<uint64_t> targetId{static_cast<uint64_t>(reinterpret_cast<uintptr_t>(target))};
  ControlInputEventMessage input{};
  input.kind = kind;
  input.keyCode = keyCode;
  input.buttons = buttons;
  input.x = x;
  input.y = y;
  input.wheelDelta = wheel;
  gSeen.clear();
  Outcome out;
  std::string resolved;
  InputFailStage stage = InputFailStage::None;
  DWORD err = 0;
  // desktopMode=false and a captureTargetHwnd of our own: the PostMessage branch, nothing else.
  out.result = inject_background_input_event(input, noExplicitTarget, targetId, /*desktopMode=*/false,
                                             /*inputDomainW=*/640, /*inputDomainH=*/480, &state,
                                             &resolved, &stage, &err);
  pump();
  Sleep(20);  // a posted message is asynchronous even to ourselves
  pump();
  out.messages = gSeen;
  return out;
}

std::string describe(const Outcome& o) {
  std::string s = std::string("result=") + result_name(o.result) + " messages=" + std::to_string(o.messages.size());
  for (const auto& m : o.messages) s += " [" + hex(m.msg) + " wp=" + hex(m.wp) + "]";
  return s;
}

bool last_is(const Outcome& o, UINT msg) { return !o.messages.empty() && o.messages.back().msg == msg; }

void test_window_mode_injection() {
  std::printf("\n--- the product's window-mode injection into a window of this process ---\n");
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = TargetProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60MouseXButtonMapTarget";
  RegisterClassExW(&wc);
  // Off screen and never activated: a real top-level window the resolver can hit-test, that
  // shows nothing and takes no focus.
  HWND target = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                                L"remote60 mouse xbutton map target", WS_POPUP, -4000, -4000, 320, 240,
                                nullptr, nullptr, wc.hInstance, nullptr);
  check("a window of this process is up for the host function to post into", target != nullptr);
  if (!target) return;
  ShowWindow(target, SW_SHOWNOACTIVATE);
  pump();

  SetEnvironmentVariableA("REMOTE60_NATIVE_MOUSE_XBUTTONS", nullptr);
  check("the feature is on by default", mouse_xbuttons_enabled());

  {
    const Outcome o = inject(target, 2, VK_XBUTTON1, 0, 320, 240);
    const bool ok = o.result == InputInjectResult::Injected && o.messages.size() == 2 &&
                    o.messages[0].msg == WM_MOUSEMOVE && last_is(o, WM_XBUTTONDOWN);
    check("X1 DOWN ARRIVES AS WM_XBUTTONDOWN (after the positioning move)", ok, describe(o));
    if (ok) {
      const WPARAM wp = o.messages.back().wp;
      check("...naming XBUTTON1 in the HIWORD", HIWORD(wp) == XBUTTON1, hex(wp));
      check("...with MK_XBUTTON1 (0x20) held in the LOWORD, and no MK_LBUTTON",
            (LOWORD(wp) & MK_XBUTTON1) != 0 && (LOWORD(wp) & MK_LBUTTON) == 0, hex(wp));
      check("...and the move carried the state word only (HIWORD 0)",
            HIWORD(o.messages[0].wp) == 0 && LOWORD(o.messages[0].wp) == MK_XBUTTON1, hex(o.messages[0].wp));
      const int cx = static_cast<short>(LOWORD(o.messages.back().lp));
      const int cy = static_cast<short>(HIWORD(o.messages.back().lp));
      // (320,240) of a 640x480 input domain onto a 320x240 client: the centre, give or take rounding.
      check("...at the mapped client point", cx >= 159 && cx <= 161 && cy >= 119 && cy <= 121,
            std::to_string(cx) + "," + std::to_string(cy));
    }
  }
  {
    const Outcome o = inject(target, 3, VK_XBUTTON1, kMouseWireX1, 320, 240);
    const bool ok = o.result == InputInjectResult::Injected && last_is(o, WM_XBUTTONUP);
    check("X1 UP ARRIVES AS WM_XBUTTONUP", ok, describe(o));
    if (ok) {
      const WPARAM wp = o.messages.back().wp;
      check("...naming XBUTTON1, with MK_XBUTTON1 no longer held",
            HIWORD(wp) == XBUTTON1 && (LOWORD(wp) & MK_XBUTTON1) == 0, hex(wp));
    }
  }
  {
    const Outcome o = inject(target, 2, VK_XBUTTON2, kMouseWireLeft, 100, 100);
    const bool ok = o.result == InputInjectResult::Injected && last_is(o, WM_XBUTTONDOWN);
    check("X2 down while left is held -> WM_XBUTTONDOWN", ok, describe(o));
    if (ok) {
      const WPARAM wp = o.messages.back().wp;
      check("...naming XBUTTON2, LOWORD MK_LBUTTON|MK_XBUTTON2",
            HIWORD(wp) == XBUTTON2 && LOWORD(wp) == (MK_LBUTTON | MK_XBUTTON2), hex(wp));
    }
    const Outcome u = inject(target, 3, VK_XBUTTON2, kMouseWireLeft | kMouseWireX2, 100, 100);
    check("X2 up -> WM_XBUTTONUP naming XBUTTON2, MK_LBUTTON still held",
          u.result == InputInjectResult::Injected && last_is(u, WM_XBUTTONUP) &&
              HIWORD(u.messages.back().wp) == XBUTTON2 && LOWORD(u.messages.back().wp) == MK_LBUTTON,
          describe(u));
  }
  {
    const Outcome o = inject(target, 2, 0x07, 0, 320, 240);
    check("AN UNKNOWN KEY (0x07) POSTS NOTHING AND IS UNSUPPORTED -- not a left click",
          o.result == InputInjectResult::Unsupported && o.messages.empty(), describe(o));
    const Outcome z = inject(target, 2, 0, 0, 320, 240);
    check("...key 0 likewise", z.result == InputInjectResult::Unsupported && z.messages.empty(), describe(z));
    const Outcome a = inject(target, 3, 'A', 0, 320, 240);
    check("...and an up for a letter", a.result == InputInjectResult::Unsupported && a.messages.empty(),
          describe(a));
  }
  {
    const Outcome d = inject(target, 2, VK_LBUTTON, 0, 320, 240);
    const Outcome u = inject(target, 3, VK_LBUTTON, kMouseWireLeft, 320, 240);
    check("a left click is still a left click (regression)",
          d.result == InputInjectResult::Injected && last_is(d, WM_LBUTTONDOWN) &&
              LOWORD(d.messages.back().wp) == MK_LBUTTON && HIWORD(d.messages.back().wp) == 0 &&
              u.result == InputInjectResult::Injected && last_is(u, WM_LBUTTONUP) && LOWORD(u.messages.back().wp) == 0,
          describe(d) + " / " + describe(u));
    const Outcome r = inject(target, 2, VK_RBUTTON, 0, 320, 240);
    const Outcome m = inject(target, 2, VK_MBUTTON, kMouseWireRight, 320, 240);
    check("right and middle unchanged",
          last_is(r, WM_RBUTTONDOWN) && LOWORD(r.messages.back().wp) == MK_RBUTTON && last_is(m, WM_MBUTTONDOWN) &&
              LOWORD(m.messages.back().wp) == (MK_RBUTTON | MK_MBUTTON),
          describe(r) + " / " + describe(m));
  }
  {
    const Outcome o = inject(target, 1, 0, kMouseWireX1 | kMouseWireX2, 200, 150);
    check("a move with X1+X2 held carries MK_XBUTTON1|MK_XBUTTON2",
          o.result == InputInjectResult::Injected && last_is(o, WM_MOUSEMOVE) &&
              o.messages.back().wp == (MK_XBUTTON1 | MK_XBUTTON2),
          describe(o));
    const Outcome w = inject(target, 4, 0, kMouseWireX1, 200, 150, 120);
    check("a wheel with X1 held carries MK_XBUTTON1 and the delta",
          w.result == InputInjectResult::Injected && last_is(w, WM_MOUSEWHEEL) &&
              LOWORD(w.messages.back().wp) == MK_XBUTTON1 && static_cast<short>(HIWORD(w.messages.back().wp)) == 120,
          describe(w));
  }
  {
    // The kill switch: an X edge is Unsupported and posts nothing; left is untouched.
    SetEnvironmentVariableA("REMOTE60_NATIVE_MOUSE_XBUTTONS", "0");
    check("REMOTE60_NATIVE_MOUSE_XBUTTONS=0 turns the feature off", !mouse_xbuttons_enabled());
    const Outcome o = inject(target, 2, VK_XBUTTON1, 0, 320, 240);
    check("...an X1 down is then Unsupported and posts nothing",
          o.result == InputInjectResult::Unsupported && o.messages.empty(), describe(o));
    const Outcome u = inject(target, 3, VK_XBUTTON2, 0, 320, 240);
    check("...and an X2 up", u.result == InputInjectResult::Unsupported && u.messages.empty(), describe(u));
    const Outcome l = inject(target, 2, VK_LBUTTON, 0, 320, 240);
    check("...while a left click still lands", l.result == InputInjectResult::Injected && last_is(l, WM_LBUTTONDOWN),
          describe(l));
    SetEnvironmentVariableA("REMOTE60_NATIVE_MOUSE_XBUTTONS", "1");
    check("=1 turns it back on", mouse_xbuttons_enabled());
    SetEnvironmentVariableA("REMOTE60_NATIVE_MOUSE_XBUTTONS", "false");
    check("'false' turns it off", !mouse_xbuttons_enabled());
    SetEnvironmentVariableA("REMOTE60_NATIVE_MOUSE_XBUTTONS", nullptr);
    check("unset is on", mouse_xbuttons_enabled());
    const Outcome back = inject(target, 2, VK_XBUTTON1, 0, 320, 240);
    check("...and X1 lands again", back.result == InputInjectResult::Injected && last_is(back, WM_XBUTTONDOWN),
          describe(back));
  }
  DestroyWindow(target);
  pump();
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test_wire_and_vk();
  test_mk_state();
  test_messages();
  test_sendinput();
  test_window_mode_injection();
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED",
              gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
