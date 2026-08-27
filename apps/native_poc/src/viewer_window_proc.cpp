// See viewer_window_proc.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_window_proc.hpp"

#include "viewer_common.hpp"
#include "viewer_cursor_overlay.hpp"
#include "viewer_gdi_util.hpp"
#include "viewer_globals.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_layout.hpp"
#include "viewer_log.hpp"
#include "viewer_nv12_renderer.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"

namespace remote60::native_poc::viewer {

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CLOSE:
      gSession.running = false;
      if (gSession.sock != INVALID_SOCKET) shutdown(gSession.sock, SD_BOTH);
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      remote60::native_poc::session_toolbar_destroy();
      destroy_cached_gdi_objects();
      PostQuitMessage(0);
      return 0;
    case kMsgRevealStreamView: {
      // The video thread saw the first frame of a selection and posted this once; CommitReveal
      // revalidates against the live selection state (see viewer_selection_gate.cpp).
      if (gSel.CommitReveal()) {
        // Dropping the picker guard opens both the paint path and the input guard (input handlers
        // early-return while the picker is up); clearing pending re-enables the picker's buttons.
        gPicker.visible.store(false, std::memory_order_relaxed);
        clear_pc_target_selection();
        remote60::native_poc::session_toolbar_set_visible(true);
        push_session_toolbar_state();
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      gSel.ReleaseRevealLatch();
      return 0;
    }
    // The toolbar is a window of its own, so it does not move with this one for free.
    case WM_WINDOWPOSCHANGED:
      remote60::native_poc::session_toolbar_follow_owner();
      return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_DPICHANGED: {
      ensure_ui_font(hwnd);
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
      if (!gPicker.visible.load(std::memory_order_relaxed)) {
        RECT toolbarZone{};
        GetClientRect(hwnd, &toolbarZone);
        remote60::native_poc::session_toolbar_notify_mouse(GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
                                                           toolbarZone.right);
      }
      if (qpc_now_us() < gInput.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gPicker.visible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      if ((gInput.mouseButtons.load(std::memory_order_relaxed) & 0x7u) == 0) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        enqueue_input_event(1, vx, vy, 0, 0);
      }
      return 0;
    case WM_LBUTTONDOWN:
      if (qpc_now_us() < gInput.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        gPicker.toggleDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        gPicker.macroButtonDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (gPicker.visible.load(std::memory_order_relaxed)) {
        if (gSel.pending.load(std::memory_order_acquire)) {
          gPicker.CancelPress();
          return 0;
        }
        // Remember which target (if any) this press started on; the UP handler only selects when
        // it ends on the same one. A press on empty picker space latches "none", and so does a
        // press within the first 300ms after the picker appeared -- the gesture must START after
        // the picker is stable, or a long-press begun against the old screen could still select.
        const int dx = GET_X_LPARAM(lp);
        const int dy = GET_Y_LPARAM(lp);
        const ClientLayout downLayout = compute_client_layout(hwnd);
        uint64_t pressedId = kPickerPressNone;
        uint64_t hitId = 0;
        if (point_in_rect(downLayout.desktopButtonRect, dx, dy)) {
          pressedId = 0;
        } else if (try_hit_window_list_item(hwnd, dx, dy, &hitId)) {
          pressedId = hitId;
        }
        gPicker.PressTarget(pressedId, qpc_now_us());
        return 0;
      }
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      SetFocus(hwnd);
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        gInput.mouseButtons.fetch_or(1);
        enqueue_input_event(2, vx, vy, 0, VK_LBUTTON);
      }
      return 0;
    case WM_LBUTTONUP: {
      if (qpc_now_us() < gInput.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      const int x = GET_X_LPARAM(lp);
      const int y = GET_Y_LPARAM(lp);
      const ClientLayout layout = compute_client_layout(hwnd);
      if (gPicker.toggleDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.toggleButtonRect, x, y)) {
          set_picker_visible_and_sync_stream(
              !gPicker.visible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (gPicker.macroButtonDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.macroButtonRect, x, y)) {
          toggle_macro_window(hwnd);
        }
        return 0;
      }
      if (gPicker.visible.load(std::memory_order_relaxed)) {
        const uint64_t pressedId = gPicker.ReleaseTarget();
        // A selection already in flight owns the picker until its first frame arrives; ignore
        // further target clicks so a double-click cannot queue a second, racing select.
        if (gSel.pending.load(std::memory_order_acquire)) return 0;
        if (point_in_rect(layout.refreshButtonRect, x, y)) {
          queue_window_list_request("window_list_request pending");
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        // Mis-click guard: selecting needs a picker that has been up for a moment (a click begun
        // before it appeared must not land on a card) AND a DOWN that started on the same target.
        const uint64_t nowUs = qpc_now_us();
        if (!gPicker.ShownLongEnough(nowUs)) return 0;
        const uint64_t shownAgeMs = gPicker.ShownAgeMs(nowUs);
        if (point_in_rect(layout.desktopButtonRect, x, y)) {
          if (pressedId != 0) return 0;
          // Explicit WindowSelect(0) even when desktop is already the selected target: one clean
          // restart with a fresh generation, so the first-frame gate has something to wait on.
          if (begin_pc_target_selection(0, "desktop_select_requested")) {
            std::cout << "[native-video-client][picker] select source=mouse x=" << x << " y=" << y
                      << " id=0 shownAgeMs=" << shownAgeMs << "\n";
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          return 0;
        }
        uint64_t hitWindowId = 0;
        if (try_hit_window_list_item(hwnd, x, y, &hitWindowId)) {
          if (pressedId != hitWindowId) return 0;
          if (begin_pc_target_selection(hitWindowId, "window_select_requested")) {
            std::cout << "[native-video-client][picker] select source=mouse x=" << x << " y=" << y
                      << " id=" << hitWindowId << " shownAgeMs=" << shownAgeMs << "\n";
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          return 0;
        }
        return 0;
      }
      if (point_in_rect(layout.refreshButtonRect, x, y)) {
        queue_window_list_request("window_list_request pending");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (point_in_rect(layout.desktopButtonRect, x, y)) {
        queue_window_select_request(0, "desktop_select_requested");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      uint64_t hitWindowId = 0;
      if (try_hit_window_list_item(hwnd, x, y, &hitWindowId)) {
        queue_window_select_request(hitWindowId, "window_select_requested");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (point_in_panel_ui(hwnd, x, y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, x, y, &vx, &vy)) return 0;
        gInput.mouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        enqueue_input_event(3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    }
    case WM_RBUTTONDOWN:
      if (qpc_now_us() < gInput.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gPicker.visible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        gInput.mouseButtons.fetch_or(2);
        enqueue_input_event(2, vx, vy, 0, VK_RBUTTON);
      }
      return 0;
    case WM_RBUTTONUP:
      if (qpc_now_us() < gInput.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gPicker.visible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        gInput.mouseButtons.fetch_and(static_cast<uint16_t>(~2u));
        enqueue_input_event(3, vx, vy, 0, VK_RBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    case WM_MBUTTONDOWN:
      if (qpc_now_us() < gInput.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gPicker.visible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        gInput.mouseButtons.fetch_or(4);
        enqueue_input_event(2, vx, vy, 0, VK_MBUTTON);
      }
      return 0;
    case WM_MBUTTONUP:
      if (qpc_now_us() < gInput.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gPicker.visible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        gInput.mouseButtons.fetch_and(static_cast<uint16_t>(~4u));
        enqueue_input_event(3, vx, vy, 0, VK_MBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    case WM_MOUSEWHEEL: {
      if (qpc_now_us() < gInput.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      const ClientLayout layout = compute_client_layout(hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) return 0;
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) return 0;
      if (gPicker.visible.load(std::memory_order_relaxed)) {
        if (point_in_rect(layout.listRect, p.x, p.y)) {
          const int wheel = GET_WHEEL_DELTA_WPARAM(wp);
          scroll_window_list(hwnd, (wheel < 0) ? 1 : -1);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_rect(layout.listRect, p.x, p.y)) {
        const int wheel = GET_WHEEL_DELTA_WPARAM(wp);
        scroll_window_list(hwnd, (wheel < 0) ? 1 : -1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (point_in_panel_ui(hwnd, p.x, p.y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(hwnd, p.x, p.y, &vx, &vy)) return 0;
      enqueue_input_event(4, vx, vy, GET_WHEEL_DELTA_WPARAM(wp), 0);
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
      gInput.suppressMouseUntilUs.store(qpc_now_us() + 300000ULL, std::memory_order_relaxed);
      const ClientLayout layout = compute_client_layout(hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          gPicker.toggleDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP && gPicker.toggleDown.exchange(false, std::memory_order_relaxed)) {
          set_picker_visible_and_sync_stream(
              !gPicker.visible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          gPicker.macroButtonDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP &&
                   gPicker.macroButtonDown.exchange(false, std::memory_order_relaxed)) {
          toggle_macro_window(hwnd);
        }
        return 0;
      }
      if (gPicker.visible.load(std::memory_order_relaxed)) {
        // A selection in flight owns the picker; also clear the latch so a gesture spanning the
        // pending window cannot leave a stale press behind.
        if (gSel.pending.load(std::memory_order_acquire)) {
          gPicker.CancelPress();
          return 0;
        }
        if (msg == WM_POINTERDOWN) {
          // Same DOWN/UP-on-the-same-target latch as the mouse path, including the "gesture must
          // start after the picker is 300ms stable" rule.
          uint64_t pressedId = kPickerPressNone;
          uint64_t hitId = 0;
          if (point_in_rect(layout.desktopButtonRect, p.x, p.y)) {
            pressedId = 0;
          } else if (try_hit_window_list_item(hwnd, p.x, p.y, &hitId)) {
            pressedId = hitId;
          }
          gPicker.PressTarget(pressedId, qpc_now_us());
          return 0;
        }
        if (msg == WM_POINTERUP) {
          const uint64_t pressedId = gPicker.ReleaseTarget();
          const uint64_t nowUs = qpc_now_us();
          const bool shownLongEnough = gPicker.ShownLongEnough(nowUs);
          const uint64_t shownAgeMs = gPicker.ShownAgeMs(nowUs);
          if (point_in_rect(layout.refreshButtonRect, p.x, p.y)) {
            queue_window_list_request("window_list_request pending");
            InvalidateRect(hwnd, nullptr, FALSE);
          } else if (point_in_rect(layout.desktopButtonRect, p.x, p.y)) {
            if (shownLongEnough && pressedId == 0 &&
                begin_pc_target_selection(0, "desktop_select_requested")) {
              std::cout << "[native-video-client][picker] select source=touch x=" << p.x
                        << " y=" << p.y << " id=0 shownAgeMs=" << shownAgeMs << "\n";
              InvalidateRect(hwnd, nullptr, FALSE);
            }
          } else {
            uint64_t hitWindowId = 0;
            if (try_hit_window_list_item(hwnd, p.x, p.y, &hitWindowId)) {
              if (shownLongEnough && pressedId == hitWindowId &&
                  begin_pc_target_selection(hitWindowId, "window_select_requested")) {
                std::cout << "[native-video-client][picker] select source=touch x=" << p.x
                          << " y=" << p.y << " id=" << hitWindowId
                          << " shownAgeMs=" << shownAgeMs << "\n";
                InvalidateRect(hwnd, nullptr, FALSE);
              }
            }
          }
        }
        return 0;
      }
      if (point_in_panel_ui(hwnd, p.x, p.y)) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(hwnd, p.x, p.y, &vx, &vy)) return 0;
      if (msg == WM_POINTERDOWN) {
        if (gInput.activeTouchDown.load(std::memory_order_relaxed)) return 0;
        SetFocus(hwnd);
        SetCapture(hwnd);
        gInput.activeTouchPointerId.store(pointerId, std::memory_order_relaxed);
        gInput.activeTouchDown.store(true, std::memory_order_relaxed);
        gInput.mouseButtons.fetch_or(1);
        enqueue_input_event(2, vx, vy, 0, VK_LBUTTON);
      } else if (msg == WM_POINTERUPDATE) {
        if (!gInput.activeTouchDown.load(std::memory_order_relaxed) ||
            gInput.activeTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        enqueue_input_event(1, vx, vy, 0, 0);
      } else {
        if (!gInput.activeTouchDown.load(std::memory_order_relaxed) ||
            gInput.activeTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        gInput.mouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        gInput.activeTouchDown.store(false, std::memory_order_relaxed);
        gInput.activeTouchPointerId.store(0, std::memory_order_relaxed);
        enqueue_input_event(3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
    case WM_POINTERCAPTURECHANGED:
      enqueue_release_for_pressed_mouse_buttons();
      gInput.activeTouchDown.store(false, std::memory_order_relaxed);
      gInput.activeTouchPointerId.store(0, std::memory_order_relaxed);
      // A gesture that lost capture mid-flight must not leave a stale picker press behind: the
      // whole point of the latch is that an UP without its own valid DOWN selects nothing.
      gPicker.CancelPress();
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
      (void)send_ime_result_text(hwnd, lp);
      return 0;
    case WM_KEYDOWN:
      if (local_hotkey_modifiers_active() && wp == VK_F5) {
        queue_window_list_request("window_list_request pending");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_F9) {
        request_capture_overview_mode();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_4) {  // [
        apply_runtime_tune_delta(-1, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_6) {  // ]
        apply_runtime_tune_delta(1, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_1) {  // ;
        apply_runtime_tune_delta(0, -1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_7) {  // '
        apply_runtime_tune_delta(0, 1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_down(wp)) enqueue_input_event(5, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_KEYUP:
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_up(wp)) enqueue_input_event(6, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_SYSKEYDOWN:
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_down(wp)) enqueue_input_event(5, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_SYSKEYUP:
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_up(wp)) enqueue_input_event(6, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_KILLFOCUS:
      // Focus is about to leave, so no more key-ups will reach this window. Release whatever
      // is held now, before Alt/Win/Alt+Tab strands it on the host.
      if (!kInputPolicyForceBlock) enqueue_release_for_pressed_keys();
      gPicker.CancelPress();
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
      if (wp == kCursorOverlayTimerId) {
        update_cursor_overlay(hwnd);
        return 0;
      }
      break;
    case WM_PAINT: {
      gFrameBuf.paintQueued = false;
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      const uint64_t paintStartUs = qpc_now_us();
      const ClientLayout layout = compute_client_layout(hwnd);
      const RECT& videoRect = layout.videoRect;
      const RECT contentRect = resolve_video_content_rect(hwnd, videoRect);
      const bool pickerVisible = gPicker.visible.load(std::memory_order_relaxed);

      std::shared_ptr<std::vector<uint8_t>> local;
      Microsoft::WRL::ComPtr<IMFSample> localSurfaceSample;
      Microsoft::WRL::ComPtr<ID3D11Texture2D> localSurfaceTexture;
      uint32_t localSurfaceSubresource = 0;
      SharedFrame::PixelFormat localFormat = SharedFrame::PixelFormat::Unknown;
      uint32_t w = 0, h = 0;
      uint32_t codedW = 0, codedH = 0;
      uint32_t visL = 0, visT = 0;
      uint32_t seq = 0;
      bool frameKey = false;
      uint64_t frameStreamGeneration = 0;
      uint64_t captureUs = 0;
      uint64_t encodeStartUs = 0;
      uint64_t encodeEndUs = 0;
      uint64_t sendUs = 0;
      uint64_t recvUs = 0;
      uint64_t decodeStartUs = 0;
      uint64_t decodeEndUs = 0;
      uint64_t queueSetUs = 0;
      uint64_t decodeToQueueUs = 0;
      uint64_t frameVersion = 0;
      {
        std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
        if ((gFrameBuf.frame.bytes && !gFrameBuf.frame.bytes->empty()) || gFrameBuf.frame.surfaceTexture) {
          local = gFrameBuf.frame.bytes;
          localSurfaceSample = gFrameBuf.frame.surfaceSample;
          localSurfaceTexture = gFrameBuf.frame.surfaceTexture;
          localSurfaceSubresource = gFrameBuf.frame.surfaceSubresource;
          localFormat = gFrameBuf.frame.format;
          w = gFrameBuf.frame.width;
          h = gFrameBuf.frame.height;
          codedW = (gFrameBuf.frame.codedWidth > 0) ? gFrameBuf.frame.codedWidth : gFrameBuf.frame.width;
          codedH = (gFrameBuf.frame.codedHeight > 0) ? gFrameBuf.frame.codedHeight : gFrameBuf.frame.height;
          visL = gFrameBuf.frame.visibleLeft;
          visT = gFrameBuf.frame.visibleTop;
          seq = gFrameBuf.frame.seq;
          frameKey = gFrameBuf.frame.key;
          frameStreamGeneration = gFrameBuf.frame.streamGeneration;
          captureUs = gFrameBuf.frame.captureUs;
          encodeStartUs = gFrameBuf.frame.encodeStartUs;
          encodeEndUs = gFrameBuf.frame.encodeEndUs;
          sendUs = gFrameBuf.frame.sendUs;
          recvUs = gFrameBuf.frame.recvUs;
          decodeStartUs = gFrameBuf.frame.decodeStartUs;
          decodeEndUs = gFrameBuf.frame.decodeEndUs;
          queueSetUs = gFrameBuf.frame.queueSetUs;
          decodeToQueueUs = gFrameBuf.frame.decodeToQueueUs;
          frameVersion = gFrameBuf.frame.version;
        }
      }
      bool presented = false;
      Nv12RenderTelemetry renderTelemetry{};
      const char* renderPath = "none";
      const char* fallbackReason = "none";
      if (!pickerVisible && (local || localSurfaceTexture) && w > 0 && h > 0) {
        if (localFormat == SharedFrame::PixelFormat::Nv12) {
          if (!gUi.nv12Renderer.ready) {
            if (!gUi.nv12Renderer.init(hwnd)) {
              ++gPresent.d3dPresentFailCount;
              ++gPresent.fallbackInitFailCount;
              fallbackReason = "d3d_init_fail";
            }
          }
          if (gUi.nv12Renderer.ready) {
            if (localSurfaceTexture) {
              presented = gUi.nv12Renderer.render_surface(
                  hwnd, contentRect, localSurfaceTexture.Get(), localSurfaceSubresource,
                  codedW, codedH, visL, visT, w, h, &renderTelemetry);
            } else {
              presented = gUi.nv12Renderer.render(hwnd, contentRect, local->data(), codedW, codedH,
                                               visL, visT, w, h, &renderTelemetry);
            }
            if (presented) {
              ++gPresent.d3dPresentSuccessCount;
              renderPath = localSurfaceTexture ? "d3d_nv12_surface" : "d3d_nv12";
            } else {
              ++gPresent.d3dPresentFailCount;
              ++gPresent.fallbackRenderFailCount;
              fallbackReason = renderTelemetry.failStage;
            }
          }
          if (!presented && local) {
            std::vector<uint8_t> bgra;
            if (nv12_to_bgra(local->data(), codedW, codedH, &bgra) && !bgra.empty()) {
              // The DIB carries the coded plane; the source rect and a row-offset base
              // pointer select only the visible picture out of it.
              BITMAPINFO bmi{};
              bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
              bmi.bmiHeader.biWidth = static_cast<LONG>(codedW);
              bmi.bmiHeader.biHeight = -static_cast<LONG>(h);
              bmi.bmiHeader.biPlanes = 1;
              bmi.bmiHeader.biBitCount = 32;
              bmi.bmiHeader.biCompression = BI_RGB;
              SetStretchBltMode(hdc, COLORONCOLOR);
              FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
              StretchDIBits(hdc, contentRect.left, contentRect.top,
                            contentRect.right - contentRect.left, contentRect.bottom - contentRect.top,
                            static_cast<int>(visL), 0, static_cast<int>(w), static_cast<int>(h),
                            bgra.data() + static_cast<size_t>(visT) * codedW * 4, &bmi,
                            DIB_RGB_COLORS, SRCCOPY);
              presented = true;
              ++gPresent.gdiFallbackPresentedCount;
              renderPath = "gdi_nv12_fallback";
            } else {
              ++gPresent.fallbackNv12ConvertFailCount;
              fallbackReason = "nv12_to_bgra_fail";
            }
          }
        } else if (localFormat == SharedFrame::PixelFormat::Bgra32) {
          BITMAPINFO bmi{};
          bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
          bmi.bmiHeader.biWidth = static_cast<LONG>(w);
          bmi.bmiHeader.biHeight = -static_cast<LONG>(h);  // top-down
          bmi.bmiHeader.biPlanes = 1;
          bmi.bmiHeader.biBitCount = 32;
          bmi.bmiHeader.biCompression = BI_RGB;
          SetStretchBltMode(hdc, COLORONCOLOR);
          FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
          StretchDIBits(hdc, contentRect.left, contentRect.top,
                        contentRect.right - contentRect.left, contentRect.bottom - contentRect.top,
                        0, 0, static_cast<int>(w), static_cast<int>(h),
                        local->data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
          presented = true;
          renderPath = "gdi_bgra";
        }
      }
      if (presented) {
        gPresent.hasPresentedAtLeastOneFrame = true;
        gFrameBuf.lastPresentedVersion.store(frameVersion, std::memory_order_relaxed);
        gFrameBuf.lastPresentedCaptureUs.store(captureUs, std::memory_order_relaxed);
        const uint64_t presentUs = qpc_now_us();
        const uint64_t presentGapUs = (gPresent.lastPresentUs > 0) ? (presentUs - gPresent.lastPresentUs) : 0;
        const uint64_t queueToPaintUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
        const uint64_t queueToPresentUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
        const uint32_t traceEvery = gPresent.traceEvery.load();
        const uint32_t traceMax = gPresent.traceMax.load();
        if (traceEvery > 0 && (seq % traceEvery) == 0 &&
            (traceMax == 0 || gPresent.tracePresentPrinted.load() < traceMax)) {
          const auto nowPrinted = gPresent.tracePresentPrinted.fetch_add(1) + 1;
          if (traceMax == 0 || nowPrinted <= traceMax) {
            const uint64_t netUs = (recvUs >= sendUs) ? (recvUs - sendUs) : 0;
            const uint64_t c2eUs = (encodeStartUs >= captureUs) ? (encodeStartUs - captureUs) : 0;
            const uint64_t encUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
            const uint64_t e2sUs = (sendUs >= encodeEndUs) ? (sendUs - encodeEndUs) : 0;
            const uint64_t r2dUs = (decodeStartUs >= recvUs) ? (decodeStartUs - recvUs) : 0;
            const uint64_t decUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
            const uint64_t d2pUs = (presentUs >= decodeEndUs) ? (presentUs - decodeEndUs) : 0;
            const uint64_t renderUs = (presentUs >= recvUs) ? (presentUs - recvUs) : 0;
            const uint64_t queueWaitUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
            const uint64_t paintUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
            const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
            std::ostringstream oss;
            oss << "[native-video-client][trace_present] seq=" << seq
                << " captureUs=" << captureUs
                << " encodeStartUs=" << encodeStartUs
                << " encodeEndUs=" << encodeEndUs
                << " sendUs=" << sendUs
                << " recvUs=" << recvUs
                << " decodeStartUs=" << decodeStartUs
                << " decodeEndUs=" << decodeEndUs
                << " presentUs=" << presentUs
                << " c2eUs=" << c2eUs
                << " encUs=" << encUs
                << " e2sUs=" << e2sUs
                << " netUs=" << netUs
                << " r2dUs=" << r2dUs
                << " decUs=" << decUs
                << " d2pUs=" << d2pUs
                << " decodeToQueueUs=" << decodeToQueueUs
                << " queueWaitUs=" << queueWaitUs
                << " paintUs=" << paintUs
                << " uploadYUs=" << renderTelemetry.uploadYUs
                << " uploadUVUs=" << renderTelemetry.uploadUVUs
                << " drawUs=" << renderTelemetry.drawUs
                << " presentBlockUs=" << renderTelemetry.presentBlockUs
                << " renderUs=" << renderUs
                << " totalUs=" << totalUs
                << " renderPath=" << renderPath
                << " fallbackReason=" << fallbackReason;
            log_client_line(oss.str());
          }
        }
        // Emitted for every present, not only the ones that crossed a warning threshold.
        // Smoothness is a property of the whole interval distribution: a stream can average a
        // clean 30fps while alternating 16ms and 50ms gaps, which is exactly what a viewer
        // reports as stutter. Gating this behind the warning thresholds left the aggregate
        // reading zero through visibly uneven playback, so there was nothing to optimise
        // against.
        if (gPresent.lastPresentUs > 0) {
          std::ostringstream gapLine;
          gapLine << "[native-video-client][present] seq=" << seq
                  << " frameGapUs=" << presentGapUs;
          log_client_line(gapLine.str());
        }
        const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
        // GNLink stream telemetry (diagnostics only): one line per presented keyframe, plus any
        // non-key frame whose present interval jumped past 1.5x the expected cadence -- the client
        // side of a periodic stutter. Joins the host 'wire seq=' log by seq+gen; steady play stays
        // quiet. This only observes the timestamps the present path already produced.
        {
          const uint32_t expIntervalUs = gPresent.presentFrameIntervalUs.load(std::memory_order_relaxed);
          const uint64_t anomalyGapUs =
              (expIntervalUs > 0) ? (static_cast<uint64_t>(expIntervalUs) * 3ULL) / 2ULL : 25000ULL;
          const bool presentAnomaly = (gPresent.lastPresentUs > 0 && presentGapUs > anomalyGapUs);
          if (frameKey || presentAnomaly) {
            uint64_t presentBacklog = 0;
            {
              std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
              presentBacklog = (gFrameBuf.frame.version >= frameVersion) ? (gFrameBuf.frame.version - frameVersion) : 0;
            }
            std::ostringstream telem;
            telem << "[native-video-client][telemetry] stage=present"
                  << " seq=" << seq
                  << " gen=" << frameStreamGeneration
                  << " key=" << (frameKey ? 1 : 0)
                  << " presentUs=" << presentUs
                  << " presentedIntervalUs=" << presentGapUs
                  << " presentBacklog=" << presentBacklog
                  << " paintUs=" << queueToPresentUs
                  << " totalUs=" << totalUs;
            log_client_line(telem.str());
          }
        }
        if ((totalUs >= kUserFeedbackLagWarnUs || (presentGapUs >= kUserFeedbackGapWarnUs && gPresent.lastPresentUs > 0)) &&
            (presentUs >= gPresent.lastUserFeedbackUs + kUserFeedbackMinIntervalUs || gPresent.lastUserFeedbackUs == 0)) {
          const uint64_t overwriteCountNow = gFrameBuf.overwriteBeforePresentCount.load(std::memory_order_relaxed);
          const uint64_t overwriteDelta = (overwriteCountNow >= gPresent.lastUserFeedbackOverwrite)
                                             ? (overwriteCountNow - gPresent.lastUserFeedbackOverwrite)
                                             : 0;
          const uint64_t d3dSuccess = gPresent.d3dPresentSuccessCount.load(std::memory_order_relaxed);
          const uint64_t d3dFail = gPresent.d3dPresentFailCount.load(std::memory_order_relaxed);
          const uint64_t gdiFallback = gPresent.gdiFallbackPresentedCount.load(std::memory_order_relaxed);
          const uint64_t paintCoalesced = gFrameBuf.paintCoalescedCount.load(std::memory_order_relaxed);
          const uint64_t queueWaitUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
          const uint64_t paintUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
          const uint64_t netUs = (recvUs >= sendUs) ? (recvUs - sendUs) : 0;
          const uint64_t c2eUs = (encodeStartUs >= captureUs) ? (encodeStartUs - captureUs) : 0;
          const uint64_t encUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
          const uint64_t e2sUs = (sendUs >= encodeEndUs) ? (sendUs - encodeEndUs) : 0;
          const uint64_t r2dUs = (decodeStartUs >= recvUs) ? (decodeStartUs - recvUs) : 0;
          const uint64_t decUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
          const uint64_t d2pUs = (presentUs >= decodeEndUs) ? (presentUs - decodeEndUs) : 0;
          std::ostringstream oss;
          oss << "[native-video-client][user-feedback] seq=" << seq
              << " totalUs=" << totalUs
              << " capGapUs=" << presentGapUs
              << " queueToPaintUs=" << queueToPaintUs
              << " queueToPresentUs=" << queueToPresentUs
              << " d3dPresentSuccess=" << d3dSuccess
              << " d3dPresentFail=" << d3dFail
              << " gdiFallback=" << gdiFallback
              << " paintCoalesced=" << paintCoalesced
              << " overwriteDelta=" << overwriteDelta
              << " c2eUs=" << c2eUs
              << " encUs=" << encUs
              << " e2sUs=" << e2sUs
              << " netUs=" << netUs
              << " r2dUs=" << r2dUs
              << " decUs=" << decUs
              << " d2pUs=" << d2pUs
              << " decodeToQueueUs=" << decodeToQueueUs
              << " queueWaitUs=" << queueWaitUs
              << " paintUs=" << paintUs
              << " presentBlockUs=" << renderTelemetry.presentBlockUs
              << " renderPath=" << renderPath
              << " fallbackReason=" << fallbackReason;
          log_client_line(oss.str());
          gPresent.lastUserFeedbackUs = presentUs;
          gPresent.lastUserFeedbackOverwrite = overwriteCountNow;
        }
        gPresent.lastPresentUs = presentUs;
      } else if (pickerVisible || !gPresent.hasPresentedAtLeastOneFrame) {
        // Before first successful frame, keep a deterministic background.
        FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
      }
      draw_overlay(hdc);
      EndPaint(hwnd, &ps);
      uint64_t latestVersion = 0;
      {
        std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
        latestVersion = gFrameBuf.frame.version;
      }
      if (!pickerVisible && latestVersion != frameVersion) {
        if (!gFrameBuf.paintQueued.exchange(true)) {
          InvalidateRect(hwnd, nullptr, FALSE);
        } else {
          ++gFrameBuf.paintCoalescedCount;
        }
      }
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

// UNICODE is not defined for this target, so the generic Win32 names resolve to the ANSI
// entry points. This window is registered and created wide, so every message API it touches
// must be the explicit *W form -- DefWindowProcA on a Unicode window read the wide title as
// ANSI and truncated it to "r", and delivered WM_CHAR as ANSI.
bool create_window() {
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

  gSession.hwnd = CreateWindowExW(0, cls, L"remote60 native video client",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          static_cast<int>(gSession.windowW), static_cast<int>(gSession.windowH),
                          nullptr, nullptr, inst, nullptr);
  if (!gSession.hwnd) return false;
  ensure_ui_font(gSession.hwnd);
  // The process is per-monitor DPI aware, so the requested size is physical pixels; rescale
  // to keep the intended logical size on scaled displays.
  if (gUi.dpi != 96) {
    SetWindowPos(gSession.hwnd, nullptr, 0, 0, dpi_scale(static_cast<int>(gSession.windowW)),
                 dpi_scale(static_cast<int>(gSession.windowH)), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  ShowWindow(gSession.hwnd, SW_SHOW);
  UpdateWindow(gSession.hwnd);
  // Remote-cursor overlay cadence: 50ms is enough for a 30Hz feed and costs nothing when hidden.
  SetTimer(gSession.hwnd, kCursorOverlayTimerId, 50, nullptr);
  // The session starts on the picker; stamp its shown-time so the select debounce has one uniform
  // contract from the very first gesture instead of a special startup exemption.
  gPicker.shownAtUs.store(qpc_now_us(), std::memory_order_relaxed);
  return true;
}

}  // namespace remote60::native_poc::viewer
