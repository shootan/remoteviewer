// Test-support (e2e harness only; no product binary includes this): ask Windows.Graphics.Capture
// whether it will make a GraphicsCaptureItem for a given window, and get the HRESULT it answers with.
//
// encoder-rc-budget r7. On the Home PC (Windows 10 22H2, RTX 3070 Ti) the host's CreateForWindow
// failed for the harness's invisible fixture window AND for the r6 visible-fallback popup; the host
// logs only "CreateForWindow failed" because its raw ABI call discards the HRESULT. The harness now
// asks the same question itself, on the same hwnd, before it starts a host: a refused window mode is
// skipped without a host launch, and the HRESULT is recorded so the OS's reason is on record.
#pragma once

#include <windows.h>
#include <roapi.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include <cstdint>

#pragma comment(lib, "runtimeobject.lib")

namespace remote60::native_poc::e2e {

/** HRESULT of IGraphicsCaptureItemInterop::CreateForWindow(hwnd) from THIS process. S_OK = an item
 *  was created (and released). Any failure is the OS's own answer for that window. */
inline HRESULT wgc_probe_create_for_window(HWND hwnd) {
  if (!hwnd) return E_HANDLE;
  // The harness thread may or may not have initialised WinRT; S_FALSE / RPC_E_CHANGED_MODE both mean
  // "already usable" and are not failures of the probe.
  const HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
  (void)init;
  Microsoft::WRL::ComPtr<IGraphicsCaptureItemInterop> interop;
  Microsoft::WRL::Wrappers::HStringReference cls(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem);
  HRESULT hr = RoGetActivationFactory(cls.Get(), __uuidof(IGraphicsCaptureItemInterop), &interop);
  if (FAILED(hr)) return hr;
  Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> item;
  hr = interop->CreateForWindow(hwnd, __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureItem), &item);
  if (SUCCEEDED(hr) && !item) hr = E_POINTER;
  return hr;
}

}  // namespace remote60::native_poc::e2e
