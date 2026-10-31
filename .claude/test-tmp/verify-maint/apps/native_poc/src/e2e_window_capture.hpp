#pragma once
// rel-0163 r2 E1 (test only): what DWM composes for ONE window, read with Windows.Graphics.Capture
// (CreateForWindow) -- including a window placed off every monitor, which nothing on the user's screen shows.
// PrintWindow cannot be used for this: for a window whose content is a D3D flip-model swap chain it returns the
// window's GDI background, not what was presented (measured 10-26: uniform grey while presents succeeded).
//
// `pump` is called while waiting (the window's thread must keep dispatching its messages, e.g. WM_PAINT).

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace remote60::native_poc::e2e {

struct WindowCapture {
  bool ok = false;
  int w = 0, h = 0;
  std::vector<uint32_t> px;  // BGRA, top-down
  std::string why;
};

inline WindowCapture e2e_capture_window(HWND hwnd, const std::function<void()>& pump, int budgetMs = 2000) {
  WindowCapture out;
  try {
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    Microsoft::WRL::ComPtr<ID3D11Device> dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> dc;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev, nullptr, &dc))) {
      out.why = "no d3d11 device";
      return out;
    }
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    dev.As(&dxgi);
    winrt::com_ptr<::IInspectable> insp;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), insp.put()))) {
      out.why = "no winrt device";
      return out;
    }
    const auto device = insp.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    auto interop = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                                 IGraphicsCaptureItemInterop>();
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    if (FAILED(interop->CreateForWindow(hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                                        winrt::put_abi(item))) ||
        !item) {
      out.why = "CreateForWindow failed";
      return out;
    }
    auto pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
        device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, item.Size());
    auto session = pool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(false);
    session.StartCapture();
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame{nullptr};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline) {
      if (pump) pump();
      frame = pool.TryGetNextFrame();
      if (frame) break;
      Sleep(10);
    }
    if (!frame) {
      out.why = "no frame from the window";
      session.Close();
      pool.Close();
      return out;
    }
    struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDxgiAccess : ::IUnknown {
      virtual HRESULT __stdcall GetInterface(REFIID id, void** object) = 0;
    };
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    frame.Surface().as<IDxgiAccess>()->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(tex.GetAddressOf()));
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    dev->CreateTexture2D(&d, nullptr, &staging);
    dc->CopyResource(staging.Get(), tex.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(dc->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
      out.w = static_cast<int>(d.Width);
      out.h = static_cast<int>(d.Height);
      out.px.resize(static_cast<size_t>(out.w) * out.h);
      for (int y = 0; y < out.h; ++y) {
        std::memcpy(&out.px[static_cast<size_t>(y) * out.w], static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                    static_cast<size_t>(out.w) * 4);
      }
      dc->Unmap(staging.Get(), 0);
      out.ok = true;
    }
    frame.Close();
    session.Close();
    pool.Close();
  } catch (const winrt::hresult_error& e) {
    out.why = "winrt error 0x" + std::to_string(static_cast<unsigned long>(e.code()));
  }
  return out;
}

inline bool e2e_save_bmp(const std::wstring& path, const WindowCapture& c) {
  if (!c.ok) return false;
  BITMAPFILEHEADER fh{};
  BITMAPINFOHEADER bh{};
  bh.biSize = sizeof(bh);
  bh.biWidth = c.w;
  bh.biHeight = -c.h;
  bh.biPlanes = 1;
  bh.biBitCount = 32;
  bh.biCompression = BI_RGB;
  const DWORD bytes = static_cast<DWORD>(c.w) * c.h * 4;
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(bh);
  fh.bfSize = fh.bfOffBits + bytes;
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
  f.write(reinterpret_cast<const char*>(&bh), sizeof(bh));
  f.write(reinterpret_cast<const char*>(c.px.data()), bytes);
  return f.good();
}

}  // namespace remote60::native_poc::e2e
