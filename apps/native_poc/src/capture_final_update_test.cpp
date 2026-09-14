#include <windows.h>
#include "d3d_capture_readback.hpp"
#include <cstdio>
#include <chrono>

int main(int argc, char**) {
  using namespace remote60::native_poc;
  const uint32_t width = argc > 1 ? 1920 : 64;
  const uint32_t height = argc > 1 ? 1080 : 64;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL level{};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                             D3D11_SDK_VERSION, &device, &level, &context))) return 2;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0xff000011);
  D3D11_SUBRESOURCE_DATA initial{pixels.data(), width * 4, 0};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
  if (FAILED(device->CreateTexture2D(&desc, &initial, &source))) return 3;
  std::mutex contextMu, resultMu;
  std::condition_variable resultReady;
  uint64_t lastCapture = 0;
  uint8_t lastPixel = 0;
  D3dCaptureReadbackPipeline pipe;
  if (!pipe.Initialize(device.Get(), context.Get(), &contextMu, width, height, 2,
      [&](std::shared_ptr<std::vector<uint8_t>> bytes, uint32_t, uint32_t, uint32_t,
          const CaptureFrameMeta& meta, uint64_t, uint64_t, uint64_t) {
        std::lock_guard<std::mutex> lock(resultMu);
        lastCapture = meta.captureUs; lastPixel = bytes->at(0); resultReady.notify_all();
      })) return 4;
  pipe.SetPublishIntervalUs(100000); // Make the formerly discarded final update deterministic.
  CaptureFrameMeta meta{}; meta.width = width; meta.height = height; meta.captureUs = 1;
  pipe.Submit(source.Get(), meta);
  {
    std::unique_lock<std::mutex> lock(resultMu);
    if (!resultReady.wait_for(lock, std::chrono::seconds(2), [&] { return lastCapture == 1; })) return 5;
  }
  // Overflow the bounded ring while publication is paced, then stop producing entirely.
  for (uint64_t i = 2; i <= 30; ++i) {
    std::fill(pixels.begin(), pixels.end(), 0xff000000u | static_cast<uint32_t>(i));
    { std::lock_guard<std::mutex> lock(contextMu); context->UpdateSubresource(source.Get(), 0, nullptr, pixels.data(), width * 4, 0); }
    meta.captureUs = i;
    if (!pipe.Submit(source.Get(), meta)) return 6;
  }
  bool pass;
  {
    std::unique_lock<std::mutex> lock(resultMu);
    pass = resultReady.wait_for(lock, std::chrono::seconds(2), [&] { return lastCapture == 30; }) && lastPixel == 30;
  }
  pipe.Shutdown();
  std::printf("capture_final_update_test: %s (WARP %ux%u, final changed pixels delivered without another callback)\n", pass ? "PASS" : "FAIL", width, height);
  return pass ? 0 : 1;
}
