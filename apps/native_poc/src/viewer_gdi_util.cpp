// See viewer_gdi_util.hpp. Extracted verbatim from native_video_client_main.cpp (Phase 0-6).

#include "viewer_gdi_util.hpp"

#include "viewer_env_util.hpp"

namespace remote60::native_poc::viewer {

int dpi_scale(int value) { return MulDiv(value, gUi.dpi, 96); }

void ensure_ui_font(HWND hwnd) {
  int dpi = 96;
  if (hwnd) {
    const UINT windowDpi = GetDpiForWindow(hwnd);
    if (windowDpi > 0) dpi = static_cast<int>(windowDpi);
  }
  if (gUi.font && dpi == gUi.dpi) return;
  if (gUi.font) {
    DeleteObject(gUi.font);
    gUi.font = nullptr;
  }
  gUi.dpi = dpi;
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(9, dpi, 72);
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
  std::wcscpy(lf.lfFaceName, L"Segoe UI");
  gUi.font = CreateFontIndirectW(&lf);
  if (gUi.titleFont) {
    DeleteObject(gUi.titleFont);
    gUi.titleFont = nullptr;
  }
  lf.lfHeight = -MulDiv(15, dpi, 72);
  lf.lfWeight = FW_SEMIBOLD;
  gUi.titleFont = CreateFontIndirectW(&lf);
}

// Paint-time solid brushes, cached by color. Cards used to create and destroy several
// brushes per paint, and the picker repaints on every thumbnail arrival. UI thread only.
std::unordered_map<COLORREF, HBRUSH>& brush_cache() {
  return gUi.brushCache;
}

HBRUSH cached_brush(COLORREF color) {
  auto& cache = brush_cache();
  const auto it = cache.find(color);
  if (it != cache.end()) return it->second;
  HBRUSH brush = CreateSolidBrush(color);
  cache.emplace(color, brush);
  return brush;
}

void destroy_cached_gdi_objects() {
  for (auto& entry : brush_cache()) {
    DeleteObject(entry.second);
  }
  brush_cache().clear();
  if (gUi.titleFont) {
    DeleteObject(gUi.titleFont);
    gUi.titleFont = nullptr;
  }
}

void draw_text_utf8(HDC hdc, const std::string& text, RECT* rect, UINT format) {
  if (!rect) return;
  const std::wstring wide = utf8_to_wide(text);
  HGDIOBJ oldFont = gUi.font ? SelectObject(hdc, gUi.font) : nullptr;
  DrawTextW(hdc, wide.c_str(), static_cast<int>(wide.size()), rect, format);
  if (oldFont) SelectObject(hdc, oldFont);
}

void draw_alpha_rect(HDC hdc, const RECT& rect, COLORREF color, BYTE alpha) {
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
  FillRect(memDc, &fillRc, cached_brush(color));
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = alpha;
  blend.AlphaFormat = 0;
  AlphaBlend(hdc, rect.left, rect.top, w, h, memDc, 0, 0, w, h, blend);
  SelectObject(memDc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(memDc);
}

void draw_panel_button(HDC hdc, const RECT& rect, const char* label, bool active,
                       bool disabled) {
  COLORREF fill = RGB(60, 68, 80);
  if (disabled) {
    fill = RGB(42, 46, 54);
  } else if (active) {
    fill = RGB(48, 96, 62);
  }
  FillRect(hdc, &rect, cached_brush(fill));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, disabled ? RGB(160, 165, 170) : RGB(240, 240, 240));
  RECT textRect = rect;
  draw_text_utf8(hdc, label ? std::string(label) : std::string{}, &textRect,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

}  // namespace remote60::native_poc::viewer
