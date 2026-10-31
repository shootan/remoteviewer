#pragma once

// D3D11 VideoProcessor-based BGRA downscaler with CPU readback (the "GPU scaler" encode path).
//
// Role:    GpuBgraScaler -- uploads a BGRA frame, blits it to the encode size through the video
//          processor (full-range RGB, auto-processing off), and reads the result back into a
//          caller-owned vector with per-stage timings (D3DReadbackTiming).
// Thread:  main encode loop only. Every D3D call is taken under the shared *d3dMutex handed to
//          initialize(), because the capture readback worker shares the same immediate context.
// Input:   BGRA buffer + geometry, target size.
// Output:  scaled BGRA in outBgra, timing split in outTiming; false on any D3D failure (caller
//          falls back to the CPU resize_bgra_bilinear path and marks the scaler unhealthy).
// Callers: native_video_host_main.cpp (encode path when gpuScalerRequested).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-3). Header-only
// (all members were already defined in-class); behavior is byte-identical.

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "host_bottleneck.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

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
    {
      const uint64_t lockWaitStartUs = qpc_now_us();
      std::lock_guard<std::mutex> lk(*d3dMutex);
      const uint64_t lockAcquiredUs = qpc_now_us();
      localTiming.d3dWaitUs =
          (lockAcquiredUs >= lockWaitStartUs) ? (lockAcquiredUs - lockWaitStartUs) : 0;
      if (!ensure_resources(inW, inH, outW, outH)) return false;

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
      if (FAILED(videoContext->VideoProcessorBlt(processor.Get(), outputView.Get(), 0, 1, &stream))) {
        return false;
      }

      context->CopyResource(dstStaging.Get(), dstTexture.Get());
      if (FAILED(context->Map(dstStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
      const uint64_t copyMapDoneUs = qpc_now_us();
      localTiming.copyMapUs =
          (copyMapDoneUs >= lockAcquiredUs) ? (copyMapDoneUs - lockAcquiredUs) : 0;
    }
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
    if (outTiming) {
      *outTiming = localTiming;
    }
    return true;
  }
};

}  // namespace remote60::native_poc
