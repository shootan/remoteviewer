#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gdi_capture_process.hpp"

namespace {

uint64_t percentile95(std::vector<uint64_t> values) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const size_t index = std::min(values.size() - 1, (values.size() * 95) / 100);
  return values[index];
}

uint64_t average(const std::vector<uint64_t>& values) {
  if (values.empty()) return 0;
  uint64_t sum = 0;
  for (const uint64_t value : values) sum += value;
  return sum / values.size();
}

}  // namespace

int main() {
  const HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!monitor || !GetMonitorInfoW(monitor, &info)) {
    std::cerr << "FAIL primary monitor query\n";
    return 1;
  }
  remote60::native_poc::GdiCaptureProcessConfig config;
  config.width = static_cast<uint32_t>(info.rcMonitor.right - info.rcMonitor.left);
  config.height = static_cast<uint32_t>(info.rcMonitor.bottom - info.rcMonitor.top);
  // A tiny producer headroom absorbs Windows timer jitter; the stream consumer still gates
  // output to its requested 60fps and always takes the newest frame.
  config.fps = 64;

  std::mutex timingMu;
  std::vector<uint64_t> captureCopyUs;
  std::vector<uint64_t> parentCopyUs;
  std::atomic<uint64_t> frames{0};
  std::atomic<bool> fallback{false};
  std::string workerLog;
  remote60::native_poc::GdiCaptureProcess capture;
  std::string detail;
  const bool started = capture.Start(
      config,
      [&](std::shared_ptr<std::vector<uint8_t>> pixels, uint32_t width,
          uint32_t height, uint32_t stride, uint64_t, uint64_t workerUs,
          uint64_t consumerUs) {
        if (!pixels || pixels->size() != static_cast<size_t>(stride) * height ||
            width != config.width || height != config.height) {
          fallback.store(true, std::memory_order_release);
          return;
        }
        {
          std::lock_guard<std::mutex> lock(timingMu);
          captureCopyUs.push_back(workerUs);
          parentCopyUs.push_back(consumerUs);
        }
        frames.fetch_add(1, std::memory_order_relaxed);
      },
      [&](const std::string&, const std::string& message) { workerLog = message; },
      [&](const std::string& reason) {
        std::cerr << "fallback=" << reason << "\n";
        fallback.store(true, std::memory_order_release);
      },
      &detail);
  if (!started) {
    std::cerr << "FAIL start detail=" << detail << "\n";
    return 1;
  }

  const auto firstDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (frames.load(std::memory_order_relaxed) == 0 &&
         std::chrono::steady_clock::now() < firstDeadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const uint64_t baseline = frames.load(std::memory_order_relaxed);
  const auto sampleStart = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::seconds(3));
  const double elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - sampleStart).count();
  const uint64_t delivered = frames.load(std::memory_order_relaxed) - baseline;
  capture.Stop();

  std::vector<uint64_t> workerTimes;
  std::vector<uint64_t> consumerTimes;
  {
    std::lock_guard<std::mutex> lock(timingMu);
    workerTimes = captureCopyUs;
    consumerTimes = parentCopyUs;
  }
  const double fps = elapsed > 0 ? static_cast<double>(delivered) / elapsed : 0;
  std::cout << "GDI_PROCESS_ISOLATED=" << (workerLog.find("pid=") != std::string::npos ? 1 : 0)
            << "\nGDI_DELIVERED_FRAMES=" << delivered
            << "\nGDI_DELIVERED_FPS=" << fps
            << "\nGDI_CAPTURE_COPY_AVG_US=" << average(workerTimes)
            << "\nGDI_CAPTURE_COPY_P95_US=" << percentile95(workerTimes)
            << "\nGDI_PARENT_COPY_AVG_US=" << average(consumerTimes)
            << "\nGDI_PARENT_COPY_P95_US=" << percentile95(consumerTimes)
            << "\n";
  // This test may run while another remote-control product is also copying the same desktop.
  // Hold the isolated fallback above 50fps and within two 60fps frame budgets under that
  // contention; the WGC end-to-end stream gate separately requires the requested rate.
  const bool pass = !fallback.load(std::memory_order_acquire) && delivered >= 150 &&
                    fps >= 50.0 && percentile95(workerTimes) <= 35000 &&
                    percentile95(consumerTimes) <= 4000;
  std::cout << (pass ? "RESULT: ALL PASS\n" : "RESULT: FAILED\n");
  return pass ? 0 : 1;
}
