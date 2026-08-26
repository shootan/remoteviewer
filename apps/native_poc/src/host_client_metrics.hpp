#pragma once

// Viewer-reported metrics + keyframe requests (ClientMetricsSnapshot).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <atomic>
#include <cstdint>

namespace remote60::native_poc {

// Client-reported metrics + keyframe requests (Phase 1-10 state struct). Written by the control
// thread as ControlClientMetrics / ControlRequestKeyFrame messages arrive, read by the main loop's
// 1s stats tick and the ABR/M9 decisions. Every field is an atomic so the two threads share it
// without a lock; Phase 4 may replace this with a lock-copied snapshot.
// thread: control (write) / main (read).
struct ClientMetricsSnapshot {
  std::atomic<uint64_t> updatedUs{0};
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
  // Keyframe request signal from the viewer (consumed by the main loop's force-key path).
  std::atomic<bool> requestedKeyFrame{false};
  std::atomic<uint16_t> keyFrameReason{0};
  std::atomic<uint64_t> keyFrameRequestCount{0};
  std::atomic<uint64_t> keyFrameRequestDropped{0};
};

}  // namespace remote60::native_poc
