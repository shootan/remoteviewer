#pragma once

// GDI/D3D resources of the viewer window (Phase 1-10 state struct).
//
// Role:    the UI fonts and the DPI they were built for, the per-colour brush cache, and the D3D11
//          NV12 presenter.
// Thread:  UI only.
// Input:   -
// Output:  -
// Callers: viewer_gdi_util, viewer_overlay_draw, viewer_window_proc, main() (decoder device sharing).
//
// Fields are the former globals gUiFont / gUiTitleFont / gUiDpi / gNv12Renderer and brush_cache()'s
// static map, initialisers unchanged (viewer split refactor Phase 1-10).

#include "viewer_common.hpp"
#include "viewer_nv12_renderer.hpp"

namespace remote60::native_poc::viewer {

struct UiResources {
  // GDI defaults to the legacy System bitmap font, which is unscalable and cannot render
  // non-Latin window titles. Everything drawn through draw_text_utf8 selects this instead.
  HFONT font = nullptr;
  HFONT titleFont = nullptr;
  int dpi = 96;
  // Paint-time solid brushes, cached by color (was brush_cache()'s function static).
  std::unordered_map<COLORREF, HBRUSH> brushCache;
  Nv12D3dRenderer nv12Renderer;
};

}  // namespace remote60::native_poc::viewer
