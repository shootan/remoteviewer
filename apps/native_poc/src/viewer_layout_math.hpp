#pragma once

// The viewer's layout arithmetic, with no window and no shared state: every input is a parameter,
// so viewer_layout_test can drive it (viewer split refactor Phase 2-7).
//
// Role:    ClientLayout / CardGridMetrics and the pure functions behind viewer_layout.cpp --
//          DPI scaling, the picker/stream layout for a client rect, the card grid, the aspect-fit
//          letterbox, the client-point -> video-coordinate mapping and the card hit test.
// Thread:  none (pure).
// Input:   rects, sizes, the DPI, picker visibility, points.
// Output:  rects, grid metrics, video coordinates, card indices.
// Callers: viewer_layout.cpp (thin wrappers that read the window / state), viewer_overlay_draw,
//          viewer_layout_test.
//
// Bodies are those of viewer_layout.cpp (native_video_client_main.cpp originally), verbatim except
// that dpi_scale(v) / kPanelButtonHeight() / kPanelButtonGap() read the `dpi` argument.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace remote60::native_poc::viewer {

struct ClientLayout {
  RECT clientRect{};
  RECT toggleButtonRect{};
  RECT macroButtonRect{};
  RECT panelRect{};
  RECT videoRect{};
  RECT refreshButtonRect{};
  RECT desktopButtonRect{};
  RECT selectedInfoRect{};
  RECT listRect{};
  RECT statsRect{};
};

// Geometry of the card grid inside ClientLayout::listRect. Cards hold a 16:10 preview and a
// one-line caption, laid out left-to-right then top-to-bottom.
struct CardGridMetrics {
  int cols = 1;
  int cardW = 0;
  int cardH = 0;
  int thumbH = 0;
  int gap = 0;
  int visibleRows = 1;
  int visibleCards = 1;
};

// Panel metrics are authored at 96 DPI and scaled per monitor.
inline int scale_dpi(int value, int dpi) { return MulDiv(value, dpi, 96); }

inline RECT make_rect(int x, int y, int w, int h) {
  RECT r{};
  r.left = x;
  r.top = y;
  r.right = x + w;
  r.bottom = y + h;
  return r;
}

inline bool point_in_rect(const RECT& r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

inline CardGridMetrics compute_card_grid_at(const RECT& gridRect, int dpi) {
  CardGridMetrics m;
  m.gap = scale_dpi(14, dpi);
  const int gridW = std::max<int>(1, gridRect.right - gridRect.left);
  const int gridH = std::max<int>(1, gridRect.bottom - gridRect.top);
  const int preferredCardW = scale_dpi(232, dpi);
  m.cols = std::max<int>(1, (gridW + m.gap) / (preferredCardW + m.gap));
  m.cardW = std::max<int>(scale_dpi(140, dpi), (gridW - (m.cols - 1) * m.gap) / m.cols);
  m.thumbH = (m.cardW * 10) / 16;
  m.cardH = m.thumbH + scale_dpi(30, dpi);
  m.visibleRows = std::max<int>(1, (gridH + m.gap) / (m.cardH + m.gap));
  m.visibleCards = m.visibleRows * m.cols;
  return m;
}

inline RECT card_rect_for_slot(const RECT& gridRect, const CardGridMetrics& m, int slot) {
  const int row = slot / m.cols;
  const int col = slot % m.cols;
  return make_rect(gridRect.left + col * (m.cardW + m.gap),
                   gridRect.top + row * (m.cardH + m.gap), m.cardW, m.cardH);
}

inline RECT aspect_fit_rect(const RECT& containerRect, uint32_t contentWidth, uint32_t contentHeight) {
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

// The picker (pickerVisible) or stream-view layout of a client rect at a DPI.
inline ClientLayout compute_client_layout_at(const RECT& clientRect, bool pickerVisible, int dpi) {
  ClientLayout layout{};
  layout.clientRect = clientRect;
  const int clientW =
      std::max<int>(1, static_cast<int>(layout.clientRect.right - layout.clientRect.left));
  const int clientH =
      std::max<int>(1, static_cast<int>(layout.clientRect.bottom - layout.clientRect.top));
  layout.videoRect = make_rect(0, 0, clientW, clientH);

  if (!pickerVisible) {
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

  const int margin = scale_dpi(24, dpi);
  const int headerH = scale_dpi(56, dpi);
  const int footerH = scale_dpi(36, dpi);
  const int buttonW = scale_dpi(130, dpi);
  const int buttonH = scale_dpi(30, dpi);   // kPanelButtonHeight()
  const int buttonGap = scale_dpi(8, dpi);  // kPanelButtonGap()

  layout.desktopButtonRect =
      make_rect(clientW - margin - buttonW, margin / 2 + (headerH - buttonH) / 2,
                buttonW, buttonH);
  layout.refreshButtonRect =
      make_rect(layout.desktopButtonRect.left - buttonGap - scale_dpi(96, dpi),
                layout.desktopButtonRect.top, scale_dpi(96, dpi), buttonH);
  layout.selectedInfoRect = make_rect(margin, margin / 2,
                                      std::max<int>(1, layout.refreshButtonRect.left - margin * 2),
                                      headerH);

  const int gridY = margin / 2 + headerH + scale_dpi(10, dpi);
  layout.listRect = make_rect(margin, gridY, std::max<int>(1, clientW - margin * 2),
                              std::max<int>(scale_dpi(120, dpi), clientH - gridY - footerH - scale_dpi(10, dpi)));
  layout.statsRect = make_rect(margin, layout.listRect.bottom + scale_dpi(6, dpi),
                               std::max<int>(1, clientW - margin * 2), footerH);
  return layout;
}

// A client point inside the letterboxed content rect -> video pixel coordinates (rounded, clamped
// to the frame). The caller checks that the point is inside contentRect.
inline void map_point_to_video(const RECT& contentRect, uint32_t frameW, uint32_t frameH, int x, int y,
                               int32_t* outVideoX, int32_t* outVideoY) {
  const int relX =
      std::clamp<int>(x - contentRect.left, 0,
                      std::max<int>(0, static_cast<int>(contentRect.right - contentRect.left - 1)));
  const int relY =
      std::clamp<int>(y - contentRect.top, 0,
                      std::max<int>(0, static_cast<int>(contentRect.bottom - contentRect.top - 1)));
  const int videoW = std::max<int>(1, static_cast<int>(contentRect.right - contentRect.left));
  const int videoH = std::max<int>(1, static_cast<int>(contentRect.bottom - contentRect.top));
  // rel runs 0..videoW-1 and must land on 0..frameW-1, so the scale is (frameW-1)/(videoW-1):
  // both endpoints map exactly. Scaling by 1/videoW instead left the last client pixel one or
  // two video pixels short of the edge (800 -> 1600: 799 landed on 1597), which made the
  // rightmost / bottom column of the remote screen unreachable by the mouse. (F-16.)
  const auto scale = [](int rel, uint32_t frameDim, int videoDim) -> int32_t {
    if (videoDim <= 1 || frameDim == 0) return 0;
    const uint64_t num = static_cast<uint64_t>(rel) * static_cast<uint64_t>(frameDim - 1) +
                         static_cast<uint64_t>((videoDim - 1) / 2);
    return static_cast<int32_t>(num / static_cast<uint64_t>(videoDim - 1));
  };
  *outVideoX = scale(relX, frameW, videoW);
  *outVideoY = scale(relY, frameH, videoH);
}

// Hit-test the card grid: which card (index 0 = the pinned Desktop card, then the window items,
// scrollRow whole rows down) is under a client point; false in the gaps or outside the grid.
inline bool card_hit_test(const RECT& listRect, const CardGridMetrics& grid, int scrollRow, int x, int y,
                          int* outCardIndex) {
  if (!point_in_rect(listRect, x, y)) return false;
  const int relX = x - listRect.left;
  const int relY = y - listRect.top;
  const int col = relX / (grid.cardW + grid.gap);
  const int row = relY / (grid.cardH + grid.gap);
  if (col < 0 || col >= grid.cols || row < 0 || row >= grid.visibleRows) return false;
  // Reject clicks that land in the gaps between cards.
  if (relX - col * (grid.cardW + grid.gap) >= grid.cardW) return false;
  if (relY - row * (grid.cardH + grid.gap) >= grid.cardH) return false;
  *outCardIndex = scrollRow * grid.cols + row * grid.cols + col;
  return true;
}

}  // namespace remote60::native_poc::viewer
