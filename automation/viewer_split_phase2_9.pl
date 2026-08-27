#!/usr/bin/perl
# Viewer split refactor Phase 2-9: WndProc handler split (viewer_window_proc.cpp).
#   - WM_RBUTTONDOWN / WM_RBUTTONUP / WM_MBUTTONDOWN / WM_MBUTTONUP (identical apart from the button bit
#     and virtual key) -> on_secondary_button(hwnd, down, bit, vk, x, y)
#   - the picker DOWN latch shared by WM_LBUTTONDOWN and WM_POINTERDOWN -> picker_press(hwnd, layout, x, y)
#   - the picker UP shared by WM_LBUTTONUP and WM_POINTERUP -> picker_release(hwnd, layout, x, y, source)
#     (the mouse path's "consume the latch, then bail while a selection is pending" becomes a cancel
#      before the call; the shown-300ms and same-target rules fold into PickerState::SelectAllowed,
#      which the touch path already applied that way)
#   - the six Ctrl+Alt hotkeys of WM_KEYDOWN -> on_local_hotkey(hwnd, wp)
# Exact anchors; a miss dies before any file is written.
use strict;
use warnings;
my $S = 'apps/native_poc/src';
sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "open $f: $!"; local $/; my $s = <$h>; close $h; $s =~ s/\r\n/\n/g; return $s; }
sub spew  { my ($f, $s) = @_; $s =~ s/\r?\n/\r\n/g; open(my $h, '>:raw', $f) or die "write $f: $!"; print $h $s; close $h; }
sub must  { my ($ok, $what) = @_; die "anchor failed: $what\n" unless $ok; print "ok: $what\n"; }

my $W = slurp("$S/viewer_window_proc.cpp");

# ---- secondary buttons ----
my $rm = qr/    case (WM_[RM]BUTTON(?:DOWN|UP)):\n      if \(qpc_now_us\(\) < gInput\.suppressMouseUntilUs\.load\(std::memory_order_relaxed\)\) return 0;\n      if \(point_in_toggle_button\(hwnd, GET_X_LPARAM\(lp\), GET_Y_LPARAM\(lp\)\)\) return 0;\n      if \(point_in_macro_button\(hwnd, GET_X_LPARAM\(lp\), GET_Y_LPARAM\(lp\)\)\) return 0;\n      if \(gPicker\.visible\.load\(std::memory_order_relaxed\)\) return 0;\n      if \(point_in_panel_ui\(hwnd, GET_X_LPARAM\(lp\), GET_Y_LPARAM\(lp\)\)\) return 0;\n      if \(kInputPolicyForceBlock\) return 0;\n      \{\n        int32_t vx = 0;\n        int32_t vy = 0;\n        if \(!map_client_point_to_video_coords\(hwnd, GET_X_LPARAM\(lp\), GET_Y_LPARAM\(lp\), &vx, &vy\)\) return 0;\n(?:        SetCapture\(hwnd\);\n        gInput\.mouseButtons\.fetch_or\((\d)\);\n        enqueue_input_event\(2, vx, vy, 0, (VK_[RM]BUTTON)\);\n|        gInput\.mouseButtons\.fetch_and\(static_cast<uint16_t>\(~(\d)u\)\);\n        enqueue_input_event\(3, vx, vy, 0, (VK_[RM]BUTTON)\);\n        release_mouse_capture_if_idle\(hwnd\);\n)      \}\n      return 0;\n/;
my $count = 0;
$W =~ s/$rm/
  my ($msg, $bitDown, $vkDown, $bitUp, $vkUp) = ($1, $2, $3, $4, $5);
  my $down = defined $bitDown ? 'true' : 'false';
  my $bit = defined $bitDown ? $bitDown : $bitUp;
  my $vk = defined $vkDown ? $vkDown : $vkUp;
  $count++;
  "    case $msg:\n      return on_secondary_button(hwnd, $down, $bit, $vk, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));\n"/ge;
must($count == 4, "secondary button cases ($count)");

# ---- picker DOWN (mouse) ----
must($W =~ s/        \/\/ Remember which target \(if any\) this press started on; the UP handler only selects when\n        \/\/ it ends on the same one\. A press on empty picker space latches "none", and so does a\n        \/\/ press within the first 300ms after the picker appeared -- the gesture must START after\n        \/\/ the picker is stable, or a long-press begun against the old screen could still select\.\n        const int dx = GET_X_LPARAM\(lp\);\n        const int dy = GET_Y_LPARAM\(lp\);\n        const ClientLayout downLayout = compute_client_layout\(hwnd\);\n        uint64_t pressedId = kPickerPressNone;\n        uint64_t hitId = 0;\n        if \(point_in_rect\(downLayout\.desktopButtonRect, dx, dy\)\) \{\n          pressedId = 0;\n        \} else if \(try_hit_window_list_item\(hwnd, dx, dy, &hitId\)\) \{\n          pressedId = hitId;\n        \}\n        gPicker\.PressTarget\(pressedId, qpc_now_us\(\)\);\n        return 0;\n/        picker_press(hwnd, compute_client_layout(hwnd), GET_X_LPARAM(lp), GET_Y_LPARAM(lp));\n        return 0;\n/, 'mouse picker press');
# ---- picker DOWN (touch) ----
must($W =~ s/        if \(msg == WM_POINTERDOWN\) \{\n          \/\/ Same DOWN\/UP-on-the-same-target latch as the mouse path, including the "gesture must\n          \/\/ start after the picker is 300ms stable" rule\.\n          uint64_t pressedId = kPickerPressNone;\n          uint64_t hitId = 0;\n          if \(point_in_rect\(layout\.desktopButtonRect, p\.x, p\.y\)\) \{\n            pressedId = 0;\n          \} else if \(try_hit_window_list_item\(hwnd, p\.x, p\.y, &hitId\)\) \{\n            pressedId = hitId;\n          \}\n          gPicker\.PressTarget\(pressedId, qpc_now_us\(\)\);\n          return 0;\n        \}\n/        if (msg == WM_POINTERDOWN) {\n          picker_press(hwnd, layout, p.x, p.y);\n          return 0;\n        }\n/, 'touch picker press');
# ---- picker UP (mouse) ----
must($W =~ s/        const uint64_t pressedId = gPicker\.ReleaseTarget\(\);\n        \/\/ A selection already in flight owns the picker until its first frame arrives; ignore\n        \/\/ further target clicks so a double-click cannot queue a second, racing select\.\n        if \(gSel\.pending\.load\(std::memory_order_acquire\)\) return 0;\n        if \(point_in_rect\(layout\.refreshButtonRect, x, y\)\) \{\n          queue_window_list_request\("window_list_request pending"\);\n          InvalidateRect\(hwnd, nullptr, FALSE\);\n          return 0;\n        \}\n        \/\/ Mis-click guard: selecting needs a picker that has been up for a moment \(a click begun\n        \/\/ before it appeared must not land on a card\) AND a DOWN that started on the same target\.\n        const uint64_t nowUs = qpc_now_us\(\);\n        if \(!gPicker\.ShownLongEnough\(nowUs\)\) return 0;\n        const uint64_t shownAgeMs = gPicker\.ShownAgeMs\(nowUs\);\n        if \(point_in_rect\(layout\.desktopButtonRect, x, y\)\) \{\n          if \(pressedId != 0\) return 0;\n          \/\/ Explicit WindowSelect\(0\) even when desktop is already the selected target: one clean\n          \/\/ restart with a fresh generation, so the first-frame gate has something to wait on\.\n          if \(begin_pc_target_selection\(0, "desktop_select_requested"\)\) \{\n            std::cout << "\[native-video-client\]\[picker\] select source=mouse x=" << x << " y=" << y\n                      << " id=0 shownAgeMs=" << shownAgeMs << "\\n";\n            InvalidateRect\(hwnd, nullptr, FALSE\);\n          \}\n          return 0;\n        \}\n        uint64_t hitWindowId = 0;\n        if \(try_hit_window_list_item\(hwnd, x, y, &hitWindowId\)\) \{\n          if \(pressedId != hitWindowId\) return 0;\n          if \(begin_pc_target_selection\(hitWindowId, "window_select_requested"\)\) \{\n            std::cout << "\[native-video-client\]\[picker\] select source=mouse x=" << x << " y=" << y\n                      << " id=" << hitWindowId << " shownAgeMs=" << shownAgeMs << "\\n";\n            InvalidateRect\(hwnd, nullptr, FALSE\);\n          \}\n          return 0;\n        \}\n        return 0;\n/        \/\/ A selection already in flight owns the picker until its first frame arrives; ignore\n        \/\/ further target clicks so a double-click cannot queue a second, racing select. (The\n        \/\/ latch is dropped either way: any UP ends the gesture.)\n        if (gSel.pending.load(std::memory_order_acquire)) {\n          gPicker.CancelPress();\n          return 0;\n        }\n        picker_release(hwnd, layout, x, y, "mouse");\n        return 0;\n/, 'mouse picker release');
# ---- picker UP (touch) ----
must($W =~ s/        if \(msg == WM_POINTERUP\) \{\n          const uint64_t pressedId = gPicker\.ReleaseTarget\(\);\n          const uint64_t nowUs = qpc_now_us\(\);\n          const bool shownLongEnough = gPicker\.ShownLongEnough\(nowUs\);\n          const uint64_t shownAgeMs = gPicker\.ShownAgeMs\(nowUs\);\n          if \(point_in_rect\(layout\.refreshButtonRect, p\.x, p\.y\)\) \{\n            queue_window_list_request\("window_list_request pending"\);\n            InvalidateRect\(hwnd, nullptr, FALSE\);\n          \} else if \(point_in_rect\(layout\.desktopButtonRect, p\.x, p\.y\)\) \{\n            if \(shownLongEnough && pressedId == 0 &&\n                begin_pc_target_selection\(0, "desktop_select_requested"\)\) \{\n              std::cout << "\[native-video-client\]\[picker\] select source=touch x=" << p\.x\n                        << " y=" << p\.y << " id=0 shownAgeMs=" << shownAgeMs << "\\n";\n              InvalidateRect\(hwnd, nullptr, FALSE\);\n            \}\n          \} else \{\n            uint64_t hitWindowId = 0;\n            if \(try_hit_window_list_item\(hwnd, p\.x, p\.y, &hitWindowId\)\) \{\n              if \(shownLongEnough && pressedId == hitWindowId &&\n                  begin_pc_target_selection\(hitWindowId, "window_select_requested"\)\) \{\n                std::cout << "\[native-video-client\]\[picker\] select source=touch x=" << p\.x\n                          << " y=" << p\.y << " id=" << hitWindowId\n                          << " shownAgeMs=" << shownAgeMs << "\\n";\n                InvalidateRect\(hwnd, nullptr, FALSE\);\n              \}\n            \}\n          \}\n        \}\n        return 0;\n/        if (msg == WM_POINTERUP) {\n          picker_release(hwnd, layout, p.x, p.y, "touch");\n        }\n        return 0;\n/, 'touch picker release');
# ---- hotkeys ----
must($W =~ s/    case WM_KEYDOWN:\n      if \(local_hotkey_modifiers_active\(\) && wp == VK_F5\) \{\n.*?      if \(local_hotkey_modifiers_active\(\) && wp == VK_OEM_7\) \{  \/\/ '\n        apply_runtime_tune_delta\(0, 1\);\n        InvalidateRect\(hwnd, nullptr, FALSE\);\n        return 0;\n      \}\n/    case WM_KEYDOWN:\n      if (on_local_hotkey(hwnd, wp)) return 0;\n/s, 'hotkeys');

# ---- the helpers, above WndProc ----
my $helpers = <<'EOF';
// WM_RBUTTONDOWN / WM_RBUTTONUP / WM_MBUTTONDOWN / WM_MBUTTONUP: identical apart from the button bit
// (2 = right, 4 = middle) and the virtual key the host receives.
LRESULT on_secondary_button(HWND hwnd, bool down, uint16_t buttonBit, uint32_t vk, int x, int y) {
  if (qpc_now_us() < gInput.suppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
  if (point_in_toggle_button(hwnd, x, y)) return 0;
  if (point_in_macro_button(hwnd, x, y)) return 0;
  if (gPicker.visible.load(std::memory_order_relaxed)) return 0;
  if (point_in_panel_ui(hwnd, x, y)) return 0;
  if (kInputPolicyForceBlock) return 0;
  int32_t vx = 0;
  int32_t vy = 0;
  if (!map_client_point_to_video_coords(hwnd, x, y, &vx, &vy)) return 0;
  if (down) {
    SetCapture(hwnd);
    gInput.mouseButtons.fetch_or(buttonBit);
    enqueue_input_event(2, vx, vy, 0, vk);
  } else {
    gInput.mouseButtons.fetch_and(static_cast<uint16_t>(~buttonBit));
    enqueue_input_event(3, vx, vy, 0, vk);
    release_mouse_capture_if_idle(hwnd);
  }
  return 0;
}

// The picker's DOWN (mouse WM_LBUTTONDOWN and touch WM_POINTERDOWN): remember which target (if any)
// this press started on; the UP handler only selects when it ends on the same one. A press on empty
// picker space latches "none", and so does a press within the first 300ms after the picker appeared
// -- the gesture must START after the picker is stable, or a long-press begun against the old
// screen could still select (PickerState::PressTarget).
void picker_press(HWND hwnd, const ClientLayout& layout, int x, int y) {
  uint64_t pressedId = kPickerPressNone;
  uint64_t hitId = 0;
  if (point_in_rect(layout.desktopButtonRect, x, y)) {
    pressedId = 0;
  } else if (try_hit_window_list_item(hwnd, x, y, &hitId)) {
    pressedId = hitId;
  }
  gPicker.PressTarget(pressedId, qpc_now_us());
}

// The picker's UP (mouse WM_LBUTTONUP and touch WM_POINTERUP). Consumes the press latch first --
// any UP ends the gesture. Refresh needs no latch; selecting needs a picker that has been up for a
// moment (a click begun before it appeared must not land on a card) AND a DOWN that started on the
// same target (PickerState::SelectAllowed). Desktop is an explicit WindowSelect(0) even when desktop
// is already the selected target: one clean restart with a fresh generation, so the first-frame
// gate has something to wait on.
void picker_release(HWND hwnd, const ClientLayout& layout, int x, int y, const char* source) {
  const uint64_t pressedId = gPicker.ReleaseTarget();
  if (point_in_rect(layout.refreshButtonRect, x, y)) {
    queue_window_list_request("window_list_request pending");
    InvalidateRect(hwnd, nullptr, FALSE);
    return;
  }
  const uint64_t nowUs = qpc_now_us();
  const uint64_t shownAgeMs = gPicker.ShownAgeMs(nowUs);
  if (point_in_rect(layout.desktopButtonRect, x, y)) {
    if (gPicker.SelectAllowed(pressedId, 0, nowUs) &&
        begin_pc_target_selection(0, "desktop_select_requested")) {
      std::cout << "[native-video-client][picker] select source=" << source << " x=" << x << " y=" << y
                << " id=0 shownAgeMs=" << shownAgeMs << "\n";
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return;
  }
  uint64_t hitWindowId = 0;
  if (try_hit_window_list_item(hwnd, x, y, &hitWindowId)) {
    if (gPicker.SelectAllowed(pressedId, hitWindowId, nowUs) &&
        begin_pc_target_selection(hitWindowId, "window_select_requested")) {
      std::cout << "[native-video-client][picker] select source=" << source << " x=" << x << " y=" << y
                << " id=" << hitWindowId << " shownAgeMs=" << shownAgeMs << "\n";
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  }
}

// The Ctrl+Alt local hotkeys of WM_KEYDOWN: F5 refresh the window list, F9 capture overview,
// [ ] bitrate down/up, ; ' keyint down/up. True when consumed.
bool on_local_hotkey(HWND hwnd, WPARAM wp) {
  if (local_hotkey_modifiers_active() && wp == VK_F5) {
    queue_window_list_request("window_list_request pending");
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_F9) {
    request_capture_overview_mode();
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_4) {  // [
    apply_runtime_tune_delta(-1, 0);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_6) {  // ]
    apply_runtime_tune_delta(1, 0);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_1) {  // ;
    apply_runtime_tune_delta(0, -1);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  if (local_hotkey_modifiers_active() && wp == VK_OEM_7) {  // '
    apply_runtime_tune_delta(0, 1);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
  }
  return false;
}

EOF
must($W =~ s/(namespace remote60::native_poc::viewer \{\n\n)(LRESULT CALLBACK WndProc)/$1$helpers$2/, 'insert helpers');
must($W =~ s/(#include "viewer_present\.hpp"\n)/$1\n#include <iostream>\n/, 'iostream include');
spew("$S/viewer_window_proc.cpp", $W);
print "done\n";
