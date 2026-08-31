#pragma once

// Client-side metrics the viewer publishes to the host and the overlay (Phase 1-4 state struct).
//
// Role:    ClientRuntimeMetrics -- the atomics behind the ControlClientMetrics message.
// Thread:  recv writes `client` once a second; the control thread snapshots it for the host; the
//          UI reads fps for the toolbar / picker footer.
// Input:   the recv thread's 1 s aggregates.
// Output:  ControlClientMetricsMessage payload, toolbar fps.
// Callers: recv thread (publish_metrics), viewer_picker (snapshot, toolbar), viewer_layout.
//
// Fields are the former global gClientMetrics, initialisers unchanged (viewer split refactor
// Phase 1-4). The 12 s overlay sample ring that came with it is gone (F-03): the recv thread
// pushed to it every second and nothing read it once the stats overlay went.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

// The once-a-second client report, as one record. It goes to the host in one message and is
// read as one message, so it is stored as one value: twenty independent atomics let the control
// thread send a width from this second next to a latency from the last -- a report no second ever
// produced. (F-15; the host side of the same fix is ClientMetricsSnapshot / ViewerMetrics.)
struct ClientRuntimeMetrics {
  uint32_t seq = 0;
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
  uint64_t updatedQpcUs = 0;
};

struct ClientMetricsState {
  // cross-thread: recv publishes a whole record under mu, control/UI copy a whole record under mu.
  std::mutex mu;
  ClientRuntimeMetrics client;
  void Publish(const ClientRuntimeMetrics& m) {
    std::lock_guard<std::mutex> lk(mu);
    const uint32_t seq = client.seq + 1;
    client = m;
    client.seq = seq;
  }
  ClientRuntimeMetrics Snapshot() {
    std::lock_guard<std::mutex> lk(mu);
    return client;
  }
};

}  // namespace remote60::native_poc::viewer
