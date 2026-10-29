// ka-pc V1 (Codex evidence candidate r2): why the managed e2e's WGC capture of the picker came back all white.
// A window of this probe -- beyond the right edge of the virtual screen, shown without activation, never on a user's
// monitor -- gets a flip-model swapchain (the viewer's own kind: FLIP_DISCARD) and presents ONE solid color, as the
// picker presents one static frame. Then:
//   A. the managed e2e's way: wait 400 ms, start a WGC capture, take its FIRST frame;
//   B. start a WGC capture, present the same color again DURING it, take the first frame that arrives after that.
// If A is white and B is the color, the white is the capture (a static window off every monitor is not composed into
// the first WGC frame), not a picker that was never drawn. No host, viewer, network or input; nothing on the screen.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>

#include "e2e_window_capture.hpp"

using Microsoft::WRL::ComPtr;
namespace e2e = remote60::native_poc::e2e;

namespace {

void pump_once() {
  MSG m{};
  while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}

struct Interior {
  size_t colors = 0;
  uint32_t first = 0;
  double share = 0;  // share of the most common color
};
// The interior only (the window's frame cut off by a margin), sampled.
Interior interior(const e2e::WindowCapture& c) {
  Interior r;
  std::vector<std::pair<uint32_t, size_t>> seen;
  size_t total = 0;
  for (int y = 60; y < c.h - 20; y += 5)
    for (int x = 20; x < c.w - 20; x += 5) {
      const uint32_t v = c.px[static_cast<size_t>(y) * c.w + x] & 0xFFFFFF;
      ++total;
      bool found = false;
      for (auto& s : seen)
        if (s.first == v) ++s.second, found = true;
      if (!found && seen.size() < 4096) seen.push_back({v, 1});
    }
  r.colors = seen.size();
  size_t best = 0;
  for (auto& s : seen)
    if (s.second > best) best = s.second, r.first = s.first;
  r.share = total ? static_cast<double>(best) / total : 0;
  return r;
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const int right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"KaPcWgcOffscreenProbe";
  wc.hbrBackground = nullptr;
  RegisterClassW(&wc);
  HWND w = CreateWindowExW(0, wc.lpszClassName, L"ka-pc wgc offscreen probe", WS_OVERLAPPEDWINDOW, right + 200,
                           GetSystemMetrics(SM_YVIRTUALSCREEN) + 40, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
  ShowWindow(w, SW_SHOWNOACTIVATE);
  RECT wr{};
  GetWindowRect(w, &wr);
  std::printf("window left=%ld virtualRight=%d (beyond every monitor: %s)\n", wr.left, right, wr.left >= right ? "yes" : "NO");
  if (wr.left < right) return 2;

  ComPtr<ID3D11Device> dev;
  ComPtr<ID3D11DeviceContext> ctx;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
    return 2;
  ComPtr<IDXGIDevice> dxgiDev;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  dev.As(&dxgiDev);
  dxgiDev->GetAdapter(&adapter);
  adapter->GetParent(IID_PPV_ARGS(&factory));
  RECT cr{};
  GetClientRect(w, &cr);
  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Width = cr.right;
  sd.Height = cr.bottom;
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 2;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ComPtr<IDXGISwapChain1> sc;
  if (FAILED(factory->CreateSwapChainForHwnd(dev.Get(), w, &sd, nullptr, nullptr, &sc))) return 2;
  auto present_color = [&] {
    ComPtr<ID3D11Texture2D> back;
    sc->GetBuffer(0, IID_PPV_ARGS(&back));
    ComPtr<ID3D11RenderTargetView> rtv;
    dev->CreateRenderTargetView(back.Get(), nullptr, &rtv);
    const float color[4] = {13 / 255.f, 15 / 255.f, 20 / 255.f, 1};  // the picker's background, RGB(13,15,20)
    ctx->ClearRenderTargetView(rtv.Get(), color);
    const HRESULT hr = sc->Present(0, 0);
    return hr;
  };
  const HRESULT first = present_color();
  std::printf("one present: hr=0x%08lx\n", static_cast<unsigned long>(first));
  for (int i = 0; i < 40; ++i) {
    pump_once();
    Sleep(10);
  }

  // A: the managed e2e's way -- the first frame of a capture started after the only present
  const auto a = e2e::e2e_capture_window(w, pump_once, 3000);
  const Interior ia = a.ok ? interior(a) : Interior{};
  std::printf("A first frame after a static present: ok=%d %dx%d interior colors=%zu dominant=%06x share=%.3f %s\n", a.ok,
              a.w, a.h, ia.colors, ia.first, ia.share, a.why.c_str());

  // B: a present DURING the capture (the capture's pump presents once more, then keeps pumping)
  int pumps = 0;
  const auto b = e2e::e2e_capture_window(w, [&] {
    if (pumps++ == 0) present_color();
    pump_once();
  }, 3000);
  const Interior ib = b.ok ? interior(b) : Interior{};
  std::printf("B a present during the capture: ok=%d %dx%d interior colors=%zu dominant=%06x share=%.3f %s\n", b.ok, b.w,
              b.h, ib.colors, ib.first, ib.share, b.why.c_str());
  const bool aWhite = a.ok && ia.colors == 1 && ia.first == 0xFFFFFF;
  const bool bColor = b.ok && ib.first == 0x0D0F14 && ib.share > 0.95;
  std::printf("RESULT: A %s, B %s\n", aWhite ? "all white" : "not all white", bColor ? "the presented color" : "not the color");
  DestroyWindow(w);

  // C: the viewer's FIRST picker of a fresh window -- no swapchain has presented yet, so the picker is painted by GDI
  // into the window's DC in WM_PAINT (viewer_present.cpp, the else branch). A GDI-only window of this probe, off
  // every monitor, paints the picker's background and one lighter "card" in WM_PAINT; then the same WGC first frame.
  WNDCLASSW gc{};
  gc.lpfnWndProc = [](HWND h, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
    if (msg == WM_ERASEBKGND) return 1;  // as the viewer: background unmanaged
    if (msg == WM_PAINT) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(h, &ps);
      RECT r{};
      GetClientRect(h, &r);
      HBRUSH bg = CreateSolidBrush(RGB(13, 15, 20));
      FillRect(dc, &r, bg);
      DeleteObject(bg);
      RECT card{24, 78, 320, 293};
      HBRUSH cb = CreateSolidBrush(RGB(52, 58, 70));
      FillRect(dc, &card, cb);
      DeleteObject(cb);
      EndPaint(h, &ps);
      return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
  };
  gc.hInstance = GetModuleHandleW(nullptr);
  gc.lpszClassName = L"KaPcWgcOffscreenProbeGdi";
  RegisterClassW(&gc);
  for (const bool onScreenSize : {false}) {
    (void)onScreenSize;
    HWND g = CreateWindowExW(0, gc.lpszClassName, L"ka-pc wgc offscreen probe (gdi)", WS_OVERLAPPEDWINDOW, right + 200,
                             GetSystemMetrics(SM_YVIRTUALSCREEN) + 40, 800, 600, nullptr, nullptr, gc.hInstance, nullptr);
    ShowWindow(g, SW_SHOWNOACTIVATE);
    UpdateWindow(g);
    for (int i = 0; i < 40; ++i) {
      pump_once();
      Sleep(10);
    }
    const auto c = e2e::e2e_capture_window(g, pump_once, 3000);
    const Interior ic = c.ok ? interior(c) : Interior{};
    std::printf("C GDI paint (WM_PAINT) of an off-screen window, first frame: ok=%d %dx%d interior colors=%zu dominant=%06x "
                "share=%.3f %s\n",
                c.ok, c.w, c.h, ic.colors, ic.first, ic.share, c.why.c_str());
    // C2: the same window asked to repaint DURING a capture
    int pumps = 0;
    const auto c2 = e2e::e2e_capture_window(g, [&] {
      if (pumps++ == 0) RedrawWindow(g, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
      pump_once();
    }, 3000);
    const Interior ic2 = c2.ok ? interior(c2) : Interior{};
    std::printf("C2 the same, repainted during the capture: ok=%d interior colors=%zu dominant=%06x share=%.3f %s\n", c2.ok,
                ic2.colors, ic2.first, ic2.share, c2.why.c_str());
    // D: PrintWindow of the same window (the client area), the full-content flag and the plain one
    for (const UINT flags : {static_cast<UINT>(PW_CLIENTONLY | PW_RENDERFULLCONTENT), static_cast<UINT>(PW_CLIENTONLY)}) {
      RECT r{};
      GetClientRect(g, &r);
      BITMAPINFO bi{};
      bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
      bi.bmiHeader.biWidth = r.right;
      bi.bmiHeader.biHeight = -r.bottom;
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      void* bits = nullptr;
      HDC screen = GetDC(nullptr);
      HDC mem = CreateCompatibleDC(screen);
      HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
      HGDIOBJ prev = SelectObject(mem, bmp);
      const BOOL printed = PrintWindow(g, mem, flags);
      GdiFlush();
      const uint32_t* px = static_cast<const uint32_t*>(bits);
      const uint32_t bgPx = px[static_cast<size_t>(400) * r.right + 600] & 0xFFFFFF;
      const uint32_t cardPx = px[static_cast<size_t>(150) * r.right + 150] & 0xFFFFFF;
      std::printf("D PrintWindow(%s): ok=%d background=%06x card=%06x -> %s\n",
                  flags & PW_RENDERFULLCONTENT ? "CLIENTONLY|RENDERFULLCONTENT" : "CLIENTONLY", printed, bgPx, cardPx,
                  bgPx == 0x0D0F14 && cardPx == 0x343A46 ? "the painted content" : "NOT the painted content");
      SelectObject(mem, prev);
      DeleteObject(bmp);
      DeleteDC(mem);
      ReleaseDC(nullptr, screen);
    }
    std::printf("RESULT: C %s, C2 %s\n",
                c.ok && ic.colors == 1 && ic.first == 0xFFFFFF ? "all white" : "not all white",
                c2.ok && ic2.colors == 1 && ic2.first == 0xFFFFFF ? "all white" : "not all white");
    DestroyWindow(g);
  }
  return 0;
}
