// See viewer_layout.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_layout.hpp"

#include "viewer_common.hpp"
#include "viewer_globals.hpp"
#include "viewer_gdi_util.hpp"

namespace remote60::native_poc::viewer {

RECT make_rect(int x, int y, int w, int h) {
  RECT r{};
  r.left = x;
  r.top = y;
  r.right = x + w;
  r.bottom = y + h;
  return r;
}

bool point_in_rect(const RECT& r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

CardGridMetrics compute_card_grid(const RECT& gridRect) {
  CardGridMetrics m;
  m.gap = dpi_scale(14);
  const int gridW = std::max<int>(1, gridRect.right - gridRect.left);
  const int gridH = std::max<int>(1, gridRect.bottom - gridRect.top);
  const int preferredCardW = dpi_scale(232);
  m.cols = std::max<int>(1, (gridW + m.gap) / (preferredCardW + m.gap));
  m.cardW = std::max<int>(dpi_scale(140), (gridW - (m.cols - 1) * m.gap) / m.cols);
  m.thumbH = (m.cardW * 10) / 16;
  m.cardH = m.thumbH + dpi_scale(30);
  m.visibleRows = std::max<int>(1, (gridH + m.gap) / (m.cardH + m.gap));
  m.visibleCards = m.visibleRows * m.cols;
  return m;
}

RECT card_rect_for_slot(const RECT& gridRect, const CardGridMetrics& m, int slot) {
  const int row = slot / m.cols;
  const int col = slot % m.cols;
  return make_rect(gridRect.left + col * (m.cardW + m.gap),
                   gridRect.top + row * (m.cardH + m.gap), m.cardW, m.cardH);
}

RECT aspect_fit_rect(const RECT& containerRect, uint32_t contentWidth, uint32_t contentHeight) {
  const int containerWidth =
      std::max<int>(1, static_cast<int>(containerRect.right - containerRect.left));
  const int containerHeight =
      std::max<int>(1, static_cast<int>(containerRect.bottom - containerRect.top));
  if (contentWidth == 0 || contentHeight == 0) {
    return containerRect;
  }

  const double containerAspect =
      static_cast<double>(containerWidth) / static_cast<double>(containerHeight);
  const double contentAspect =
      static_cast<double>(contentWidth) / static_cast<double>(contentHeight);
  int drawWidth = containerWidth;
  int drawHeight = containerHeight;
  if (contentAspect > containerAspect) {
    drawHeight = std::max<int>(1, static_cast<int>(std::lround(
        static_cast<double>(containerWidth) / contentAspect)));
  } else {
    drawWidth = std::max<int>(1, static_cast<int>(std::lround(
        static_cast<double>(containerHeight) * contentAspect)));
  }

  const int offsetX = (containerWidth - drawWidth) / 2;
  const int offsetY = (containerHeight - drawHeight) / 2;
  return make_rect(containerRect.left + offsetX, containerRect.top + offsetY, drawWidth, drawHeight);
}

bool resolve_active_video_content_size(uint32_t* outWidth, uint32_t* outHeight) {
  if (!outWidth || !outHeight) return false;
  *outWidth = 0;
  *outHeight = 0;

  const WindowPanelSnapshot panelSnapshot = gWindowPanelState.Snapshot();
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
  ClientLayout layout{};
  if (hwnd && IsWindow(hwnd)) {
    GetClientRect(hwnd, &layout.clientRect);
  } else {
    layout.clientRect = make_rect(0, 0, static_cast<int>(gSession.windowW), static_cast<int>(gSession.windowH));
  }
  const int clientW =
      std::max<int>(1, static_cast<int>(layout.clientRect.right - layout.clientRect.left));
  const int clientH =
      std::max<int>(1, static_cast<int>(layout.clientRect.bottom - layout.clientRect.top));
  layout.videoRect = make_rect(0, 0, clientW, clientH);

  if (!gWindowPickerVisible.load(std::memory_order_relaxed)) {
    // No legacy top-left Targets/Macro buttons in the stream view. The flip-model swapchain
    // erases anything GDI paints before the user can see it, so these existed only as INVISIBLE
    // hit zones -- sitting exactly where games put their top-left UI. In the field, clicking a
    // map's region breadcrumb silently toggled the picker ("the screen froze") and the spot next
    // to it opened the macro window. The session toolbar (its own composited window, summoned by
    // the top-center dwell) carries Targets/Macro/Monitor now; empty rects keep every handler
    // branch dead without touching the input-forwarding paths.
    layout.toggleButtonRect = make_rect(0, 0, 0, 0);
    layout.macroButtonRect = make_rect(0, 0, 0, 0);
    layout.panelRect = make_rect(0, 0, 0, 0);
    layout.refreshButtonRect = make_rect(0, 0, 0, 0);
    layout.desktopButtonRect = make_rect(0, 0, 0, 0);
    layout.selectedInfoRect = make_rect(0, 0, 0, 0);
    layout.listRect = make_rect(0, 0, 0, 0);
    layout.statsRect = make_rect(0, 0, 0, 0);
    return layout;
  }

  // The home screen owns the whole window: a header band with the actions, a card grid of
  // capture targets, and a one-line status footer.
  layout.panelRect = layout.clientRect;
  layout.toggleButtonRect = make_rect(0, 0, 0, 0);
  layout.macroButtonRect = make_rect(0, 0, 0, 0);

  const int margin = dpi_scale(24);
  const int headerH = dpi_scale(56);
  const int footerH = dpi_scale(36);
  const int buttonW = dpi_scale(130);

  layout.desktopButtonRect =
      make_rect(clientW - margin - buttonW, margin / 2 + (headerH - kPanelButtonHeight()) / 2,
                buttonW, kPanelButtonHeight());
  layout.refreshButtonRect =
      make_rect(layout.desktopButtonRect.left - kPanelButtonGap() - dpi_scale(96),
                layout.desktopButtonRect.top, dpi_scale(96), kPanelButtonHeight());
  layout.selectedInfoRect = make_rect(margin, margin / 2,
                                      std::max<int>(1, layout.refreshButtonRect.left - margin * 2),
                                      headerH);

  const int gridY = margin / 2 + headerH + dpi_scale(10);
  layout.listRect = make_rect(margin, gridY, std::max<int>(1, clientW - margin * 2),
                              std::max<int>(dpi_scale(120), clientH - gridY - footerH - dpi_scale(10)));
  layout.statsRect = make_rect(margin, layout.listRect.bottom + dpi_scale(6),
                               std::max<int>(1, clientW - margin * 2), footerH);
  return layout;
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
  const int relX =
      std::clamp<int>(x - contentRect.left, 0,
                      std::max<int>(0, static_cast<int>(contentRect.right - contentRect.left - 1)));
  const int relY =
      std::clamp<int>(y - contentRect.top, 0,
                      std::max<int>(0, static_cast<int>(contentRect.bottom - contentRect.top - 1)));
  const int videoW = std::max<int>(1, static_cast<int>(contentRect.right - contentRect.left));
  const int videoH = std::max<int>(1, static_cast<int>(contentRect.bottom - contentRect.top));
  *outVideoX = static_cast<int32_t>((static_cast<uint64_t>(relX) * static_cast<uint64_t>(frameW - 1) +
                                     static_cast<uint64_t>(videoW / 2)) /
                                    static_cast<uint64_t>(videoW));
  *outVideoY = static_cast<int32_t>((static_cast<uint64_t>(relY) * static_cast<uint64_t>(frameH - 1) +
                                     static_cast<uint64_t>(videoH / 2)) /
                                    static_cast<uint64_t>(videoH));
  gLastInputVideoX.store(*outVideoX, std::memory_order_relaxed);
  gLastInputVideoY.store(*outVideoY, std::memory_order_relaxed);
  return true;
}

}  // namespace remote60::native_poc::viewer
