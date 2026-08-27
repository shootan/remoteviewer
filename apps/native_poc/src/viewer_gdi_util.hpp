#pragma once

// GDI helpers of the viewer window.
//
// Role:    DPI scaling (dpi_scale), the UI fonts (ensure_ui_font), the per-colour brush cache
//          (brush_cache / cached_brush / destroy_cached_gdi_objects), and the small drawing
//          primitives (draw_text_utf8, draw_alpha_rect, draw_panel_button).
// Thread:  UI only -- every function touches GDI objects owned by the window thread.
// Input:   HDC/RECT/colours/UTF-8 text.
// Output:  pixels on the paint DC; cached HFONT/HBRUSH objects (freed by destroy_cached_gdi_objects).
// Callers: viewer_layout (dpi_scale), viewer_overlay_draw, WndProc (WM_DPICHANGED, WM_DESTROY), create_window.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-6).

#include "viewer_common.hpp"
#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

int dpi_scale(int value);

void ensure_ui_font(HWND hwnd);

// Paint-time solid brushes, cached by color. Cards used to create and destroy several
// brushes per paint, and the picker repaints on every thumbnail arrival. UI thread only.
std::unordered_map<COLORREF, HBRUSH>& brush_cache();

HBRUSH cached_brush(COLORREF color);

void destroy_cached_gdi_objects();

void draw_text_utf8(HDC hdc, const std::string& text, RECT* rect, UINT format);

void draw_alpha_rect(HDC hdc, const RECT& rect, COLORREF color, BYTE alpha);

void draw_panel_button(HDC hdc, const RECT& rect, const char* label, bool active = false,
                       bool disabled = false);

}  // namespace remote60::native_poc::viewer
