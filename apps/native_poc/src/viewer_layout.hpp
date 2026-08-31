#pragma once

// Geometry of the viewer window: the picker/stream layout, the card grid, aspect-fit letterboxing
// and the mapping from client pixels to video coordinates.
//
// Role:    ClientLayout/compute_client_layout, CardGridMetrics/compute_card_grid/card_rect_for_slot,
//          aspect_fit_rect, resolve_active_video_content_size/rect, point_in_* hit tests,
//          map_client_point_to_video_coords.
// Thread:  UI mostly; apply_window_list_snapshot (control thread) also computes the grid, and
//          resolve_active_video_content_size reads ctx.frameBuf.frame under its mutex from any thread.
// Input:   HWND client rect, picker visibility, the selected target / frame / metrics sizes.
// Output:  RECTs and video coordinates.
// Callers: WndProc, viewer_overlay_draw, viewer_picker, viewer_cursor_overlay.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-8).

#include "viewer_common.hpp"
#include "viewer_state.hpp"
#include "viewer_gdi_util.hpp"
#include "viewer_layout_math.hpp"

namespace remote60::native_poc::viewer {

// The kPanel* DPI-scaled metrics that used to sit here had no callers: Phase 2-7 moved the panel
// geometry into viewer_layout_math.hpp, which takes the DPI as an argument. (Removed under F-17.)

CardGridMetrics compute_card_grid(ViewerState& ctx, const RECT& gridRect);

bool resolve_active_video_content_size(ViewerState& ctx, uint32_t* outWidth, uint32_t* outHeight);

RECT resolve_video_content_rect(ViewerState& ctx, HWND hwnd, const RECT& containerRect);

ClientLayout compute_client_layout(ViewerState& ctx, HWND hwnd);

bool point_in_toggle_button(ViewerState& ctx, HWND hwnd, int x, int y);

bool point_in_macro_button(ViewerState& ctx, HWND hwnd, int x, int y);

bool point_in_panel_ui(ViewerState& ctx, HWND hwnd, int x, int y);

bool map_client_point_to_video_coords(ViewerState& ctx, HWND hwnd, int x, int y, int32_t* outVideoX, int32_t* outVideoY);

}  // namespace remote60::native_poc::viewer
