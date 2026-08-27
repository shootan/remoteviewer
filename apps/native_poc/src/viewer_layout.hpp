#pragma once

// Geometry of the viewer window: the picker/stream layout, the card grid, aspect-fit letterboxing
// and the mapping from client pixels to video coordinates.
//
// Role:    ClientLayout/compute_client_layout, CardGridMetrics/compute_card_grid/card_rect_for_slot,
//          aspect_fit_rect, resolve_active_video_content_size/rect, point_in_* hit tests,
//          map_client_point_to_video_coords, the DPI-scaled kPanel* metrics.
// Thread:  UI mostly; apply_window_list_snapshot (control thread) also computes the grid, and
//          resolve_active_video_content_size reads gFrame under its mutex from any thread.
// Input:   HWND client rect, picker visibility, the selected target / frame / metrics sizes.
// Output:  RECTs and video coordinates.
// Callers: WndProc, viewer_overlay_draw, viewer_picker, viewer_cursor_overlay.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-8).

#include "viewer_common.hpp"
#include "viewer_globals.hpp"
#include "viewer_gdi_util.hpp"

namespace remote60::native_poc::viewer {

// Panel metrics are authored at 96 DPI and scaled per monitor; the process is
// per-monitor DPI aware, so raw pixel constants would render tiny on a scaled display.
inline int kPickerPanelPreferredWidth() { return dpi_scale(560); }

inline int kPickerPanelMinWidth() { return dpi_scale(420); }

inline int kPanelMargin() { return dpi_scale(12); }

inline int kPanelButtonHeight() { return dpi_scale(30); }

inline int kPanelButtonGap() { return dpi_scale(8); }

inline int kPanelSectionGap() { return dpi_scale(12); }

inline int kPanelInfoHeight() { return dpi_scale(64); }

inline int kPanelStatsHeight() { return dpi_scale(128); }

inline int kPanelItemHeight() { return dpi_scale(28); }

inline int kPanelItemGap() { return dpi_scale(4); }

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

RECT make_rect(int x, int y, int w, int h);

bool point_in_rect(const RECT& r, int x, int y);

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

CardGridMetrics compute_card_grid(const RECT& gridRect);

RECT card_rect_for_slot(const RECT& gridRect, const CardGridMetrics& m, int slot);

RECT aspect_fit_rect(const RECT& containerRect, uint32_t contentWidth, uint32_t contentHeight);

bool resolve_active_video_content_size(uint32_t* outWidth, uint32_t* outHeight);

RECT resolve_video_content_rect(HWND hwnd, const RECT& containerRect);

ClientLayout compute_client_layout(HWND hwnd);

bool point_in_toggle_button(HWND hwnd, int x, int y);

bool point_in_macro_button(HWND hwnd, int x, int y);

bool point_in_panel_ui(HWND hwnd, int x, int y);

bool point_in_video_rect(HWND hwnd, int x, int y);

bool map_client_point_to_video_coords(HWND hwnd, int x, int y, int32_t* outVideoX, int32_t* outVideoY);

}  // namespace remote60::native_poc::viewer
