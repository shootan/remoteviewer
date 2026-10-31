// Drives the whole "대상 선택" chain without a host, and watches each joint.
//
// The chain was described but never exercised end to end outside a live session:
//
//   real WM_LBUTTONDOWN/UP on the real toolbar window
//     -> the product's onTargets wiring
//        -> set_picker_visible_and_sync_stream(ctx, true)   [product function, not a copy]
//           -> picker.visible flips, swapchain released, toolbar hidden
//              -> draw_overlay paints the picker
//
// Every piece here is the shipped one. What is NOT shipped is the three-line lambda in
// viewer_startup.cpp that connects the toolbar callback to the picker call; that is reproduced,
// and the difference is stated rather than glossed over.
//
// The point is to decide between the five hypotheses for "the button does nothing" without
// waiting for a user's session: if the chain runs here, the fault is not in these joints.

// winsock2 first: the viewer headers bring in socket types and windows.h would redefine them.
#include <winsock2.h>

#include <windows.h>

#include <cstdio>
#include <string>
#include <cstdint>
#include <vector>

#include "client_session_toolbar.hpp"
#include "viewer_gdi_util.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"
#include "viewer_state.hpp"

namespace {

using remote60::native_poc::viewer::ViewerState;

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

LRESULT CALLBACK owner_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  return DefWindowProcW(h, m, w, l);
}

LPARAM at(int x, int y) { return MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(y)); }

}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = owner_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60PickerChainOwner";
  RegisterClassExW(&wc);
  HWND owner = CreateWindowExW(0, wc.lpszClassName, L"picker chain", WS_OVERLAPPEDWINDOW, -4000,
                               -4000, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);
  if (!owner) {
    std::printf("FAIL  no owner window\n");
    return 1;
  }
  ShowWindow(owner, SW_SHOWNOACTIVATE);

  ViewerState ctx;
  ctx.session.hwnd = owner;
  remote60::native_poc::viewer::ensure_ui_font(ctx, owner);
  ctx.control.connected.store(true, std::memory_order_relaxed);

  // The picker starts closed, which is the state a mid-session user is in when they reach for the
  // button. Starting it open would test nothing.
  ctx.picker.visible.store(false, std::memory_order_relaxed);

  remote60::native_poc::SessionToolbarCallbacks callbacks;
  callbacks.onLog = [](const std::string& line) { gLines.push_back(line); };
  // The three lines from viewer_startup.cpp:196. Reproduced, not shipped -- the rest of the
  // chain below is the product's own code.
  callbacks.onTargets = [&ctx] {
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, true);
    remote60::native_poc::viewer::push_session_toolbar_state(ctx);
    if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
  };
  ok(remote60::native_poc::session_toolbar_create(owner, std::move(callbacks)),
     "the toolbar exists");
  remote60::native_poc::session_toolbar_set_visible(true);
  remote60::native_poc::session_toolbar_follow_owner();
  pump(400);

  HWND bar = FindWindowExW(nullptr, nullptr, L"Remote60SessionToolbar", nullptr);
  ok(bar != nullptr, "and can be found");
  if (!bar) {
    std::printf("picker_open_chain_test: FAIL (%d passed, %d failed)\n", gPass, gFail);
    return 1;
  }

  RECT bounds{};
  GetClientRect(bar, &bounds);
  const int midY = (bounds.bottom - bounds.top) / 2;

  // Find 대상 선택 by its reported id rather than by a coordinate nobody maintains.
  int hitX = -1;
  for (int x = 4; x < bounds.right - 4 && hitX < 0; x += 6) {
    gLines.clear();
    SendMessageW(bar, WM_LBUTTONDOWN, 0, at(x, midY));
    SendMessageW(bar, WM_LBUTTONUP, 0, at(x, midY));
    if (said("down id=1")) hitX = x;   // kButtonTargets
  }
  ok(hitX >= 0, "the 대상 선택 button is where a click can reach it",
     "x=" + std::to_string(hitX));
  if (hitX < 0) {
    remote60::native_poc::session_toolbar_destroy();
    DestroyWindow(owner);
    std::printf("picker_open_chain_test: FAIL (%d passed, %d failed)\n", gPass, gFail);
    return 1;
  }

  // ------------------------------------------------------------------ the click, for real
  ctx.picker.visible.store(false, std::memory_order_relaxed);
  gLines.clear();
  SendMessageW(bar, WM_LBUTTONDOWN, 0, at(hitX, midY));
  SendMessageW(bar, WM_LBUTTONUP, 0, at(hitX, midY));
  pump(200);

  ok(said("targets clicked, callback present"), "hypothesis 1: the click reaches the callback");
  ok(ctx.picker.visible.load(std::memory_order_relaxed),
     "hypothesis 2: the picker's visible flag actually flips");

  // ------------------------------------------------------------------ does anything get drawn
  //
  // Pixels, not a log line. The picker writes "first paint after show" to stdout, which this
  // process cannot capture, and in any case "it said it painted" and "something is on screen"
  // are different claims. Both states are rendered into identical bitmaps and compared: shown
  // has to differ from hidden, and it has to differ a lot.
  //
  // Painted into a memory DC rather than waiting for WM_PAINT -- the owner is off screen and DWM
  // need not paint it. What is being decided is whether the picker HAS something to draw.
  // The client rect, not the window size. The picker fills `layout.clientRect`, which excludes
  // the title bar and borders -- measuring against the outer 1280x800 gave 93% and looked like a
  // gap in the drawing when it was a gap in the measurement.
  RECT clientRect{};
  GetClientRect(owner, &clientRect);
  const int kW = clientRect.right - clientRect.left;
  const int kH = clientRect.bottom - clientRect.top;
  std::printf("owner client=%dx%d\n", kW, kH);
  const auto render = [&](bool visible, std::vector<uint32_t>* out) {
    ctx.picker.visible.store(visible, std::memory_order_relaxed);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kW;
    bmi.bmiHeader.biHeight = -kH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ prev = SelectObject(mem, bitmap);
    remote60::native_poc::viewer::draw_overlay(ctx, mem);
    GdiFlush();
    out->assign(static_cast<uint32_t*>(bits), static_cast<uint32_t*>(bits) + kW * kH);
    SelectObject(mem, prev);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
  };

  std::vector<uint32_t> hidden;
  std::vector<uint32_t> shown;
  render(false, &hidden);
  render(true, &shown);

  // ⚠️ The first version of this asserted that most pixels change between the two states and got
  // 4%. The expectation was wrong, not the code: with no frame ever presented, the HIDDEN state
  // also fills the whole window -- with "연결하는 중…" -- so both renders are a full dark
  // rectangle and only the header, the card and the footer differ.
  //
  // So the two claims are separated. First: the picker covers the window, measured against a
  // bitmap nothing drew into.
  std::vector<uint32_t> blank(static_cast<size_t>(kW) * kH, 0u);
  size_t painted = 0;
  for (size_t i = 0; i < shown.size(); ++i) {
    if (shown[i] != blank[i]) ++painted;
  }
  const double coverage = static_cast<double>(painted) / static_cast<double>(shown.size());
  ok(coverage > 0.95, "hypothesis 3a: the shown picker paints the whole window",
     std::to_string(static_cast<int>(coverage * 100)) + "% covered");

  // Second: it is not the same picture as the closed state. Small is fine -- a header, a card and
  // a footer is not half a screen -- but zero would mean the click changed nothing visible.
  size_t differing = 0;
  for (size_t i = 0; i < shown.size(); ++i) {
    if (shown[i] != hidden[i]) ++differing;
  }
  const double share = static_cast<double>(differing) / static_cast<double>(shown.size());
  ok(share > 0.01, "hypothesis 3b: and it differs from the closed state",
     std::to_string(static_cast<int>(share * 1000) / 10.0) + "% of pixels differ");

  // ------------------------------------------------------------------ and the toolbar stands down
  ok(IsWindowVisible(bar) == 0,
     "the toolbar hides while the picker is up, so the two do not overlap",
     IsWindowVisible(bar) ? std::string("still up") : std::string("hidden"));

  // ------------------------------------------------------------------ closing puts it back
  remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, false);
  pump(200);
  ok(!ctx.picker.visible.load(std::memory_order_relaxed), "closing clears the flag");
  ok(IsWindowVisible(bar) != 0, "and the toolbar comes back",
     IsWindowVisible(bar) ? std::string("visible") : std::string("still hidden"));

  // ------------------------------------------------------------------ the negative control
  //
  // With the callback missing, the same click must NOT open anything. Without this the chain
  // above could be passing on something other than the click.
  remote60::native_poc::session_toolbar_destroy();
  {
    remote60::native_poc::SessionToolbarCallbacks bare;
    bare.onLog = [](const std::string& line) { gLines.push_back(line); };
    bare.onMacro = [] {};
    remote60::native_poc::session_toolbar_create(owner, std::move(bare));
    remote60::native_poc::session_toolbar_set_visible(true);
    remote60::native_poc::session_toolbar_follow_owner();
    pump(300);
    HWND bare_bar = FindWindowExW(nullptr, nullptr, L"Remote60SessionToolbar", nullptr);
    ctx.picker.visible.store(false, std::memory_order_relaxed);
    gLines.clear();
    if (bare_bar) {
      SendMessageW(bare_bar, WM_LBUTTONDOWN, 0, at(hitX, midY));
      SendMessageW(bare_bar, WM_LBUTTONUP, 0, at(hitX, midY));
      pump(200);
    }
    ok(!ctx.picker.visible.load(std::memory_order_relaxed),
       "negative control: with no 대상 선택 wiring the same click opens nothing");
  }

  remote60::native_poc::session_toolbar_destroy();
  DestroyWindow(owner);

  std::printf("picker_open_chain_test: %s (%d passed, %d failed)\n", gFail == 0 ? "PASS" : "FAIL",
              gPass, gFail);
  return gFail == 0 ? 0 : 1;
}
