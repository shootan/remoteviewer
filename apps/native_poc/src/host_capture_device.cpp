// See host_capture_device.hpp for the module summary. Bodies below are moved verbatim from
// native_video_host_main.cpp (host split refactor Phase 0-6); no logic change.

#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "capture_backend_dxgi.hpp"
#include "host_capture_device.hpp"
#include "host_string_util.hpp"

namespace remote60::native_poc {

bool compute_window_client_crop(HWND hwnd, uint32_t frameW, uint32_t frameH, uint32_t* outX,
                                uint32_t* outY, uint32_t* outW, uint32_t* outH) {
  if (!hwnd || frameW < 2 || frameH < 2 || !outX || !outY || !outW || !outH) return false;
  RECT windowRect{};
  RECT clientRect{};
  if (!GetWindowRect(hwnd, &windowRect)) return false;
  if (!GetClientRect(hwnd, &clientRect)) return false;
  POINT tl{clientRect.left, clientRect.top};
  POINT br{clientRect.right, clientRect.bottom};
  if (!ClientToScreen(hwnd, &tl) || !ClientToScreen(hwnd, &br)) return false;
  const int windowW = windowRect.right - windowRect.left;
  const int windowH = windowRect.bottom - windowRect.top;
  const int clientW = br.x - tl.x;
  const int clientH = br.y - tl.y;
  if (windowW <= 0 || windowH <= 0 || clientW <= 1 || clientH <= 1) return false;
  const double scaleX = static_cast<double>(frameW) / static_cast<double>(windowW);
  const double scaleY = static_cast<double>(frameH) / static_cast<double>(windowH);
  int cropX = static_cast<int>((tl.x - windowRect.left) * scaleX + 0.5);
  int cropY = static_cast<int>((tl.y - windowRect.top) * scaleY + 0.5);
  int cropW = static_cast<int>(clientW * scaleX + 0.5);
  int cropH = static_cast<int>(clientH * scaleY + 0.5);
  cropX = std::clamp(cropX, 0, static_cast<int>(frameW) - 1);
  cropY = std::clamp(cropY, 0, static_cast<int>(frameH) - 1);
  cropW = std::clamp(cropW, 1, static_cast<int>(frameW) - cropX);
  cropH = std::clamp(cropH, 1, static_cast<int>(frameH) - cropY);
  // NV12 is 4:2:0, so an odd crop leaves the trailing chroma column/row unwritten and shows
  // as a coloured edge line. Round the extent down to even.
  cropW &= ~1;
  cropH &= ~1;
  *outX = static_cast<uint32_t>(cropX);
  *outY = static_cast<uint32_t>(cropY);
  *outW = static_cast<uint32_t>(cropW);
  *outH = static_cast<uint32_t>(cropH);
  return (*outW >= 2 && *outH >= 2);
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForPrimaryMonitor(
    HWND preferredWindow, const char* preferredSource,
    HMONITOR preferredMonitor) {
  auto interop = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};

  auto logHresult = [](const char* label, const winrt::hresult_error& e) {
    std::cerr << "[native-video-host] " << label << ", hr=0x" << std::hex
              << static_cast<unsigned long>(e.code()) << std::dec << "\n";
  };

  auto createForMonitor = [&](HMONITOR monitor, const char* source) {
    if (!monitor) return false;
    if (!item) {
      try {
        interop->CreateForMonitor(monitor, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                                 winrt::put_abi(item));
      } catch (const winrt::hresult_error& e) {
        logHresult("CreateForMonitor failed", e);
        item = nullptr;
      }
    }
    if (!item) {
      std::cerr << "[native-video-host] CreateForMonitor failed, source=" << source << "\n";
    } else {
      std::cout << "[native-video-host] capture item source=" << source << "\n";
    }
    return static_cast<bool>(item);
  };

  auto createForWindow = [&](HWND hwnd, const char* source) {
    if (!hwnd) return false;
    if (!item) {
      try {
        interop->CreateForWindow(hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                                winrt::put_abi(item));
      } catch (const winrt::hresult_error& e) {
        logHresult("CreateForWindow failed", e);
        item = nullptr;
      }
    }
    if (!item) {
      std::cerr << "[native-video-host] CreateForWindow failed, source=" << source << "\n";
    } else {
      std::cout << "[native-video-host] capture item source=" << source << "\n";
    }
    return static_cast<bool>(item);
  };

  if (preferredWindow) {
    createForWindow(preferredWindow, preferredSource ? preferredSource : "CreateForWindow(preferred)");
    if (item) return item;
  }

  // A specific screen when one was chosen. Everything below is the primary-monitor path, which
  // stays the default: a client that never asks for a monitor sees exactly what it always did.
  if (preferredMonitor) {
    createForMonitor(preferredMonitor, "CreateForMonitor(selected)");
    if (item) return item;
    std::cerr << "[native-video-host] selected monitor unavailable; falling back to primary\n";
  }

  HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);

  createForMonitor(monitor, "MonitorFromWindow(GetDesktopWindow())");
  if (item) return item;

  monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  createForMonitor(monitor, "MonitorFromPoint(0,0)");
  if (item) return item;

  struct EnumFirstMonitorState {
    HMONITOR monitor = nullptr;
  };
  EnumFirstMonitorState enumState{};
  auto enumCb = [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
    auto* state = reinterpret_cast<EnumFirstMonitorState*>(lParam);
    if (state && !state->monitor) {
      state->monitor = hMonitor;
    }
    return TRUE;
  };
  EnumDisplayMonitors(nullptr, nullptr, enumCb, reinterpret_cast<LPARAM>(&enumState));
  if (enumState.monitor) {
    createForMonitor(enumState.monitor, "EnumDisplayMonitors(first)");
    if (item) return item;
  }

  createForWindow(GetForegroundWindow(), "CreateForWindow(GetForegroundWindow)");
  if (item) return item;

  createForWindow(GetDesktopWindow(), "CreateForWindow(GetDesktopWindow())");
  if (item) return item;

  createForWindow(GetShellWindow(), "CreateForWindow(GetShellWindow())");
  if (item) return item;

  createForWindow(GetConsoleWindow(), "CreateForWindow(GetConsoleWindow())");
  if (item) return item;

  struct EnumCaptureWindowState {
    HWND hwnd = nullptr;
  };
  EnumCaptureWindowState enumWindowState{};
  auto enumWindowCb = [](HWND hwnd, LPARAM lParam) -> BOOL {
    if (!hwnd) return TRUE;
    auto* state = reinterpret_cast<EnumCaptureWindowState*>(lParam);
    if (!state->hwnd) {
      const LONG style = GetWindowLongPtr(hwnd, GWL_STYLE);
      if ((style & WS_VISIBLE) && (style & WS_OVERLAPPEDWINDOW)) {
        state->hwnd = hwnd;
      }
    }
    return state->hwnd ? FALSE : TRUE;
  };
  EnumWindows(enumWindowCb, reinterpret_cast<LPARAM>(&enumWindowState));
  if (enumWindowState.hwnd) {
    createForWindow(enumWindowState.hwnd, "EnumWindows(first visible overlapped)");
    if (item) return item;
  }

  HWND shellWorkerW = FindWindowW(L"Progman", nullptr);
  if (shellWorkerW) {
    createForWindow(shellWorkerW, "FindWindowW(Progman)");
    if (item) return item;
  }

  return item;
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> SurfaceToTexture(
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface) {
  winrt::com_ptr<::IInspectable> inspectable = surface.as<::IInspectable>();
  Microsoft::WRL::ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
  winrt::check_hresult(inspectable->QueryInterface(IID_PPV_ARGS(&access)));
  Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
  winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),
                                            reinterpret_cast<void**>(tex.GetAddressOf())));
  return tex;
}

DesktopCaptureBackend desktop_capture_backend_from_env() {
  const char* raw = std::getenv("REMOTE60_DESKTOP_CAPTURE_BACKEND");
  if (!raw || !*raw) return DesktopCaptureBackend::Dxgi;
  std::string s(raw);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "wgc") return DesktopCaptureBackend::Wgc;
  if (s == "gdi") return DesktopCaptureBackend::Gdi;
  return DesktopCaptureBackend::Dxgi;
}

bool desktop_capture_backend_from_code(uint16_t code, DesktopCaptureBackend* out) {
  if (!out) return false;
  switch (code) {
    case 1:
      *out = DesktopCaptureBackend::Dxgi;
      return true;
    case 2:
      *out = DesktopCaptureBackend::Wgc;
      return true;
    case 3:
      *out = DesktopCaptureBackend::Gdi;
      return true;
    default:
      return false;
  }
}

uint16_t desktop_capture_backend_code(DesktopCaptureBackend backend) {
  switch (backend) {
    case DesktopCaptureBackend::Dxgi:
      return 1;
    case DesktopCaptureBackend::Wgc:
      return 2;
    case DesktopCaptureBackend::Gdi:
      return 3;
  }
  return 1;
}

const char* desktop_capture_backend_name(DesktopCaptureBackend backend) {
  switch (backend) {
    case DesktopCaptureBackend::Dxgi: return "dxgi";
    case DesktopCaptureBackend::Wgc: return "wgc";
    case DesktopCaptureBackend::Gdi: return "gdi";
  }
  return "unknown";
}

std::optional<PrimaryMonitorInfo> primary_monitor_info() {
  const HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  if (!monitor) return std::nullopt;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoA(monitor, &info)) return std::nullopt;
  const LONG width = info.rcMonitor.right - info.rcMonitor.left;
  const LONG height = info.rcMonitor.bottom - info.rcMonitor.top;
  if (width <= 0 || height <= 0) return std::nullopt;
  PrimaryMonitorInfo out;
  out.monitor = monitor;
  out.width = static_cast<uint32_t>(width);
  out.height = static_cast<uint32_t>(height);
  out.originX = info.rcMonitor.left;
  out.originY = info.rcMonitor.top;
  return out;
}

// WGCDEV (#355): does this D3D device sit on the adapter that owns the primary monitor?
//
// WGC's frame pool must be created on the adapter the target window renders on (the primary GPU).
// If the device is on a stale adapter -- the classic case is the host starting over RDP, where the
// device lands on the Microsoft Remote Display Adapter, and RDP then disconnecting so that adapter
// loses every output -- the pool still creates but DWM never shares a surface, so no frame ever
// arrives and window capture is silently dead. G1 rebuilds res.d3d for the DXGI/desktop path on an
// adapter change but left the WGC/res.d3dDevice path stale; this predicate is how the WGC path
// notices it must rebuild. Conservative: if the answer cannot be determined, returns true so a
// healthy device is never torn down on a probe failure.
bool d3d_device_owns_primary_monitor(ID3D11Device* device) {
  if (!device) return true;
  const HMONITOR primary = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  if (!primary) return true;
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || !dxgiDevice) return true;
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  if (FAILED(dxgiDevice->GetAdapter(&adapter)) || !adapter) return true;
  for (UINT i = 0;; ++i) {
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    const HRESULT hr = adapter->EnumOutputs(i, &output);
    if (hr == DXGI_ERROR_NOT_FOUND) break;
    if (FAILED(hr) || !output) break;
    DXGI_OUTPUT_DESC desc{};
    if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == primary) return true;
  }
  return false;  // adapter enumerated but does not own the primary monitor -> stale device
}

HRESULT create_d3d11_device_for_primary_monitor(Microsoft::WRL::ComPtr<ID3D11Device>* outDevice,
                                                Microsoft::WRL::ComPtr<ID3D11DeviceContext>* outContext,
                                                D3D_FEATURE_LEVEL* outLevel) {
  if (!outDevice || !outContext) return E_INVALIDARG;
  outDevice->Reset();
  outContext->Reset();

  Microsoft::WRL::ComPtr<IDXGIAdapter1> targetAdapter;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> fallbackAdapter;
  const HMONITOR targetMonitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  if (targetMonitor) {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && factory) {
      for (UINT adapterIndex = 0; !targetAdapter; ++adapterIndex) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) continue;
        for (UINT outputIndex = 0;; ++outputIndex) {
          Microsoft::WRL::ComPtr<IDXGIOutput> output;
          if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
          if (!output) continue;
          if (!fallbackAdapter) fallbackAdapter = adapter;
          DXGI_OUTPUT_DESC desc{};
          if (FAILED(output->GetDesc(&desc))) continue;
          if (desc.Monitor == targetMonitor) {
            targetAdapter = adapter;
            break;
          }
        }
      }
    }
  }
  if (!targetAdapter && fallbackAdapter) {
    targetAdapter = fallbackAdapter;
  }

  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  const HRESULT hr = D3D11CreateDevice(targetAdapter.Get(),
                                       targetAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       nullptr,
                                       0,
                                       D3D11_SDK_VERSION,
                                       outDevice->GetAddressOf(),
                                       &level,
                                       outContext->GetAddressOf());
  if (SUCCEEDED(hr) && outLevel) *outLevel = level;
  return hr;
}

}  // namespace remote60::native_poc
