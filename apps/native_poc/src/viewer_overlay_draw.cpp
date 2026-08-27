// See viewer_overlay_draw.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_overlay_draw.hpp"

#include "viewer_common.hpp"
#include "viewer_gdi_util.hpp"
#include "viewer_globals.hpp"
#include "viewer_layout.hpp"

namespace remote60::native_poc::viewer {

void push_overlay_metric_sample(uint32_t recvFpsX100, uint32_t decodedFpsX100, uint32_t recvMbpsX1000,
                                uint64_t avgLatencyUs, uint64_t nowUs) {
  std::lock_guard<std::mutex> lk(gMetrics.overlayMu);
  gMetrics.overlay.push_back({nowUs, recvFpsX100, decodedFpsX100, recvMbpsX1000, avgLatencyUs});
  const uint64_t keepWindowUs = 12000000ULL;
  while (!gMetrics.overlay.empty() && nowUs > gMetrics.overlay.front().tsUs &&
         (nowUs - gMetrics.overlay.front().tsUs) > keepWindowUs) {
    gMetrics.overlay.pop_front();
  }
}

OverlayMetricAverages collect_overlay_averages(uint64_t nowUs, uint64_t windowUs) {
  OverlayMetricAverages out{};
  std::lock_guard<std::mutex> lk(gMetrics.overlayMu);
  uint64_t sumRecvFpsX100 = 0;
  uint64_t sumDecodedFpsX100 = 0;
  uint64_t sumRecvMbpsX1000 = 0;
  uint64_t sumLatencyUs = 0;
  for (const auto& s : gMetrics.overlay) {
    if (nowUs >= s.tsUs && (nowUs - s.tsUs) <= windowUs) {
      ++out.sampleCount;
      sumRecvFpsX100 += s.recvFpsX100;
      sumDecodedFpsX100 += s.decodedFpsX100;
      sumRecvMbpsX1000 += s.recvMbpsX1000;
      sumLatencyUs += s.avgLatencyUs;
    }
  }
  if (out.sampleCount > 0) {
    out.recvFpsX100 = static_cast<uint32_t>(sumRecvFpsX100 / out.sampleCount);
    out.decodedFpsX100 = static_cast<uint32_t>(sumDecodedFpsX100 / out.sampleCount);
    out.recvMbpsX1000 = static_cast<uint32_t>(sumRecvMbpsX1000 / out.sampleCount);
    out.avgLatencyUs = sumLatencyUs / out.sampleCount;
  }
  return out;
}

void apply_runtime_tune_delta(int bitrateStep, int keyintStep) {
  gControl.runtimeTune.ApplyDelta(
      bitrateStep, keyintStep, gMetrics.client.recvMbpsX1000.load(std::memory_order_relaxed));
}

void draw_thumbnail_into(HDC hdc, const RECT& dst, const WindowThumb& thumb) {
  if (thumb.bgra.empty() || thumb.width == 0 || thumb.height == 0) return;
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = static_cast<LONG>(thumb.width);
  bmi.bmiHeader.biHeight = -static_cast<LONG>(thumb.height);
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  const RECT fit = aspect_fit_rect(dst, thumb.width, thumb.height);
  // Thumbnails repaint rarely, so the quality mode is affordable here.
  SetStretchBltMode(hdc, HALFTONE);
  SetBrushOrgEx(hdc, 0, 0, nullptr);
  StretchDIBits(hdc, fit.left, fit.top, fit.right - fit.left, fit.bottom - fit.top, 0, 0,
                static_cast<int>(thumb.width), static_cast<int>(thumb.height), thumb.bgra.data(),
                &bmi, DIB_RGB_COLORS, SRCCOPY);
}

void draw_target_card(HDC hdc, const RECT& card, const CardGridMetrics& grid,
                      uint64_t windowId, const std::string& title, bool active, bool disabled) {
  const RECT thumbRect = make_rect(card.left, card.top, card.right - card.left, grid.thumbH);
  const RECT captionRect = make_rect(card.left, card.top + grid.thumbH, card.right - card.left,
                                     card.bottom - card.top - grid.thumbH);

  FillRect(hdc, &thumbRect, cached_brush(RGB(24, 28, 36)));
  FillRect(hdc, &captionRect, cached_brush(active ? RGB(38, 70, 52) : RGB(32, 37, 46)));

  // Snapshot under the lock, draw outside it: StretchDIBits under gPicker.thumbMu made the fetch
  // thread and the paint stall each other.
  std::shared_ptr<const WindowThumb> thumb;
  {
    std::lock_guard<std::mutex> lk(gPicker.thumbMu);
    const auto it = gPicker.thumbs.find(windowId);
    if (it != gPicker.thumbs.end()) thumb = it->second;
  }
  if (thumb) {
    draw_thumbnail_into(hdc, thumbRect, *thumb);
  } else {
    RECT ph = thumbRect;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(110, 118, 130));
    draw_text_utf8(hdc, windowId == 0 ? std::string("Desktop") : std::string("Loading preview..."),
                   &ph, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, disabled ? RGB(150, 155, 162) : RGB(236, 239, 243));
  RECT text = captionRect;
  text.left += dpi_scale(10);
  text.right -= dpi_scale(10);
  draw_text_utf8(hdc, title, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  RECT frame = card;
  FrameRect(hdc, &frame, cached_brush(active ? RGB(88, 178, 122) : RGB(52, 58, 70)));
  if (active) {
    RECT inner{card.left + 1, card.top + 1, card.right - 1, card.bottom - 1};
    FrameRect(hdc, &inner, cached_brush(RGB(88, 178, 122)));
  }
}

void draw_overlay(HDC hdc) {
  const ClientLayout layout = compute_client_layout(gSession.hwnd);
  const bool pickerVisible = gPicker.visible.load(std::memory_order_relaxed);
  if (!pickerVisible) {
    // Nothing to draw over the stream: the legacy Targets/Macro buttons were invisible ghost
    // hit-zones under the flip-model video (see compute_client_layout); the toolbar owns that UI.
    return;
  }

  draw_alpha_rect(hdc, layout.clientRect, RGB(13, 15, 20), 255);

  const WindowPanelSnapshot windowPanel = gPicker.windowPanel.Snapshot();
  const std::vector<WindowTargetUiEntry>& windowItems = windowPanel.items;
  const uint64_t selectedId = windowPanel.selectedId;
  const std::string& panelStatus = windowPanel.status;
  const bool selectionLocked = windowPanel.selectionLocked;

  // Header: product title and status on the left, actions on the right.
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(240, 243, 247));
  RECT titleRect = layout.selectedInfoRect;
  {
    HGDIOBJ old = gUiTitleFont ? SelectObject(hdc, gUiTitleFont) : nullptr;
    RECT t = titleRect;
    DrawTextW(hdc, L"Remote60", -1, &t, DT_LEFT | DT_SINGLELINE);
    if (old) SelectObject(hdc, old);
  }
  // Once a target is picked the picker locks: the buttons and cards read as disabled while the
  // stream spins up, and the sub-header says whether we are still waiting on the host's ack or
  // on its first frame.
  const bool selectionPending = gSelectionPending.load(std::memory_order_acquire);
  const bool awaitingAck = gSelectionAwaitingAck.load(std::memory_order_acquire);

  RECT subRect = titleRect;
  subRect.top += dpi_scale(28);
  SetTextColor(hdc, RGB(150, 158, 170));
  std::string statusLine =
      selectionLocked ? std::string("Target locked by host config") : panelStatus;
  if (selectionPending) {
    statusLine = awaitingAck ? std::string("Selecting target...")
                             : std::string("Waiting for first frame...");
  }
  if (!gControl.connected.load(std::memory_order_relaxed)) statusLine = "Connecting to host...";
  draw_text_utf8(hdc, statusLine, &subRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

  const bool actionsDisabled =
      !gControl.connected.load(std::memory_order_relaxed) || selectionLocked || selectionPending;
  draw_panel_button(hdc, layout.refreshButtonRect, "Refresh", false,
                    !gControl.connected.load(std::memory_order_relaxed) || selectionPending);
  draw_panel_button(hdc, layout.desktopButtonRect, "Desktop", selectedId == 0, actionsDisabled);

  // Card grid: desktop preview first, then one card per shareable window.
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int totalCards = 1 + static_cast<int>(windowItems.size());
  const int totalRows = (totalCards + grid.cols - 1) / grid.cols;
  const int maxScrollRow = std::max(0, totalRows - grid.visibleRows);
  int scrollRow = std::clamp(gPicker.gridScrollRow.load(std::memory_order_relaxed), 0, maxScrollRow);
  gPicker.gridScrollRow.store(scrollRow, std::memory_order_relaxed);
  const int firstCard = scrollRow * grid.cols;

  for (int slot = 0; slot < grid.visibleCards; ++slot) {
    const int cardIndex = firstCard + slot;
    if (cardIndex >= totalCards) break;
    const RECT card = card_rect_for_slot(layout.listRect, grid, slot);
    if (cardIndex == 0) {
      draw_target_card(hdc, card, grid, 0, "Desktop (full screen)", selectedId == 0,
                       selectionLocked || selectionPending);
    } else {
      const auto& entry = windowItems[static_cast<size_t>(cardIndex - 1)];
      draw_target_card(hdc, card, grid, entry.id, entry.title, entry.id == selectedId,
                       selectionLocked || selectionPending);
    }
  }

  if (windowItems.empty()) {
    RECT emptyRect = layout.listRect;
    emptyRect.top += grid.cardH + dpi_scale(18);
    SetTextColor(hdc, RGB(150, 158, 170));
    draw_text_utf8(hdc,
                   selectionLocked ? std::string("Window list hidden by host config")
                                   : std::string("No shareable windows yet. Click Refresh."),
                   &emptyRect, DT_CENTER | DT_SINGLELINE);
  }

  // Footer: connection and input state in one quiet line.
  std::ostringstream foot;
  foot << (gControl.connected.load(std::memory_order_relaxed) ? "Connected" : "Disconnected")
       << "   Input " << (gSession.inputEnabled.load(std::memory_order_relaxed) ? "on" : "off");
  const uint32_t decFpsX100 = gMetrics.client.decodedFpsX100.load(std::memory_order_relaxed);
  if (decFpsX100 > 0) foot << "   " << (decFpsX100 / 100) << " fps";
  if (totalRows > grid.visibleRows) {
    foot << "   Rows " << (scrollRow + 1) << "-"
         << std::min(totalRows, scrollRow + grid.visibleRows) << " / " << totalRows
         << " (wheel to scroll)";
  }
  RECT footRect = layout.statsRect;
  SetTextColor(hdc, RGB(140, 148, 160));
  draw_text_utf8(hdc, foot.str(), &footRect,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

}  // namespace remote60::native_poc::viewer
