#pragma once

// The recv thread's once-a-second counters (Phase 1-11 state struct).
//
// Role:    everything the recv thread accumulates between two stats lines: received / decoded /
//          skipped frames and bytes, latency and decode-tail sums and maxima, decode failure
//          counters, the UDP assembly counters, the queue-depth histogram and the present-counter
//          snapshot used for deltas.
// Thread:  recv only.
// Input:   every received frame / datagram.
// Output:  the "[native-video-client] recvFrames=..." line and publish_metrics.
// Callers: recv thread (process_h264_frame, the UDP and TCP loops, the helper lambdas).
//
// Fields are the former locals of the recvThread lambda, initial values unchanged (viewer split
// refactor Phase 1-11); statAtUs and lastPresentCounters are assigned at thread start exactly where
// the locals were initialised.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

struct PresentCounterSnapshot {
  uint64_t d3dPresentSuccess = 0;
  uint64_t d3dPresentFail = 0;
  uint64_t gdiFallbackPresented = 0;
  uint64_t fallbackInitFail = 0;
  uint64_t fallbackRenderFail = 0;
  uint64_t fallbackNv12ConvertFail = 0;
  uint64_t paintCoalesced = 0;
  uint64_t overwriteBeforePresent = 0;
};

struct RecvStats {
  uint64_t statAtUs = 0;  // next stats line; set to qpc_now_us() + 1s at thread start
  uint64_t recvFrames = 0;
  uint64_t decodedFrames = 0;
  uint64_t skippedQueued = 0;
  uint64_t recvBytes = 0;
  uint64_t decodedBytes = 0;
  uint64_t sumLatencyUs = 0;
  uint64_t maxLatencyUs = 0;
  uint64_t sumDecodeTailUs = 0;
  uint64_t maxDecodeTailUs = 0;
  uint64_t decodeFailCount = 0;
  uint64_t decodeTimestampOverflowCount = 0;
  uint64_t decodeEmptyCount = 0;
  uint64_t decodeEmptyRecoveryCount = 0;
  uint64_t udpChunkRecvCount = 0;
  uint64_t udpAssemblyCompletedCount = 0;
  uint64_t udpAssemblyDroppedCount = 0;
  uint64_t udpAssemblyMalformedCount = 0;
  uint64_t udpAssemblyReorderCount = 0;
  uint64_t udpAssemblyKeyReqCount = 0;
  uint64_t udpAssemblyFecRecoveredCount = 0;
  uint32_t udpAssemblyDropPmLast = 0;
  uint64_t queueDepthSampleCount = 0;
  uint64_t queueDepthHist[5] = {0, 0, 0, 0, 0};
  uint32_t queueDepthFramesMax = 0;
  PresentCounterSnapshot lastPresentCounters;  // set from load_present_counters() at thread start
};

}  // namespace remote60::native_poc::viewer
