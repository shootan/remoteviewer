#pragma once

// The picker overlay paint and the overlay metric ring of the viewer.
//
// Role:    draw_overlay (the target picker: header, actions, card grid, footer), draw_target_card,
//          draw_thumbnail_into, apply_runtime_tune_delta.
// Thread:  UI paints.
// Input:   the paint DC, picker/selection/thumbnail state, client metrics.
// Output:  the picker pixels; runtime tune deltas queued for the control thread.
// Callers: WM_PAINT, WM_KEYDOWN hotkeys.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-11).

#include "viewer_common.hpp"
#include "viewer_state.hpp"
#include "viewer_layout.hpp"

namespace remote60::native_poc::viewer {

void apply_runtime_tune_delta(ViewerState& ctx, int bitrateStep, int keyintStep);

void draw_thumbnail_into(HDC hdc, const RECT& dst, const WindowThumb& thumb);

void draw_target_card(ViewerState& ctx, HDC hdc, const RECT& card, const CardGridMetrics& grid,
                      uint64_t windowId, const std::string& title, bool active, bool disabled);

void draw_overlay(ViewerState& ctx, HDC hdc);

}  // namespace remote60::native_poc::viewer
