#pragma once

// The single-slot handoff record between the capture publisher and the host encode loop.
//
// Role:    FrameState -- the latest published frame (payload or GPU NV12 slot) plus the capture /
//          callback / readback timings the encode loop folds into its stats. version bumps on
//          every publish; the loop waits on cv for it.
// Thread:  written by the capture publish callback (WGC/DXGI readback worker or GDI process
//          reader), read by the main encode loop; every access is under `mu`, and `cv` is the
//          wake-up. Whoever pops a frame with nv12Slot >= 0 owns that slot and must release it.
// Input:   published frames from the capture path.
// Output:  one frame at a time to the encode loop (newer publishes overwrite = skippedByOverwrite).
// Callers: native_video_host_main.cpp (publish_captured_texture, GDI reader, main loop pop).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-10). Header-only;
// behavior is byte-identical.

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace remote60::native_poc {

struct FrameState {
  std::mutex mu;
  std::condition_variable cv;
  uint64_t version = 0;
  uint32_t seq = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint64_t streamGeneration = 0;
  uint64_t captureUs = 0;
  uint64_t callbackUs = 0;
  uint64_t callbackIntervalUs = 0;
  uint64_t captureAgeAtCallbackUs = 0;
  uint64_t captureClockSkewUs = 0;
  uint64_t queuePushUs = 0;
  uint64_t captureIntervalUs = 0;
  uint64_t captureD3DWaitUs = 0;
  uint64_t captureCopyMapUs = 0;
  uint64_t captureMemcpyUs = 0;
  uint64_t captureUnmapWaitUs = 0;
  uint64_t captureUnmapUs = 0;
  uint64_t captureWorkerCtxWaitUs = 0;  // readback worker waits on d3dContextMu (0.2.98)
  uint64_t captureWorkerD3dCallUs = 0;  // readback worker time inside GetData/Map/Unmap (0.2.98)
  // GPU NV12 conversion of this frame for the zero-copy encode path; -1 when absent.
  // Whoever pops the frame claims the slot and must release it.
  int32_t nv12Slot = -1;
  uint64_t nv12Generation = 0;
  uint32_t nv12W = 0;
  uint32_t nv12H = 0;
  std::shared_ptr<std::vector<uint8_t>> payload;
};

}  // namespace remote60::native_poc
