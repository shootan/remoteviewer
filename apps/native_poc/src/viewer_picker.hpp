#pragma once

// Target picker and selection gate glue of the viewer.
//
// Role:    window/monitor list + select requests, apply_window_list_snapshot / apply_window_selected_result,
//          the picker show/hide + stream sync (set_picker_visible_and_sync_stream), the PC-side selection
//          gate (begin/clear/post_pc_selection_reveal), thumbnail fetch queueing, card grid scroll and
//          hit test, capture-mode requests, push_session_toolbar_state, the control metrics snapshot.
// Thread:  UI begins/commits selections and scrolls; control applies lists/acks and fetches thumbnails;
//          recv posts the reveal. The pre-refactor rules are in the comments on each function.
// Input:   control replies, UI gestures, selection state.
// Output:  request state consumed by the control scheduler, picker state read by paint/hit-test.
// Callers: WndProc, control thread, recv thread, main() toolbar callbacks.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-10).

#include "viewer_common.hpp"
#include "viewer_globals.hpp"
#include "viewer_layout.hpp"

namespace remote60::native_poc::viewer {

ClientControlMetricsSnapshot capture_client_control_metrics_snapshot();

void queue_thumbnail_fetches_from_panel();

void queue_window_list_request(const char* statusText = nullptr);


void set_window_panel_status(const std::string& status);

void apply_window_list_snapshot(const ControlWindowListMessage& msg);

/** Mirrors the session state into the toolbar window. Cheap enough to call on every change. */
void push_session_toolbar_state();

void set_picker_visible_and_sync_stream(bool visible);

// Clears the in-flight selection state. Safe to call from any thread; touches only atomics and
// the stream request (itself atomic-backed).
void clear_pc_target_selection();

// UI-thread entry for picking a target from the picker. Orders the control traffic so the host's
// "!streamActive" continue-gate is already passed when the select arrives: the scheduler always
// sends StreamState ahead of WindowSelect, so requesting stream-on here (before or after the
// select is queued) still reaches the host first. Desktop is sent as an explicit WindowSelect(0)
// -- one clean restart -- rather than the "already selected, just hide" shortcut, for mobile
// parity. Ignores clicks while a selection is already in flight, which is the double-click guard.
// Returns true only when the select request was actually accepted for sending, so callers can
// log/refresh on real selections instead of refused attempts (disconnected, locked target).
bool begin_pc_target_selection(uint64_t windowId, const char* statusText);

// Video-thread half of the reveal. It only *records* the candidate (the generation and epoch of
// the first decoded frame) and posts the reveal once; it deliberately touches none of the live
// selection state. The UI-thread handler revalidates against that state before committing, so a
// cancel / new selection / disconnect that races the post cannot wrongly close the picker.
void post_pc_selection_reveal(uint64_t readyGeneration, uint64_t readyEpoch);

void apply_window_selected_result(const ControlWindowSelectedMessage& msg);

void scroll_window_list(HWND hwnd, int deltaSteps);

// Hit-test a grid card. Card index 0 is the pinned Desktop card; window items follow.
bool try_hit_window_list_item(HWND hwnd, int x, int y, uint64_t* outWindowId);

void enqueue_capture_mode_request(uint16_t mode, uint32_t xPermille, uint32_t yPermille);

void request_capture_overview_mode();

}  // namespace remote60::native_poc::viewer
