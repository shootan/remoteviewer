// See viewer_picker.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_picker.hpp"

#include "viewer_common.hpp"
#include "viewer_globals.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_layout.hpp"
#include "viewer_log.hpp"

namespace remote60::native_poc::viewer {

ClientControlMetricsSnapshot capture_client_control_metrics_snapshot() {
  ClientControlMetricsSnapshot snapshot{};
  snapshot.updatedQpcUs = gMetrics.client.updatedQpcUs.load(std::memory_order_relaxed);
  snapshot.message.width = gMetrics.client.width.load(std::memory_order_relaxed);
  snapshot.message.height = gMetrics.client.height.load(std::memory_order_relaxed);
  snapshot.message.recvFpsX100 = gMetrics.client.recvFpsX100.load(std::memory_order_relaxed);
  snapshot.message.decodedFpsX100 = gMetrics.client.decodedFpsX100.load(std::memory_order_relaxed);
  snapshot.message.recvMbpsX1000 = gMetrics.client.recvMbpsX1000.load(std::memory_order_relaxed);
  snapshot.message.skippedFrames = gMetrics.client.skippedFrames.load(std::memory_order_relaxed);
  snapshot.message.avgLatencyUs = gMetrics.client.avgLatencyUs.load(std::memory_order_relaxed);
  snapshot.message.maxLatencyUs = gMetrics.client.maxLatencyUs.load(std::memory_order_relaxed);
  snapshot.message.avgDecodeTailUs = gMetrics.client.avgDecodeTailUs.load(std::memory_order_relaxed);
  snapshot.message.maxDecodeTailUs = gMetrics.client.maxDecodeTailUs.load(std::memory_order_relaxed);
  snapshot.message.congestionState = gMetrics.client.congestionState.load(std::memory_order_relaxed);
  snapshot.message.congestionTransitions = gMetrics.client.congestionTransitions.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryCount =
      gMetrics.client.congestionRecoveryCount.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryReq =
      gMetrics.client.congestionRecoveryReq.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryMaxUs =
      gMetrics.client.congestionRecoveryMaxUs.load(std::memory_order_relaxed);
  snapshot.message.queueDepthMax = gMetrics.client.queueDepthMax.load(std::memory_order_relaxed);
  snapshot.message.queueDepthH4p = gMetrics.client.queueDepthH4p.load(std::memory_order_relaxed);
  snapshot.message.udpAssemblyDropPm = gMetrics.client.udpAssemblyDropPm.load(std::memory_order_relaxed);
  return snapshot;
}

void queue_thumbnail_fetches_from_panel() {
  if (!gPicker.hostSupportsThumbnails.load(std::memory_order_relaxed)) return;
  const WindowPanelSnapshot snap = gPicker.windowPanel.Snapshot();
  const uint64_t nowUs = qpc_now_us();
  std::lock_guard<std::mutex> lk(gPicker.thumbMu);
  auto want = [&](uint64_t id) {
    const auto it = gPicker.thumbs.find(id);
    if (it != gPicker.thumbs.end() && it->second && nowUs - it->second->fetchedUs < kThumbRefreshUs) return;
    if (std::find(gPicker.thumbFetchQueue.begin(), gPicker.thumbFetchQueue.end(), id) != gPicker.thumbFetchQueue.end()) {
      return;
    }
    gPicker.thumbFetchQueue.push_back(id);
  };
  want(0);
  for (const auto& item : snap.items) want(item.id);
}

void queue_window_list_request(const char* statusText) {
  gPicker.windowPanel.RequestList(statusText);
}

void queue_window_select_request(uint64_t windowId, const char* statusText) {
  gPicker.windowPanel.RequestSelect(windowId, statusText);
}

void set_window_panel_status(const std::string& status) {
  gPicker.windowPanel.SetStatus(status);
}

void apply_window_list_snapshot(const ControlWindowListMessage& msg) {
  const ClientLayout layout = compute_client_layout(gSession.hwnd);
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const auto result = gPicker.windowPanel.ApplyWindowList(msg, grid.visibleCards);
  gPicker.hostSupportsThumbnails.store(
      (msg.flags & remote60::native_poc::kControlWindowListFlagThumbnails) != 0,
      std::memory_order_relaxed);
  queue_thumbnail_fetches_from_panel();
  log_client_line(result.logLine);
}

/** Mirrors the session state into the toolbar window. Cheap enough to call on every change. */
void push_session_toolbar_state() {
  const WindowPanelSnapshot panel = gPicker.windowPanel.Snapshot();
  remote60::native_poc::SessionToolbarState state;
  state.connected = gControl.connected.load(std::memory_order_relaxed);
  state.inputOn = gSession.inputEnabled.load(std::memory_order_relaxed);
  state.macroOpen = remote60::native_poc::macro_window_visible();
  state.relay = gSession.relayPath.load(std::memory_order_relaxed);
  state.fps = gMetrics.client.decodedFpsX100.load(std::memory_order_relaxed) / 100;
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
void set_picker_visible_and_sync_stream(bool visible) {
  gPicker.visible.store(visible, std::memory_order_relaxed);
  if (visible) {
    gPicker.shownAtUs.store(qpc_now_us(), std::memory_order_relaxed);
    gPicker.CancelPress();
    // Mid-session the stream KEEPS RUNNING behind the picker overlay. Stopping it here made every
    // "is it frozen?" peek tear the capture down (host detaches after 5 idle seconds), a real
    // multi-second blackout, and a reselect/keyframe churn on close -- the recovery gesture was
    // manufacturing the very freeze it was checking for. Only the initial picker, before any
    // selection has been revealed (gSel.activeStreamGeneration==0), still holds the stream off.
    if (gSel.activeStreamGeneration.load(std::memory_order_acquire) == 0) {
      gControl.streamState.Request(false);
    }
  } else {
    gControl.streamState.Request(true);
    // The present anchor froze while the picker covered the stream; drop it and hold catchup off
    // until the first post-close present re-anchors it, so the pause cannot read as backlog.
    gFrameBuf.lastPresentedCaptureUs.store(0, std::memory_order_relaxed);
    gFrameBuf.catchupSuppressUntilUs.store(qpc_now_us() + 500000, std::memory_order_relaxed);
  }
  // The picker draws its own header, so the toolbar belongs to the session view alone.
  remote60::native_poc::session_toolbar_set_visible(!visible);
  push_session_toolbar_state();
}

// Clears the in-flight selection state. Safe to call from any thread; touches only atomics and
// the stream request (itself atomic-backed).
void clear_pc_target_selection() {
  gSel.Clear();
}

// UI-thread entry for picking a target from the picker. Orders the control traffic so the host's
// "!streamActive" continue-gate is already passed when the select arrives: the scheduler always
// sends StreamState ahead of WindowSelect, so requesting stream-on here (before or after the
// select is queued) still reaches the host first. Desktop is sent as an explicit WindowSelect(0)
// -- one clean restart -- rather than the "already selected, just hide" shortcut, for mobile
// parity. Ignores clicks while a selection is already in flight, which is the double-click guard.
// Returns true only when the select request was actually accepted for sending, so callers can
// log/refresh on real selections instead of refused attempts (disconnected, locked target).
bool begin_pc_target_selection(uint64_t windowId, const char* statusText) {
  if (gSel.pending.load(std::memory_order_acquire)) return false;
  if (!gControl.connected.load(std::memory_order_relaxed)) return false;
  // RequestSelect refuses when the target is locked by host config; do not touch the stream then.
  if (!gPicker.windowPanel.RequestSelect(windowId, statusText)) return false;
  gSel.Begin();
  gControl.streamState.Request(true);
  // A selection is a generation change; drop the remote-cursor sample so the previous target's
  // pointer cannot linger over the new one while the first fenced sample is in flight.
  gCursor.updateUs.store(0, std::memory_order_release);
  if (gSession.hwnd) InvalidateRect(gSession.hwnd, nullptr, FALSE);
  return true;
}

// Video-thread half of the reveal. It only *records* the candidate (the generation and epoch of
// the first decoded frame) and posts the reveal once; it deliberately touches none of the live
// selection state. The UI-thread handler revalidates against that state before committing, so a
// cancel / new selection / disconnect that races the post cannot wrongly close the picker.
void post_pc_selection_reveal(uint64_t readyGeneration, uint64_t readyEpoch) {
  if (gSel.RecordReveal(readyGeneration, readyEpoch)) {
    if (gSession.hwnd) PostMessageW(gSession.hwnd, kMsgRevealStreamView, 0, 0);
  }
}

void apply_window_selected_result(const ControlWindowSelectedMessage& msg) {
  const auto result = gPicker.windowPanel.ApplyWindowSelected(msg);
  log_client_line(result.logLine);
  switch (gSel.ApplyAck(result.ok, msg.streamGeneration)) {
    case SelectionAck::Acked:
      gPicker.windowPanel.SetStatus("waiting_first_frame");
      if (gSession.hwnd) InvalidateRect(gSession.hwnd, nullptr, FALSE);
      return;
    case SelectionAck::Failed:
      // Select failed: stop the stream we speculatively started, keep the picker, allow a retry.
      gControl.streamState.Request(false);
      clear_pc_target_selection();
      if (gSession.hwnd) InvalidateRect(gSession.hwnd, nullptr, FALSE);
      return;
    case SelectionAck::NotPending:
      break;
  }
  // No PC-side selection tracked (e.g. a legacy stream-view session): behave as before.
  if (result.ok) {
    set_picker_visible_and_sync_stream(false);
  }
}

void scroll_window_list(HWND hwnd, int deltaSteps) {
  const ClientLayout layout = compute_client_layout(hwnd);
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int totalCards = 1 + static_cast<int>(gPicker.windowPanel.Snapshot().items.size());
  const int totalRows = (totalCards + grid.cols - 1) / grid.cols;
  const int maxScrollRow = std::max(0, totalRows - grid.visibleRows);
  const int cur = gPicker.gridScrollRow.load(std::memory_order_relaxed);
  gPicker.gridScrollRow.store(std::clamp(cur + deltaSteps, 0, maxScrollRow), std::memory_order_relaxed);
}

// Hit-test a grid card. Card index 0 is the pinned Desktop card; window items follow.
bool try_hit_window_list_item(HWND hwnd, int x, int y, uint64_t* outWindowId) {
  if (!outWindowId) return false;
  const ClientLayout layout = compute_client_layout(hwnd);
  if (!point_in_rect(layout.listRect, x, y)) return false;
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  int cardIndex = 0;
  if (!card_hit_test(layout.listRect, grid, gPicker.gridScrollRow.load(std::memory_order_relaxed), x, y, &cardIndex)) {
    return false;
  }
  const WindowPanelSnapshot snap = gPicker.windowPanel.Snapshot();
  if (cardIndex == 0) {
    *outWindowId = 0;
    return true;
  }
  const int itemIndex = cardIndex - 1;
  if (itemIndex < 0 || itemIndex >= static_cast<int>(snap.items.size())) return false;
  *outWindowId = snap.items[static_cast<size_t>(itemIndex)].id;
  return true;
}

void enqueue_capture_mode_request(uint16_t mode, uint32_t xPermille, uint32_t yPermille) {
  gControl.captureModeRequests.Request(mode, xPermille, yPermille);
}

void request_capture_overview_mode() {
  enqueue_capture_mode_request(1, 5000, 5000);
}

void request_capture_focus_from_client_point(HWND hwnd, int x, int y) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int clientW = std::max<int>(1, static_cast<int>(rc.right - rc.left));
  const int clientH = std::max<int>(1, static_cast<int>(rc.bottom - rc.top));
  enqueue_capture_mode_request(2, coord_to_permille(x, clientW), coord_to_permille(y, clientH));
}

}  // namespace remote60::native_poc::viewer
