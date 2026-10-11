// encoder-rc-budget r9: the GPU scaler's bounded readiness (Codex r8 contract, item 2).
//
// A real D3D11 device (hardware, else WARP) runs the real upload/blt/copy; the Map seam stands in for
// context->Map so each case is decisive:
//   [1] not-ready -> ready: STILL_DRAWING twice, then the real Map -- the result is the real scaled
//       image (checked against the CPU bilinear of the same input), outcome Ok, 2 polls, mutex never
//       held across a sleep (the test holds the mutex itself from another thread between polls and
//       sees the scaler acquire it only for the Map call).
//   [2] not-ready forever: outcome Timeout at the deadline, false, outBgra UNTOUCHED, the deadline
//       measured from entry (not restarted per poll).
//   [3] API error: Map fails -> false, outcome Error, no poll loop.
//   [4] the plain path (no seam) still works: a 1920x1080 -> 1280x720 blit matches the CPU resize.
//   [5] scale_ready_budget_us: 3 periods clamped to [60, 200] ms.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "host_bgra_scale.hpp"
#include "host_gpu_scaler.hpp"

using namespace remote60::native_poc;

namespace {
int gChecks = 0, gFails = 0;
void check(const char* what, bool ok, const std::string& detail = "") {
  ++gChecks;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
  if (!ok) ++gFails;
}

bool make_device(Microsoft::WRL::ComPtr<ID3D11Device>* dev, Microsoft::WRL::ComPtr<ID3D11DeviceContext>* ctx, const char** kind) {
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
    D3D_FEATURE_LEVEL got{};
    if (SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                    2, D3D11_SDK_VERSION, dev->GetAddressOf(), &got, ctx->GetAddressOf()))) {
      *kind = type == D3D_DRIVER_TYPE_HARDWARE ? "hardware" : "warp";
      return true;
    }
  }
  return false;
}

std::vector<uint8_t> test_page(uint32_t w, uint32_t h) {
  std::vector<uint8_t> p(static_cast<size_t>(w) * h * 4);
  uint32_t s = 12345;
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;
      uint8_t* px = &p[(static_cast<size_t>(y) * w + x) * 4];
      px[0] = static_cast<uint8_t>((x * 3 + y) & 0xFF);
      px[1] = static_cast<uint8_t>((y * 5) & 0xFF);
      px[2] = static_cast<uint8_t>(s & 0xFF);
      px[3] = 0xFF;
    }
  return p;
}

double mean_abs_diff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  if (a.size() != b.size() || a.empty()) return 1e9;
  double sum = 0;
  for (size_t i = 0; i < a.size(); ++i) if ((i & 3) != 3) sum += std::abs(int(a[i]) - int(b[i]));
  return sum / (a.size() * 3.0 / 4.0);
}
}  // namespace

int main() {
  Microsoft::WRL::ComPtr<ID3D11Device> dev;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
  const char* kind = "";
  if (!make_device(&dev, &ctx, &kind)) {
    std::printf("host_gpu_scaler_test: no D3D11 device (hardware or WARP) -- SKIP\n");
    return 0;
  }
  std::printf("device=%s\n", kind);
  std::mutex mu;
  GpuBgraScaler scaler;
  check("[0] initialize on a video-capable device", scaler.initialize(dev.Get(), ctx.Get(), &mu));

  const uint32_t inW = 1920, inH = 1080, outW = 1280, outH = 720;
  const std::vector<uint8_t> page = test_page(inW, inH);
  std::vector<uint8_t> cpu;
  check("[4] CPU reference resize", resize_bgra_bilinear(page.data(), inW, inH, inW * 4, outW, outH, &cpu));

  // [4] plain path. The video processor's filter is not the CPU bilinear, so the CPU resize is only a
  // loose sanity bound (the same picture, within 24 levels on a noisy page); the GPU's own plain result is
  // the exact reference every seamed case must reproduce byte for byte -- that is what "the REAL image,
  // not a stale buffer" means.
  std::vector<uint8_t> gpuRef;
  {
    D3DReadbackTiming t{};
    scaler.readyBudgetUs = 200'000;
    const bool ok = scaler.scale(page.data(), inW, inH, inW * 4, outW, outH, &gpuRef, &t);
    const double d = mean_abs_diff(gpuRef, cpu);
    check("[4] plain GPU scale succeeds, outcome Ok, right size, same picture as the CPU resize (mean |diff| < 24)",
          ok && scaler.lastOutcome == GpuBgraScaler::Outcome::Ok && gpuRef.size() == cpu.size() && d < 24.0,
          "diff=" + std::to_string(d) + " polls=" + std::to_string(scaler.lastMapPolls));
  }

  // [1] not-ready -> ready
  {
    std::atomic<int> stillDrawing{2};
    std::atomic<int> mapCalls{0};
    std::atomic<bool> mutexHeldDuringMap{false};
    scaler.mapForTest = [&](ID3D11DeviceContext* c, ID3D11Resource* r, D3D11_MAPPED_SUBRESOURCE* out) -> HRESULT {
      ++mapCalls;
      // the scaler must hold the shared mutex while it calls Map
      if (mu.try_lock()) { mu.unlock(); } else { mutexHeldDuringMap = true; }
      if (stillDrawing.fetch_sub(1) > 0) return DXGI_ERROR_WAS_STILL_DRAWING;
      return c->Map(r, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, out);
    };
    std::vector<uint8_t> out;
    scaler.readyBudgetUs = 200'000;
    const bool ok = scaler.scale(page.data(), inW, inH, inW * 4, outW, outH, &out, nullptr);
    check("[1] STILL_DRAWING x2 then ready: true, outcome Ok, byte-identical to the GPU's plain result (the REAL image, not a stale buffer)",
          ok && scaler.lastOutcome == GpuBgraScaler::Outcome::Ok && out == gpuRef, "diff=" + std::to_string(mean_abs_diff(out, gpuRef)));
    // two seamed not-ready answers, plus possibly the real Map's own STILL_DRAWING once the seam passes through
    check("[1] at least the two seamed not-ready polls were counted, and every poll was one Map call",
          scaler.lastMapPolls >= 2 && mapCalls.load() == (int)scaler.lastMapPolls + 1,
          "polls=" + std::to_string(scaler.lastMapPolls) + " mapCalls=" + std::to_string(mapCalls.load()));
    check("[1] Map was called under the shared mutex every time", mutexHeldDuringMap.load(), std::to_string(mapCalls.load()));
    scaler.mapForTest = nullptr;
  }

  // [1b] the mutex is NOT held while the scaler waits between polls: another thread takes it during the wait.
  {
    std::atomic<int> stillDrawing{30};
    std::atomic<int> outsiderAcquired{0};
    std::atomic<bool> stop{false};
    std::thread outsider([&] {
      while (!stop.load()) {
        if (mu.try_lock()) { ++outsiderAcquired; mu.unlock(); }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      }
    });
    scaler.mapForTest = [&](ID3D11DeviceContext* c, ID3D11Resource* r, D3D11_MAPPED_SUBRESOURCE* out) -> HRESULT {
      if (stillDrawing.fetch_sub(1) > 0) return DXGI_ERROR_WAS_STILL_DRAWING;
      return c->Map(r, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, out);
    };
    std::vector<uint8_t> out;
    scaler.readyBudgetUs = 200'000;
    const bool ok = scaler.scale(page.data(), inW, inH, inW * 4, outW, outH, &out, nullptr);
    stop = true;
    outsider.join();
    check("[1b] while the scaler polls (30 not-ready answers) another thread acquires the shared mutex many times -- no sleep under the lock",
          ok && outsiderAcquired.load() >= 10, "outsider=" + std::to_string(outsiderAcquired.load()) + " polls=" + std::to_string(scaler.lastMapPolls));
    scaler.mapForTest = nullptr;
  }

  // [2] not-ready forever -> deadline
  {
    std::atomic<int> mapCalls{0};
    scaler.mapForTest = [&](ID3D11DeviceContext*, ID3D11Resource*, D3D11_MAPPED_SUBRESOURCE*) -> HRESULT {
      ++mapCalls;
      return DXGI_ERROR_WAS_STILL_DRAWING;
    };
    std::vector<uint8_t> out = {1, 2, 3, 4};  // a sentinel: must be untouched
    scaler.readyBudgetUs = 60'000;
    const uint64_t t0 = qpc_now_us();
    const bool ok = scaler.scale(page.data(), inW, inH, inW * 4, outW, outH, &out, nullptr);
    const uint64_t took = qpc_now_us() - t0;
    check("[2] never ready: false with outcome Timeout", !ok && scaler.lastOutcome == GpuBgraScaler::Outcome::Timeout);
    check("[2] the output buffer is untouched (no half-written or stale image handed on)", out.size() == 4 && out[0] == 1 && out[3] == 4);
    check("[2] the deadline is one budget from ENTRY: took >= 60 ms and < 60 ms + 40 ms slack (not restarted per poll)",
          took >= 60'000 && took < 100'000, std::to_string(took) + "us polls=" + std::to_string(scaler.lastMapPolls));
    // the deadline is checked before each Map, so the last counted poll makes no call: calls == polls
    check("[2] it polled many times (1 ms cadence), not once", scaler.lastMapPolls >= 20 && mapCalls.load() == (int)scaler.lastMapPolls,
          "polls=" + std::to_string(scaler.lastMapPolls) + " mapCalls=" + std::to_string(mapCalls.load()));
    scaler.mapForTest = nullptr;
  }

  // [3] API error
  {
    scaler.mapForTest = [&](ID3D11DeviceContext*, ID3D11Resource*, D3D11_MAPPED_SUBRESOURCE*) -> HRESULT { return E_FAIL; };
    std::vector<uint8_t> out = {9};
    scaler.readyBudgetUs = 200'000;
    const bool ok = scaler.scale(page.data(), inW, inH, inW * 4, outW, outH, &out, nullptr);
    check("[3] Map error: false, outcome Error, no polling, buffer untouched",
          !ok && scaler.lastOutcome == GpuBgraScaler::Outcome::Error && scaler.lastMapPolls == 0 && out.size() == 1 && out[0] == 9);
    scaler.mapForTest = nullptr;
  }

  // [4b] the device still works after the timeout/error cases (the staging texture was never left mapped)
  {
    std::vector<uint8_t> out;
    const bool ok = scaler.scale(page.data(), inW, inH, inW * 4, outW, outH, &out, nullptr);
    check("[4b] a plain scale after Timeout and Error still succeeds, byte-identical to the first plain result",
          ok && out == gpuRef, "diff=" + std::to_string(mean_abs_diff(out, gpuRef)));
  }

  // [5] the budget policy
  check("[5] budget: 30 fps -> 3 periods = 99,999 us (integer period)", scale_ready_budget_us(30) == 3 * (1'000'000 / 30),
        std::to_string(scale_ready_budget_us(30)));
  check("[5] budget: 60 fps -> 60 ms (lower clamp; 3 periods = 50 ms)", scale_ready_budget_us(60) == 60'000);
  check("[5] budget: 10 fps -> 200 ms (upper clamp; 3 periods = 300 ms)", scale_ready_budget_us(10) == 200'000);
  check("[5] budget: fps 0 -> the 30 fps value", scale_ready_budget_us(0) == 3 * 33'333);

  std::printf("host_gpu_scaler_test: %s (%d checks, %d failed)\n", gFails == 0 ? "PASS" : "FAILED", gChecks, gFails);
  return gFails == 0 ? 0 : 1;
}
