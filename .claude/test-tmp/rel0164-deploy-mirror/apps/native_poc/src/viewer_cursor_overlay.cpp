// See viewer_cursor_overlay.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_cursor_overlay.hpp"

#include "viewer_common.hpp"
#include "viewer_env_util.hpp"
#include "viewer_state.hpp"
#include "viewer_layout.hpp"

namespace remote60::native_poc::viewer {

// Remote-cursor overlay: a small layered, click-through popup owned by the video window. GDI
// drawn over a flip-model swapchain does not compose reliably, so the cursor lives in its own
// window that just moves. Content is a blue ring with a center dot (a deliberately distinct
// marker -- a second arrow would ghost behind the local one by an RTT), rasterized once.
void ensure_cursor_overlay(ViewerState& ctx, HWND owner) {
  if (ctx.cursor.overlayHwnd) return;
  HINSTANCE inst = GetModuleHandle(nullptr);
  static bool registered = false;
  const wchar_t* cls = L"Remote60CursorOverlay";
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = cls;
    if (!RegisterClassExW(&wc)) return;
    registered = true;
  }
  constexpr int kSize = kCursorOverlaySize;
  ctx.cursor.overlayHwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls, L"",
      WS_POPUP, 0, 0, kSize, kSize, owner, nullptr, inst, nullptr);
  if (!ctx.cursor.overlayHwnd) return;
  // Rasterize the arrow into a premultiplied 32bpp DIB: GDI writes alpha 0, so pixels that got
  // color are promoted to opaque afterwards; untouched pixels stay fully transparent.
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = kSize;
  bi.bmiHeader.biHeight = -kSize;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC screenDc = GetDC(nullptr);
  HDC memDc = CreateCompatibleDC(screenDc);
  HBITMAP dib = CreateDIBSection(memDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (dib && bits) {
    HGDIOBJ oldBmp = SelectObject(memDc, dib);
    std::memset(bits, 0, static_cast<size_t>(kSize) * kSize * 4);
    // A distinct remote marker, not a second arrow: the local cursor is already an arrow, and a
    // ghost twin trailing it by one RTT reads as a rendering bug. A colored ring with a center
    // dot is unmistakably "the remote pointer" -- and every drawn pixel is non-black, which keeps
    // the alpha promotion below honest (a pure-black outline would be indistinguishable from the
    // untouched transparent background and vanish).
    HPEN ring = CreatePen(PS_SOLID, 3, RGB(64, 160, 255));
    HGDIOBJ oldPen = SelectObject(memDc, ring);
    HGDIOBJ oldBrush = SelectObject(memDc, GetStockObject(HOLLOW_BRUSH));
    Ellipse(memDc, 3, 3, kSize - 3, kSize - 3);
    SelectObject(memDc, oldBrush);
    HBRUSH dot = CreateSolidBrush(RGB(64, 160, 255));
    HGDIOBJ oldBrush2 = SelectObject(memDc, dot);
    HGDIOBJ oldPen2 = SelectObject(memDc, GetStockObject(NULL_PEN));
    const int c = kSize / 2;
    Ellipse(memDc, c - 3, c - 3, c + 3, c + 3);
    SelectObject(memDc, oldBrush2);
    SelectObject(memDc, oldPen2);
    SelectObject(memDc, oldPen);
    DeleteObject(ring);
    DeleteObject(dot);
    auto* px = static_cast<uint32_t*>(bits);
    for (int i = 0; i < kSize * kSize; ++i) {
      if (px[i] != 0) px[i] |= 0xFF000000u;  // colored pixel -> opaque (already premultiplied)
    }
    POINT zero{0, 0};
    SIZE size{kSize, kSize};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT origin{0, 0};
    UpdateLayeredWindow(ctx.cursor.overlayHwnd, screenDc, nullptr, &size, memDc, &zero, 0, &blend,
                        ULW_ALPHA);
    SelectObject(memDc, oldBmp);
  }
  if (dib) DeleteObject(dib);
  DeleteDC(memDc);
  ReleaseDC(nullptr, screenDc);
}

// Timer body: maps the latest remote-cursor sample (capture pixels) into the letterboxed video
// rect and moves the overlay; hides it when stale (>500ms), invisible, occluded by the picker,
// or when the window is minimized.
void update_cursor_overlay(ViewerState& ctx, HWND hwnd) {
  // Field verdict: the ring reads as clutter -- disabled by default per the user, kept behind an
  // env for future reconsideration. Static refresh (the thing that keeps still screens alive) is
  // an independent host feature and is unaffected by this.
  // Same parser as the host side (1/true/on), so a future re-enable cannot end up half-on.
  static const bool remoteCursorEnabled = env_truthy("REMOTE60_NATIVE_REMOTE_CURSOR");
  if (!remoteCursorEnabled) {
    if (ctx.cursor.overlayHwnd) ShowWindow(ctx.cursor.overlayHwnd, SW_HIDE);
    return;
  }
  ensure_cursor_overlay(ctx, hwnd);
  if (!ctx.cursor.overlayHwnd) return;
  const RemoteCursorSample cur = ctx.cursor.Snapshot();  // one consistent sample (F-15)
  const uint64_t updUs = cur.updateUs;
  const uint32_t capW = cur.capW;
  const uint32_t capH = cur.capH;
  // Generation fence: a sample from the previous target must not paint over a freshly selected
  // one. activeGen==0 = legacy stream view before any PC-side selection; accept anything there.
  const uint64_t cursorGen = cur.generation;
  const uint64_t activeGen = ctx.sel.activeStreamGeneration.load(std::memory_order_acquire);
  const bool fresh = updUs != 0 && (qpc_now_us() - updUs) < kRemoteCursorStaleUs;
  const bool show = fresh && cur.visible &&
                    capW > 0 && capH > 0 &&
                    (activeGen == 0 || cursorGen == activeGen) &&
                    !ctx.picker.visible.load(std::memory_order_relaxed) && !IsIconic(hwnd);
  if (!show) {
    ShowWindow(ctx.cursor.overlayHwnd, SW_HIDE);
    return;
  }
  const ClientLayout layout = compute_client_layout(ctx, hwnd);
  const RECT content = resolve_video_content_rect(ctx, hwnd, layout.videoRect);
  const int videoW = std::max<int>(1, static_cast<int>(content.right - content.left));
  const int videoH = std::max<int>(1, static_cast<int>(content.bottom - content.top));
  const int32_t cx = cur.x;
  const int32_t cy = cur.y;
  POINT pt{};
  pt.x = content.left + static_cast<int>(static_cast<int64_t>(std::clamp<int32_t>(cx, 0, capW - 1)) *
                                         videoW / static_cast<int>(capW));
  pt.y = content.top + static_cast<int>(static_cast<int64_t>(std::clamp<int32_t>(cy, 0, capH - 1)) *
                                        videoH / static_cast<int>(capH));
  ClientToScreen(hwnd, &pt);
  // The marker is a ring; center it on the reported point rather than hanging it off a corner.
  SetWindowPos(ctx.cursor.overlayHwnd, nullptr, pt.x - kCursorOverlaySize / 2,
               pt.y - kCursorOverlaySize / 2, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

}  // namespace remote60::native_poc::viewer
