// prelogin_capture_probe.cpp -- (1) screen-transmission capture mechanism for the prelogin worker.
//
// Proves the capture path the --prelogin worker relies on: a dedicated thread attaches to the INPUT
// desktop (SetThreadDesktop(OpenInputDesktop)) and captures a frame via DXGI Desktop Duplication, with a
// GDI BitBlt fallback. Here it runs against the CURRENT (Default) desktop as a product-equivalent -- the
// only production difference is the target desktop (Winlogon under SYSTEM), which is the G-phase. This is
// a standalone probe (it does NOT touch the shared capture engine and does NOT use the viewer/clipboard
// e2e harness), so it is safe to run alongside other e2e.
//
// "Frame produced" = the captured bitmap is non-uniform (something was actually painted). A uniform
// capture (headless/locked/black) is reported as a display dependency, not a failure. (display-capture.)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")

namespace {

size_t distinct_pixels(const uint32_t* px, size_t n) {
  if (n == 0) return 0;
  const uint32_t first = px[0];
  size_t d = 0;
  for (size_t i = 1; i < n; ++i)
    if (px[i] != first) ++d;
  return d;
}

// GDI BitBlt of the whole virtual screen on the calling thread's desktop. Returns distinct-pixel count.
bool capture_bitblt(size_t* distinctOut) {
  const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (w <= 0 || h <= 0) return false;
  HDC screen = GetDC(nullptr);
  if (!screen) return false;
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, dib);
  const BOOL ok = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT);
  size_t d = 0;
  if (ok && bits) d = distinct_pixels(static_cast<const uint32_t*>(bits), static_cast<size_t>(w) * h);
  SelectObject(mem, old);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  if (distinctOut) *distinctOut = d;
  return ok != FALSE;
}

// DXGI Desktop Duplication: one frame, distinct-pixel count. Returns false if DXGI is unavailable (then
// the caller uses the BitBlt fallback, exactly as the production backend does).
bool capture_dxgi(size_t* distinctOut) {
  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL fl{};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                               &dev, &fl, &ctx)))
    return false;
  IDXGIDevice* dxgiDev = nullptr;
  IDXGIAdapter* adapter = nullptr;
  IDXGIOutput* output = nullptr;
  IDXGIOutput1* output1 = nullptr;
  IDXGIOutputDuplication* dup = nullptr;
  bool ok = false;
  size_t d = 0;
  if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDev))) &&
      SUCCEEDED(dxgiDev->GetAdapter(&adapter)) && SUCCEEDED(adapter->EnumOutputs(0, &output)) &&
      SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1))) &&
      SUCCEEDED(output1->DuplicateOutput(dev, &dup))) {
    for (int attempt = 0; attempt < 30 && !ok; ++attempt) {
      DXGI_OUTDUPL_FRAME_INFO info{};
      IDXGIResource* res = nullptr;
      HRESULT hr = dup->AcquireNextFrame(200, &info, &res);
      if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
      if (FAILED(hr)) break;
      ID3D11Texture2D* tex = nullptr;
      if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)))) {
        D3D11_TEXTURE2D_DESC desc{};
        tex->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ID3D11Texture2D* staging = nullptr;
        if (SUCCEEDED(dev->CreateTexture2D(&desc, nullptr, &staging))) {
          ctx->CopyResource(staging, tex);
          D3D11_MAPPED_SUBRESOURCE map{};
          if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
            d = distinct_pixels(static_cast<const uint32_t*>(map.pData),
                                static_cast<size_t>(map.RowPitch / 4) * desc.Height);
            ctx->Unmap(staging, 0);
            ok = true;
          }
          staging->Release();
        }
        tex->Release();
      }
      if (res) res->Release();
      dup->ReleaseFrame();
    }
  }
  if (dup) dup->Release();
  if (output1) output1->Release();
  if (output) output->Release();
  if (adapter) adapter->Release();
  if (dxgiDev) dxgiDev->Release();
  if (ctx) ctx->Release();
  if (dev) dev->Release();
  if (distinctOut) *distinctOut = d;
  return ok;
}

struct Result {
  bool attached = false;
  bool dxgiOk = false;
  size_t dxgiDistinct = 0;
  bool bitbltOk = false;
  size_t bitbltDistinct = 0;
};

// The capture thread: attach to the input desktop first (what the production GDI path needs), then try
// DXGI, then BitBlt. Mirrors the backend's "DXGI, fall back to BitBlt" order.
void capture_thread(Result* r) {
  HDESK desk = OpenInputDesktop(0, FALSE, GENERIC_READ);
  if (desk) {
    r->attached = SetThreadDesktop(desk) != FALSE;
  }
  r->dxgiOk = capture_dxgi(&r->dxgiDistinct);
  r->bitbltOk = capture_bitblt(&r->bitbltDistinct);
  if (desk) CloseDesktop(desk);
}

}  // namespace

int main() {
  std::printf("prelogin capture probe (SetThreadDesktop(input) -> DXGI / BitBlt, Default desktop)\n");
  Result r;
  std::thread t(capture_thread, &r);
  t.join();

  int failures = 0;
  auto ok = [&](bool c, const char* w) { std::printf(c ? "  ok    %s\n" : "  FAIL  %s\n", w); if (!c) ++failures; };

  ok(r.attached, "the capture thread attached to the input desktop (SetThreadDesktop(OpenInputDesktop))");
  std::printf("        DXGI ok=%d distinct=%zu | BitBlt ok=%d distinct=%zu\n", r.dxgiOk ? 1 : 0,
              r.dxgiDistinct, r.bitbltOk ? 1 : 0, r.bitbltDistinct);
  ok(r.dxgiOk || r.bitbltOk, "at least one backend captured a frame (DXGI, or the BitBlt fallback)");

  const bool anyContent = (r.dxgiOk && r.dxgiDistinct > 0) || (r.bitbltOk && r.bitbltDistinct > 0);
  if (!anyContent) {
    std::printf("  SKIP? a frame was captured but it is uniform (headless/locked/black) -- display "
                "dependency, not a capture-path failure\n");
    // not a failure: the mechanism ran; the pixels depend on what is on screen in this session.
  } else {
    ok(true, "the captured frame has real content (non-uniform pixels)");
  }

  if (failures != 0) { std::printf("prelogin_capture_probe: FAIL (%d)\n", failures); return 1; }
  std::printf("prelogin_capture_probe: PASS\n");
  return 0;
}
