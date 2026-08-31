// See viewer_gdi_util.hpp. Extracted verbatim from native_video_client_main.cpp (Phase 0-6).

#include "viewer_gdi_util.hpp"

#include "viewer_env_util.hpp"

namespace remote60::native_poc::viewer {

int dpi_scale(ViewerState& ctx, int value) { return MulDiv(value, ctx.ui.dpi, 96); }

void ensure_ui_font(ViewerState& ctx, HWND hwnd) {
  int dpi = 96;
  if (hwnd) {
    const UINT windowDpi = GetDpiForWindow(hwnd);
    if (windowDpi > 0) dpi = static_cast<int>(windowDpi);
  }
  if (ctx.ui.font && dpi == ctx.ui.dpi) return;
  if (ctx.ui.font) {
    DeleteObject(ctx.ui.font);
    ctx.ui.font = nullptr;
  }
  ctx.ui.dpi = dpi;
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(9, dpi, 72);
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
  std::wcscpy(lf.lfFaceName, L"Segoe UI");
  ctx.ui.font = CreateFontIndirectW(&lf);
  if (ctx.ui.titleFont) {
    DeleteObject(ctx.ui.titleFont);
    ctx.ui.titleFont = nullptr;
  }
  lf.lfHeight = -MulDiv(15, dpi, 72);
  lf.lfWeight = FW_SEMIBOLD;
  ctx.ui.titleFont = CreateFontIndirectW(&lf);
}

// Paint-time solid brushes, cached by color. Cards used to create and destroy several
// brushes per paint, and the picker repaints on every thumbnail arrival. UI thread only.
HBRUSH cached_brush(ViewerState& ctx, COLORREF color) {
  auto& cache = ctx.ui.brushCache;
  const auto it = cache.find(color);
  if (it != cache.end()) return it->second;
  HBRUSH brush = CreateSolidBrush(color);
  cache.emplace(color, brush);
  return brush;
}

void destroy_cached_gdi_objects(ViewerState& ctx) {
  for (auto& entry : ctx.ui.brushCache) {
    DeleteObject(entry.second);
  }
  ctx.ui.brushCache.clear();
  if (ctx.ui.titleFont) {
    DeleteObject(ctx.ui.titleFont);
    ctx.ui.titleFont = nullptr;
  }
}

void draw_text_utf8(ViewerState& ctx, HDC hdc, const std::string& text, RECT* rect, UINT format) {
  if (!rect) return;
  const std::wstring wide = utf8_to_wide(text);
  HGDIOBJ oldFont = ctx.ui.font ? SelectObject(hdc, ctx.ui.font) : nullptr;
  DrawTextW(hdc, wide.c_str(), static_cast<int>(wide.size()), rect, format);
  if (oldFont) SelectObject(hdc, oldFont);
}

void draw_alpha_rect(ViewerState& ctx, HDC hdc, const RECT& rect, COLORREF color, BYTE alpha) {
  const int w = rect.right - rect.left;
  const int h = rect.bottom - rect.top;
  if (w <= 0 || h <= 0) return;
  HDC memDc = CreateCompatibleDC(hdc);
  if (!memDc) return;
  HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
  if (!bmp) {
    DeleteDC(memDc);
    return;
  }
  HGDIOBJ oldBmp = SelectObject(memDc, bmp);
  RECT fillRc{0, 0, w, h};
  FillRect(memDc, &fillRc, cached_brush(ctx, color));
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = alpha;
  blend.AlphaFormat = 0;
  AlphaBlend(hdc, rect.left, rect.top, w, h, memDc, 0, 0, w, h, blend);
  SelectObject(memDc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(memDc);
}

void draw_panel_button(ViewerState& ctx, HDC hdc, const RECT& rect, const char* label, bool active,
                       bool disabled) {
  COLORREF fill = RGB(60, 68, 80);
  if (disabled) {
    fill = RGB(42, 46, 54);
  } else if (active) {
    fill = RGB(48, 96, 62);
  }
  FillRect(hdc, &rect, cached_brush(ctx, fill));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, disabled ? RGB(160, 165, 170) : RGB(240, 240, 240));
  RECT textRect = rect;
  draw_text_utf8(ctx, hdc, label ? std::string(label) : std::string{}, &textRect,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

}  // namespace remote60::native_poc::viewer
