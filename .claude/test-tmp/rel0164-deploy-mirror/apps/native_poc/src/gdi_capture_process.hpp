#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace remote60::native_poc {

struct GdiCaptureProcessConfig {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t fps = 60;
  bool captureLayeredWindows = false;
};

using GdiCaptureFrameHandler = std::function<void(
    std::shared_ptr<std::vector<uint8_t>> pixels, uint32_t width, uint32_t height,
    uint32_t stride, uint64_t captureQpcUs, uint64_t captureCopyUs,
    uint64_t parentCopyUs)>;
using GdiCaptureLogHandler =
    std::function<void(const std::string& phase, const std::string& message)>;
using GdiCaptureFallbackHandler = std::function<void(const std::string& reason)>;

/**
 * Supervises the GDI capture worker and consumes its latest-wins shared-memory ring.
 * Capture/driver faults are isolated from the stream, control, and encoder process.
 */
class GdiCaptureProcess {
 public:
  GdiCaptureProcess();
  ~GdiCaptureProcess();

  GdiCaptureProcess(const GdiCaptureProcess&) = delete;
  GdiCaptureProcess& operator=(const GdiCaptureProcess&) = delete;

  bool Start(const GdiCaptureProcessConfig& config,
             GdiCaptureFrameHandler onFrame,
             GdiCaptureLogHandler onLog,
             GdiCaptureFallbackHandler onFallback,
             std::string* detailOut);
  void Stop();
  bool running() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace remote60::native_poc
