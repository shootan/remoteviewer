// See viewer_picker.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_picker.hpp"

#include "thumbnail_fetch_policy.hpp"

#include "viewer_common.hpp"
#include "viewer_state.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_layout.hpp"
#include "viewer_log.hpp"

namespace remote60::native_poc::viewer {

ClientControlMetricsSnapshot capture_client_control_metrics_snapshot(ViewerState& ctx) {
  // One locked copy, then plain field reads: every field below comes from the same report. (F-15.)
  const ClientRuntimeMetrics m = ctx.metrics.Snapshot();
  ClientControlMetricsSnapshot snapshot{};
  snapshot.updatedQpcUs = m.updatedQpcUs;
  snapshot.message.width = m.width;
  snapshot.message.height = m.height;
  snapshot.message.recvFpsX100 = m.recvFpsX100;
  snapshot.message.decodedFpsX100 = m.decodedFpsX100;
  snapshot.message.recvMbpsX1000 = m.recvMbpsX1000;
  snapshot.message.skippedFrames = m.skippedFrames;
  snapshot.message.avgLatencyUs = m.avgLatencyUs;
  snapshot.message.maxLatencyUs = m.maxLatencyUs;
  snapshot.message.avgDecodeTailUs = m.avgDecodeTailUs;
  snapshot.message.maxDecodeTailUs = m.maxDecodeTailUs;
  snapshot.message.congestionState = m.congestionState;
  snapshot.message.congestionTransitions = m.congestionTransitions;
  snapshot.message.congestionRecoveryCount = m.congestionRecoveryCount;
  snapshot.message.congestionRecoveryReq = m.congestionRecoveryReq;
  snapshot.message.congestionRecoveryMaxUs = m.congestionRecoveryMaxUs;
  snapshot.message.queueDepthMax = m.queueDepthMax;
  snapshot.message.queueDepthH4p = m.queueDepthH4p;
  snapshot.message.udpAssemblyDropPm = m.udpAssemblyDropPm;
  return snapshot;
}

void queue_thumbnail_fetches_from_panel(ViewerState& ctx) {
  if (!ctx.picker.hostSupportsThumbnails.load(std::memory_order_relaxed)) return;
  const WindowPanelSnapshot snap = ctx.picker.windowPanel.Snapshot();
  const uint64_t nowUs = qpc_now_us();
  std::lock_guard<std::mutex> lk(ctx.picker.thumbMu);
  auto want = [&](uint64_t id) {
    // The cache alone cannot decide this. A window whose preview never arrives is absent from
    // `thumbs`, so keying the throttle off it re-queued that window on every refresh -- forever,
    // while the host was deliberately skipping it. thumbnail_fetch_due weighs the last ATTEMPT
    // too, and the two clients share it so the rule cannot drift apart again.
    remote60::native_poc::ThumbnailFetchState state;
    const auto it = ctx.picker.thumbs.find(id);
    if (it != ctx.picker.thumbs.end() && it->second) {
      state.havePreview = true;
      state.previewFetchedUs = it->second->fetchedUs;
    }
    const auto attempt = ctx.picker.thumbAttempts.find(id);
    if (attempt != ctx.picker.thumbAttempts.end()) {
      state.attempted = true;
      state.lastAttemptUs = attempt->second.lastAttemptUs;
      state.lastAttemptFailed = attempt->second.failed;
    }
    remote60::native_poc::ThumbnailFetchPolicy policy;
    policy.refreshUs = kThumbRefreshUs;
    policy.retryAfterFailureUs = kThumbRetryAfterFailureUs;
    if (!remote60::native_poc::thumbnail_fetch_due(state, policy, nowUs)) return;
    if (std::find(ctx.picker.thumbFetchQueue.begin(), ctx.picker.thumbFetchQueue.end(), id) != ctx.picker.thumbFetchQueue.end()) {
      return;
    }
    ctx.picker.thumbFetchQueue.push_back(id);
  };
  want(0);
  // Only the desktop preview is shown, so fetching a thumbnail per window would spend host
  // control-thread time on pictures nobody sees. (F-21.)
  if (kPickerListsWindows) {
    for (const auto& item : snap.items) want(item.id);
  } else {
    (void)snap;
  }
}

void queue_window_list_request(ViewerState& ctx, const char* statusText) {
  ctx.picker.windowPanel.RequestList(statusText);
}

void set_window_panel_status(ViewerState& ctx, const std::string& status) {
  ctx.picker.windowPanel.SetStatus(status);
}

void apply_window_list_snapshot(ViewerState& ctx, const ControlWindowListMessage& msg) {
  const ClientLayout layout = compute_client_layout(ctx, ctx.session.hwnd);
  const CardGridMetrics grid = compute_card_grid(ctx, layout.listRect);
  const auto result = ctx.picker.windowPanel.ApplyWindowList(msg, grid.visibleCards);
  ctx.picker.hostSupportsThumbnails.store(
      (msg.flags & remote60::native_poc::kControlWindowListFlagThumbnails) != 0,
      std::memory_order_relaxed);
  queue_thumbnail_fetches_from_panel(ctx);
  log_client_line(ctx, result.logLine);
}

/** Mirrors the session state into the toolbar window. Cheap enough to call on every change. */
void push_session_toolbar_state(ViewerState& ctx) {
  const WindowPanelSnapshot panel = ctx.picker.windowPanel.Snapshot();
  remote60::native_poc::SessionToolbarState state;
  state.connected = ctx.control.connected.load(std::memory_order_relaxed);
  state.inputOn = ctx.session.inputEnabled.load(std::memory_order_relaxed);
  state.macroOpen = remote60::native_poc::macro_window_visible();
  state.relay = ctx.session.relayPath.load(std::memory_order_relaxed);
  state.pathKnown = ctx.session.relayPathKnown.load(std::memory_order_relaxed);
  state.fps = ctx.metrics.Snapshot().decodedFpsX100 / 100;
  state.selectedMonitorId = panel.selectedMonitorId;
  for (const auto& monitor : panel.monitors) {
    state.monitors.push_back({monitor.id, monitor.width, monitor.height, monitor.primary});
  }
  remote60::native_poc::session_toolbar_update(state);
}

// Browsing targets must not keep the host encoding (F1). The request rides the control
// scheduler, which orders stream state ahead of window selection. Sent only on explicit
// picker transitions: startup leaves the host's default-active stream alone, so headless
// harness clients that never open the picker keep receiving video unchanged.
void set_picker_visible_and_sync_stream(ViewerState& ctx, bool visible) {
  const bool was = ctx.picker.visible.exchange(visible, std::memory_order_relaxed);
  // The transition, and the generation that decides whether the stream is held off. Between them
  // these separate "the flag never changed" from "it changed and nothing was drawn".
  log_client_line(ctx, std::string("[picker] visible ") + (was ? "1" : "0") + "->" +
                           (visible ? "1" : "0") + " activeGen=" +
                           std::to_string(ctx.sel.activeStreamGeneration.load(
                               std::memory_order_acquire)));
  if (visible) {
    // 🔴 F-21 superseded. It used to release the swapchain here, on the premise that dropping it
    // "hands the window back to GDI". It does not: once a flip-model swapchain has presented on an
    // HWND, GDI drawn into that window stops reaching the screen, and destroying the swapchain
    // does not undo that. Documented for DXGI_SWAP_EFFECT_FLIP_*, and measured here first -- the
    // window's own DC held the picker while the screen held the last video frame, through a forced
    // repaint, SWP_FRAMECHANGED and a resize (viewer_window_proc_isolated_test,
    // docs/ui_state_table.md §28.3). So the release made no difference to what the user saw and
    // cost a swapchain rebuild on the way back.
    //
    // The swapchain now stays, and the picker is presented through it: drawn by the same GDI code
    // into an offscreen surface and uploaded (viewer_present.cpp present_picker_frame). The video
    // path, the device the decoder shares, and the NV12 rendering are all unchanged.
    ctx.picker.shownAtUs.store(qpc_now_us(), std::memory_order_relaxed);
    ctx.picker.CancelPress();
    // Mid-session the stream KEEPS RUNNING behind the picker overlay. Stopping it here made every
    // "is it frozen?" peek tear the capture down (host detaches after 5 idle seconds), a real
    // multi-second blackout, and a reselect/keyframe churn on close -- the recovery gesture was
    // manufacturing the very freeze it was checking for. Only the initial picker, before any
    // selection has been revealed (ctx.sel.activeStreamGeneration==0), still holds the stream off.
    if (ctx.sel.activeStreamGeneration.load(std::memory_order_acquire) == 0) {
      ctx.control.streamState.Request(false);
    }
  } else {
    ctx.control.streamState.Request(true);
    // The present anchor froze while the picker covered the stream; drop it and hold catchup off
    // until the first post-close present re-anchors it, so the pause cannot read as backlog.
    ctx.frameBuf.lastPresentedCaptureUs.store(0, std::memory_order_relaxed);
    ctx.frameBuf.catchupSuppressUntilUs.store(qpc_now_us() + 500000, std::memory_order_relaxed);
  }
  // The picker draws its own header, so the toolbar belongs to the session view alone.
  remote60::native_poc::session_toolbar_set_visible(!visible);
  push_session_toolbar_state(ctx);
}

// Clears the in-flight selection state. Safe to call from any thread; touches only atomics and
// the stream request (itself atomic-backed).
void clear_pc_target_selection(ViewerState& ctx) {
  ctx.sel.Clear();
}

// UI-thread entry for picking a target from the picker. Orders the control traffic so the host's
// "!streamActive" continue-gate is already passed when the select arrives: the scheduler always
// sends StreamState ahead of WindowSelect, so requesting stream-on here (before or after the
// select is queued) still reaches the host first. Desktop is sent as an explicit WindowSelect(0)
// -- one clean restart -- rather than the "already selected, just hide" shortcut, for mobile
// parity. Ignores clicks while a selection is already in flight, which is the double-click guard.
// Returns true only when the select request was actually accepted for sending, so callers can
// log/refresh on real selections instead of refused attempts (disconnected, locked target).
bool begin_pc_target_selection(ViewerState& ctx, uint64_t windowId, const char* statusText) {
  if (ctx.sel.pending.load(std::memory_order_acquire)) return false;
  if (!ctx.control.connected.load(std::memory_order_relaxed)) return false;
  // RequestSelect refuses when the target is locked by host config; do not touch the stream then.
  if (!ctx.picker.windowPanel.RequestSelect(windowId, statusText)) return false;
  ctx.sel.Begin();
  ctx.control.streamState.Request(true);
  // A selection is a generation change; drop the remote-cursor sample so the previous target's
  // pointer cannot linger over the new one while the first fenced sample is in flight.
  ctx.cursor.Publish(RemoteCursorSample{});  // updateUs=0: the overlay treats it as stale (F-15)
  if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
  return true;
}

// Video-thread half of the reveal. It only *records* the candidate (the generation and epoch of
// the first decoded frame) and posts the reveal once; it deliberately touches none of the live
// selection state. The UI-thread handler revalidates against that state before committing, so a
// cancel / new selection / disconnect that races the post cannot wrongly close the picker.
void post_pc_selection_reveal(ViewerState& ctx, uint64_t readyGeneration, uint64_t readyEpoch) {
  if (ctx.sel.RecordReveal(readyGeneration, readyEpoch)) {
    if (ctx.session.hwnd) PostMessageW(ctx.session.hwnd, kMsgRevealStreamView, 0, 0);
  }
}

void apply_window_selected_result(ViewerState& ctx, const ControlWindowSelectedMessage& msg) {
  const auto result = ctx.picker.windowPanel.ApplyWindowSelected(msg);
  log_client_line(ctx, result.logLine);
  switch (ctx.sel.ApplyAck(result.ok, msg.streamGeneration)) {
    case SelectionAck::Acked:
      ctx.picker.windowPanel.SetStatus("waiting_first_frame");
      if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
      return;
    case SelectionAck::Failed:
      // Select failed: stop the stream we speculatively started, keep the picker, allow a retry.
      ctx.control.streamState.Request(false);
      clear_pc_target_selection(ctx);
      if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
      return;
    case SelectionAck::NotPending:
      break;
  }
  // No PC-side selection tracked (e.g. a legacy stream-view session): behave as before.
  if (result.ok) {
    set_picker_visible_and_sync_stream(ctx, false);
  }
}

void scroll_window_list(ViewerState& ctx, HWND hwnd, int deltaSteps) {
  const ClientLayout layout = compute_client_layout(ctx, hwnd);
  const CardGridMetrics grid = compute_card_grid(ctx, layout.listRect);
  const int totalCards =
      kPickerListsWindows ? 1 + static_cast<int>(ctx.picker.windowPanel.Snapshot().items.size()) : 1;
  const int totalRows = (totalCards + grid.cols - 1) / grid.cols;
  const int maxScrollRow = std::max(0, totalRows - grid.visibleRows);
  const int cur = ctx.picker.gridScrollRow.load(std::memory_order_relaxed);
  ctx.picker.gridScrollRow.store(std::clamp(cur + deltaSteps, 0, maxScrollRow), std::memory_order_relaxed);
}

// Hit-test a grid card. Card index 0 is the pinned Desktop card; window items follow.
bool try_hit_window_list_item(ViewerState& ctx, HWND hwnd, int x, int y, uint64_t* outWindowId) {
  if (!outWindowId) return false;
  const ClientLayout layout = compute_client_layout(ctx, hwnd);
  if (!point_in_rect(layout.listRect, x, y)) return false;
  const CardGridMetrics grid = compute_card_grid(ctx, layout.listRect);
  int cardIndex = 0;
  if (!card_hit_test(layout.listRect, grid, ctx.picker.gridScrollRow.load(std::memory_order_relaxed), x, y, &cardIndex)) {
    return false;
  }
  const WindowPanelSnapshot snap = ctx.picker.windowPanel.Snapshot();
  if (cardIndex == 0) {
    *outWindowId = 0;
    return true;
  }
  if (!kPickerListsWindows) return false;  // desktop is the only target (F-21)
  const int itemIndex = cardIndex - 1;
  if (itemIndex < 0 || itemIndex >= static_cast<int>(snap.items.size())) return false;
  *outWindowId = snap.items[static_cast<size_t>(itemIndex)].id;
  return true;
}

void enqueue_capture_mode_request(ViewerState& ctx, uint16_t mode, uint32_t xPermille, uint32_t yPermille) {
  ctx.control.captureModeRequests.Request(mode, xPermille, yPermille);
}

void request_capture_overview_mode(ViewerState& ctx) {
  enqueue_capture_mode_request(ctx, 1, 5000, 5000);
}

}  // namespace remote60::native_poc::viewer
