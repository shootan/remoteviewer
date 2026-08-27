#pragma once

// Host pipeline statistics accumulators (HostStats).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <atomic>
#include <cstdint>

namespace remote60::native_poc {

// Host-side pipeline statistics (Phase 1-12 state struct): the per-print-interval accumulators
// and lifetime counters the 1s stats tick folds into the "[native-video-host] stats" line --
// capture readback / GPU-scale stage timings (sum + max), queue push/pop/wait counts, drop and
// fallback counters, and the print cadence itself. ABR reads ClientMetricsSnapshot and
// RateControlState instead -- but see the callbackFrames note below: this struct is not purely
// descriptive any more.
// thread: main loop owns everything; callbackFrames / queuePushCount / queueDepthMax are atomics
// because the capture publish callback increments them.
// NOTE: callbackFrames is NOT display-only -- the readback-drain and GDI low-push watchdogs
// restart capture on its per-window value, which is why the 1s tick consumes it with an
// exchange rather than reading it and zeroing it later. (Ledger H-12.)

// Monotonic max for an atomic counter (formerly the update_u64_max lambda in main()).
inline void update_u64_max(std::atomic<uint64_t>& target, const uint64_t value) {
  auto old = target.load(std::memory_order_relaxed);
  while (value > old && !target.compare_exchange_weak(old, value, std::memory_order_release, std::memory_order_relaxed)) {
  }
}
struct HostStats {
  uint32_t printEverySec = 0;   // REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC
  uint64_t ticks = 0;
  uint64_t nextAtUs = 0;
  uint64_t rawEquivalentBytes = 0;
  uint64_t skippedByOverwrite = 0;
  uint64_t lastVersionSent = 0;
  uint64_t tracePrinted = 0;
  uint64_t staleEncodedDropCount = 0;
  uint64_t stalePreEncodeDropCount = 0;
  // GPU scaler outcome counters + stage timings.
  uint64_t gpuScaleAttempts = 0;
  uint64_t gpuScaleSuccess = 0;
  uint64_t gpuScaleFail = 0;
  uint64_t gpuScaleCpuFallback = 0;
  uint64_t gpuScaleTimedCount = 0;
  uint64_t gpuScaleD3DWaitSumUs = 0;
  uint64_t gpuScaleD3DWaitMaxUs = 0;
  uint64_t gpuScaleCopyMapSumUs = 0;
  uint64_t gpuScaleCopyMapMaxUs = 0;
  uint64_t gpuScaleMemcpySumUs = 0;
  uint64_t gpuScaleMemcpyMaxUs = 0;
  uint64_t gpuScaleUnmapWaitSumUs = 0;
  uint64_t gpuScaleUnmapWaitMaxUs = 0;
  uint64_t gpuScaleUnmapSumUs = 0;
  uint64_t gpuScaleUnmapMaxUs = 0;
  // Capture readback stage timings (per published frame).
  uint64_t captureReadbackSamples = 0;
  uint64_t captureD3DWaitSumUs = 0;
  uint64_t captureD3DWaitMaxUs = 0;
  uint64_t captureCopyMapSumUs = 0;
  uint64_t captureCopyMapMaxUs = 0;
  uint64_t captureMemcpySumUs = 0;
  uint64_t captureMemcpyMaxUs = 0;
  uint64_t captureUnmapWaitSumUs = 0;
  uint64_t captureUnmapWaitMaxUs = 0;
  uint64_t captureUnmapSumUs = 0;
  uint64_t captureUnmapMaxUs = 0;
  uint64_t captureAgeSumUs = 0;
  uint64_t captureAgeMaxUs = 0;
  uint64_t callbackToEncodeStartSumUs = 0;
  uint64_t callbackToEncodeStartMaxUs = 0;
  uint64_t idleHoldTotal = 0;
  uint64_t lastSendStartUs = 0;
  uint64_t firstSentLoggedGeneration = 0;
  // Frame queue accounting (callback -> main handoff).
  std::atomic<uint64_t> callbackFrames{0};
  std::atomic<uint64_t> queuePushCount{0};
  uint64_t queuePushCountLastSample = 0;  // only read from main thread
  uint64_t queuePushPerSecLatest = 0;
  uint64_t queuePopCount = 0;
  uint64_t queueWaitTimeoutCount = 0;
  // Last size actually popped from the capture ring. The 1s tick prints it instead of the
  // current TickContext, because that tick now runs before the pop (see stage order in
  // native_video_host_main.cpp). (Ledger H-10.)
  uint32_t lastFrameW = 0;
  uint32_t lastFrameH = 0;
  uint64_t queueWaitNoWorkCount = 0;
  // LIFETIME worst, never reset -- that is deliberate (it has behaved this way since it was
  // introduced) but it is printed next to per-second counters with nothing saying so, which is
  // how it kept reading as "the last second was that deep". The label stays for log
  // compatibility; queueDepthWindowMax below is the per-interval peak that was missing.
  // (Ledger H-14.)
  std::atomic<uint64_t> queueDepthMax{0};
  std::atomic<uint64_t> queueDepthWindowMax{0};  // reset every stats tick
};

}  // namespace remote60::native_poc
