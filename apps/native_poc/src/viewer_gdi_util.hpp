#pragma once

// GDI helpers of the viewer window.
//
// Role:    DPI scaling (dpi_scale), the UI fonts (ensure_ui_font), the per-colour brush cache
//          (cached_brush / destroy_cached_gdi_objects over ctx.ui.brushCache), and the small drawing
//          primitives (draw_text_utf8, draw_alpha_rect, draw_panel_button).
// Thread:  UI only -- every function touches GDI objects owned by the window thread.
// Input:   HDC/RECT/colours/UTF-8 text.
// Output:  pixels on the paint DC; cached HFONT/HBRUSH objects (freed by destroy_cached_gdi_objects).
// Callers: viewer_layout (dpi_scale), viewer_overlay_draw, WndProc (WM_DPICHANGED, WM_DESTROY), create_window.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-6).

#include "viewer_common.hpp"
#include "viewer_state.hpp"

namespace remote60::native_poc::viewer {

int dpi_scale(ViewerState& ctx, int value);

void ensure_ui_font(ViewerState& ctx, HWND hwnd);

// Paint-time solid brushes, cached by color. Cards used to create and destroy several
// brushes per paint, and the picker repaints on every thumbnail arrival. UI thread only.
HBRUSH cached_brush(ViewerState& ctx, COLORREF color);

void destroy_cached_gdi_objects(ViewerState& ctx);

void draw_text_utf8(ViewerState& ctx, HDC hdc, const std::string& text, RECT* rect, UINT format);

void draw_alpha_rect(ViewerState& ctx, HDC hdc, const RECT& rect, COLORREF color, BYTE alpha);

void draw_panel_button(ViewerState& ctx, HDC hdc, const RECT& rect, const char* label, bool active = false,
                       bool disabled = false);

}  // namespace remote60::native_poc::viewer
