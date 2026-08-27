#pragma once

// The picker overlay paint and the overlay metric ring of the viewer.
//
// Role:    draw_overlay (the target picker: header, actions, card grid, footer), draw_target_card,
//          draw_thumbnail_into, push_overlay_metric_sample / collect_overlay_averages, apply_runtime_tune_delta.
// Thread:  UI paints; recv pushes metric samples under gOverlayMetricsMu.
// Input:   the paint DC, picker/selection/thumbnail state, client metrics.
// Output:  the picker pixels; runtime tune deltas queued for the control thread.
// Callers: WM_PAINT, recv thread (publish_metrics), WM_KEYDOWN hotkeys.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-11).

#include "viewer_common.hpp"
#include "viewer_globals.hpp"
#include "viewer_layout.hpp"

namespace remote60::native_poc::viewer {

void push_overlay_metric_sample(uint32_t recvFpsX100, uint32_t decodedFpsX100, uint32_t recvMbpsX1000,
                                uint64_t avgLatencyUs, uint64_t nowUs);

OverlayMetricAverages collect_overlay_averages(uint64_t nowUs, uint64_t windowUs);

void apply_runtime_tune_delta(int bitrateStep, int keyintStep);

void draw_thumbnail_into(HDC hdc, const RECT& dst, const WindowThumb& thumb);

void draw_target_card(HDC hdc, const RECT& card, const CardGridMetrics& grid,
                      uint64_t windowId, const std::string& title, bool active, bool disabled);

void draw_overlay(HDC hdc);

}  // namespace remote60::native_poc::viewer
