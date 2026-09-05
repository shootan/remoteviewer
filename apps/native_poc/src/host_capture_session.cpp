// See host_capture_session.hpp for the module summary. The member bodies below are the former
// capture lambdas of native_video_host_main.cpp, moved verbatim (host split refactor Phase 2-4):
// "capture" aliases *this and the RAII objects are aliased from CaptureResources so the moved
// text reads unchanged. Cross-calls between them were rewritten to member calls.

#include <winsock2.h>
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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture_backend_dxgi.hpp"
#include "d3d_capture_readback.hpp"
#include "gdi_capture_process.hpp"
#include "host_backend_policy.hpp"
#include "host_bgra_scale.hpp"
#include "host_capture_device.hpp"
#include "host_capture_session.hpp"
#include "host_encoder_manager.hpp"
#include "host_frame_gate.hpp"
#include "host_input_inject.hpp"
#include "host_kick.hpp"
#include "host_session.hpp"
#include "host_stats.hpp"
#include "host_string_util.hpp"
#include "host_window_enum.hpp"
#include "time_utils.hpp"

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::host::DxgiDesktopCaptureConfig;
using remote60::host::DxgiDesktopCaptureSession;

namespace remote60::native_poc {

namespace {
// WGCDEV transaction (Codex review #356): rebuild the capture D3D device and its WinRT projection on
// the primary adapter atomically. Every handle is built into a local first; res.d3d / res.ctx /
// res.inspectable / res.d3dDevice are committed together only after all steps succeed. A WinRT
// wrapper failure can therefore never leave a split-brain device -- native on the new adapter, WinRT
// on the old -- which a later restart (whose predicate only looks at res.d3d) would never re-detect.
// Returns false with res untouched on any failure.
bool RecreateCaptureDeviceOnPrimary(CaptureState& capture, CaptureResources& res,
                                    EncoderState& encoder, bool useH264, const char* reason) {
  Microsoft::WRL::ComPtr<ID3D11Device> newD3d;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> newCtx;
  D3D_FEATURE_LEVEL newFl = D3D_FEATURE_LEVEL_11_0;
  const HRESULT devHr = create_d3d11_device_for_primary_monitor(&newD3d, &newCtx, &newFl);
  if (FAILED(devHr) || !newD3d || !newCtx) {
    std::cerr << "[native-video-host] device-recreate(" << reason << ") create failed hr="
              << hr_hex(devHr) << "\n";
    return false;
  }
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgiWr;
  if (FAILED(newD3d.As(&dxgiWr)) || !dxgiWr) {
    std::cerr << "[native-video-host] device-recreate(" << reason << ") IDXGIDevice query failed\n";
    return false;
  }
  winrt::com_ptr<::IInspectable> newInspectable;
  const HRESULT wrapHr = CreateDirect3D11DeviceFromDXGIDevice(dxgiWr.Get(), newInspectable.put());
  if (FAILED(wrapHr) || !newInspectable) {
    std::cerr << "[native-video-host] device-recreate(" << reason << ") winrt wrapper failed hr="
              << hr_hex(wrapHr) << "\n";
    return false;
  }
  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice newWinrt{nullptr};
  try {
    newWinrt = newInspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
  } catch (...) {
    std::cerr << "[native-video-host] device-recreate(" << reason << ") winrt projection threw\n";
    return false;
  }
  // All locals valid -> commit the quartet together.
  res.d3d = newD3d;
  res.ctx = newCtx;
  res.fl = newFl;
  res.inspectable = newInspectable;
  res.d3dDevice = newWinrt;
  // Rebind device-scoped dependents on the fresh device. NOTE (ledger #356 / HIGH 2): encoder
  // set_d3d11_device does not rebind a running MFT (only initialize() sends SET_D3D_MANAGER), so this
  // is only correct while the encoder uses the CPU readback path (default). NV12 surface mode would
  // need a full encoder reinit here -- tracked in docs/구현계획.md, not fixed in this hotfix.
  if (useH264) (void)encoder.codec.set_d3d11_device(res.d3d.Get());
  std::cout << d3d_multithread_log_line("device-recreated", res.d3d.Get(), res.ctx.Get());
  res.gpuScaler = GpuBgraScaler();
  capture.gpuScalerHealthy = false;
  if (capture.gpuScalerRequested) {
    capture.gpuScalerHealthy =
        res.gpuScaler.initialize(res.d3d.Get(), res.ctx.Get(), &res.d3dContextMu);
  }
  // Field-verification gate (Codex review #357, #359): both halves are built from newD3d so they
  // resolve to one adapter by construction, but log native adapter LUID and the LUID the WinRT wrapper
  // unwraps to so the field capture proves there is no split-brain. Track *Known flags separately:
  // if a probe fails both LUIDs stay 0:0, so a value-only compare would print a false luidMatch=1.
  // luidMatch is meaningful only when both probes succeeded. Best-effort: a probe failure degrades the
  // log, never the commit.
  LUID nativeLuid{};
  bool nativeKnown = false;
  {
    Microsoft::WRL::ComPtr<IDXGIDevice> nd;
    Microsoft::WRL::ComPtr<IDXGIAdapter> na;
    DXGI_ADAPTER_DESC nDesc{};
    if (SUCCEEDED(res.d3d.As(&nd)) && nd && SUCCEEDED(nd->GetAdapter(&na)) && na &&
        SUCCEEDED(na->GetDesc(&nDesc))) {
      nativeLuid = nDesc.AdapterLuid;
      nativeKnown = true;
    }
  }
  LUID winrtLuid{};
  bool winrtKnown = false;
  {
    // Unwrap the WinRT projection back to its native IDXGIDevice via IDirect3DDxgiInterfaceAccess
    // (the inverse of CreateDirect3D11DeviceFromDXGIDevice) off res.inspectable, the com_ptr we just
    // committed. winrt::as<>() rejects raw COM interfaces on this toolchain, so QI through WRL.
    Microsoft::WRL::ComPtr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
    Microsoft::WRL::ComPtr<IDXGIDevice> wd;
    Microsoft::WRL::ComPtr<IDXGIAdapter> wa;
    DXGI_ADAPTER_DESC wDesc{};
    if (res.inspectable &&
        SUCCEEDED(res.inspectable->QueryInterface(IID_PPV_ARGS(access.GetAddressOf()))) && access &&
        SUCCEEDED(access.Get()->GetInterface(IID_PPV_ARGS(wd.GetAddressOf()))) && wd &&
        SUCCEEDED(wd->GetAdapter(&wa)) && wa && SUCCEEDED(wa->GetDesc(&wDesc))) {
      winrtLuid = wDesc.AdapterLuid;
      winrtKnown = true;
    }
  }
  const bool luidMatch = nativeKnown && winrtKnown &&
                         nativeLuid.HighPart == winrtLuid.HighPart &&
                         nativeLuid.LowPart == winrtLuid.LowPart;
  // Confirm the freshly committed device actually owns an attached output -- rules out a headless
  // recreate loop where the fallback adapter itself has zero outputs (Codex review #359 Q3).
  const DeviceAdapterOutputState newState = device_adapter_output_state(res.d3d.Get());
  const char* newStateStr = newState == DeviceAdapterOutputState::HasAttached ? "has"
                            : newState == DeviceAdapterOutputState::None       ? "none"
                                                                              : "unknown";
  // create_d3d11_device_for_primary_monitor falls back to another adapter (or default hardware) when
  // the primary is unresolved, so the target is "primary-or-fallback", not guaranteed primary.
  std::cout << "[native-video-host] device-recreate(" << reason
            << ") committed target=primary-or-fallback nativeKnown=" << (nativeKnown ? 1 : 0)
            << " winrtKnown=" << (winrtKnown ? 1 : 0)
            << " nativeLuid=" << nativeLuid.HighPart << ":" << static_cast<long>(nativeLuid.LowPart)
            << " winrtLuid=" << winrtLuid.HighPart << ":" << static_cast<long>(winrtLuid.LowPart)
            << " luidMatch=" << (luidMatch ? 1 : 0) << " newAdapterState=" << newStateStr << "\n";
  return true;
}
}  // namespace

bool CaptureState::CreateStaging(CaptureResources& res, EncoderState& encoder, bool useH264, uint32_t srcW, uint32_t srcH) {
  CaptureState& capture = *this;
  auto& d3d = res.d3d;
  auto& ctx = res.ctx;
  auto& d3dContextMu = res.d3dContextMu;
  auto& fl = res.fl;
  auto& gpuScaler = res.gpuScaler;
  auto& captureReadback = res.captureReadback;
  auto& capturePublishFn = res.capturePublishFn;
  captureReadback.Shutdown();
  if (!captureReadback.Initialize(d3d.Get(), ctx.Get(), &d3dContextMu, srcW, srcH,
                                  capture.stagingSlotCount, capturePublishFn)) {
    std::cerr << "[native-video-host] recreating D3D device after readback init failure size="
              << srcW << "x" << srcH << "\n";
    // Codex review #357 (field blocker): recreate via the single device transaction so the WinRT
    // wrapper (res.d3dDevice) is rebuilt in lockstep with the native device. The old native-only
    // recreate here left res.d3dDevice on the previous adapter, reopening the same cross-device
    // split-brain the early transaction closes -- but on the readback-init failure path. d3d/ctx are
    // references into res, so they reflect the committed new device after the helper returns.
    if (!RecreateCaptureDeviceOnPrimary(capture, res, encoder, useH264, "readback-init-fallback")) {
      return false;
    }
    if (!captureReadback.Initialize(d3d.Get(), ctx.Get(), &d3dContextMu, srcW, srcH,
                                    capture.stagingSlotCount, capturePublishFn)) {
      std::cerr << "[native-video-host] readback init retry failed size="
                << srcW << "x" << srcH << "\n";
      return false;
    }
  }
  if (useH264) {
    captureReadback.SetOutputSize(encoder.activeEncodeW, encoder.activeEncodeH);
    // Opt-in until a healthy-driver A/B lands: the path is functionally verified (color,
    // e2e), but on the bring-up machine the driver threw internal errors mid-run and an
    // H3-triggered cause could not be ruled out. The product path stays the H1/H2 one.
    captureReadback.SetNv12Enabled(
        encoder.codec.using_hardware() && env_truthy("REMOTE60_NATIVE_NV12_SURFACE"));
  }
  return true;
}

void CaptureState::PublishCapturedTexture(CaptureResources& res, ID3D11Texture2D* src, uint64_t callbackUs, uint64_t sourceCaptureUs, uint64_t captureAgeAtCallbackUs, uint64_t captureClockSkewUs, bool hasNewContent) {
  CaptureState& capture = *this;
  auto& captureReadback = res.captureReadback;
  if (!src) return;
  // WGC/DXGI commonly callback at the monitor refresh rate even when the encoder target is
  // 30fps. Submitting all 60 copies made the staging ring and GPU fight over obsolete
  // frames; query completion then oscillated between 16 and 50ms. Limit before the copy,
  // using a phase-preserving deadline so the accepted frames stay evenly spaced.
  {
    std::lock_guard<std::mutex> lk(capture.cadenceMu);
    capture.cadenceGate.SetEnabled(capture.submitLimitEnabled);
    capture.cadenceGate.SetEarlyTolerancePercent(capture.submitEarlyTolerancePercent);
    capture.cadenceGate.SetRequestedIntervalUs(
        std::max<uint64_t>(1, capture.submitMinIntervalUs.load(std::memory_order_acquire)));
    if (!capture.cadenceGate.ShouldAccept(callbackUs, hasNewContent)) return;
  }
  uint32_t frameW = 0;
  uint32_t frameH = 0;
  {
    std::lock_guard<std::mutex> lk(capture.resourceMu);
    frameW = capture.width;
    frameH = capture.height;
  }
  if (frameW < 2 || frameH < 2) return;
  D3D11_TEXTURE2D_DESC srcDesc{};
  src->GetDesc(&srcDesc);
  if (srcDesc.Width != frameW || srcDesc.Height != frameH) {
    capture.sizeChangePending.store(1, std::memory_order_release);
    return;
  }
  remote60::native_poc::CaptureFrameMeta meta{};
  meta.width = frameW;
  meta.height = frameH;
  meta.callbackUs = callbackUs;
  meta.captureUs = sourceCaptureUs;
  meta.captureAgeAtCallbackUs = captureAgeAtCallbackUs;
  meta.captureClockSkewUs = captureClockSkewUs;
  meta.streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
  meta.attachmentCookie = capture.attachmentCookie.load(std::memory_order_acquire);
  if (capture.windowModeActive && capture.windowClientOnlyActive) {
    const HWND cropHwnd = reinterpret_cast<HWND>(
        static_cast<uintptr_t>(capture.targetHwnd.load(std::memory_order_acquire)));
    uint32_t cropX = 0;
    uint32_t cropY = 0;
    uint32_t cropW = 0;
    uint32_t cropH = 0;
    if (cropHwnd && compute_window_client_crop(cropHwnd, frameW, frameH, &cropX, &cropY, &cropW, &cropH)) {
      meta.cropActive = true;
      meta.cropX = cropX;
      meta.cropY = cropY;
      meta.cropW = cropW;
      meta.cropH = cropH;
    }
  }
  (void)captureReadback.Submit(src, meta);
}

void CaptureState::AttachFrameArrived(CaptureResources& res, SessionState& clientSession, std::atomic<bool>& stop, winrt::event_token& token) {
  CaptureState& capture = *this;
  auto& frame = res.frame;
  auto& pool = res.pool;
  token = pool.FrameArrived([&](Direct3D11CaptureFramePool const& framePool,
                                winrt::Windows::Foundation::IInspectable const&) {
    if (stop.load()) return;
    // Snapshot the capture attachment cookie on entry, before reading any capture geometry or
    // generation. If a main-thread recreate bumps it while this callback runs, the pre-publish
    // recheck below drops the frame instead of stamping it with the new target/generation.
    const uint64_t myAttachmentCookie = capture.attachmentCookie.load(std::memory_order_acquire);
    try {
      auto latest = framePool.TryGetNextFrame();
      if (!latest) return;
      // Drain queued frames and keep only the newest one to avoid stale-frame backlog.
      while (auto newer = framePool.TryGetNextFrame()) {
        latest = newer;
      }
      if (!clientSession.streamControlActive.load(std::memory_order_acquire)) return;

      // A WGC frame-pool surface is a FIXED buffer size (capture.width x capture.height, chosen when
      // the pool was created); frame.ContentSize() is the actual content region and shrinks/grows
      // with the window. Copying the whole surface would fold the stale size-delta band (undefined
      // pixels beyond ContentSize) into the encoded frame -- that reads as "an old frame mixed into
      // the current one". Microsoft's own sample gates on ContentSize and recreates the pool when
      // it changes. Here the callback NEVER recreates capture resources: it records the pending
      // content size + a flag and drops the frame, and the main thread settles then recreates.
      const auto contentSize = latest.ContentSize();
      const uint32_t contentW = contentSize.Width > 0 ? static_cast<uint32_t>(contentSize.Width) : 0;
      const uint32_t contentH = contentSize.Height > 0 ? static_cast<uint32_t>(contentSize.Height) : 0;
      uint32_t poolW = 0;
      uint32_t poolH = 0;
      {
        std::lock_guard<std::mutex> lk(capture.resourceMu);
        poolW = capture.width;
        poolH = capture.height;
      }
      if (contentW >= 2 && contentH >= 2 && (contentW != poolW || contentH != poolH)) {
        capture.wgcContentSizeMismatchDrops.fetch_add(1, std::memory_order_relaxed);
        capture.wgcPendingContentW.store(contentW, std::memory_order_release);
        capture.wgcPendingContentH.store(contentH, std::memory_order_release);
        capture.wgcContentSizeMismatchPending.store(1, std::memory_order_release);
        return;  // drop; the main thread will settle then recreate the pool at the new size
      }

      auto src = SurfaceToTexture(latest.Surface());
      if (!src) return;
      const uint64_t callbackUs = qpc_now_us();
      uint64_t sourceCaptureUs = callbackUs;
      uint64_t captureAgeAtCallbackUs = 0;
      uint64_t captureClockSkewUs = 0;
      // Align WGC frame timestamp to qpc_now_us domain using a minimum-offset estimator.
      const auto relTime = latest.SystemRelativeTime();
      const int64_t t100ns = relTime.count();
      if (t100ns > 0) {
        const int64_t wgcUs = t100ns / 10;
        if (static_cast<int64_t>(callbackUs) >= wgcUs) {
          captureAgeAtCallbackUs = static_cast<uint64_t>(static_cast<int64_t>(callbackUs) - wgcUs);
        }
        const int64_t offsetCandidate = static_cast<int64_t>(callbackUs) - wgcUs;
        if (offsetCandidate > 0) {
          int64_t cur = capture.clockOffsetUs.load(std::memory_order_acquire);
          if (cur == std::numeric_limits<int64_t>::max()) {
            capture.clockOffsetUs.store(offsetCandidate, std::memory_order_release);
            cur = offsetCandidate;
          } else {
            while (offsetCandidate < cur &&
                   !capture.clockOffsetUs.compare_exchange_weak(cur, offsetCandidate, std::memory_order_acq_rel,
                                                              std::memory_order_acquire)) {
            }
          }
          const int64_t bestOffset = capture.clockOffsetUs.load();
          if (bestOffset != std::numeric_limits<int64_t>::max()) {
            const int64_t aligned = wgcUs + bestOffset;
            const int64_t alignedSkewUs = aligned - static_cast<int64_t>(callbackUs);
            if (aligned > 0 && alignedSkewUs >= -500000 && alignedSkewUs <= 500000) {
              captureClockSkewUs = alignedSkewUs >= 0
                  ? static_cast<uint64_t>(alignedSkewUs)
                  : static_cast<uint64_t>(-alignedSkewUs);
              sourceCaptureUs = static_cast<uint64_t>(aligned);
            }
          }
        }
      }
      // A recreate may have started while this callback was running. If the attachment cookie
      // moved, this frame belongs to the previous attachment -- drop it rather than publish it
      // under the new target/generation.
      if (capture.attachmentCookie.load(std::memory_order_acquire) != myAttachmentCookie) return;
      capture.PublishCapturedTexture(res, src.Get(), callbackUs, sourceCaptureUs, captureAgeAtCallbackUs,
                               captureClockSkewUs, true);
    } catch (...) {
    }
  });
}

void CaptureState::DetachCaptureSession(CaptureResources& res, winrt::event_token& token) {
  CaptureState& capture = *this;
  auto& pool = res.pool;
  auto& session = res.session;
  auto& dxgiCaptureSession = res.dxgiCaptureSession;
  auto& gdiCaptureProcess = res.gdiCaptureProcess;
  // Invalidate any capture callback or readback completion that began under the current
  // attachment before we tear the pool down: bumping the cookie makes that in-flight work drop
  // instead of being published under the post-recreate target/geometry/generation.
  capture.attachmentCookie.fetch_add(1, std::memory_order_acq_rel);
  // Re-read the console session with the new cookie: the next attachment's frames are stamped
  // from here instead of paying a syscall per publish. (Ledger H-18.)
  capture.attachedConsoleSessionId.store(WTSGetActiveConsoleSessionId(), std::memory_order_release);
  capture.sessionReady.store(false, std::memory_order_release);
  if (capture.dxgiStarted) {
    dxgiCaptureSession.Stop();
    capture.dxgiStarted = false;
  }
  if (capture.gdiStarted) {
    gdiCaptureProcess.Stop();
    capture.gdiStarted = false;
  }
  try {
    if (pool) {
      pool.FrameArrived(token);
    }
  } catch (...) {
  }
  token = winrt::event_token{};
  try {
    if (session) session.Close();
  } catch (...) {
  }
  try {
    if (pool) pool.Close();
  } catch (...) {
  }
  const bool hadPool = static_cast<bool>(pool);
  session = nullptr;
  pool = nullptr;
  if (hadPool) std::cout << d3d_multithread_log_line("wgc-closed", res.d3d.Get(), res.ctx.Get());
}

bool CaptureState::RestartCaptureSessionImpl(CaptureResources& res, DesktopBackendState& backend, SessionState& clientSession, EncoderState& encoder, std::atomic<bool>& stop, bool useH264, winrt::Windows::Graphics::Capture::GraphicsCaptureItem& item, winrt::event_token& token) {
  CaptureState& capture = *this;
  auto& d3d = res.d3d;
  auto& frame = res.frame;
  auto& capturePublishFn = res.capturePublishFn;
  auto& d3dDevice = res.d3dDevice;
  auto& pool = res.pool;
  auto& session = res.session;
  auto& dxgiCaptureSession = res.dxgiCaptureSession;
  auto& gdiCaptureProcess = res.gdiCaptureProcess;
  capture.DetachCaptureSession(res, token);
  try {
    if (!capture.windowModeActive && backend.active == DesktopCaptureBackend::Dxgi) {
      capture.monitorInfo = primary_monitor_info();
      if (!capture.monitorInfo.has_value()) {
        std::cerr << "[native-video-host] primary monitor query failed on restart\n";
        return false;
      }
      if (capture.monitorInfo->width < capture.monitorInfo->height) {
        backend.active = DesktopCaptureBackend::Wgc;
        capture.SetDxgiFallbackReason("rotation_unsupported");
        std::cout << "[native-video-host] rotation_unsupported fallback_reason=rotation_unsupported\n";
      }
    }
    if (capture.windowModeActive) {
      const uintptr_t hwndRaw = static_cast<uintptr_t>(capture.targetHwnd.load(std::memory_order_relaxed));
      HWND targetHwnd = reinterpret_cast<HWND>(hwndRaw);
      if (targetHwnd && IsWindow(targetHwnd)) {
        auto refreshedItem = CreateItemForPrimaryMonitor(targetHwnd, "CreateForWindow(restart-refresh)");
        if (refreshedItem) {
          item = refreshedItem;
        }
      }
    } else if (backend.active == DesktopCaptureBackend::Wgc) {
      auto refreshedItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(restart-refresh)");
      if (refreshedItem) {
        item = refreshedItem;
      }
    } else {
      item = nullptr;
    }
    winrt::Windows::Graphics::SizeInt32 newSize{};
    uint32_t newW = 0;
    uint32_t newH = 0;
    if (item) {
      newSize = item.Size();
      newW = static_cast<uint32_t>(newSize.Width);
      newH = static_cast<uint32_t>(newSize.Height);
    } else if (capture.monitorInfo.has_value()) {
      newW = capture.monitorInfo->width;
      newH = capture.monitorInfo->height;
      newSize.Width = static_cast<int32_t>(newW);
      newSize.Height = static_cast<int32_t>(newH);
    }
    if (newW < 2 || newH < 2) {
      std::cerr << "[native-video-host] invalid capture size on restart\n";
      return false;
    }
    uint32_t prevW = 0;
    uint32_t prevH = 0;
    {
      std::lock_guard<std::mutex> lk(capture.resourceMu);
      prevW = capture.width;
      prevH = capture.height;
    }
    // WGCDEV (#355, Codex review #356): perform the device transaction BEFORE the readback/staging is
    // built, so staging, the encoder and (later) the WGC frame pool all share one device. If the
    // capture device's adapter has lost every attached output -- the host started over RDP and RDP
    // has since disconnected, stranding the device on the Remote Display Adapter -- rebuild it on the
    // primary now. Only a certain `None` (zero outputs) triggers a recreate; `Unknown` is left alone
    // so a transient probe error never churns a working device. Covers window mode too (window
    // capture never enters the DXGI adapter-changed branch below).
    // GDI desktop capture runs in a separate CPU process and needs no live-adapter GPU device, so a
    // recreate failure must not abort an otherwise-serviceable GDI restart (Codex review #357). Window
    // mode and WGC/DXGI do need it. If GDI later falls back to WGC, that path's CreateStaging runs the
    // full transaction on init failure, so the dead-adapter case is still covered there.
    const bool needsGpuCaptureDevice =
        capture.windowModeActive || backend.active != DesktopCaptureBackend::Gdi;
    if (needsGpuCaptureDevice &&
        device_adapter_output_state(res.d3d.Get()) == DeviceAdapterOutputState::None) {
      std::cout << "[native-video-host] capture-device-adapter-stale: no attached output, "
                   "recreating on primary before staging\n";
      if (!RecreateCaptureDeviceOnPrimary(capture, res, encoder, useH264, "no-attached-output")) {
        std::cerr << "[native-video-host] capture device recreate failed; aborting restart\n";
        return false;
      }
    }
    if (!capture.CreateStaging(res, encoder, useH264, newW, newH)) {
      std::cerr << "[native-video-host] staging texture recreate failed size="
                << newW << "x" << newH << "\n";
      return false;
    }
    {
      std::lock_guard<std::mutex> lk(capture.resourceMu);
      capture.size = newSize;
      capture.width = newW;
      capture.height = newH;
    }
    if (prevW != newW || prevH != newH) {
      std::cout << "[native-video-host] capture-size-updated old=" << prevW << "x" << prevH
                << " new=" << newW << "x" << newH << "\n";
    }
    if (!capture.windowModeActive && backend.active == DesktopCaptureBackend::Dxgi) {
      DxgiDesktopCaptureConfig config;
      config.d3dDevice = d3d.Get();
      config.monitor = capture.monitorInfo->monitor;
      config.landscapeOnly = true;
      // 0.2.98: short acquire timeout + idle sleep outside the call so the duplication's internal
      // device lock is released often on a still desktop (see capture_backend_dxgi.cpp). Both env
      // tunable for the A/B the field test needs: 100/0 = legacy, 8/0 = timeout only, 8/2000 = both.
      config.acquireTimeoutMs = env_u32_clamped("REMOTE60_NATIVE_DXGI_ACQUIRE_TIMEOUT_MS", 8, 1, 1000);
      config.acquireIdleSleepUs = env_u32_clamped("REMOTE60_NATIVE_DXGI_ACQUIRE_IDLE_SLEEP_US", 2000, 0, 100000);
      // Capture-thread side of the cursor forwarder: just stores the latest sample; the main
      // loop's pump_cursor_forward() throttles and sends. No lock, no send from this thread.
      config.onPointer = [&](int32_t px, int32_t py, bool visible) {
        capture.dxgiPointerX.store(px, std::memory_order_relaxed);
        capture.dxgiPointerY.store(py, std::memory_order_relaxed);
        capture.dxgiPointerVisible.store(visible, std::memory_order_relaxed);
        capture.dxgiPointerGeneration.store(
            capture.streamGenerationState.load(std::memory_order_acquire),
            std::memory_order_relaxed);
        capture.dxgiPointerUpdateUs.store(qpc_now_us(), std::memory_order_release);
      };
      std::string dxgiDetail;
      auto startDxgi = [&](std::string* detailOut) {
        return dxgiCaptureSession.Start(
            config,
            [&](ID3D11Texture2D* texture, uint32_t width, uint32_t height,
                uint32_t accumulatedFrames) {
              if (stop.load()) return;
              if (!clientSession.streamControlActive.load(std::memory_order_acquire)) return;
              const uint64_t callbackUs = qpc_now_us();
              capture.PublishCapturedTexture(res, texture, callbackUs, callbackUs, 0, 0,
                                       accumulatedFrames > 0);
            },
            [&](const std::string&, const std::string& message) {
              std::cout << "[native-video-host] " << message << "\n";
            },
            [&](const std::string& reason) {
              capture.SetDxgiFallbackReason(reason);
              capture.dxgiFallbackRequested.store(true, std::memory_order_release);
            },
            detailOut);
      };
      bool started = startDxgi(&dxgiDetail);
      // G1 / R1 missing half: the desktop moved to another adapter -- typically an RDP disconnect,
      // where the device was created on the Remote Display Adapter (which then loses its output)
      // while the desktop returns to the physical GPU. DXGI can only duplicate outputs on its own
      // device's adapter, so resolve_output reports dxgi_adapter_changed and the code used to concede
      // straight to WGC: a black lock screen, then a soft keyframe-thrashing desktop. R1 detects the
      // move but deliberately left the device recreation to the caller, which never did it. This is
      // that half. monitorInfo / newW / newH were already refreshed to the current primary at the top
      // of this function, so recreate the D3D device on that adapter, rebuild the readback on it, and
      // retry DXGI once before falling back to WGC. (history #339)
      if (!started && dxgiDetail == "dxgi_adapter_changed") {
        // The device is alive but its adapter no longer owns the desktop's output (the desktop moved
        // to another adapter with outputs, so the "no attached output" check at the top did not
        // fire). Rebuild on the primary via the same transaction and retry DXGI once. (history #339)
        std::cout << "[native-video-host] dxgi_adapter_changed: recreating D3D device on the current desktop adapter\n";
        if (RecreateCaptureDeviceOnPrimary(capture, res, encoder, useH264, "dxgi_adapter_changed")) {
          if (capture.CreateStaging(res, encoder, useH264, newW, newH)) {
            config.d3dDevice = res.d3d.Get();
            started = startDxgi(&dxgiDetail);
            if (started) {
              std::cout << "[native-video-host] desktop-backend-restored reason=adapter_recreated backend=dxgi\n";
            } else {
              std::cout << "[native-video-host] adapter recreate: DXGI still failed detail=" << dxgiDetail << "\n";
            }
          } else {
            std::cerr << "[native-video-host] adapter recreate: staging rebuild failed\n";
          }
        }
      }
      if (!started) {
        std::cout << "[native-video-host] fallback_reason=" << dxgiDetail << "\n";
        backend.active = DesktopCaptureBackend::Wgc;
        auto refreshedItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(dxgi-fallback)");
        if (!refreshedItem) return false;
        item = refreshedItem;
        newSize = item.Size();
        newW = static_cast<uint32_t>(newSize.Width);
        newH = static_cast<uint32_t>(newSize.Height);
        if (newW < 2 || newH < 2) return false;
        if (!capture.CreateStaging(res, encoder, useH264, newW, newH)) return false;
        {
          std::lock_guard<std::mutex> lk(capture.resourceMu);
          capture.size = newSize;
          capture.width = newW;
          capture.height = newH;
        }
      } else {
        capture.dxgiStarted = true;
        capture.sessionStartedUs = qpc_now_us();
        capture.sessionReady.store(true, std::memory_order_release);
        capture.sizeChangePending.store(0, std::memory_order_release);
        std::cout << "[native-video-host] desktop_backend=dxgi capture-started=1 acquireTimeoutMs="
                  << config.acquireTimeoutMs << " acquireIdleSleepUs=" << config.acquireIdleSleepUs
                  << " d3dMt=" << d3d_multithread_state_name(d3d_multithread_state(res.ctx.Get())) << "\n";
        return true;
      }
    }
    if (!capture.windowModeActive && backend.active == DesktopCaptureBackend::Gdi) {
      GdiCaptureProcessConfig config;
      config.width = newW;
      config.height = newH;
      const uint32_t gdiDefaultFps =
          encoder.activeFps >= 50 ? std::min<uint32_t>(120u, encoder.activeFps + 4u) : encoder.activeFps;
      config.fps = env_u32_clamped("REMOTE60_GDI_CAPTURE_FPS",
                                   gdiDefaultFps, 1, 120);
      config.captureLayeredWindows = env_truthy("REMOTE60_GDI_CAPTURE_LAYERED");
      std::string gdiDetail;
      const bool started = gdiCaptureProcess.Start(
          config,
          [&](std::shared_ptr<std::vector<uint8_t>> pixels, uint32_t width,
              uint32_t height, uint32_t stride, uint64_t captureQpcUs,
              uint64_t captureCopyUs, uint64_t parentCopyUs) {
            if (stop.load() || !clientSession.streamControlActive.load(std::memory_order_acquire)) return;
            const uint64_t callbackUs = qpc_now_us();
            remote60::native_poc::CaptureFrameMeta meta{};
            meta.width = width;
            meta.height = height;
            meta.callbackUs = callbackUs;
            meta.captureUs = captureQpcUs;
            meta.captureAgeAtCallbackUs =
                callbackUs >= captureQpcUs ? callbackUs - captureQpcUs : 0;
            meta.submitCopyUs = captureCopyUs;
            meta.streamGeneration =
                capture.streamGenerationState.load(std::memory_order_acquire);
            capturePublishFn(std::move(pixels), width, height, stride, meta,
                             0, 0, parentCopyUs);
          },
          [&](const std::string&, const std::string& message) {
            std::cout << "[native-video-host] " << message << "\n";
          },
          [&](const std::string& reason) {
            capture.SetGdiFallbackReason(reason);
            capture.gdiFallbackRequested.store(true, std::memory_order_release);
          },
          &gdiDetail);
      if (!started) {
        std::cout << "[native-video-host] fallback_reason=" << gdiDetail << "\n";
        backend.active = DesktopCaptureBackend::Wgc;
        // Codex review #359: GDI skipped the early transaction (it needs no GPU device), so on the
        // GDI->WGC fallback the device may still be stranded on a dead adapter. WGC needs a live one,
        // and CreateStaging can succeed on a stale-but-valid device without triggering its own
        // recreate, so run the transaction here -- before this path's CreateStaging -- to keep staging
        // and the WGC pool on one live device.
        if (device_adapter_output_state(res.d3d.Get()) == DeviceAdapterOutputState::None &&
            !RecreateCaptureDeviceOnPrimary(capture, res, encoder, useH264, "gdi-wgc-fallback")) {
          return false;
        }
        auto refreshedItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(gdi-fallback)");
        if (!refreshedItem) return false;
        item = refreshedItem;
        newSize = item.Size();
        newW = static_cast<uint32_t>(newSize.Width);
        newH = static_cast<uint32_t>(newSize.Height);
        if (newW < 2 || newH < 2) return false;
        if (!capture.CreateStaging(res, encoder, useH264, newW, newH)) return false;
        {
          std::lock_guard<std::mutex> lk(capture.resourceMu);
          capture.size = newSize;
          capture.width = newW;
          capture.height = newH;
        }
      } else {
        capture.gdiStarted = true;
        capture.sessionStartedUs = qpc_now_us();
        capture.sessionReady.store(true, std::memory_order_release);
        capture.sizeChangePending.store(0, std::memory_order_release);
        std::cout << "[native-video-host] desktop_backend=gdi capture-started=1 processIsolated=1\n";
        return true;
      }
    }
    // WGCDEV (#355, Codex review #356): the WGC frame pool is built from res.d3dDevice, so the pool,
    // the readback staging and the encoder must all share one device. That device transaction now
    // runs once at the top of this function (before CreateStaging) via the "no attached output"
    // check, which covers window mode too -- window capture never enters the DXGI adapter-changed
    // branch. Nothing device-related is left to do here.
    std::cout << d3d_multithread_log_line("wgc-pool-before", res.d3d.Get(), res.ctx.Get());
    pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        d3dDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        capture.framePoolBuffers, capture.size);
    session = pool.CreateCaptureSession(item);
    // Windows draws a yellow "being captured" border on the session by default; it lands
    // inside the encoded frame and reads as a rendering artifact on the viewer.
    try {
      session.IsBorderRequired(false);
    } catch (...) {
      std::cout << "[native-video-host] wgc_border_hide=unsupported\n";
    }
    // A remote-control viewer needs to see the pointer to aim clicks, so keep the cursor
    // composited unless it is explicitly turned off.
    try {
      session.IsCursorCaptureEnabled(!env_truthy("REMOTE60_NATIVE_HIDE_CURSOR"));
    } catch (...) {
      std::cout << "[native-video-host] wgc_cursor_toggle=unsupported\n";
    }
    capture.AttachFrameArrived(res, clientSession, stop, token);
    session.StartCapture();
    std::cout << d3d_multithread_log_line("wgc-started", res.d3d.Get(), res.ctx.Get());
    capture.sessionStartedUs = qpc_now_us();
    capture.sessionReady.store(true, std::memory_order_release);
    capture.sizeChangePending.store(0, std::memory_order_release);
    std::cout << "[native-video-host] desktop_backend="
              << (capture.windowModeActive ? "wgc_window" : desktop_capture_backend_name(backend.active))
              << " capture-started=1\n";
    return true;
  } catch (...) {
    capture.DetachCaptureSession(res, token);
    return false;
  }
}

void CaptureState::FlushCapturePipelineState(CaptureResources& res, FrameGatingState& frameGating, HostStats& stats, const char* reason) {
  CaptureState& capture = *this;
  auto& frame = res.frame;
  frameGating.refPayload.reset();
  frameGating.refW = 0;
  frameGating.refH = 0;
  frameGating.refStride = 0;
  frameGating.staticStreak = 0;
  frameGating.motionStreak = 0;
  frameGating.staticMode = false;
  frameGating.lastSentUs = 0;
  stats.firstSentLoggedGeneration = 0;
  capture.firstCallbackLoggedGeneration.store(0, std::memory_order_release);
  capture.nextSubmitUs.store(0, std::memory_order_release);
  {
    // The measured offer rate describes the old target and the old content; carrying it
    // into a restart would pace the first second against something no longer true.
    std::lock_guard<std::mutex> lk(capture.cadenceMu);
    capture.cadenceGate.Reset();
  }

  uint64_t flushedVersion = 0;
  {
    std::lock_guard<std::mutex> lk(frame.mu);
    frame.payload.reset();
    frame.width = 0;
    frame.height = 0;
    frame.stride = 0;
    frame.streamGeneration = capture.streamGenerationState.load(std::memory_order_acquire);
    frame.captureUs = 0;
    frame.callbackUs = 0;
    frame.callbackIntervalUs = 0;
    frame.captureAgeAtCallbackUs = 0;
    frame.captureClockSkewUs = 0;
    frame.queuePushUs = 0;
    frame.captureIntervalUs = 0;
    frame.captureD3DWaitUs = 0;
    frame.captureCopyMapUs = 0;
    frame.captureMemcpyUs = 0;
    frame.captureUnmapWaitUs = 0;
    frame.captureUnmapUs = 0;
    frame.seq += 1;
    frame.version += 1;
    flushedVersion = frame.version;
    stats.lastVersionSent = flushedVersion;
  }
  capture.lastPopFrameVersion.store(flushedVersion, std::memory_order_release);
  frame.cv.notify_all();
  std::cout << "[native-video-host] capture-pipeline-flushed reason="
            << (reason ? reason : "unknown")
            << capture.DescribeActiveTarget()
            << " version=" << flushedVersion
            << "\n";
}

void CaptureState::LogFirstSentGeneration(CaptureResources& res, HostStats& stats, const char* path, uint64_t streamGeneration, uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height) {
  CaptureState& capture = *this;
  auto& frame = res.frame;
  if (streamGeneration == 0 || stats.firstSentLoggedGeneration == streamGeneration) return;
  stats.firstSentLoggedGeneration = streamGeneration;
  std::cout << "[native-video-host] capture-switch first-frame"
            << " path=" << (path ? path : "unknown")
            << capture.DescribeActiveTarget()
            << " sendQpcUs=" << sendStartUs
            << " captureQpcUs=" << captureStampUs
            << " size=" << width << "x" << height
            << "\n";
}

bool CaptureState::KickTryFill(SessionState& clientSession, KickState& kick, std::shared_ptr<std::vector<uint8_t>>& outPayload, uint32_t& outW, uint32_t& outH, uint32_t& outStride, uint64_t nowUs) {
  CaptureState& capture = *this;
  BootstrapFrameCache snap;
  {
    std::lock_guard<std::mutex> lk(capture.bootstrapCacheMu);
    snap = capture.bootstrapCache;  // copies the shared_ptr (keeps pixels alive) + identity fields
  }
  if (!snap.payload || snap.payload->empty() || snap.width < 2 || snap.height < 2) return false;
  // Identity: the cached pixels must belong to the target the session is watching now.
  if (snap.windowMode != capture.windowModeActive.load(std::memory_order_acquire)) return false;
  if (snap.selectedWindowId != capture.selectedWindowId.load(std::memory_order_acquire)) return false;
  if (snap.targetHwnd != capture.targetHwnd.load(std::memory_order_acquire)) return false;
  if (snap.targetPid != capture.targetPid.load(std::memory_order_acquire)) return false;
  if (snap.streamGeneration != capture.streamGenerationState.load(std::memory_order_acquire))
    return false;
  if (snap.consoleSessionId != WTSGetActiveConsoleSessionId()) return false;
  {
    std::lock_guard<std::mutex> lk(capture.resourceMu);
    if (snap.srcCaptureWidth != capture.width || snap.srcCaptureHeight != capture.height)
      return false;
  }
  // Re-check secure/lock state live (the shared query caches ~250ms; do not trust it here).
  if (!interactive_desktop_is_default_uncached()) return false;
  outPayload = snap.payload;
  outW = snap.width;
  outH = snap.height;
  outStride = snap.stride;
  ++kick.count;
  kick.lastSourceAgeUs = (nowUs > snap.captureQpcUs) ? (nowUs - snap.captureQpcUs) : 0;
  std::cout << "[native-video-host] trailing-edge kick epoch="
            << clientSession.epoch.load(std::memory_order_acquire)
            << " ageUs=" << kick.lastSourceAgeUs << " size=" << outW << "x" << outH
            << " gen=" << snap.streamGeneration << "\n";
  return true;
}

uint64_t CaptureState::EffectiveQueueWaitTimeoutUs(EncoderState& encoder) {
  CaptureState& capture = *this;
  if (capture.queueWaitTimeoutUsOverride > 0) {
    return std::max<uint64_t>(kQueueWaitTimeoutUsMin, capture.queueWaitTimeoutUsOverride);
  }
  const uint64_t keepaliveIntervalUs =
      (capture.stallKeepaliveIntervalUsOverride > 0)
          ? std::max<uint64_t>(kQueueWaitTimeoutUsMin, capture.stallKeepaliveIntervalUsOverride)
          : std::max<uint64_t>(kQueueWaitTimeoutUsMin, encoder.activeFrameIntervalUs);
  const uint64_t dynamicTimeoutUs =
      std::max<uint64_t>(kQueueWaitTimeoutUsMin, keepaliveIntervalUs / 4ULL);
  return std::min<uint64_t>(kQueueWaitTimeoutUsDefault, dynamicTimeoutUs);
}

// Readback-worker publish callback (Phase 3.6): the former res.capturePublishFn lambda of main(),
// verbatim. Runs on the readback worker thread; hands the finished frame to FrameState, updates the
// bootstrap cache and the capture timing stats.
void CaptureState::PublishFrame(CaptureResources& res, HostStats& stats,
                                std::shared_ptr<std::vector<uint8_t>> payload, uint32_t frameW,
                                uint32_t frameH, uint32_t stride, const CaptureFrameMeta& meta,
                                uint64_t gpuPendingUs, uint64_t workerMapUs, uint64_t workerMemcpyUs) {
  CaptureState& capture = *this;
  if (!payload || payload->empty() || frameW < 2 || frameH < 2) {
    // The NV12 lease was handed to us with the frame; dropping the frame without giving it
    // back strands a ring slot for the rest of the session. The attachment-cookie guard just
    // below already does this -- only this first guard was missing it. (Ledger H-03d.)
    if (meta.nv12Slot >= 0) {
      res.captureReadback.ReleaseNv12Slot(meta.nv12Slot, meta.nv12Generation);
    }
    return;
  }
  // Drop a readback completion whose Submit happened under a previous capture attachment: a pool
  // recreate bumped the cookie in between, so these pixels belong to the old target/geometry. The
  // stream-generation check downstream does not catch a same-generation size-change recreate (the
  // WGC ContentSize path and capture.sizeChangePending keep the generation), so the cookie is what
  // makes that case safe. Release the NV12 slot first or the zero-copy ring leaks.
  if (meta.attachmentCookie != 0 &&
      meta.attachmentCookie != capture.attachmentCookie.load(std::memory_order_acquire)) {
    if (meta.nv12Slot >= 0) {
      res.captureReadback.ReleaseNv12Slot(meta.nv12Slot, meta.nv12Generation);
    }
    return;
  }
  const uint64_t queuePushUs = qpc_now_us();
  capture.lastPublishUs.store(queuePushUs, std::memory_order_release);
  const uint64_t prevCallbackUs = capture.lastCallbackUs.load(std::memory_order_acquire);
  const uint64_t prevCaptureUs = capture.lastCaptureUsForInterval.load(std::memory_order_acquire);
  uint64_t callbackIntervalUs = 0;
  uint64_t captureIntervalUs = 0;
  if (prevCallbackUs > 0 && meta.callbackUs >= prevCallbackUs) {
    callbackIntervalUs = meta.callbackUs - prevCallbackUs;
  }
  if (prevCaptureUs > 0 && meta.captureUs >= prevCaptureUs) {
    captureIntervalUs = meta.captureUs - prevCaptureUs;
  }
  capture.lastCallbackUs.store(meta.callbackUs, std::memory_order_release);
  capture.lastCaptureUsForInterval.store(meta.captureUs, std::memory_order_release);
  // Update the static-screen bootstrap cache from this real publish -- the ONLY writer. Copy the
  // payload shared_ptr (do NOT move: `frame` still takes ownership below). The buffer pool
  // recycles a payload only once its LAST holder releases, so holding this copy keeps the pixels
  // alive and immutable until the next publish replaces it. meta.width/height are the pre-crop
  // capture source dims; frameW/frameH are the post-crop payload dims we must encode.
  {
    std::lock_guard<std::mutex> lk(capture.bootstrapCacheMu);
    capture.bootstrapCache.payload = payload;
    capture.bootstrapCache.width = frameW;
    capture.bootstrapCache.height = frameH;
    capture.bootstrapCache.stride = stride;
    capture.bootstrapCache.captureQpcUs = meta.captureUs;
    capture.bootstrapCache.streamGeneration = meta.streamGeneration;
    capture.bootstrapCache.windowMode = capture.windowModeActive.load(std::memory_order_acquire);
    capture.bootstrapCache.selectedWindowId = capture.selectedWindowId.load(std::memory_order_acquire);
    capture.bootstrapCache.targetHwnd = capture.targetHwnd.load(std::memory_order_acquire);
    capture.bootstrapCache.targetPid = capture.targetPid.load(std::memory_order_acquire);
    capture.bootstrapCache.srcCaptureWidth = meta.width;
    capture.bootstrapCache.srcCaptureHeight = meta.height;
    capture.bootstrapCache.consoleSessionId =
        capture.attachedConsoleSessionId.load(std::memory_order_acquire);
  }
  uint64_t currentVersion = 0;
  {
    std::lock_guard<std::mutex> lk(res.frame.mu);
    if (res.frame.nv12Slot >= 0) {
      // The consumer never claimed the previous frame's conversion (latest-wins overwrite);
      // give the slot back or the ring drains to nothing.
      res.captureReadback.ReleaseNv12Slot(res.frame.nv12Slot, res.frame.nv12Generation);
    }
    res.frame.nv12Slot = meta.nv12Slot;
    res.frame.nv12Generation = meta.nv12Generation;
    res.frame.nv12W = meta.nv12W;
    res.frame.nv12H = meta.nv12H;
    res.frame.payload = std::move(payload);
    res.frame.width = frameW;
    res.frame.height = frameH;
    res.frame.stride = stride;
    res.frame.streamGeneration = meta.streamGeneration;
    res.frame.captureUs = meta.captureUs;
    res.frame.callbackUs = meta.callbackUs;
    res.frame.captureAgeAtCallbackUs = meta.captureAgeAtCallbackUs;
    res.frame.captureClockSkewUs = meta.captureClockSkewUs;
    res.frame.queuePushUs = queuePushUs;
    res.frame.callbackIntervalUs = callbackIntervalUs;
    res.frame.captureIntervalUs = captureIntervalUs;
    res.frame.captureD3DWaitUs = meta.d3dWaitUs;       // callback wait on res.d3dContextMu
    res.frame.captureCopyMapUs = meta.submitCopyUs;    // callback CopyResource + query End
    res.frame.captureMemcpyUs = workerMemcpyUs;        // worker memcpy incl. crop
    res.frame.captureUnmapWaitUs = gpuPendingUs;       // submit -> GPU copy finished
    res.frame.captureUnmapUs = workerMapUs;            // worker Map of the finished copy
    res.frame.captureWorkerCtxWaitUs = meta.workerCtxWaitUs;  // worker waits on res.d3dContextMu (0.2.98)
    res.frame.captureWorkerD3dCallUs = meta.workerD3dCallUs;  // worker GetData+Map+Unmap call time (0.2.98)
    res.frame.seq += 1;
    res.frame.version += 1;
    currentVersion = res.frame.version;
  }
  const uint64_t currentPopVersion = capture.lastPopFrameVersion.load(std::memory_order_acquire);
  const uint64_t depthNow = (currentVersion >= currentPopVersion) ? (currentVersion - currentPopVersion) : 0;
  update_u64_max(stats.queueDepthMax, depthNow);
  update_u64_max(stats.queueDepthWindowMax, depthNow);
  ++stats.queuePushCount;
  stats.callbackFrames += 1;
  uint64_t loggedGeneration = capture.firstCallbackLoggedGeneration.load(std::memory_order_acquire);
  if (meta.streamGeneration != 0 && loggedGeneration != meta.streamGeneration &&
      capture.firstCallbackLoggedGeneration.compare_exchange_strong(
          loggedGeneration, meta.streamGeneration,
          std::memory_order_acq_rel, std::memory_order_acquire)) {
    std::cout << "[native-video-host] capture-switch first-callback"
              << capture.DescribeActiveTarget()
              << " callbackUs=" << meta.callbackUs
              << " captureUs=" << meta.captureUs
              << "\n";
  }
  res.frame.cv.notify_one();
}

}  // namespace remote60::native_poc
