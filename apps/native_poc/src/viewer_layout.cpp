// See viewer_layout.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_layout.hpp"

#include "viewer_common.hpp"
#include "viewer_globals.hpp"
#include "viewer_gdi_util.hpp"

namespace remote60::native_poc::viewer {

CardGridMetrics compute_card_grid(const RECT& gridRect) {
  return compute_card_grid_at(gridRect, gUi.dpi);
}

bool resolve_active_video_content_size(uint32_t* outWidth, uint32_t* outHeight) {
  if (!outWidth || !outHeight) return false;
  *outWidth = 0;
  *outHeight = 0;

  const WindowPanelSnapshot panelSnapshot = gPicker.windowPanel.Snapshot();
  const uint32_t selectedWidth = panelSnapshot.selectedWidth;
  const uint32_t selectedHeight = panelSnapshot.selectedHeight;
  const uint64_t selectedStreamGeneration = panelSnapshot.lastSelectStreamGeneration;

  uint32_t frameWidth = 0;
  uint32_t frameHeight = 0;
  uint64_t frameStreamGeneration = 0;
  {
    std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
    frameWidth = gFrameBuf.frame.width;
    frameHeight = gFrameBuf.frame.height;
    frameStreamGeneration = gFrameBuf.frame.streamGeneration;
  }

  if (selectedWidth > 0 && selectedHeight > 0) {
    if (frameWidth > 0 && frameHeight > 0 &&
        (selectedStreamGeneration == 0 || frameStreamGeneration == selectedStreamGeneration)) {
      *outWidth = frameWidth;
      *outHeight = frameHeight;
    } else {
      *outWidth = selectedWidth;
      *outHeight = selectedHeight;
    }
    return true;
  }

  const uint32_t metricWidth = gMetrics.client.width.load(std::memory_order_relaxed);
  const uint32_t metricHeight = gMetrics.client.height.load(std::memory_order_relaxed);
  if (metricWidth > 0 && metricHeight > 0) {
    *outWidth = metricWidth;
    *outHeight = metricHeight;
    return true;
  }

  if (frameWidth > 0 && frameHeight > 0) {
    *outWidth = frameWidth;
    *outHeight = frameHeight;
    return true;
  }
  return false;
}

RECT resolve_video_content_rect(HWND hwnd, const RECT& containerRect) {
  (void)hwnd;
  uint32_t contentWidth = 0;
  uint32_t contentHeight = 0;
  if (!resolve_active_video_content_size(&contentWidth, &contentHeight)) {
    return containerRect;
  }
  return aspect_fit_rect(containerRect, contentWidth, contentHeight);
}

ClientLayout compute_client_layout(HWND hwnd) {
  RECT clientRect{};
  if (hwnd && IsWindow(hwnd)) {
    GetClientRect(hwnd, &clientRect);
  } else {
    clientRect = make_rect(0, 0, static_cast<int>(gSession.windowW), static_cast<int>(gSession.windowH));
  }
  return compute_client_layout_at(clientRect, gPicker.visible.load(std::memory_order_relaxed), gUi.dpi);
}

bool point_in_toggle_button(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.toggleButtonRect, x, y);
}

bool point_in_macro_button(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.macroButtonRect, x, y);
}

bool point_in_panel_ui(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.panelRect, x, y);
}

bool point_in_video_rect(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.videoRect, x, y);
}

bool map_client_point_to_video_coords(HWND hwnd, int x, int y, int32_t* outVideoX, int32_t* outVideoY) {
  if (!outVideoX || !outVideoY) return false;
  const ClientLayout layout = compute_client_layout(hwnd);
  const RECT contentRect = resolve_video_content_rect(hwnd, layout.videoRect);
  if (!point_in_rect(contentRect, x, y)) return false;
  uint32_t frameW = 0;
  uint32_t frameH = 0;
  if (!resolve_active_video_content_size(&frameW, &frameH)) return false;
  map_point_to_video(contentRect, frameW, frameH, x, y, outVideoX, outVideoY);
  gInput.lastVideoX.store(*outVideoX, std::memory_order_relaxed);
  gInput.lastVideoY.store(*outVideoY, std::memory_order_relaxed);
  return true;
}

}  // namespace remote60::native_poc::viewer
