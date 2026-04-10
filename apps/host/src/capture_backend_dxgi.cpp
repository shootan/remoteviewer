#include "capture_backend_dxgi.hpp"

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>
#include <utility>

namespace remote60::host {
namespace {

std::string hresult_hex(HRESULT hr) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
  return oss.str();
}

}  // namespace

struct DxgiDesktopCaptureSession::Impl {
  DxgiDesktopCaptureConfig config;
  DxgiDesktopFrameHandler onFrame;
  DxgiDesktopLogHandler onLog;
  DxgiDesktopFallbackHandler onFallback;
  std::atomic<bool> stopRequested{false};
  std::thread worker;
  Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
  HMONITOR monitor = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;

  void log(const std::string& phase, const std::string& message) const {
    if (onLog) onLog(phase, message);
  }

  bool resolve_output(Microsoft::WRL::ComPtr<IDXGIOutput1>* outOutput1,
                      DXGI_OUTPUT_DESC* outDesc,
                      std::string* detailOut) {
    if (!outOutput1 || !outDesc) {
      if (detailOut) *detailOut = "dxgi_bad_output_arg";
      return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice.As(&dxgiDevice);
    if (FAILED(hr) || !dxgiDevice) {
      if (detailOut) *detailOut = "dxgi_device_qi_failed_" + hresult_hex(hr);
      return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr) || !adapter) {
      if (detailOut) *detailOut = "dxgi_adapter_failed_" + hresult_hex(hr);
      return false;
    }

    const HMONITOR targetMonitor =
        config.monitor ? config.monitor : MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    if (!targetMonitor) {
      if (detailOut) *detailOut = "primary_monitor_missing";
      return false;
    }

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    Microsoft::WRL::ComPtr<IDXGIOutput> firstOutput;
    DXGI_OUTPUT_DESC desc{};
    DXGI_OUTPUT_DESC firstDesc{};
    bool found = false;
    for (UINT idx = 0;; ++idx) {
      Microsoft::WRL::ComPtr<IDXGIOutput> candidate;
      hr = adapter->EnumOutputs(idx, &candidate);
      if (hr == DXGI_ERROR_NOT_FOUND) break;
      if (FAILED(hr) || !candidate) {
        if (detailOut) *detailOut = "dxgi_enum_outputs_failed_" + hresult_hex(hr);
        return false;
      }
      DXGI_OUTPUT_DESC candidateDesc{};
      hr = candidate->GetDesc(&candidateDesc);
      if (FAILED(hr)) {
        if (detailOut) *detailOut = "dxgi_output_desc_failed_" + hresult_hex(hr);
        return false;
      }
      if (!firstOutput) {
        firstOutput = candidate;
        firstDesc = candidateDesc;
      }
      if (candidateDesc.Monitor == targetMonitor) {
        output = candidate;
        desc = candidateDesc;
        found = true;
        break;
      }
    }

    if (!output && firstOutput) {
      output = firstOutput;
      desc = firstDesc;
      found = true;
      log("capture", "dxgi_output_match=fallback_first_output");
    }

    if (!found || !output) {
      if (detailOut) *detailOut = "dxgi_no_output_found";
      return false;
    }

    const LONG outputWidth = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    const LONG outputHeight = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    if (outputWidth <= 0 || outputHeight <= 0) {
      if (detailOut) *detailOut = "dxgi_output_size_invalid";
      return false;
    }

    const bool rotatedPortrait = desc.Rotation == DXGI_MODE_ROTATION_ROTATE90 ||
                                 desc.Rotation == DXGI_MODE_ROTATION_ROTATE270;
    if (config.landscapeOnly && (rotatedPortrait || outputWidth < outputHeight)) {
      if (detailOut) *detailOut = "rotation_unsupported";
      return false;
    }

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr) || !output1) {
      if (detailOut) *detailOut = "dxgi_output1_qi_failed_" + hresult_hex(hr);
      return false;
    }

    *outOutput1 = std::move(output1);
    *outDesc = desc;
    return true;
  }

  bool recreate_duplication(std::string* detailOut) {
    duplication.Reset();

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    DXGI_OUTPUT_DESC desc{};
    if (!resolve_output(&output1, &desc, detailOut)) return false;

    HRESULT hr = output1->DuplicateOutput(d3dDevice.Get(), &duplication);
    if (FAILED(hr) || !duplication) {
      if (detailOut) *detailOut = "dxgi_duplicate_output_failed_" + hresult_hex(hr);
      return false;
    }

    monitor = desc.Monitor;
    width = static_cast<uint32_t>(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
    height = static_cast<uint32_t>(desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
    return true;
  }

  void request_fallback(const std::string& reason) {
    log("capture", "fallback_reason=" + reason);
    if (onFallback) onFallback(reason);
  }

  void run() {
    while (!stopRequested.load()) {
      DXGI_OUTDUPL_FRAME_INFO frameInfo{};
      Microsoft::WRL::ComPtr<IDXGIResource> resource;
      const HRESULT hr = duplication->AcquireNextFrame(config.acquireTimeoutMs, &frameInfo, &resource);
      if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        continue;
      }
      if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_ACCESS_DENIED) {
        log("capture", "duplication_recreate begin reason=" + hresult_hex(hr));
        std::string recreateDetail;
        if (!recreate_duplication(&recreateDetail)) {
          log("capture", "duplication_recreate end success=0 detail=" + recreateDetail);
          request_fallback(recreateDetail);
          return;
        }
        log("capture", "duplication_recreate end success=1 width=" + std::to_string(width) +
                           " height=" + std::to_string(height));
        continue;
      }
      if (FAILED(hr)) {
        request_fallback("dxgi_acquire_failed_" + hresult_hex(hr));
        return;
      }

      bool frameHeld = true;
      auto releaseFrame = [&]() {
        if (!frameHeld || !duplication) return;
        duplication->ReleaseFrame();
        frameHeld = false;
      };

      Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
      const HRESULT texHr = resource.As(&texture);
      if (FAILED(texHr) || !texture) {
        releaseFrame();
        request_fallback("dxgi_frame_texture_qi_failed_" + hresult_hex(texHr));
        return;
      }

      D3D11_TEXTURE2D_DESC desc{};
      texture->GetDesc(&desc);
      if (desc.Width != width || desc.Height != height) {
        releaseFrame();
        request_fallback("dxgi_frame_size_changed");
        return;
      }

      try {
        if (onFrame) onFrame(texture.Get(), width, height);
      } catch (...) {
        releaseFrame();
        request_fallback("dxgi_frame_handler_exception");
        return;
      }

      releaseFrame();
    }
  }
};

DxgiDesktopCaptureSession::DxgiDesktopCaptureSession() : impl_(std::make_unique<Impl>()) {}

DxgiDesktopCaptureSession::~DxgiDesktopCaptureSession() {
  Stop();
}

bool DxgiDesktopCaptureSession::Start(const DxgiDesktopCaptureConfig& config,
                                      DxgiDesktopFrameHandler onFrame,
                                      DxgiDesktopLogHandler onLog,
                                      DxgiDesktopFallbackHandler onFallback,
                                      std::string* detailOut) {
  Stop();
  impl_ = std::make_unique<Impl>();
  impl_->config = config;
  impl_->onFrame = std::move(onFrame);
  impl_->onLog = std::move(onLog);
  impl_->onFallback = std::move(onFallback);
  impl_->d3dDevice = config.d3dDevice;
  if (!impl_->d3dDevice) {
    if (detailOut) *detailOut = "dxgi_missing_device";
    impl_.reset();
    return false;
  }

  std::string recreateDetail;
  if (!impl_->recreate_duplication(&recreateDetail)) {
    if (detailOut) *detailOut = recreateDetail;
    impl_.reset();
    return false;
  }

  impl_->worker = std::thread([impl = impl_.get()]() { impl->run(); });
  if (detailOut) *detailOut = "ok";
  return true;
}

void DxgiDesktopCaptureSession::Stop() {
  if (!impl_) return;
  impl_->stopRequested = true;
  if (impl_->worker.joinable()) impl_->worker.join();
  impl_->duplication.Reset();
}

uint32_t DxgiDesktopCaptureSession::width() const {
  return impl_ ? impl_->width : 0;
}

uint32_t DxgiDesktopCaptureSession::height() const {
  return impl_ ? impl_->height : 0;
}

}  // namespace remote60::host
