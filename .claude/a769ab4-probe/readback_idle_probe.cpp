#include "d3d_capture_readback.hpp"
#include <chrono>
#include <cstdio>
#include <future>

using namespace remote60::native_poc;
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

static uint64_t now_us() {
  LARGE_INTEGER f{}, c{};
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return static_cast<uint64_t>(c.QuadPart) * 1000000ULL / f.QuadPart;
}

struct Result { uint64_t workerWait, calls, total, pending; };
int main() {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
      &device, nullptr, &context);
  if (FAILED(hr)) { std::printf("WARP failed: %08lx\n", hr); return 2; }
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = desc.Height = 2; desc.MipLevels = desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  const uint32_t pixels[4] = {0xff112233, 0xff112233, 0xff112233, 0xff112233};
  D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = pixels; data.SysMemPitch = 8;
  ComPtr<ID3D11Texture2D> texture;
  if (FAILED(device->CreateTexture2D(&desc, &data, &texture))) return 3;
  std::mutex contextMu;
  std::unique_lock<std::mutex> hold(contextMu);
  std::promise<Result> result;
  auto future = result.get_future();
  D3dCaptureReadbackPipeline pipeline;
  if (!pipeline.Initialize(device.Get(), context.Get(), &contextMu, 2, 2, 3,
      [&](auto, uint32_t, uint32_t, uint32_t, const CaptureFrameMeta& meta,
          uint64_t pending, uint64_t, uint64_t) {
        result.set_value({meta.workerCtxWaitUs, meta.workerD3dCallUs,
            now_us() - meta.captureUs, pending});
      })) return 4;
  // No frame exists: force an idle worker's context lock acquisition to wait.
  std::this_thread::sleep_for(250ms);
  hold.unlock();
  // Finish the artificial idle contention well BEFORE the frame is captured.
  std::this_thread::sleep_for(100ms);
  CaptureFrameMeta meta{};
  meta.width = meta.height = 2;
  meta.captureUs = meta.callbackUs = now_us();
  if (!pipeline.Submit(texture.Get(), meta)) return 5;
  if (future.wait_for(5s) != std::future_status::ready) return 6;
  const auto observed = future.get();
  pipeline.Shutdown();
  std::printf("workerCtxWaitUs=%llu workerD3dCallUs=%llu captureToPublishUs=%llu gpuPendingUs=%llu\n",
      observed.workerWait, observed.calls, observed.total, observed.pending);
  const bool contaminated = observed.workerWait > 150000 && observed.workerWait > observed.total;
  std::printf("idle wait attributed to later frame: %s\n", contaminated ? "REPRODUCED" : "NOT REPRODUCED");
  return contaminated ? 0 : 1;
}
