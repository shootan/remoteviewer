#pragma once

// Viewer-reported metrics + keyframe requests (ViewerMetrics, ClientMetricsSnapshot).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own. Phase 4 replaced the field-by-field atomics with a lock-copied
// snapshot -- see the note on ViewerMetrics.

#include <atomic>
#include <cstdint>
#include <mutex>

namespace remote60::native_poc {

// One ControlClientMetrics report, as a value.
//
// These arrive as a single message and describe a single instant on the viewer, so they belong
// together. They used to be nineteen separate atomics: the control thread stored them one by one
// and the 1s tick loaded them one by one, which meant the host could act on a mix of two reports
// -- a fresh latency next to a stale congestion state -- and no reader could tell. The values feed
// the ABR/M9 decision, so that is a decision made on a state the viewer was never in.
// (Phase 4: ClientMetricsSnapshot.)
struct ViewerMetrics {
  uint64_t updatedUs = 0;  // 0 = nothing reported yet; readers use it for a freshness gate
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t recvFpsX100 = 0;
  uint32_t decodedFpsX100 = 0;
  uint32_t recvMbpsX1000 = 0;
  uint32_t skippedFrames = 0;
  uint64_t avgLatencyUs = 0;
  uint64_t maxLatencyUs = 0;
  uint64_t avgDecodeTailUs = 0;
  uint64_t maxDecodeTailUs = 0;
  uint32_t congestionState = 0;
  uint32_t congestionTransitions = 0;
  uint32_t congestionRecoveryCount = 0;
  uint32_t congestionRecoveryReq = 0;
  uint32_t congestionRecoveryMaxUs = 0;
  uint32_t queueDepthMax = 0;
  uint32_t queueDepthH4p = 0;
  uint32_t udpAssemblyDropPm = 0;
};

// Client-reported metrics + keyframe requests (Phase 1-10 state struct). The control thread
// publishes a whole ViewerMetrics as each ControlClientMetrics message arrives; the main loop's
// 1s tick and the ABR/M9 decision take a whole copy.
// thread: control (write) / main (read), under mu. The keyframe-request counters stay atomic --
// they are lifetime telemetry, not part of the reported state.
struct ClientMetricsSnapshot {
  std::mutex mu;
  ViewerMetrics metrics;

  void Publish(const ViewerMetrics& m) {
    std::lock_guard<std::mutex> lk(mu);
    metrics = m;
  }
  ViewerMetrics Snapshot() {
    std::lock_guard<std::mutex> lk(mu);
    return metrics;
  }
  // A new session starts with nothing reported; updatedUs = 0 fails every freshness gate.
  void Reset() {
    std::lock_guard<std::mutex> lk(mu);
    metrics = ViewerMetrics{};
  }

  // Keyframe-request accounting. The request itself is a MainLoopMailbox post (Phase 4); these
  // two are lifetime counters for the stats line and the throttle log.
  std::atomic<uint64_t> keyFrameRequestCount{0};
  std::atomic<uint64_t> keyFrameRequestDropped{0};
};

}  // namespace remote60::native_poc
