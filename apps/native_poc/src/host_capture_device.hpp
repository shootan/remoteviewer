#pragma once

// Capture-device plumbing shared by the host's WGC / DXGI / GDI desktop and window capture paths.
//
// Role:    (1) encoder backend request matching + fallback-reason classification (the strings the
//              stats line and client see); (2) DesktopCaptureBackend <-> env / wire code / name;
//          (3) window client-area crop in frame pixels; (4) WGC GraphicsCaptureItem creation with
//              the monitor/window fallback ladder; (5) IDirect3DSurface -> ID3D11Texture2D;
//          (6) primary monitor geometry and the D3D11 device created on that monitor's adapter.
// Thread:  pure functions / one-shot creators; called from main() startup and the main loop's
//          capture (re)start path. CreateItemForPrimaryMonitor logs to std::cout/std::cerr.
// Input:   backend request strings, HWND/HMONITOR, WinRT surfaces.
// Output:  reason strings, backend enum/codes, crop rects, capture items, D3D device+context.
// Callers: native_video_host_main.cpp (startup, restart_capture_session, backend policy).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-6). Definitions
// live in host_capture_device.cpp; behavior is byte-identical.

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <cstdint>
#include <optional>
#include <string>

#include "backend_request_match.hpp"
#include "capture_backend_dxgi.hpp"

namespace remote60::native_poc {

using remote60::host::DesktopCaptureBackend;

// --- desktop capture backend selection ----------------------------------------------------------
DesktopCaptureBackend desktop_capture_backend_from_env();
bool desktop_capture_backend_from_code(uint16_t code, DesktopCaptureBackend* out);
uint16_t desktop_capture_backend_code(DesktopCaptureBackend backend);
const char* desktop_capture_backend_name(DesktopCaptureBackend backend);

// --- geometry -----------------------------------------------------------------------------------
bool compute_window_client_crop(HWND hwnd, uint32_t frameW, uint32_t frameH, uint32_t* outX,
                                uint32_t* outY, uint32_t* outW, uint32_t* outH);

struct PrimaryMonitorInfo {
  HMONITOR monitor = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  // Desktop position of this monitor. Nonzero whenever it is not the top-left one, and negative
  // for a monitor placed left of or above the primary, which is why these are signed.
  int32_t originX = 0;
  int32_t originY = 0;
};

std::optional<PrimaryMonitorInfo> primary_monitor_info();

// --- WGC / D3D device creation ------------------------------------------------------------------
winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForPrimaryMonitor(
    HWND preferredWindow = nullptr, const char* preferredSource = nullptr,
    HMONITOR preferredMonitor = nullptr);

Microsoft::WRL::ComPtr<ID3D11Texture2D> SurfaceToTexture(
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface);

HRESULT create_d3d11_device_for_primary_monitor(Microsoft::WRL::ComPtr<ID3D11Device>* outDevice,
                                                Microsoft::WRL::ComPtr<ID3D11DeviceContext>* outContext,
                                                D3D_FEATURE_LEVEL* outLevel);

// WGCDEV (#355, #356): whether `device`'s adapter still has any attached desktop output.
//
// The minimal, safe signal that a capture device has gone stale is that its adapter no longer owns
// a single attached output -- the exact state the Microsoft Remote Display Adapter enters when the
// host started over RDP and RDP later disconnected. "Owns the primary monitor" was rejected as the
// trigger: a window/monitor target can legitimately live on a secondary monitor or another adapter,
// and hybrid-GPU / IddCx cross-adapter composition is normal. Recreate only on `None`; `Unknown`
// (probe could not decide) must never tear a working device down.
enum class DeviceAdapterOutputState { HasAttached, None, Unknown };
DeviceAdapterOutputState device_adapter_output_state(ID3D11Device* device);

}  // namespace remote60::native_poc
