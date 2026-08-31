// See viewer_window_proc.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_window_proc.hpp"

#include "viewer_common.hpp"
#include "viewer_cursor_overlay.hpp"
#include "viewer_gdi_util.hpp"
#include "viewer_state.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_layout.hpp"
#include "viewer_log.hpp"
#include "viewer_nv12_renderer.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"
#include "viewer_present.hpp"

#include <iostream>

namespace remote60::native_poc::viewer {

// WM_RBUTTONDOWN / WM_RBUTTONUP / WM_MBUTTONDOWN / WM_MBUTTONUP: identical apart from the button bit
// (2 = right, 4 = middle) and the virtual key the host receives.
LRESULT on_secondary_button(ViewerState& ctx, HWND hwnd, bool down, uint16_t buttonBit, uint32_t vk, int x, int y) {
  if (qpc_now_us() < ctx.input.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
  if (point_in_toggle_button(ctx, hwnd, x, y)) return 0;
  if (point_in_macro_button(ctx, hwnd, x, y)) return 0;
  if (ctx.picker.visible.load(std::memory_order_relaxed)) return 0;
  if (point_in_panel_ui(ctx, hwnd, x, y)) return 0;
  if (kInputPolicyForceBlock) return 0;
  int32_t vx = 0;
  int32_t vy = 0;
  if (!map_client_point_to_video_coords(ctx, hwnd, x, y, &vx, &vy)) return 0;
  if (down) {
    SetCapture(hwnd);
    ctx.input.mouseButtons.fetch_or(buttonBit);
    enqueue_input_event(ctx, 2, vx, vy, 0, vk);
  } else {
    ctx.input.mouseButtons.fetch_and(static_cast<uint16_t>(~buttonBit));
    enqueue_input_event(ctx, 3, vx, vy, 0, vk);
    release_mouse_capture_if_idle(ctx, hwnd);
  }
  return 0;
}

// The picker's DOWN (mouse WM_LBUTTONDOWN and touch WM_POINTERDOWN): remember which target (if any)
// this press started on; the UP handler only selects when it ends on the same one. A press on empty
// picker space latches "none", and so does a press within the first 300ms after the picker appeared
// -- the gesture must START after the picker is stable, or a long-press begun against the old
// screen could still select (PickerState::PressTarget).
void picker_press(ViewerState& ctx, HWND hwnd, const ClientLayout& layout, int x, int y) {
  uint64_t pressedId = kPickerPressNone;
  uint64_t hitId = 0;
  if (point_in_rect(layout.desktopButtonRect, x, y)) {
    pressedId = 0;
  } else if (try_hit_window_list_item(ctx, hwnd, x, y, &hitId)) {
    pressedId = hitId;
  }
  ctx.picker.PressTarget(pressedId, qpc_now_us());
}

// The picker's UP (mouse WM_LBUTTONUP and touch WM_POINTERUP). Consumes the press latch first --
// any UP ends the gesture. Refresh needs no latch; selecting needs a picker that has been up for a
// moment (a click begun before it appeared must not land on a card) AND a DOWN that started on the
// same target (PickerState::SelectAllowed). Desktop is an explicit WindowSelect(0) even when desktop
// is already the selected target: one clean restart with a fresh generation, so the first-frame
// gate has something to wait on.
void picker_release(ViewerState& ctx, HWND hwnd, const ClientLayout& layout, int x, int y, const char* source) {
  const uint64_t pressedId = ctx.picker.ReleaseTarget();
  if (point_in_rect(layout.refreshButtonRect, x, y)) {
    queue_window_list_request(ctx, "window_list_request pending");
    InvalidateRect(hwnd, nullptr, FALSE);
    return;
  }
  const uint64_t nowUs = qpc_now_us();
  const uint64_t shownAgeMs = ctx.picker.ShownAgeMs(nowUs);
  if (point_in_rect(layout.desktopButtonRect, x, y)) {
    if (ctx.picker.SelectAllowed(pressedId, 0, nowUs) &&
        begin_pc_target_selection(ctx, 0, "desktop_select_requested")) {
      std::cout << "[native-video-client][picker] select source=" << source << " x=" << x << " y=" << y
                << " id=0 shownAgeMs=" << shownAgeMs << "\n";
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return;
  }
  uint64_t hitWindowId = 0;
  if (try_hit_window_list_item(ctx, hwnd, x, y, &hitWindowId)) {
    if (ctx.picker.SelectAllowed(pressedId, hitWindowId, nowUs) &&
        begin_pc_target_selection(ctx, hitWindowId, "window_select_requested")) {
      std::cout << "[native-video-client][picker] select source=" << source << " x=" << x << " y=" << y
                << " id=" << hitWindowId << " shownAgeMs=" << shownAgeMs << "\n";
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  }
}

// The Ctrl+Alt local hotkeys of WM_KEYDOWN: F5 refresh the window list, F9 capture overview,
// [ ] bitrate down/up, ; ' keyint down/up. True when consumed.
bool on_local_hotkey(ViewerState& ctx, HWND hwnd, WPARAM wp) {
  if (local_hotkey_modifiers_active() && wp == VK_F5) {
    queue_window_list_request(ctx, "window_list_request pending");
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_F9) {
    request_capture_overview_mode(ctx);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_4) {  // [
    apply_runtime_tune_delta(ctx, -1, 0);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_6) {  // ]
    apply_runtime_tune_delta(ctx, 1, 0);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_1) {  // ;
    apply_runtime_tune_delta(ctx, 0, -1);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_7) {  // '
    apply_runtime_tune_delta(ctx, 0, 1);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  return false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  // The state arrives with the creation parameters and is pinned to the window for its lifetime
  // (F-17). It is stamped with the handle here, before CreateWindowExW returns, so the messages the
  // creation itself generates already see the window the state describes.
  if (msg == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
    auto* state = create ? static_cast<ViewerState*>(create->lpCreateParams) : nullptr;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    if (state) state->session.hwnd = hwnd;
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  auto* state = reinterpret_cast<ViewerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!state) return DefWindowProcW(hwnd, msg, wp, lp);
  ViewerState& ctx = *state;
  switch (msg) {
    case WM_CLOSE:
      ctx.session.running = false;
      if (ctx.session.sock != INVALID_SOCKET) shutdown(ctx.session.sock, SD_BOTH);
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      remote60::native_poc::session_toolbar_destroy();
      destroy_cached_gdi_objects(ctx);
      PostQuitMessage(0);
      return 0;
    case kMsgApplyWindowList: {
      // Ownership of the copy arrives with the message (F-07). A message still queued when the
      // window dies is one small leak at exit, which is cheaper than a drain protocol.
      std::unique_ptr<ControlWindowListMessage> msg(reinterpret_cast<ControlWindowListMessage*>(lp));
      if (msg) apply_window_list_snapshot(ctx, *msg);
      return 0;
    }
    case kMsgRevealStreamView: {
      // The video thread saw the first frame of a selection and posted this once; CommitReveal
      // revalidates against the live selection state (see viewer_selection_gate.cpp).
      if (ctx.sel.CommitReveal()) {
        // A new stream episode begins here: the per-episode UI-thread state must not carry over
        // from the previous target (F-14). reportedSecure is control-thread state and is left
        // alone -- it is "say it once per process", which is still the right cadence.
        ctx.present.ResetForNewEpisode();
        ctx.session.nextToolbarPushUs = 0;
        // Dropping the picker guard opens both the paint path and the input guard (input handlers
        // early-return while the picker is up); clearing pending re-enables the picker's buttons.
        ctx.picker.visible.store(false, std::memory_order_relaxed);
        clear_pc_target_selection(ctx);
        remote60::native_poc::session_toolbar_set_visible(true);
        push_session_toolbar_state(ctx);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      ctx.sel.ReleaseRevealLatch();
      return 0;
    }
    // The toolbar is a window of its own, so it does not move with this one for free.
    case WM_WINDOWPOSCHANGED:
      remote60::native_poc::session_toolbar_follow_owner();
      return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_DPICHANGED: {
      ensure_ui_font(ctx, hwnd);
      const RECT* suggested = reinterpret_cast<const RECT*>(lp);
      if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;
    }
    case WM_MOUSEMOVE:
      // The toolbar hides itself so it stops blocking clicks, which leaves it deaf: a hidden
      // window gets no mouse events, so this window watches for the summoning dwell for it.
      if (!ctx.picker.visible.load(std::memory_order_relaxed)) {
        RECT toolbarZone{};
        GetClientRect(hwnd, &toolbarZone);
        remote60::native_poc::session_toolbar_notify_mouse(GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
                                                           toolbarZone.right);
      }
      if (qpc_now_us() < ctx.input.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (ctx.picker.visible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      if ((ctx.input.mouseButtons.load(std::memory_order_relaxed) & 0x7u) == 0) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        enqueue_input_event(ctx, 1, vx, vy, 0, 0);
      }
      return 0;
    case WM_LBUTTONDOWN:
      if (qpc_now_us() < ctx.input.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        ctx.picker.toggleDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (point_in_macro_button(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        ctx.picker.macroButtonDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (ctx.picker.visible.load(std::memory_order_relaxed)) {
        if (ctx.sel.pending.load(std::memory_order_acquire)) {
          ctx.picker.CancelPress();
          return 0;
        }
        picker_press(ctx, hwnd, compute_client_layout(ctx, hwnd), GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
      }
      if (point_in_panel_ui(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      SetFocus(hwnd);
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(ctx, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        ctx.input.mouseButtons.fetch_or(1);
        enqueue_input_event(ctx, 2, vx, vy, 0, VK_LBUTTON);
      }
      return 0;
    case WM_LBUTTONUP: {
      if (qpc_now_us() < ctx.input.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      const int x = GET_X_LPARAM(lp);
      const int y = GET_Y_LPARAM(lp);
      const ClientLayout layout = compute_client_layout(ctx, hwnd);
      if (ctx.picker.toggleDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.toggleButtonRect, x, y)) {
          set_picker_visible_and_sync_stream(ctx, 
              !ctx.picker.visible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (ctx.picker.macroButtonDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.macroButtonRect, x, y)) {
          toggle_macro_window(ctx, hwnd);
        }
        return 0;
      }
      if (ctx.picker.visible.load(std::memory_order_relaxed)) {
        // A selection already in flight owns the picker until its first frame arrives; ignore
        // further target clicks so a double-click cannot queue a second, racing select. (The
        // latch is dropped either way: any UP ends the gesture.)
        if (ctx.sel.pending.load(std::memory_order_acquire)) {
          ctx.picker.CancelPress();
          return 0;
        }
        picker_release(ctx, hwnd, layout, x, y, "mouse");
        return 0;
      }
      if (point_in_panel_ui(ctx, hwnd, x, y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(ctx, hwnd, x, y, &vx, &vy)) return 0;
        ctx.input.mouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        enqueue_input_event(ctx, 3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(ctx, hwnd);
      }
      return 0;
    }
    case WM_RBUTTONDOWN:
      return on_secondary_button(ctx, hwnd, true, 2, VK_RBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    case WM_RBUTTONUP:
      return on_secondary_button(ctx, hwnd, false, 2, VK_RBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    case WM_MBUTTONDOWN:
      return on_secondary_button(ctx, hwnd, true, 4, VK_MBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    case WM_MBUTTONUP:
      return on_secondary_button(ctx, hwnd, false, 4, VK_MBUTTON, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    case WM_MOUSEWHEEL: {
      if (qpc_now_us() < ctx.input.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      const ClientLayout layout = compute_client_layout(ctx, hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) return 0;
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) return 0;
      if (ctx.picker.visible.load(std::memory_order_relaxed)) {
        if (point_in_rect(layout.listRect, p.x, p.y)) {
          const int wheel = GET_WHEEL_DELTA_WPARAM(wp);
          scroll_window_list(ctx, hwnd, (wheel < 0) ? 1 : -1);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_panel_ui(ctx, hwnd, p.x, p.y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(ctx, hwnd, p.x, p.y, &vx, &vy)) return 0;
      enqueue_input_event(ctx, 4, vx, vy, GET_WHEEL_DELTA_WPARAM(wp), 0);
      return 0;
    }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP: {
      UINT32 pointerId = GET_POINTERID_WPARAM(wp);
      POINTER_INPUT_TYPE pointerType = PT_POINTER;
      if (!GetPointerType(pointerId, &pointerType) || pointerType != PT_TOUCH) {
        return DefWindowProcW(hwnd, msg, wp, lp);
      }
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      ctx.input.suppressMouseUntilUs.store(qpc_now_us() + 300000ULL, std::memory_order_relaxed);
      const ClientLayout layout = compute_client_layout(ctx, hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          ctx.picker.toggleDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP && ctx.picker.toggleDown.exchange(false, std::memory_order_relaxed)) {
          set_picker_visible_and_sync_stream(ctx, 
              !ctx.picker.visible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          ctx.picker.macroButtonDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP &&
                   ctx.picker.macroButtonDown.exchange(false, std::memory_order_relaxed)) {
          toggle_macro_window(ctx, hwnd);
        }
        return 0;
      }
      if (ctx.picker.visible.load(std::memory_order_relaxed)) {
        // A selection in flight owns the picker; also clear the latch so a gesture spanning the
        // pending window cannot leave a stale press behind.
        if (ctx.sel.pending.load(std::memory_order_acquire)) {
          ctx.picker.CancelPress();
          return 0;
        }
        if (msg == WM_POINTERDOWN) {
          picker_press(ctx, hwnd, layout, p.x, p.y);
          return 0;
        }
        if (msg == WM_POINTERUP) {
          picker_release(ctx, hwnd, layout, p.x, p.y, "touch");
        }
        return 0;
      }
      if (point_in_panel_ui(ctx, hwnd, p.x, p.y)) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(ctx, hwnd, p.x, p.y, &vx, &vy)) return 0;
      if (msg == WM_POINTERDOWN) {
        if (ctx.input.activeTouchDown.load(std::memory_order_relaxed)) return 0;
        SetFocus(hwnd);
        SetCapture(hwnd);
        ctx.input.activeTouchPointerId.store(pointerId, std::memory_order_relaxed);
        ctx.input.activeTouchDown.store(true, std::memory_order_relaxed);
        ctx.input.mouseButtons.fetch_or(1);
        enqueue_input_event(ctx, 2, vx, vy, 0, VK_LBUTTON);
      } else if (msg == WM_POINTERUPDATE) {
        if (!ctx.input.activeTouchDown.load(std::memory_order_relaxed) ||
            ctx.input.activeTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        enqueue_input_event(ctx, 1, vx, vy, 0, 0);
      } else {
        if (!ctx.input.activeTouchDown.load(std::memory_order_relaxed) ||
            ctx.input.activeTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        ctx.input.mouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        ctx.input.activeTouchDown.store(false, std::memory_order_relaxed);
        ctx.input.activeTouchPointerId.store(0, std::memory_order_relaxed);
        enqueue_input_event(ctx, 3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(ctx, hwnd);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
    case WM_POINTERCAPTURECHANGED:
      enqueue_release_for_pressed_mouse_buttons(ctx);
      ctx.input.activeTouchDown.store(false, std::memory_order_relaxed);
      ctx.input.activeTouchPointerId.store(0, std::memory_order_relaxed);
      // A gesture that lost capture mid-flight must not leave a stale picker press behind: the
      // whole point of the latch is that an UP without its own valid DOWN selects nothing.
      ctx.picker.CancelPress();
      return 0;
    case WM_IME_SETCONTEXT: {
      const LPARAM masked =
          lp & ~(static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 0) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 1) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 2) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 3) |
                 static_cast<LPARAM>(ISC_SHOWUIGUIDELINE));
      return DefWindowProcW(hwnd, msg, wp, masked);
    }
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_CHAR:
      return 0;
    case WM_IME_COMPOSITION:
      if (kInputPolicyForceBlock) return 0;
      (void)send_ime_result_text(ctx, hwnd, lp);
      return 0;
    case WM_KEYDOWN:
      if (on_local_hotkey(ctx, hwnd, wp)) return 0;
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_down(ctx, wp)) enqueue_input_event(ctx, 5, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_KEYUP:
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_up(ctx, wp)) enqueue_input_event(ctx, 6, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_SYSKEYDOWN:
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_down(ctx, wp)) enqueue_input_event(ctx, 5, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_SYSKEYUP:
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_up(ctx, wp)) enqueue_input_event(ctx, 6, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_KILLFOCUS:
      // Focus is about to leave, so no more key-ups will reach this window. Release whatever
      // is held now, before Alt/Win/Alt+Tab strands it on the host.
      if (!kInputPolicyForceBlock) enqueue_release_for_pressed_keys(ctx);
      ctx.picker.CancelPress();
      return 0;
    case WM_CHAR:
      // Ignored on purpose. Every physical key now travels the key-event path, and IME
      // composition results travel the text path from WM_IME_COMPOSITION. Emitting text here
      // too would double every printable -- and it was this handler's IME-suppression
      // bookkeeping, drifting after a Hangul commit, that swallowed digits and space. With the
      // two paths cleanly split, there is nothing left for WM_CHAR to do.
      return 0;
    case WM_SYSCHAR:
      return 0;
    case WM_ERASEBKGND:
      // Avoid background erase flicker between frames.
      return 1;
    case WM_TIMER:
      if (wp == kPacedPresentTimerId) {
        // One-shot: the held frame's wait is over (F-11).
        KillTimer(hwnd, kPacedPresentTimerId);
        request_video_paint(ctx, hwnd);
        return 0;
      }
      if (wp == kCursorOverlayTimerId) {
        update_cursor_overlay(ctx, hwnd);
        // Safety net, not the primary path: if a repaint request was ever lost, this notices that
        // a newer frame is sitting unpresented and asks again. Correctness lives in
        // request_video_paint; this only bounds the damage to one tick. (Viewer ledger F-20.)
        poll_video_paint_liveness(ctx, hwnd);
        return 0;
      }
      break;
    case WM_PAINT:
      return paint_video_frame(ctx, hwnd);
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

// UNICODE is not defined for this target, so the generic Win32 names resolve to the ANSI
// entry points. This window is registered and created wide, so every message API it touches
// must be the explicit *W form -- DefWindowProcA on a Unicode window read the wide title as
// ANSI and truncated it to "r", and delivered WM_CHAR as ANSI.
bool create_window(ViewerState& ctx) {
  HINSTANCE inst = GetModuleHandle(nullptr);
  const wchar_t* cls = L"Remote60NativeVideoClient";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // Keep background unmanaged so WM_ERASEBKGND can suppress flicker.
  wc.hbrBackground = nullptr;
  wc.lpszClassName = cls;
  if (!RegisterClassExW(&wc)) return false;

  ctx.session.hwnd = CreateWindowExW(0, cls, L"remote60 native video client",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          static_cast<int>(ctx.session.windowW), static_cast<int>(ctx.session.windowH),
                          nullptr, nullptr, inst, &ctx);  // lpParam: WndProc pins it at WM_NCCREATE
  if (!ctx.session.hwnd) return false;
  ensure_ui_font(ctx, ctx.session.hwnd);
  // The process is per-monitor DPI aware, so the requested size is physical pixels; rescale
  // to keep the intended logical size on scaled displays.
  if (ctx.ui.dpi != 96) {
    SetWindowPos(ctx.session.hwnd, nullptr, 0, 0, dpi_scale(ctx, static_cast<int>(ctx.session.windowW)),
                 dpi_scale(ctx, static_cast<int>(ctx.session.windowH)), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  ShowWindow(ctx.session.hwnd, SW_SHOW);
  UpdateWindow(ctx.session.hwnd);
  // Remote-cursor overlay cadence: 50ms is enough for a 30Hz feed and costs nothing when hidden.
  SetTimer(ctx.session.hwnd, kCursorOverlayTimerId, 50, nullptr);
  // The session starts on the picker; stamp its shown-time so the select debounce has one uniform
  // contract from the very first gesture instead of a special startup exemption.
  ctx.picker.shownAtUs.store(qpc_now_us(), std::memory_order_relaxed);
  return true;
}

}  // namespace remote60::native_poc::viewer
