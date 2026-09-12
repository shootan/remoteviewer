// The shipped window procedure, linked on its own, with a real swapchain in front of it.
//
// Two of the surviving explanations for "대상 선택 does nothing" are decided here.
//
// HYPOTHESIS 4 -- the input gate. The viewer's mouse messages can be swallowed before they reach
// the picker at all: five `if (qpc_now_us() < suppressMouseUntilUs) return 0;` lines at the top of
// the mouse handlers in viewer_window_proc.cpp. Nothing had ever exercised them. A gate that
// swallows a click is indistinguishable from a dead button, and it used to leave no trace either
// way.
//
// HYPOTHESIS 5 -- the reveal. A flip-model swapchain composites OVER the GDI the picker is drawn
// with, so the picker can be painted correctly and never be seen. Reproducing that needs an actual
// swapchain, which is why this harness links the real present path (viewer_present.cpp,
// mf_h264_codec.cpp, d3d11/dxgi/d3dcompiler/mfplat/mf/mfuuid) and puts a synthetic NV12 frame
// through the shipped renderer. No host, no decoder, no protocol.
//
// ⚠️ An earlier version of this comment said the opposite -- that the harness could not speak to
// hypothesis 5 because there was no swapchain here. That was true when it was written and stopped
// being true in the same commit, and it sent a reviewer looking for a reproduction that was in
// front of them. A stale label on evidence is worse than no label.
//
// WHAT IS SHIPPED HERE: viewer_window_proc.cpp (the gate, the mouse branches, the picker press /
// release routing), viewer_picker.cpp, viewer_present.cpp, the NV12 renderer, the layout
// arithmetic, the session toolbar, the window class and creation.
//
// WHAT IS NOT: sixteen functions that viewer_window_proc.cpp calls for features this test does not
// exercise -- key forwarding, IME, the unlock prompt, the cursor overlay, the liveness polls -- and
// the macro window, which is a WebView2 window. They are defined at the bottom of this file as
// recording doubles. None of them implements a rule being tested; the one that sits on the path
// under test (enqueue_input_event) is there to be counted, which is what makes "the click was
// swallowed" measurable rather than asserted.
//
// ⚠️ This is one machine, one console session. Whether other GPUs and drivers behave the same way
// is not known, and that is part of the conclusion rather than a footnote to it. It has been run
// from two independent build trees, which rules out one tree's artefact and nothing else: same
// machine, same GPU, same driver. Two builds is not two machines.
//
// Build: remote60_viewer_window_proc_isolated_test (CMake).

#include <winsock2.h>

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "client_session_toolbar.hpp"
#include "viewer_cursor_overlay.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_layout_math.hpp"
#include "viewer_picker.hpp"
#include "viewer_present.hpp"
#include "viewer_session_watchdog.hpp"
#include "viewer_state.hpp"
#include "viewer_unlock.hpp"
#include "viewer_window_proc.hpp"

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

LPARAM at(int x, int y) { return MAKELPARAM(x, y); }

// A window list the picker can show, built the way the product receives one.
void give_the_picker_something_to_show(ViewerState& ctx) {
  remote60::native_poc::ControlWindowListMessage msg{};
  msg.seq = 1;
  msg.flags = 0;
  msg.selectedWindowId = 0;
  msg.itemCount = 1;
  msg.items[0].id = 7;
  std::snprintf(msg.items[0].title, sizeof(msg.items[0].title), "%s", "메모장");
  msg.items[0].width = 1280;
  msg.items[0].height = 720;
  ctx.picker.windowPanel.ApplyWindowList(msg, 8);
}

}  // namespace

std::vector<std::string> gToolbarLines;
int gInputEvents = 0;
int gVideoPaintRequests = 0;

int main() {
  ViewerState ctx;
  if (!remote60::native_poc::viewer::create_window(ctx)) {
    std::printf("viewer_window_proc_isolated_test: FAIL (no window)\n");
    return 1;
  }
  HWND hwnd = ctx.session.hwnd;
  pump(200);

  // A picker that is up, connected, and has been up long enough that the press latch will accept
  // a gesture (PickerState::SelectAllowed ignores anything begun within 300ms of it appearing).
  ctx.control.connected.store(true, std::memory_order_relaxed);
  give_the_picker_something_to_show(ctx);
  remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, true);
  pump(500);
  ok(ctx.picker.visible.load(std::memory_order_relaxed), "the picker is up");

  // The desktop card, found through the product's own layout arithmetic rather than a guess.
  const remote60::native_poc::viewer::ClientLayout layout =
      remote60::native_poc::viewer::compute_client_layout(ctx, hwnd);
  const remote60::native_poc::viewer::CardGridMetrics grid =
      remote60::native_poc::viewer::compute_card_grid(ctx, layout.listRect);
  const RECT card = remote60::native_poc::viewer::card_rect_for_slot(layout.listRect, grid, 0);
  const int cx = (card.left + card.right) / 2;
  const int cy = (card.top + card.bottom) / 2;

  auto click_the_card = [&]() {
    SendMessageW(hwnd, WM_LBUTTONDOWN, 0, at(cx, cy));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, at(cx, cy));
    pump(60);
  };
  auto forget_the_selection = [&]() {
    remote60::native_poc::viewer::clear_pc_target_selection(ctx);
    pump(30);
  };

  // ---------------------------------------------------------------- the gate open
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  forget_the_selection();
  click_the_card();
  ok(ctx.sel.pending.load(std::memory_order_acquire),
     "with the gate open, a click on the desktop card starts a selection");

  // ---------------------------------------------------------------- the gate armed
  //
  // 300ms is the window the touch path arms. Nothing about the click changes -- same window, same
  // coordinates, same messages -- so whatever happens here is the gate and nothing else.
  forget_the_selection();
  const int eventsBefore = gInputEvents;
  ctx.input.suppressMouseUntilUs.store(remote60::native_poc::viewer::qpc_now_us() + 300000ULL,
                                       std::memory_order_relaxed);
  click_the_card();
  ok(!ctx.sel.pending.load(std::memory_order_acquire),
     "with the gate armed, the same click starts nothing");
  ok(gInputEvents == eventsBefore,
     "and nothing was forwarded to the host either, so the message stopped at the gate",
     "events=" + std::to_string(gInputEvents - eventsBefore));
  // And it now leaves a trace. Before this counter the gate returned 0 in silence, so a swallowed
  // click and a button that does nothing produced the same empty log -- which is the pair the
  // whole "대상 선택 does nothing" investigation has been trying to tell apart.
  ok(ctx.input.suppressedMouseCount.load(std::memory_order_relaxed) >= 2,
     "and the gate counts what it swallowed instead of dropping it in silence",
     "swallowed=" +
         std::to_string(ctx.input.suppressedMouseCount.load(std::memory_order_relaxed)));

  // ---------------------------------------------------------------- and it lets go
  //
  // The gate is a window, not a latch. If it were sticky, a single touch would kill the mouse for
  // the rest of the session -- which is the shape the reported symptom would have.
  pump(400);
  forget_the_selection();
  click_the_card();
  ok(ctx.sel.pending.load(std::memory_order_acquire),
     "once the 300ms window passes, the same click works again");

  // ---------------------------------------------------------------- who can arm it
  //
  // The gate is armed in exactly one place (viewer_window_proc.cpp, the WM_POINTER* handler) and
  // cleared in one (viewer_startup.cpp). Mouse traffic must never arm it, or a mouse-only session
  // could gate itself -- which would put hypothesis 4 back in play for the reported machine.
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  forget_the_selection();
  SendMessageW(hwnd, WM_MOUSEMOVE, 0, at(cx, cy));
  click_the_card();
  SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), at(cx, cy));
  pump(60);
  ok(ctx.input.suppressMouseUntilUs.load(std::memory_order_relaxed) == 0,
     "mouse messages never arm the gate, so a mouse-only session cannot gate itself",
     "until=" + std::to_string(ctx.input.suppressMouseUntilUs.load(std::memory_order_relaxed)));

  // ---------------------------------------------------------------- the control
  //
  // Every assertion above rests on sel.pending being a real reading of the product. If the picker
  // accepted a click that lands nowhere near a card, it would be answering something other than
  // the question asked.
  forget_the_selection();
  SendMessageW(hwnd, WM_LBUTTONDOWN, 0, at(layout.listRect.right - 2, layout.listRect.bottom - 2));
  SendMessageW(hwnd, WM_LBUTTONUP, 0, at(layout.listRect.right - 2, layout.listRect.bottom - 2));
  pump(60);
  ok(!ctx.sel.pending.load(std::memory_order_acquire),
     "a click on empty picker space selects nothing, so the readings above are about the card");

  // ---------------------------------------------------------------- the real toolbar
  //
  // 대상 선택 lives on the session toolbar, a window of its own, and it is the only road back to
  // target selection mid-session. The callback below is the three lines from viewer_startup.cpp:200
  // -- reproduced, because that file is a main(); everything they call is the product's.
  remote60::native_poc::SessionToolbarCallbacks toolbarCallbacks;
  toolbarCallbacks.onLog = [](const std::string& line) { gToolbarLines.push_back(line); };
  toolbarCallbacks.onTargets = [&ctx] {
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, true);
    remote60::native_poc::viewer::push_session_toolbar_state(ctx);
    if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
  };
  ok(remote60::native_poc::session_toolbar_create(hwnd, std::move(toolbarCallbacks)),
     "the session toolbar exists");
  remote60::native_poc::session_toolbar_set_visible(true);
  remote60::native_poc::session_toolbar_follow_owner();
  pump(400);
  HWND bar = FindWindowExW(nullptr, nullptr, L"Remote60SessionToolbar", nullptr);
  ok(bar != nullptr, "and can be found");
  RECT bounds{};
  int midY = 0;
  int hitX = -1;
  if (bar) {
    GetClientRect(bar, &bounds);
    midY = (bounds.bottom - bounds.top) / 2;
    // Found by the id the button reports, not by a coordinate nobody maintains.
    for (int x = 4; x < bounds.right - 4 && hitX < 0; x += 6) {
      gToolbarLines.clear();
      SendMessageW(bar, WM_LBUTTONDOWN, 0, at(x, midY));
      SendMessageW(bar, WM_LBUTTONUP, 0, at(x, midY));
      for (const std::string& line : gToolbarLines) {
        if (line.find("down id=1") != std::string::npos) hitX = x;
      }
    }
  }
  ok(hitX >= 0, "the 대상 선택 button is where a click can reach it", "x=" + std::to_string(hitX));

  // ---------------------------------------------------------------- hypothesis 5: the reveal
  //
  // A flip-model swapchain composites OVER the GDI the picker used to be drawn with, and once it
  // has presented, GDI no longer reaches that window at all -- destroying the swapchain does not
  // give it back. So the picker was drawn correctly and nobody saw it. That was reproduced here
  // first and is documented behaviour of DXGI_SWAP_EFFECT_FLIP_*.
  //
  // The picker now goes through the same swapchain: same draw_overlay, same layout, same hit
  // testing, rendered into an offscreen DIB and presented. This walks the whole round trip and
  // reads the SCREEN, because the screen is the thing that was wrong -- the window's own DC was
  // right the entire time it was broken.
  //
  // No host, no decoder, no protocol: the frames are synthetic NV12.
  {
    using namespace remote60::native_poc::viewer;
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, false);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(hwnd);
    pump(300);

    auto client_size = [&](int* w, int* h) {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      *w = (rc.right - rc.left) & ~1;
      *h = (rc.bottom - rc.top) & ~1;
    };
    // A solid green frame in NV12. One pixel decides it: nothing else here is green and the picker
    // behind it is nearly black.
    auto green_frame = [](int w, int h) {
      std::vector<uint8_t> nv12(static_cast<size_t>(w) * h * 3 / 2);
      std::fill(nv12.begin(), nv12.begin() + static_cast<size_t>(w) * h, static_cast<uint8_t>(149));
      for (size_t i = static_cast<size_t>(w) * h; i + 1 < nv12.size(); i += 2) {
        nv12[i] = 43;
        nv12[i + 1] = 21;
      }
      return nv12;
    };
    auto show_video = [&]() {
      int w = 0, h = 0;
      client_size(&w, &h);
      const std::vector<uint8_t> nv12 = green_frame(w, h);
      remote60::native_poc::viewer::Nv12RenderTelemetry t{};
      const RECT dest{0, 0, w, h};
      const bool ok = ctx.ui.nv12Renderer.render(hwnd, dest, nv12.data(), static_cast<uint32_t>(w),
                                                 static_cast<uint32_t>(h), 0, 0,
                                                 static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                                 &t);
      ctx.present.hasPresentedAtLeastOneFrame = ok;
      pump(250);
      return ok;
    };
    // The screen, not the window. A window DC reads the GDI surface, which was correct throughout
    // the defect; only the composited screen shows what the user gets.
    auto screen_pixel = [&]() -> COLORREF {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      POINT mid{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
      ClientToScreen(hwnd, &mid);
      HDC screen = GetDC(nullptr);
      const COLORREF c = GetPixel(screen, mid.x, mid.y);
      ReleaseDC(nullptr, screen);
      return c;
    };
    auto window_dc_pixel = [&]() -> COLORREF {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      HDC wdc = GetDC(hwnd);
      const COLORREF c = wdc ? GetPixel(wdc, (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2)
                             : CLR_INVALID;
      if (wdc) ReleaseDC(hwnd, wdc);
      return c;
    };
    auto is_green = [](COLORREF c) {
      return c != CLR_INVALID && GetGValue(c) > 90 && GetGValue(c) > GetRValue(c) + 40 &&
             GetGValue(c) > GetBValue(c) + 40;
    };
    auto is_picker = [](COLORREF c) {
      return c != CLR_INVALID && GetRValue(c) < 60 && GetGValue(c) < 60 && GetBValue(c) < 60;
    };
    auto hex2 = [](COLORREF c) {
      char buf[32];
      if (c == CLR_INVALID) return std::string("(none)");
      std::snprintf(buf, sizeof(buf), "%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
      return std::string(buf);
    };
    // The toolbar's 대상 선택, pressed with real messages, going through the product's own
    // callback -- the same three-line wiring viewer_startup.cpp does.
    auto press_targets = [&]() {
      SendMessageW(bar, WM_LBUTTONDOWN, 0, at(hitX, midY));
      SendMessageW(bar, WM_LBUTTONUP, 0, at(hitX, midY));
      pump(350);
    };

    // (1) Before any frame. The window is an ordinary GDI window here and always worked; this is
    // the control that says the camera and the geometry are right.
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, true);
    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);
    pump(350);
    ok(ctx.ui.nv12Renderer.gdi_reaches_the_screen(),
       "before any present, GDI still reaches this window");
    ok(is_picker(screen_pixel()), "and the picker is on the screen", hex2(screen_pixel()));
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, false);
    pump(150);

    // (2) One video frame, which is the whole difference.
    ok(ctx.ui.nv12Renderer.init(hwnd), "a real D3D11 swapchain can be created in this harness");
    std::printf("      device=%s  swapEffect=%s\n", ctx.ui.nv12Renderer.usedWarp ? "WARP" : "hardware",
                ctx.ui.nv12Renderer.usedFlipModel ? "FLIP_DISCARD" : "DISCARD (legacy fallback)");
    ok(show_video(), "and a frame reaches it without a host or a decoder");
    ok(is_green(screen_pixel()), "the video is what is on the screen", hex2(screen_pixel()));
    ok(!ctx.ui.nv12Renderer.gdi_reaches_the_screen(),
       "and from now on GDI does NOT reach this window -- this is the contract, not a symptom");

    // (3) The round trip, driven by the product's own toolbar button.
    const uint64_t videoPresentsBefore = ctx.ui.nv12Renderer.videoPresentCount;
    const uint64_t pickerPresentsBefore = ctx.ui.nv12Renderer.pickerPresentCount;
    press_targets();
    ok(ctx.picker.visible.load(std::memory_order_relaxed),
       "pressing 대상 선택 on the real toolbar opens the picker");
    const COLORREF shown = screen_pixel();
    ok(is_picker(shown), "and the picker is ON THE SCREEN over a live swapchain", hex2(shown));
    ok(is_picker(window_dc_pixel()),
       "with the window's own DC agreeing -- the two readings used to disagree, which was the bug",
       hex2(window_dc_pixel()));
    ok(ctx.ui.nv12Renderer.pickerPresentCount > pickerPresentsBefore,
       "and it got there through the swapchain rather than by luck",
       "picker presents=" +
           std::to_string(ctx.ui.nv12Renderer.pickerPresentCount - pickerPresentsBefore));
    ok(ctx.ui.nv12Renderer.videoPresentCount == videoPresentsBefore,
       "showing the picker presented no video frames");

    // (4) A target is selected with a real click, and the video comes back.
    const ClientLayout pickLayout = compute_client_layout(ctx, hwnd);
    const CardGridMetrics pickGrid = compute_card_grid(ctx, pickLayout.listRect);
    const RECT firstCard = card_rect_for_slot(pickLayout.listRect, pickGrid, 0);
    remote60::native_poc::viewer::clear_pc_target_selection(ctx);
    SendMessageW(hwnd, WM_LBUTTONDOWN, 0,
                 at((firstCard.left + firstCard.right) / 2, (firstCard.top + firstCard.bottom) / 2));
    SendMessageW(hwnd, WM_LBUTTONUP, 0,
                 at((firstCard.left + firstCard.right) / 2, (firstCard.top + firstCard.bottom) / 2));
    pump(120);
    ok(ctx.sel.pending.load(std::memory_order_acquire),
       "a card on the presented picker is clickable -- the hit test still matches what is drawn");
    remote60::native_poc::viewer::clear_pc_target_selection(ctx);
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, false);
    ok(show_video(), "the stream resumes after the picker closes");
    ok(is_green(screen_pixel()), "and the video is back on the screen", hex2(screen_pixel()));

    // (5) Round trips. A single pass could pass on a swapchain that is quietly rebuilt each time;
    // this is the same window going back and forth.
    // The toolbar button only opens; the product closes the picker when a selection reveals or
    // when the in-window toggle is used, so the close here goes through the same product call.
    int tripsOk = 0;
    for (int trip = 0; trip < 3; ++trip) {
      press_targets();
      const bool up = is_picker(screen_pixel());
      set_picker_visible_and_sync_stream(ctx, false);
      pump(100);
      const bool back = show_video() && is_green(screen_pixel());
      if (up && back) ++tripsOk;
    }
    ok(tripsOk == 3, "three picker/video round trips, each seen on the screen",
       std::to_string(tripsOk) + "/3");

    // (6) Resize while the picker is up: the offscreen surface has to follow the client area, or
    // the presented picture is the previous size stretched or clipped.
    press_targets();
    RECT wr{};
    GetWindowRect(hwnd, &wr);
    SetWindowPos(hwnd, nullptr, 0, 0, (wr.right - wr.left) - 120, (wr.bottom - wr.top) - 80,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    pump(400);
    RECT resized{};
    GetClientRect(hwnd, &resized);
    ok(is_picker(screen_pixel()), "the picker survives a resize on screen", hex2(screen_pixel()));
    ok(ctx.ui.pickerW == resized.right - resized.left && ctx.ui.pickerH == resized.bottom - resized.top,
       "and the offscreen surface followed the new client size",
       "surface " + std::to_string(ctx.ui.pickerW) + "x" + std::to_string(ctx.ui.pickerH) +
           ", client " + std::to_string(resized.right - resized.left) + "x" +
           std::to_string(resized.bottom - resized.top));
    SetWindowPos(hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    pump(300);

    // (7) Thumbnails, wide and tall. They arrive after the picker is already up and repaint it, so
    // they are the case where a cached surface could go stale.
    {
      auto thumb = [](uint32_t tw, uint32_t th) {
        auto t = std::make_shared<remote60::native_poc::viewer::WindowThumb>();
        t->width = tw;
        t->height = th;
        t->bgra.assign(static_cast<size_t>(tw) * th * 4, 0xB0);
        return t;
      };
      std::lock_guard<std::mutex> lk(ctx.picker.thumbMu);
      ctx.picker.thumbs[0] = thumb(1920, 1080);
      ctx.picker.thumbs[7] = thumb(720, 1280);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    pump(350);
    ok(is_picker(screen_pixel()) || screen_pixel() != CLR_INVALID,
       "a repaint carrying wide and tall thumbnails still presents", hex2(screen_pixel()));

    // (8) What the video path pays for all this. The picker uploads only when the picker repaints;
    // if a video frame touched it, this number would climb.
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, false);
    pump(150);
    const uint64_t pickerBeforeVideoRun = ctx.ui.nv12Renderer.pickerPresentCount;
    int w = 0, h = 0;
    client_size(&w, &h);
    const std::vector<uint8_t> nv12 = green_frame(w, h);
    const RECT dest{0, 0, w, h};
    const uint64_t startUs = remote60::native_poc::viewer::qpc_now_us();
    int frames = 0;
    for (int i = 0; i < 60; ++i) {
      remote60::native_poc::viewer::Nv12RenderTelemetry t{};
      if (ctx.ui.nv12Renderer.render(hwnd, dest, nv12.data(), static_cast<uint32_t>(w),
                                     static_cast<uint32_t>(h), 0, 0, static_cast<uint32_t>(w),
                                     static_cast<uint32_t>(h), &t)) {
        ++frames;
      }
    }
    const uint64_t elapsedUs = remote60::native_poc::viewer::qpc_now_us() - startUs;
    std::printf("      video path: %d frames in %llu us  (%.0f us/frame, %dx%d)\n", frames,
                static_cast<unsigned long long>(elapsedUs),
                frames ? static_cast<double>(elapsedUs) / frames : 0.0, w, h);
    ok(frames == 60, "sixty video frames present with the picker closed",
       std::to_string(frames) + "/60");
    ok(ctx.ui.nv12Renderer.pickerPresentCount == pickerBeforeVideoRun,
       "and none of them uploaded the picker",
       "picker presents during video=" +
           std::to_string(ctx.ui.nv12Renderer.pickerPresentCount - pickerBeforeVideoRun));

    // (9) Nothing was torn down to achieve any of it.
    ok(ctx.control.connected.load(std::memory_order_relaxed),
       "the control channel was never dropped across the round trips");
    ok(ctx.ui.nv12Renderer.swapChain.Get() != nullptr,
       "and the swapchain was never released -- the old fix released it on every picker entry");

    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }

  remote60::native_poc::session_toolbar_destroy();
  DestroyWindow(hwnd);
  pump(50);
  std::printf("viewer_window_proc_isolated_test: %s (%d passed, %d failed)\n", gFail == 0 ? "PASS" : "FAIL",
              gPass, gFail);
  return gFail == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------
// The doubles.
//
// Everything below is a feature this test does not drive. They exist so viewer_window_proc.cpp can
// be linked on its own: without them the shipped window procedure drags in the decoder, the
// control client, the WebView2 macro window and the D3D present path, and the thing under test
// disappears into that. Two of them count calls, because "the click never got through" has to be
// observed somewhere.
namespace remote60::native_poc::viewer {

void enqueue_input_event(ViewerState&, uint16_t, int32_t, int32_t, int32_t, uint32_t) {
  ++gInputEvents;
}
void update_cursor_overlay(ViewerState&, HWND) {}
bool local_hotkey_modifiers_active() { return false; }
bool forward_key_down(ViewerState&, WPARAM) { return false; }
bool forward_key_up(ViewerState&, WPARAM) { return false; }
bool send_ime_result_text(ViewerState&, HWND, LPARAM) { return false; }
void release_mouse_capture_if_idle(ViewerState&, HWND) {}
void enqueue_release_for_pressed_mouse_buttons(ViewerState&) {}
void enqueue_release_for_pressed_keys(ViewerState&) {}
bool host_ime_mode(ViewerState&) { return false; }
bool enqueue_physical_key(ViewerState&, bool, uint16_t, uint16_t, bool, bool, bool) { return false; }
void toggle_macro_window(ViewerState&, HWND) {}
bool save_unlock_password(uint64_t, const std::wstring&) { return false; }
bool has_unlock_password(uint64_t) { return false; }
bool prompt_unlock_password(HWND, std::wstring*) { return false; }
void poll_session_liveness(ViewerState&, HWND) {}

}  // namespace remote60::native_poc::viewer

// The session toolbar itself is linked for real -- it is part of the chain under investigation.
// Only the macro window, which is a WebView2 window, stands in.
namespace remote60::native_poc {
bool macro_window_visible() { return false; }
}  // namespace remote60::native_poc
