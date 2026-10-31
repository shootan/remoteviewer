#!/usr/bin/env bash
# Viewer split refactor Phase 0-8 (viewer_layout), 0-9 (viewer_input_forward), 0-10 (viewer_picker),
# 0-11 (viewer_overlay_draw), 0-12 (viewer_cursor_overlay), 0-13 (viewer_window_proc). Same pattern:
# verbatim move (viewer_split_move.pl), include wiring, identity check against HEAD, build + e2e, commit.
# Run from the repo root on a clean tree. Pass a step number to start from it (e.g. "10").
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_client_main.cpp
C=apps/native_poc/CMakeLists.txt
FROM=${1:-8}
git diff --quiet HEAD || { echo "tree not clean"; exit 1; }
T=/tmp/v0813; rm -rf $T; mkdir -p $T

add_include() { perl -0pi -e 's/((?:#include "viewer_[a-z_0-9]+\.hpp"\r\n)+)/$1#include "'"$1"'"\r\n/ or die "include anchor"' "$M"; }
add_cmake()   { perl -0pi -e 's/(  src\/viewer_globals\.cpp\r?\n)/$1  src\/'"$1"'\r\n/ or die "cmake anchor"' "$C"; }
check() { local r=$1; shift; cat "$@" > $T/concat.txt; perl automation/viewer_split_check.pl HEAD "$r" $T/concat.txt; }
ranges() { echo "$1" | sed -n 's/^RANGES //p'; }
commit_step() {  # $1 step, $2 title, $3 ranges, $4 extra body, rest: files
  local step=$1 title=$2 r=$3 extra=$4; shift 4
  git add "$@"
  git commit -q -F - <<EOF
refactor(viewer): Phase $step — $title (verbatim)

Source ranges $r of the previous revision. $extra
Gates: move check PASS, build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
  git log -1 --format='%h %s'
  echo "main.cpp now $(wc -l < "$M") lines"
}
hpp_prelude() {  # $1 out, $2.. comment lines (already prefixed with //), then includes given via INCLUDES var
  local out=$1; shift
  { echo '#pragma once'; echo; printf '%s\n' "$@"; echo; for h in $INCLUDES; do echo "#include \"$h\""; done; echo; echo 'namespace remote60::native_poc::viewer {'; echo; } > "$out"
}
cpp_prelude() {  # $1 out, $2 own header, then INCLUDES var
  local out=$1 own=$2
  { echo "// See $own. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0)."; echo; echo "#include \"$own\""; echo; for h in $INCLUDES; do echo "#include \"$h\""; done; echo; echo 'namespace remote60::native_poc::viewer {'; echo; } > "$out"
}

# ================= 0-8 viewer_layout =================
if [ "$FROM" -le 8 ]; then
INCLUDES='viewer_common.hpp viewer_globals.hpp viewer_gdi_util.hpp'
hpp_prelude $T/layout_hpp.txt \
'// Geometry of the viewer window: the picker/stream layout, the card grid, aspect-fit letterboxing' \
'// and the mapping from client pixels to video coordinates.' \
'//' \
'// Role:    ClientLayout/compute_client_layout, CardGridMetrics/compute_card_grid/card_rect_for_slot,' \
'//          aspect_fit_rect, resolve_active_video_content_size/rect, point_in_* hit tests,' \
'//          map_client_point_to_video_coords, the DPI-scaled kPanel* metrics.' \
'// Thread:  UI mostly; apply_window_list_snapshot (control thread) also computes the grid, and' \
'//          resolve_active_video_content_size reads gFrame under its mutex from any thread.' \
'// Input:   HWND client rect, picker visibility, the selected target / frame / metrics sizes.' \
'// Output:  RECTs and video coordinates.' \
'// Callers: WndProc, viewer_overlay_draw, viewer_picker, viewer_cursor_overlay.' \
'//' \
'// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-8).'
cpp_prelude $T/layout_cpp.txt viewer_layout.hpp
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_layout.hpp --hpp-prelude $T/layout_hpp.txt \
  --cpp $S/viewer_layout.cpp --cpp-prelude $T/layout_cpp.txt \
  --start '^inline int kPickerPanelPreferredWidth\(\)' --start '^inline int kPickerPanelMinWidth\(\)' \
  --start '^inline int kPanelMargin\(\)' --start '^inline int kPanelButtonHeight\(\)' --start '^inline int kPanelButtonGap\(\)' \
  --start '^inline int kPanelSectionGap\(\)' --start '^inline int kPanelInfoHeight\(\)' --start '^inline int kPanelStatsHeight\(\)' \
  --start '^inline int kPanelItemHeight\(\)' --start '^inline int kPanelItemGap\(\)' \
  --start '^struct ClientLayout \{$' --start '^RECT make_rect\(int x, int y, int w, int h\) \{$' \
  --start '^bool point_in_rect\(const RECT& r, int x, int y\) \{$' --start '^struct CardGridMetrics \{$' \
  --start '^CardGridMetrics compute_card_grid\(const RECT& gridRect\) \{$' \
  --start '^RECT card_rect_for_slot\(const RECT& gridRect, const CardGridMetrics& m, int slot\) \{$' \
  --start '^RECT aspect_fit_rect\(const RECT& containerRect, uint32_t contentWidth, uint32_t contentHeight\) \{$' \
  --start '^bool resolve_active_video_content_size\(uint32_t\* outWidth, uint32_t\* outHeight\) \{$' \
  --start '^RECT resolve_video_content_rect\(HWND hwnd, const RECT& containerRect\) \{$' \
  --start '^ClientLayout compute_client_layout\(HWND hwnd\) \{$' \
  --start '^bool point_in_toggle_button\(HWND hwnd, int x, int y\) \{$' --start '^bool point_in_macro_button\(HWND hwnd, int x, int y\) \{$' \
  --start '^bool point_in_panel_ui\(HWND hwnd, int x, int y\) \{$' --start '^bool point_in_video_rect\(HWND hwnd, int x, int y\) \{$' \
  --start '^bool map_client_point_to_video_coords\(HWND hwnd, int x, int y, int32_t\* outVideoX, int32_t\* outVideoY\) \{$')
echo "$OUT"; R=$(ranges "$OUT")
add_include viewer_layout.hpp; add_cmake viewer_layout.cpp
check "$R" $S/viewer_layout.hpp $S/viewer_layout.cpp
bash automation/viewer_split_gate.sh --e2e
commit_step 0-8 "layout, card grid, aspect fit and point mapping to viewer_layout" "$R" "The ten DPI-scaled kPanel* inline helpers stay inline in the header." "$M" "$C" $S/viewer_layout.hpp $S/viewer_layout.cpp
fi

# ================= 0-9 viewer_input_forward =================
if [ "$FROM" -le 9 ]; then
INCLUDES='viewer_common.hpp viewer_globals.hpp'
hpp_prelude $T/input_hpp.txt \
'// Input forwarding of the viewer: mouse/key/IME events become ControlInputEvent/Text messages.' \
'//' \
'// Role:    enqueue_input_event / enqueue_input_text_units / enqueue_macro_step (queue for the control' \
'//          thread), the key-forwarding memory (forward_key_down/up, key_event_should_forward,' \
'//          enqueue_release_for_pressed_keys), IME result text, mouse capture release, coord_to_permille,' \
'//          toggle_macro_window.' \
'// Thread:  UI (WndProc) produces; the control thread drains gInputQueueState. The macro window replays' \
'//          through enqueue_macro_step on the UI thread.' \
'// Input:   virtual keys, video coordinates, UTF-16 text, macro steps.' \
'// Output:  QueuedControlInputMessage entries; macro recording taps enqueue_input_event.' \
'// Callers: WndProc, viewer_picker (coord_to_permille), the toolbar/macro callbacks in main().' \
'//' \
'// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-9).'
cpp_prelude $T/input_cpp.txt viewer_input_forward.hpp
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_input_forward.hpp --hpp-prelude $T/input_hpp.txt \
  --cpp $S/viewer_input_forward.cpp --cpp-prelude $T/input_cpp.txt \
  --start '^void enqueue_control_input_message\(const QueuedControlInputMessage& msg\) \{$' \
  --start '^void enqueue_input_text_units\(const uint16_t\* text, size_t count\) \{$' \
  --start '^bool local_hotkey_modifiers_active\(\) \{$' \
  --start '^bool key_event_should_forward\(WPARAM vk\) \{$' \
  --start '^bool forward_key_down\(WPARAM vk\) \{$' --start '^bool forward_key_up\(WPARAM vk\) \{$' \
  --start '^bool send_ime_result_text\(HWND hwnd, LPARAM imeFlags\) \{$' \
  --start '^void release_mouse_capture_if_idle\(HWND hwnd\) \{$' \
  --start '^void enqueue_release_for_pressed_mouse_buttons\(\) \{$' \
  --start '^void enqueue_release_for_pressed_keys\(\) \{$' \
  --start '^uint32_t coord_to_permille\(int coord, int extent\) \{$' \
  --start '^void enqueue_input_event\(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode\) \{$' \
  --start '^void enqueue_macro_step\(const remote60::native_poc::MacroStep& step\) \{$' \
  --start '^void toggle_macro_window\(HWND owner\) \{$')
echo "$OUT"; R=$(ranges "$OUT")
# the two forward declarations main.cpp carried are the header's job now (deleted AFTER the move so
# the ranges above still index the HEAD revision)
perl -0pi -e 's{void enqueue_input_event\(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode\);\r\nvoid enqueue_input_text_units\(const uint16_t\* text, size_t count\);\r\n\r\n}{} or die "fwd decls"' "$M"
add_include viewer_input_forward.hpp; add_cmake viewer_input_forward.cpp
check "$R" $S/viewer_input_forward.hpp $S/viewer_input_forward.cpp
bash automation/viewer_split_gate.sh --e2e
commit_step 0-9 "input forwarding (events, text, key memory, IME, macro) to viewer_input_forward" "$R" "The two forward declarations main.cpp carried are dropped in favour of the header." "$M" "$C" $S/viewer_input_forward.hpp $S/viewer_input_forward.cpp
fi

# ================= 0-10 viewer_picker =================
if [ "$FROM" -le 10 ]; then
INCLUDES='viewer_common.hpp viewer_globals.hpp viewer_layout.hpp'
hpp_prelude $T/picker_hpp.txt \
'// Target picker and selection gate glue of the viewer.' \
'//' \
'// Role:    window/monitor list + select requests, apply_window_list_snapshot / apply_window_selected_result,' \
'//          the picker show/hide + stream sync (set_picker_visible_and_sync_stream), the PC-side selection' \
'//          gate (begin/clear/post_pc_selection_reveal), thumbnail fetch queueing, card grid scroll and' \
'//          hit test, capture-mode requests, push_session_toolbar_state, the control metrics snapshot.' \
'// Thread:  UI begins/commits selections and scrolls; control applies lists/acks and fetches thumbnails;' \
'//          recv posts the reveal. The pre-refactor rules are in the comments on each function.' \
'// Input:   control replies, UI gestures, selection state.' \
'// Output:  request state consumed by the control scheduler, picker state read by paint/hit-test.' \
'// Callers: WndProc, control thread, recv thread, main() toolbar callbacks.' \
'//' \
'// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-10).'
INCLUDES='viewer_common.hpp viewer_globals.hpp viewer_input_forward.hpp viewer_layout.hpp viewer_log.hpp'
cpp_prelude $T/picker_cpp.txt viewer_picker.hpp
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_picker.hpp --hpp-prelude $T/picker_hpp.txt \
  --cpp $S/viewer_picker.cpp --cpp-prelude $T/picker_cpp.txt \
  --start '^ClientControlMetricsSnapshot capture_client_control_metrics_snapshot\(\) \{$' \
  --start '^void queue_thumbnail_fetches_from_panel\(\) \{$' \
  --start '^void queue_window_list_request\(const char\* statusText = nullptr\) \{$' \
  --start '^void queue_window_select_request\(uint64_t windowId, const char\* statusText = nullptr\) \{$' \
  --start '^void set_window_panel_status\(const std::string& status\) \{$' \
  --start '^void apply_window_list_snapshot\(const ControlWindowListMessage& msg\) \{$' \
  --start '^void push_session_toolbar_state\(\) \{$' \
  --start '^void set_picker_visible_and_sync_stream\(bool visible\) \{$' \
  --start '^void clear_pc_target_selection\(\) \{$' \
  --start '^bool begin_pc_target_selection\(uint64_t windowId, const char\* statusText\) \{$' \
  --start '^void post_pc_selection_reveal\(uint64_t readyGeneration, uint64_t readyEpoch\) \{$' \
  --start '^void apply_window_selected_result\(const ControlWindowSelectedMessage& msg\) \{$' \
  --start '^void scroll_window_list\(HWND hwnd, int deltaSteps\) \{$' \
  --start '^bool try_hit_window_list_item\(HWND hwnd, int x, int y, uint64_t\* outWindowId\) \{$' \
  --start '^void enqueue_capture_mode_request\(uint16_t mode, uint32_t xPermille, uint32_t yPermille\) \{$' \
  --start '^void request_capture_overview_mode\(\) \{$' \
  --start '^void request_capture_focus_from_client_point\(HWND hwnd, int x, int y\) \{$')
echo "$OUT"; R=$(ranges "$OUT")
# the forward declaration (and its explanatory comment) of set_picker_visible_and_sync_stream move to
# the definition (deleted AFTER the move so the ranges above still index the HEAD revision)
perl -0pi -e 's{// Browsing targets must not keep the host encoding \(F1\)\. The request rides the control\r\n// scheduler, which orders stream state ahead of window selection\. Sent only on explicit\r\n// picker transitions: startup leaves the host.s default-active stream alone, so headless\r\n// harness clients that never open the picker keep receiving video unchanged\.\r\nvoid set_picker_visible_and_sync_stream\(bool visible\);\r\n}{} or die "picker fwd decl"' "$M"
perl -0pi -e 's~\r\nvoid set_picker_visible_and_sync_stream\(bool visible\) \{~\r\n// Browsing targets must not keep the host encoding (F1). The request rides the control\r\n// scheduler, which orders stream state ahead of window selection. Sent only on explicit\r\n// picker transitions: startup leaves the host\x27s default-active stream alone, so headless\r\n// harness clients that never open the picker keep receiving video unchanged.\r\nvoid set_picker_visible_and_sync_stream(bool visible) {~ or die "picker comment"' $S/viewer_picker.cpp
add_include viewer_picker.hpp; add_cmake viewer_picker.cpp
check "$R" $S/viewer_picker.hpp $S/viewer_picker.cpp
bash automation/viewer_split_gate.sh --e2e
commit_step 0-10 "picker, selection gate glue and control snapshot to viewer_picker" "$R" "The forward declaration of set_picker_visible_and_sync_stream (with its comment, now above the definition) is dropped in favour of the header." "$M" "$C" $S/viewer_picker.hpp $S/viewer_picker.cpp
fi

# ================= 0-11 viewer_overlay_draw =================
if [ "$FROM" -le 11 ]; then
INCLUDES='viewer_common.hpp viewer_globals.hpp viewer_layout.hpp'
hpp_prelude $T/overlay_hpp.txt \
'// The picker overlay paint and the overlay metric ring of the viewer.' \
'//' \
'// Role:    draw_overlay (the target picker: header, actions, card grid, footer), draw_target_card,' \
'//          draw_thumbnail_into, push_overlay_metric_sample / collect_overlay_averages, apply_runtime_tune_delta.' \
'// Thread:  UI paints; recv pushes metric samples under gOverlayMetricsMu.' \
'// Input:   the paint DC, picker/selection/thumbnail state, client metrics.' \
'// Output:  the picker pixels; runtime tune deltas queued for the control thread.' \
'// Callers: WM_PAINT, recv thread (publish_metrics), WM_KEYDOWN hotkeys.' \
'//' \
'// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-11).'
INCLUDES='viewer_common.hpp viewer_gdi_util.hpp viewer_globals.hpp viewer_layout.hpp'
cpp_prelude $T/overlay_cpp.txt viewer_overlay_draw.hpp
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_overlay_draw.hpp --hpp-prelude $T/overlay_hpp.txt \
  --cpp $S/viewer_overlay_draw.cpp --cpp-prelude $T/overlay_cpp.txt \
  --start '^void push_overlay_metric_sample\(uint32_t recvFpsX100, uint32_t decodedFpsX100, uint32_t recvMbpsX1000,$' \
  --start '^OverlayMetricAverages collect_overlay_averages\(uint64_t nowUs, uint64_t windowUs\) \{$' \
  --start '^void apply_runtime_tune_delta\(int bitrateStep, int keyintStep\) \{$' \
  --start '^void draw_thumbnail_into\(HDC hdc, const RECT& dst, const WindowThumb& thumb\) \{$' \
  --start '^void draw_target_card\(HDC hdc, const RECT& card, const CardGridMetrics& grid,$' \
  --start '^void draw_overlay\(HDC hdc\) \{$')
echo "$OUT"; R=$(ranges "$OUT")
add_include viewer_overlay_draw.hpp; add_cmake viewer_overlay_draw.cpp
check "$R" $S/viewer_overlay_draw.hpp $S/viewer_overlay_draw.cpp
bash automation/viewer_split_gate.sh --e2e
commit_step 0-11 "picker overlay paint and overlay metrics to viewer_overlay_draw" "$R" "" "$M" "$C" $S/viewer_overlay_draw.hpp $S/viewer_overlay_draw.cpp
fi

# ================= 0-12 viewer_cursor_overlay =================
if [ "$FROM" -le 12 ]; then
INCLUDES='viewer_common.hpp viewer_globals.hpp'
hpp_prelude $T/cursor_hpp.txt \
'// Remote-cursor overlay: a layered, click-through popup that follows the host cursor sample.' \
'//' \
'// Role:    ensure_cursor_overlay (create + rasterise the ring marker once), update_cursor_overlay' \
'//          (WM_TIMER body: map the latest sample into the letterboxed video rect, show/hide/move).' \
'// Thread:  UI only (owned window + timer); the sample itself is written by the recv thread.' \
'// Input:   gRemoteCursor* sample, picker visibility, active stream generation.' \
'// Output:  the overlay window position/visibility. Off unless REMOTE60_NATIVE_REMOTE_CURSOR is set.' \
'// Callers: WndProc WM_TIMER (kCursorOverlayTimerId).' \
'//' \
'// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-12).'
INCLUDES='viewer_common.hpp viewer_env_util.hpp viewer_globals.hpp viewer_layout.hpp'
cpp_prelude $T/cursor_cpp.txt viewer_cursor_overlay.hpp
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_cursor_overlay.hpp --hpp-prelude $T/cursor_hpp.txt \
  --cpp $S/viewer_cursor_overlay.cpp --cpp-prelude $T/cursor_cpp.txt \
  --start '^void ensure_cursor_overlay\(HWND owner\) \{$' --start '^void update_cursor_overlay\(HWND hwnd\) \{$')
echo "$OUT"; R=$(ranges "$OUT")
perl -0pi -e 's{void ensure_cursor_overlay\(HWND owner\);\r\nvoid update_cursor_overlay\(HWND hwnd\);\r\n}{} or die "cursor fwd decls"' "$M"
add_include viewer_cursor_overlay.hpp; add_cmake viewer_cursor_overlay.cpp
check "$R" $S/viewer_cursor_overlay.hpp $S/viewer_cursor_overlay.cpp
bash automation/viewer_split_gate.sh --e2e
commit_step 0-12 "remote-cursor overlay to viewer_cursor_overlay" "$R" "The two forward declarations main.cpp carried are dropped in favour of the header." "$M" "$C" $S/viewer_cursor_overlay.hpp $S/viewer_cursor_overlay.cpp
fi

# ================= 0-13 viewer_window_proc =================
if [ "$FROM" -le 13 ]; then
INCLUDES='viewer_common.hpp'
hpp_prelude $T/wnd_hpp.txt \
'// The viewer window: class registration, creation, and its window procedure.' \
'//' \
'// Role:    WndProc (every WM_* the session window handles: mouse/touch/keyboard/IME forwarding,' \
'//          picker gestures, the reveal message, WM_PAINT present path, timers) and create_window.' \
'// Thread:  UI only.' \
'// Input:   window messages.' \
'// Output:  input queued for the control thread, picker/selection state, presented frames.' \
'// Callers: main() (create_window), the message pump.' \
'//' \
'// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-13). Still 870' \
'// lines: Phase 2-8 moves the WM_PAINT body to viewer_present.cpp and 2-9 splits the handlers.'
INCLUDES='viewer_common.hpp viewer_cursor_overlay.hpp viewer_gdi_util.hpp viewer_globals.hpp viewer_input_forward.hpp viewer_layout.hpp viewer_log.hpp viewer_nv12_renderer.hpp viewer_overlay_draw.hpp viewer_picker.hpp'
cpp_prelude $T/wnd_cpp.txt viewer_window_proc.hpp
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_window_proc.hpp --hpp-prelude $T/wnd_hpp.txt \
  --cpp $S/viewer_window_proc.cpp --cpp-prelude $T/wnd_cpp.txt \
  --start '^LRESULT CALLBACK WndProc\(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp\) \{$' --start '^bool create_window\(\) \{$')
echo "$OUT"; R=$(ranges "$OUT")
add_include viewer_window_proc.hpp; add_cmake viewer_window_proc.cpp
check "$R" $S/viewer_window_proc.hpp $S/viewer_window_proc.cpp
bash automation/viewer_split_gate.sh --e2e
commit_step 0-13 "WndProc and create_window to viewer_window_proc" "$R" "" "$M" "$C" $S/viewer_window_proc.hpp $S/viewer_window_proc.cpp
fi
