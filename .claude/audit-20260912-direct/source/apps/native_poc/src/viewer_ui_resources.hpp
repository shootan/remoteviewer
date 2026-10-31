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

  // Where the picker is drawn before it is presented.
  //
  // GDI does not reach a window that a flip-model swapchain has presented on, so the picker is
  // drawn into this offscreen top-down BGRA surface by the same code as ever and then uploaded and
  // presented through that swapchain (viewer_present.cpp). Kept rather than rebuilt per paint: a
  // burst of arriving thumbnails repaints the picker several times in a row, and this is a
  // full-window bitmap.
  HDC pickerDc = nullptr;
  HBITMAP pickerBitmap = nullptr;
  HGDIOBJ pickerOldBitmap = nullptr;
  void* pickerBits = nullptr;
  int pickerW = 0;
  int pickerH = 0;
};

}  // namespace remote60::native_poc::viewer
