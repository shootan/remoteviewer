#include "capture_bootstrap.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/base.h>

namespace remote60::host {

CaptureProbeResult probe_capture_stack() {
  CaptureProbeResult r;

  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;

  const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                       D3D11_SDK_VERSION, &device, &level, &context);
  r.d3d11Ok = SUCCEEDED(hr);

  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    r.wgcSupported = winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
  } catch (...) {
    r.wgcSupported = false;
  }

  r.detail = std::string("d3d11=") + (r.d3d11Ok ? "ok" : "fail") + ", wgc=" +
             (r.wgcSupported ? "supported" : "unsupported");
  return r;
}

}  // namespace remote60::host
