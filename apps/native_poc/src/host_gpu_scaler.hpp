#pragma once

// D3D11 VideoProcessor-based BGRA downscaler with CPU readback (the "GPU scaler" encode path).
//
// Role:    GpuBgraScaler -- uploads a BGRA frame, blits it to the encode size through the video
//          processor (full-range RGB, auto-processing off), and reads the result back into a
//          caller-owned vector with per-stage timings (D3DReadbackTiming).
// Thread:  main encode loop only. Every D3D call is taken under the shared *d3dMutex handed to
//          initialize(), because the capture readback worker shares the same immediate context.
// Input:   BGRA buffer + geometry, target size.
// Output:  scaled BGRA in outBgra, timing split in outTiming; false on any D3D failure OR when the
//          copy is not readable within readyBudgetUs (caller falls back to the CPU
//          resize_bgra_bilinear path on the ORIGINAL BGRA and marks the scaler unhealthy for the
//          rest of this device generation -- host_capture_session recreates it with the device).
// Callers: host_stage_encode_send_h264.cpp (encode path when gpuScalerRequested).
//
// encoder-rc-budget r9 (Codex r8 contract, item 2): the Map(READ) used to be unbounded. On the Home PC
// (Windows 10, RTX 3070 Ti, WGC window capture) every 720p cell -- the only cells that scale -- hung
// the main loop before its first frame, baseline and candidate alike; the scaler is the first
// suspect, not the proven cause (the dump decides). Map now asks with D3D11_MAP_FLAG_DO_NOT_WAIT and
// treats DXGI_ERROR_WAS_STILL_DRAWING as "pending"; pending is polled OUTSIDE the shared mutex (1 ms
// sleeps, never holding it), against ONE deadline taken at entry (lock wait included, never
// restarted). A deadline is a Timeout: false, outBgra untouched (no half-written or stale buffer is
// ever handed on). What this cannot bound: the mutex acquisition itself and the UpdateSubresource/
// Blt/CopyResource/Map driver calls -- if one of those never returns, only the watchdog can act, and
// it still does (nothing here stamps progress).

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "host_bottleneck.hpp"
#include "host_watchdog.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

// r9: how long scale() may wait for the GPU copy before giving the frame to the CPU path. Three frame
// periods, clamped to [60 ms, 200 ms]: the copy normally completes in well under a millisecond
// (gpuScaleCopyMapAvgUs in the stats), so waiting past three periods is already a dropped-frame hitch
// on screen, while the CPU bilinear 1080p->720p costs about 10-20 ms -- cheaper than waiting longer.
// The lower clamp keeps a GPU that is briefly busy with the encoder from tripping the fallback at
// 60 fps; the upper keeps a 15 fps setting from waiting a fifth of a second. This is the first value,
// chosen from those two costs, and it is measured (lastWaitUs in the disable log line), not assumed.
inline uint64_t scale_ready_budget_us(uint32_t fps) {
  const uint64_t period = fps > 0 ? 1'000'000ULL / fps : 33'333ULL;
  const uint64_t three = period * 3;
  return three < 60'000ULL ? 60'000ULL : three > 200'000ULL ? 200'000ULL : three;
}

struct GpuBgraScaler {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> srcTexture;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> dstTexture;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> dstStaging;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
  std::mutex* d3dMutex = nullptr;
  uint32_t srcW = 0;
  uint32_t srcH = 0;
  uint32_t dstW = 0;
  uint32_t dstH = 0;
  bool initialized = false;

  // r9: bounded readiness. The caller sets the budget per call from the frame period (see
  // scale_ready_budget_us); the outcome of the last call is kept for its log line.
  enum class Outcome : uint32_t { Ok = 0, Timeout, Error };
  uint64_t readyBudgetUs = 100'000;
  Outcome lastOutcome = Outcome::Ok;
  uint32_t lastMapPolls = 0;      // Map(DO_NOT_WAIT) calls that answered STILL_DRAWING
  uint64_t lastWaitUs = 0;        // entry -> ready (or -> timeout)
  std::atomic<uint32_t>* subphase = nullptr;             // the watchdog's MainLoopSubphase, if wired
  std::atomic<uint64_t>* subphaseSinceUs = nullptr;
  // Test seam: stands in for context->Map so a test can answer STILL_DRAWING n times, then pass
  // through (not-ready -> ready), never pass (deadline), or fail (error). Null in the product.
  std::function<HRESULT(ID3D11DeviceContext*, ID3D11Resource*, D3D11_MAPPED_SUBRESOURCE*)> mapForTest;

  void stamp(MainLoopSubphase s) {
    if (subphase) subphase->store(static_cast<uint32_t>(s), std::memory_order_release);
    if (subphaseSinceUs) subphaseSinceUs->store(qpc_now_us(), std::memory_order_release);
  }
  HRESULT map_do_not_wait(D3D11_MAPPED_SUBRESOURCE* out) {
    if (mapForTest) return mapForTest(context.Get(), dstStaging.Get(), out);
    return context->Map(dstStaging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, out);
  }
  static const char* outcome_name(Outcome o) {
    return o == Outcome::Ok ? "ok" : o == Outcome::Timeout ? "timeout" : "error";
  }

  bool initialize(ID3D11Device* d, ID3D11DeviceContext* c, std::mutex* mu) {
    if (!d || !c) return false;
    device = d;
    context = c;
    d3dMutex = mu;
    if (FAILED(device.As(&videoDevice)) || !videoDevice) return false;
    if (FAILED(context.As(&videoContext)) || !videoContext) return false;
    initialized = true;
    return true;
  }

  bool ensure_resources(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH) {
    if (!initialized || !videoDevice || !videoContext) return false;
    if (inW == 0 || inH == 0 || outW == 0 || outH == 0) return false;
    if (srcTexture && dstTexture && dstStaging && inputView && outputView &&
        srcW == inW && srcH == inH && dstW == outW && dstH == outH) {
      return true;
    }

    enumerator.Reset();
    processor.Reset();
    srcTexture.Reset();
    dstTexture.Reset();
    dstStaging.Reset();
    inputView.Reset();
    outputView.Reset();

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputWidth = inW;
    desc.InputHeight = inH;
    desc.OutputWidth = outW;
    desc.OutputHeight = outH;
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&desc, &enumerator)) || !enumerator) return false;

    UINT formatSupport = 0;
    if (FAILED(enumerator->CheckVideoProcessorFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, &formatSupport))) {
      return false;
    }
    const UINT requiredFormatSupport =
        D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT;
    if ((formatSupport & requiredFormatSupport) != requiredFormatSupport) return false;

    if (FAILED(videoDevice->CreateVideoProcessor(enumerator.Get(), 0, &processor)) || !processor) return false;

    D3D11_TEXTURE2D_DESC srcDesc{};
    srcDesc.Width = inW;
    srcDesc.Height = inH;
    srcDesc.MipLevels = 1;
    srcDesc.ArraySize = 1;
    srcDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srcDesc.SampleDesc.Count = 1;
    srcDesc.Usage = D3D11_USAGE_DEFAULT;
    srcDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(device->CreateTexture2D(&srcDesc, nullptr, &srcTexture)) || !srcTexture) return false;

    D3D11_TEXTURE2D_DESC dstDesc = srcDesc;
    dstDesc.Width = outW;
    dstDesc.Height = outH;
    if (FAILED(device->CreateTexture2D(&dstDesc, nullptr, &dstTexture)) || !dstTexture) return false;

    D3D11_TEXTURE2D_DESC stagingDesc = dstDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &dstStaging)) || !dstStaging) return false;

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inViewDesc{};
    inViewDesc.FourCC = 0;
    inViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inViewDesc.Texture2D.MipSlice = 0;
    inViewDesc.Texture2D.ArraySlice = 0;
    if (FAILED(videoDevice->CreateVideoProcessorInputView(
            srcTexture.Get(), enumerator.Get(), &inViewDesc, &inputView)) || !inputView) {
      return false;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outViewDesc{};
    outViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outViewDesc.Texture2D.MipSlice = 0;
    if (FAILED(videoDevice->CreateVideoProcessorOutputView(
            dstTexture.Get(), enumerator.Get(), &outViewDesc, &outputView)) || !outputView) {
      return false;
    }

    srcW = inW;
    srcH = inH;
    dstW = outW;
    dstH = outH;
    return true;
  }

  bool scale(const uint8_t* src, uint32_t inW, uint32_t inH, uint32_t srcStride,
             uint32_t outW, uint32_t outH, std::vector<uint8_t>* outBgra,
             D3DReadbackTiming* outTiming = nullptr) {
    if (!src || !outBgra || srcStride < inW * 4) return false;
    D3DReadbackTiming localTiming{};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    lastOutcome = Outcome::Error;
    lastMapPolls = 0;
    lastWaitUs = 0;
    const uint64_t entryUs = qpc_now_us();
    const uint64_t deadlineUs = entryUs + readyBudgetUs;  // ONE deadline, from entry, never restarted
    bool mappedOk = false;
    uint64_t lockAcquiredUs = 0;
    {
      stamp(MainLoopSubphase::ScaleLock);
      const uint64_t lockWaitStartUs = qpc_now_us();
      std::lock_guard<std::mutex> lk(*d3dMutex);
      lockAcquiredUs = qpc_now_us();
      localTiming.d3dWaitUs =
          (lockAcquiredUs >= lockWaitStartUs) ? (lockAcquiredUs - lockWaitStartUs) : 0;
      if (!ensure_resources(inW, inH, outW, outH)) { stamp(MainLoopSubphase::None); return false; }

      stamp(MainLoopSubphase::ScaleUpload);
      context->UpdateSubresource(srcTexture.Get(), 0, nullptr, src, srcStride, 0);

      RECT srcRect{};
      srcRect.left = 0;
      srcRect.top = 0;
      srcRect.right = static_cast<LONG>(inW);
      srcRect.bottom = static_cast<LONG>(inH);
      RECT dstRect{};
      dstRect.left = 0;
      dstRect.top = 0;
      dstRect.right = static_cast<LONG>(outW);
      dstRect.bottom = static_cast<LONG>(outH);

      videoContext->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE, &dstRect);
      videoContext->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, &srcRect);
      videoContext->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &dstRect);
      videoContext->VideoProcessorSetStreamFrameFormat(
          processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

      // This blt is BGRA->BGRA, so both ends must be declared full-range RGB. Leaving the
      // color spaces unset lets the driver assume studio range on one side and crush levels.
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace{};
      colorSpace.Usage = 0;             // playback (full precision)
      colorSpace.RGB_Range = 0;         // 0 = full range (0-255)
      colorSpace.YCbCr_Matrix = 1;      // BT.709, matches apply_video_colorimetry
      colorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
      videoContext->VideoProcessorSetStreamColorSpace(processor.Get(), 0, &colorSpace);
      videoContext->VideoProcessorSetOutputColorSpace(processor.Get(), &colorSpace);
      // Vendor auto-processing (edge enhancement / denoise) is tuned for video, not text,
      // and produces ringing around UI glyphs.
      videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.Get(), 0, FALSE);

      D3D11_VIDEO_PROCESSOR_STREAM stream{};
      stream.Enable = TRUE;
      stream.pInputSurface = inputView.Get();
      stamp(MainLoopSubphase::ScaleBlt);
      if (FAILED(videoContext->VideoProcessorBlt(processor.Get(), outputView.Get(), 0, 1, &stream))) {
        stamp(MainLoopSubphase::None);
        return false;
      }

      stamp(MainLoopSubphase::ScaleCopy);
      context->CopyResource(dstStaging.Get(), dstTexture.Get());
      stamp(MainLoopSubphase::ScaleMapWait);
      const HRESULT mapHr = map_do_not_wait(&mapped);
      if (SUCCEEDED(mapHr)) {
        mappedOk = true;
      } else if (mapHr != DXGI_ERROR_WAS_STILL_DRAWING) {
        stamp(MainLoopSubphase::None);
        return false;  // Outcome::Error
      }
    }  // the shared mutex is released before any waiting below
    // Bounded readiness: the copy is still in flight. Poll with the mutex released between tries;
    // the deadline is the one taken at entry. On timeout nothing has been read from the staging
    // texture and outBgra is untouched -- the caller scales the original frame on the CPU.
    while (!mappedOk) {
      ++lastMapPolls;
      if (qpc_now_us() >= deadlineUs) {
        lastOutcome = Outcome::Timeout;
        lastWaitUs = qpc_now_us() - entryUs;
        stamp(MainLoopSubphase::None);
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));  // mutex NOT held
      stamp(MainLoopSubphase::ScaleMapWait);
      std::lock_guard<std::mutex> lk(*d3dMutex);
      const HRESULT hr = map_do_not_wait(&mapped);
      if (SUCCEEDED(hr)) {
        mappedOk = true;
      } else if (hr != DXGI_ERROR_WAS_STILL_DRAWING) {
        stamp(MainLoopSubphase::None);
        return false;  // Outcome::Error
      }
    }
    const uint64_t copyMapDoneUs = qpc_now_us();
    localTiming.copyMapUs =
        (copyMapDoneUs >= lockAcquiredUs) ? (copyMapDoneUs - lockAcquiredUs) : 0;
    lastWaitUs = copyMapDoneUs - entryUs;
    stamp(MainLoopSubphase::ScaleReadback);
    outBgra->resize(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4);
    const uint32_t outStride = outW * 4;
    auto* dst = outBgra->data();
    const auto* mappedData = reinterpret_cast<const uint8_t*>(mapped.pData);
    const uint64_t memcpyStartUs = qpc_now_us();
    for (uint32_t row = 0; row < outH; ++row) {
      std::memcpy(dst + static_cast<size_t>(row) * outStride,
                  mappedData + static_cast<size_t>(row) * mapped.RowPitch, outStride);
    }
    const uint64_t memcpyDoneUs = qpc_now_us();
    localTiming.memcpyUs =
        (memcpyDoneUs >= memcpyStartUs) ? (memcpyDoneUs - memcpyStartUs) : 0;
    {
      stamp(MainLoopSubphase::ScaleUnmap);
      const uint64_t unmapWaitStartUs = qpc_now_us();
      std::lock_guard<std::mutex> lk(*d3dMutex);
      const uint64_t unmapLockAcquiredUs = qpc_now_us();
      localTiming.unmapWaitUs =
          (unmapLockAcquiredUs >= unmapWaitStartUs) ? (unmapLockAcquiredUs - unmapWaitStartUs) : 0;
      context->Unmap(dstStaging.Get(), 0);
      const uint64_t unmapDoneUs = qpc_now_us();
      localTiming.unmapUs =
          (unmapDoneUs >= unmapLockAcquiredUs) ? (unmapDoneUs - unmapLockAcquiredUs) : 0;
    }
    lastOutcome = Outcome::Ok;
    stamp(MainLoopSubphase::None);
    if (outTiming) {
      *outTiming = localTiming;
    }
    return true;
  }
};

}  // namespace remote60::native_poc
