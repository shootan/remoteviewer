#pragma once

// Client-side metrics the viewer publishes to the host and the overlay (Phase 1-4 state struct).
//
// Role:    ClientRuntimeMetrics (the atomics behind the ControlClientMetrics message) and the
//          overlay metric ring (12 s of 1 s samples).
// Thread:  recv writes `client` once a second and pushes an overlay sample under `overlayMu`;
//          the control thread snapshots `client` for the host; the UI reads fps for the toolbar /
//          picker footer and (formerly) the overlay averages.
// Input:   the recv thread's 1 s aggregates.
// Output:  ControlClientMetricsMessage payload, toolbar fps.
// Callers: recv thread (publish_metrics), viewer_picker (snapshot, toolbar), viewer_overlay_draw, viewer_layout.
//
// Fields are the former globals gClientMetrics / gOverlayMetricsMu / gOverlayMetrics, initialisers
// unchanged (viewer split refactor Phase 1-4).

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

struct ClientRuntimeMetrics {
  std::atomic<uint32_t> seq{0};
  std::atomic<uint32_t> width{0};
  std::atomic<uint32_t> height{0};
  std::atomic<uint32_t> recvFpsX100{0};
  std::atomic<uint32_t> decodedFpsX100{0};
  std::atomic<uint32_t> recvMbpsX1000{0};
  std::atomic<uint32_t> skippedFrames{0};
  std::atomic<uint64_t> avgLatencyUs{0};
  std::atomic<uint64_t> maxLatencyUs{0};
  std::atomic<uint64_t> avgDecodeTailUs{0};
  std::atomic<uint64_t> maxDecodeTailUs{0};
  std::atomic<uint32_t> congestionState{0};
  std::atomic<uint32_t> congestionTransitions{0};
  std::atomic<uint32_t> congestionRecoveryCount{0};
  std::atomic<uint32_t> congestionRecoveryReq{0};
  std::atomic<uint32_t> congestionRecoveryMaxUs{0};
  std::atomic<uint32_t> queueDepthMax{0};
  std::atomic<uint32_t> queueDepthH4p{0};
  std::atomic<uint32_t> udpAssemblyDropPm{0};
  std::atomic<uint64_t> updatedQpcUs{0};
};

struct OverlayMetricSample {
  uint64_t tsUs = 0;
  uint32_t recvFpsX100 = 0;
  uint32_t decodedFpsX100 = 0;
  uint32_t recvMbpsX1000 = 0;
  uint64_t avgLatencyUs = 0;
};

struct OverlayMetricAverages {
  uint32_t recvFpsX100 = 0;
  uint32_t decodedFpsX100 = 0;
  uint32_t recvMbpsX1000 = 0;
  uint64_t avgLatencyUs = 0;
  uint32_t sampleCount = 0;
};

struct ClientMetricsState {
  // cross-thread: recv writes, control/UI read (every field atomic).
  ClientRuntimeMetrics client;
  // cross-thread: recv pushes, UI reads, both under overlayMu. dead: F-03 (no consumer left).
  std::mutex overlayMu;
  std::deque<OverlayMetricSample> overlay;
};

}  // namespace remote60::native_poc::viewer
