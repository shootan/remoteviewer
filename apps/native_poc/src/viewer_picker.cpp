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
  snapshot.updatedQpcUs = gClientMetrics.updatedQpcUs.load(std::memory_order_relaxed);
  snapshot.message.width = gClientMetrics.width.load(std::memory_order_relaxed);
  snapshot.message.height = gClientMetrics.height.load(std::memory_order_relaxed);
  snapshot.message.recvFpsX100 = gClientMetrics.recvFpsX100.load(std::memory_order_relaxed);
  snapshot.message.decodedFpsX100 = gClientMetrics.decodedFpsX100.load(std::memory_order_relaxed);
  snapshot.message.recvMbpsX1000 = gClientMetrics.recvMbpsX1000.load(std::memory_order_relaxed);
  snapshot.message.skippedFrames = gClientMetrics.skippedFrames.load(std::memory_order_relaxed);
  snapshot.message.avgLatencyUs = gClientMetrics.avgLatencyUs.load(std::memory_order_relaxed);
  snapshot.message.maxLatencyUs = gClientMetrics.maxLatencyUs.load(std::memory_order_relaxed);
  snapshot.message.avgDecodeTailUs = gClientMetrics.avgDecodeTailUs.load(std::memory_order_relaxed);
  snapshot.message.maxDecodeTailUs = gClientMetrics.maxDecodeTailUs.load(std::memory_order_relaxed);
  snapshot.message.congestionState = gClientMetrics.congestionState.load(std::memory_order_relaxed);
  snapshot.message.congestionTransitions = gClientMetrics.congestionTransitions.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryCount =
      gClientMetrics.congestionRecoveryCount.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryReq =
      gClientMetrics.congestionRecoveryReq.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryMaxUs =
      gClientMetrics.congestionRecoveryMaxUs.load(std::memory_order_relaxed);
  snapshot.message.queueDepthMax = gClientMetrics.queueDepthMax.load(std::memory_order_relaxed);
  snapshot.message.queueDepthH4p = gClientMetrics.queueDepthH4p.load(std::memory_order_relaxed);
  snapshot.message.udpAssemblyDropPm = gClientMetrics.udpAssemblyDropPm.load(std::memory_order_relaxed);
  return snapshot;
}

void queue_thumbnail_fetches_from_panel() {
  if (!gHostSupportsThumbnails.load(std::memory_order_relaxed)) return;
  const WindowPanelSnapshot snap = gWindowPanelState.Snapshot();
  const uint64_t nowUs = qpc_now_us();
  std::lock_guard<std::mutex> lk(gThumbMu);
  auto want = [&](uint64_t id) {
    const auto it = gThumbs.find(id);
    if (it != gThumbs.end() && it->second && nowUs - it->second->fetchedUs < kThumbRefreshUs) return;
    if (std::find(gThumbFetchQueue.begin(), gThumbFetchQueue.end(), id) != gThumbFetchQueue.end()) {
      return;
    }
    gThumbFetchQueue.push_back(id);
  };
  want(0);
  for (const auto& item : snap.items) want(item.id);
}

void queue_window_list_request(const char* statusText) {
  gWindowPanelState.RequestList(statusText);
}

void queue_window_select_request(uint64_t windowId, const char* statusText) {
  gWindowPanelState.RequestSelect(windowId, statusText);
}

void set_window_panel_status(const std::string& status) {
  gWindowPanelState.SetStatus(status);
}

void apply_window_list_snapshot(const ControlWindowListMessage& msg) {
  const ClientLayout layout = compute_client_layout(gHwnd);
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const auto result = gWindowPanelState.ApplyWindowList(msg, grid.visibleCards);
  gHostSupportsThumbnails.store(
      (msg.flags & remote60::native_poc::kControlWindowListFlagThumbnails) != 0,
      std::memory_order_relaxed);
  queue_thumbnail_fetches_from_panel();
  log_client_line(result.logLine);
}

/** Mirrors the session state into the toolbar window. Cheap enough to call on every change. */
void push_session_toolbar_state() {
  const WindowPanelSnapshot panel = gWindowPanelState.Snapshot();
  remote60::native_poc::SessionToolbarState state;
  state.connected = gControlConnected.load(std::memory_order_relaxed);
  state.inputOn = gInputEnabled.load(std::memory_order_relaxed);
  state.macroOpen = remote60::native_poc::macro_window_visible();
  state.relay = gRelayPath.load(std::memory_order_relaxed);
  state.fps = gClientMetrics.decodedFpsX100.load(std::memory_order_relaxed) / 100;
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
  gWindowPickerVisible.store(visible, std::memory_order_relaxed);
  if (visible) {
    gPickerShownAtUs.store(qpc_now_us(), std::memory_order_relaxed);
    gPickerPressTargetId.store(kPickerPressNone, std::memory_order_relaxed);
    // Mid-session the stream KEEPS RUNNING behind the picker overlay. Stopping it here made every
    // "is it frozen?" peek tear the capture down (host detaches after 5 idle seconds), a real
    // multi-second blackout, and a reselect/keyframe churn on close -- the recovery gesture was
    // manufacturing the very freeze it was checking for. Only the initial picker, before any
    // selection has been revealed (gActiveStreamGeneration==0), still holds the stream off.
    if (gActiveStreamGeneration.load(std::memory_order_acquire) == 0) {
      gStreamStateControl.Request(false);
    }
  } else {
    gStreamStateControl.Request(true);
    // The present anchor froze while the picker covered the stream; drop it and hold catchup off
    // until the first post-close present re-anchors it, so the pause cannot read as backlog.
    gLastPresentedCaptureUs.store(0, std::memory_order_relaxed);
    gCatchupSuppressUntilUs.store(qpc_now_us() + 500000, std::memory_order_relaxed);
  }
  // The picker draws its own header, so the toolbar belongs to the session view alone.
  remote60::native_poc::session_toolbar_set_visible(!visible);
  push_session_toolbar_state();
}

// Clears the in-flight selection state. Safe to call from any thread; touches only atomics and
// the stream request (itself atomic-backed).
void clear_pc_target_selection() {
  gSelectionPending.store(false, std::memory_order_release);
  gSelectionAwaitingAck.store(false, std::memory_order_release);
  gSelectionExpectedGeneration.store(0, std::memory_order_release);
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
  if (gSelectionPending.load(std::memory_order_acquire)) return false;
  if (!gControlConnected.load(std::memory_order_relaxed)) return false;
  // RequestSelect refuses when the target is locked by host config; do not touch the stream then.
  if (!gWindowPanelState.RequestSelect(windowId, statusText)) return false;
  gSelectionExpectedGeneration.store(0, std::memory_order_release);
  gSelectionAwaitingAck.store(true, std::memory_order_release);
  gSelectionPending.store(true, std::memory_order_release);
  // Bumped so the receive loop resets the decoder and holds for the new generation's keyframe.
  gSelectionEpoch.fetch_add(1, std::memory_order_acq_rel);
  gStreamStateControl.Request(true);
  // A selection is a generation change; drop the remote-cursor sample so the previous target's
  // pointer cannot linger over the new one while the first fenced sample is in flight.
  gRemoteCursorUpdateUs.store(0, std::memory_order_release);
  if (gHwnd) InvalidateRect(gHwnd, nullptr, FALSE);
  return true;
}

// Video-thread half of the reveal. It only *records* the candidate (the generation and epoch of
// the first decoded frame) and posts the reveal once; it deliberately touches none of the live
// selection state. The UI-thread handler revalidates against that state before committing, so a
// cancel / new selection / disconnect that races the post cannot wrongly close the picker.
void post_pc_selection_reveal(uint64_t readyGeneration, uint64_t readyEpoch) {
  gSelectionReadyGeneration.store(readyGeneration, std::memory_order_release);
  gSelectionReadyEpoch.store(readyEpoch, std::memory_order_release);
  bool expected = false;
  if (gSelectionRevealPosted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    if (gHwnd) PostMessageW(gHwnd, kMsgRevealStreamView, 0, 0);
  }
}

void apply_window_selected_result(const ControlWindowSelectedMessage& msg) {
  const auto result = gWindowPanelState.ApplyWindowSelected(msg);
  log_client_line(result.logLine);
  if (gSelectionPending.load(std::memory_order_acquire)) {
    if (result.ok) {
      // Ack received: hold the picker up until the first frame of this generation is presented.
      // Do NOT hide the picker here -- that is what the first-frame gate is for.
      gSelectionExpectedGeneration.store(msg.streamGeneration, std::memory_order_release);
      gSelectionAwaitingAck.store(false, std::memory_order_release);
      gWindowPanelState.SetStatus("waiting_first_frame");
    } else {
      // Select failed: stop the stream we speculatively started, keep the picker, allow a retry.
      gStreamStateControl.Request(false);
      clear_pc_target_selection();
    }
    if (gHwnd) InvalidateRect(gHwnd, nullptr, FALSE);
    return;
  }
  // No PC-side selection tracked (e.g. a legacy stream-view session): behave as before.
  if (result.ok) {
    set_picker_visible_and_sync_stream(false);
  }
}

void scroll_window_list(HWND hwnd, int deltaSteps) {
  const ClientLayout layout = compute_client_layout(hwnd);
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int totalCards = 1 + static_cast<int>(gWindowPanelState.Snapshot().items.size());
  const int totalRows = (totalCards + grid.cols - 1) / grid.cols;
  const int maxScrollRow = std::max(0, totalRows - grid.visibleRows);
  const int cur = gGridScrollRow.load(std::memory_order_relaxed);
  gGridScrollRow.store(std::clamp(cur + deltaSteps, 0, maxScrollRow), std::memory_order_relaxed);
}

// Hit-test a grid card. Card index 0 is the pinned Desktop card; window items follow.
bool try_hit_window_list_item(HWND hwnd, int x, int y, uint64_t* outWindowId) {
  if (!outWindowId) return false;
  const ClientLayout layout = compute_client_layout(hwnd);
  if (!point_in_rect(layout.listRect, x, y)) return false;
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int relX = x - layout.listRect.left;
  const int relY = y - layout.listRect.top;
  const int col = relX / (grid.cardW + grid.gap);
  const int row = relY / (grid.cardH + grid.gap);
  if (col < 0 || col >= grid.cols || row < 0 || row >= grid.visibleRows) return false;
  // Reject clicks that land in the gaps between cards.
  if (relX - col * (grid.cardW + grid.gap) >= grid.cardW) return false;
  if (relY - row * (grid.cardH + grid.gap) >= grid.cardH) return false;
  const WindowPanelSnapshot snap = gWindowPanelState.Snapshot();
  const int cardIndex =
      gGridScrollRow.load(std::memory_order_relaxed) * grid.cols + row * grid.cols + col;
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
  gCaptureModeRequests.Request(mode, xPermille, yPermille);
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
