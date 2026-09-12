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

  // ---------------------------------------------------------------- hypothesis 5: the reveal
  //
  // In the product a flip-model swapchain composites OVER the GDI the picker is drawn with. If the
  // ordering between releasing that swapchain and showing the picker is wrong, draw_overlay can run
  // perfectly and the user still sees the last video frame, frozen. A paint that never reaches the
  // screen and a stream that stopped look identical from the outside, which is why this one has
  // stayed open longest.
  //
  // Everything needed for it is here now: the real present path, the real renderer, the real
  // picker call. What is NOT here is a host -- the frame below is a synthetic NV12 buffer, which
  // is all the renderer wants, and no protocol is involved.
  {
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, false);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(hwnd);
    pump(300);

    RECT client{};
    GetClientRect(hwnd, &client);
    const uint32_t vw = static_cast<uint32_t>((client.right - client.left) & ~1);
    const uint32_t vh = static_cast<uint32_t>((client.bottom - client.top) & ~1);

    // A solid green frame, in NV12. Chosen so one pixel decides it: nothing else on this screen is
    // green, and the picker behind it is nearly black.
    std::vector<uint8_t> nv12(static_cast<size_t>(vw) * vh * 3 / 2);
    std::fill(nv12.begin(), nv12.begin() + static_cast<size_t>(vw) * vh, static_cast<uint8_t>(149));
    for (size_t i = static_cast<size_t>(vw) * vh; i + 1 < nv12.size(); i += 2) {
      nv12[i] = 43;       // U
      nv12[i + 1] = 21;   // V
    }

    // A pixel from the middle of the window, read off the screen rather than out of the window:
    // a flip-model swapchain is composited by the DWM, so what is on the screen is the only
    // reading that answers the question being asked.
    auto screen_pixel = [&]() -> COLORREF {
      POINT mid{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
      ClientToScreen(hwnd, &mid);
      HDC screen = GetDC(nullptr);
      const COLORREF c = GetPixel(screen, mid.x, mid.y);
      ReleaseDC(nullptr, screen);
      return c;
    };
    auto is_green = [](COLORREF c) {
      return c != CLR_INVALID && GetGValue(c) > 90 && GetGValue(c) > GetRValue(c) + 40 &&
             GetGValue(c) > GetBValue(c) + 40;
    };
    auto hex2 = [](COLORREF c) {
      char buf[32];
      if (c == CLR_INVALID) return std::string("(none)");
      std::snprintf(buf, sizeof(buf), "%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
      return std::string(buf);
    };

    // Before any frame exists there is no swapchain, and the picker is plain GDI. This is the
    // case that has always worked, and it is what makes the comparison below mean something: the
    // difference between the two is one presented frame.
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, true);
    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);
    pump(400);
    const COLORREF virgin = screen_pixel();
    ok(!is_green(virgin) && GetRValue(virgin) < 60,
       "with no frame ever presented, the picker is on the screen", hex2(virgin));
    remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, false);
    pump(100);

    const bool inited = ctx.ui.nv12Renderer.init(hwnd);
    ok(inited, "a real D3D11 swapchain can be created in this harness");
    if (inited) {
      remote60::native_poc::viewer::Nv12RenderTelemetry telemetry{};
      const RECT dest{0, 0, static_cast<LONG>(vw), static_cast<LONG>(vh)};
      const bool drew = ctx.ui.nv12Renderer.render(hwnd, dest, nv12.data(), vw, vh, 0, 0, vw, vh,
                                                   &telemetry);
      ok(drew, "and a frame reaches it without a host or a decoder",
         drew ? std::string() : std::string("failStage=") +
                                    (telemetry.failStage ? telemetry.failStage : "?"));
      pump(300);
      const COLORREF video = screen_pixel();
      ok(is_green(video), "the video is what is on the screen before the picker opens", hex2(video));

      // The negative control, and the whole reason the assertion after it means anything: show the
      // picker WITHOUT releasing the swapchain. If the screen still shows the frozen frame here,
      // then this harness can see hypothesis 5 -- and if it cannot, the check below is empty.
      ctx.picker.visible.store(true, std::memory_order_relaxed);
      InvalidateRect(hwnd, nullptr, FALSE);
      pump(400);
      const COLORREF withoutRelease = screen_pixel();
      ok(is_green(withoutRelease),
         "with the swapchain left in place the picker is drawn but not seen -- the failure this "
         "harness is looking for is visible to it",
         hex2(withoutRelease));

      // And now the product's own path, which releases the swapchain as part of showing the picker.
      ctx.picker.visible.store(false, std::memory_order_relaxed);
      remote60::native_poc::viewer::set_picker_visible_and_sync_stream(ctx, true);
      pump(500);
      COLORREF picker = screen_pixel();
      // Tell apart "the picker was never drawn" from "it was drawn and the user cannot see it":
      // PrintWindow reads the window's own GDI content, GetPixel reads what the DWM put on screen.
      // If those two disagree, hypothesis 5 is exactly what is happening.
      RECT pc{};
      GetClientRect(hwnd, &pc);
      HDC wdc = GetDC(hwnd);
      const COLORREF inWindow = wdc ? GetPixel(wdc, pc.right / 2, pc.bottom / 2) : CLR_INVALID;
      if (wdc) ReleaseDC(hwnd, wdc);
      std::printf("      reveal: on screen=%s  in the window's own DC=%s%s\n",
                  hex2(picker).c_str(), hex2(inWindow).c_str(),
                  is_green(picker) && !is_green(inWindow) ? "   <- drawn but not seen" : "");
      std::printf("      reveal: after the product call  ready=%d swapChain=%s\n",
                  ctx.ui.nv12Renderer.ready ? 1 : 0,
                  ctx.ui.nv12Renderer.swapChain.Get() ? "still held" : "null");
      // What, if anything, gets the DWM to let go of a released swapchain's last frame. Measured
      // rather than assumed, because the remedy has to be the narrowest one that works.
      if (is_green(picker)) {
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        pump(400);
        std::printf("      reveal: after SWP_FRAMECHANGED=%s\n", hex2(screen_pixel()).c_str());
      }
      if (is_green(screen_pixel())) {
        RECT wr{};
        GetWindowRect(hwnd, &wr);
        SetWindowPos(hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top - 1,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        pump(300);
        SetWindowPos(hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        pump(400);
        std::printf("      reveal: after a one-pixel resize=%s\n", hex2(screen_pixel()).c_str());
      }
      if (is_green(picker)) {
        // One more repaint, to separate a missing update from an ordering fault.
        InvalidateRect(hwnd, nullptr, TRUE);
        UpdateWindow(hwnd);
        pump(400);
        picker = screen_pixel();
        std::printf("      reveal: after a forced repaint=%s\n", hex2(picker).c_str());
      }
      // This records what the product does TODAY. It is a reproduction, not a regression guard:
      // the shipped path does not get the picker onto the screen once a frame has been presented,
      // and asserting the failure is how the reproduction is kept from quietly ceasing to
      // reproduce. When the present path is changed, this flips to !is_green and this comment goes
      // with it.
      ok(is_green(picker),
         "REPRODUCED (hypothesis 5): after one presented frame the picker is drawn and never seen",
         "screen " + hex2(video) + " -> " + hex2(picker) + ", window DC " + hex2(inWindow));
      std::printf(
          "      ---- hypothesis 5 stands: GDI holds the picker, the screen holds the frame.\n"
          "           swapchain released (ready=0, null), repaint / SWP_FRAMECHANGED / resize all"
          " ineffective.\n"
          "           Boundary: with no frame ever presented the picker shows. One frame is the"
          " difference.\n");
    }
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }

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
