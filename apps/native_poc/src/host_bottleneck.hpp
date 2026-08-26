#pragma once

// Per-frame pipeline stage timing records for the host's bottleneck telemetry.
//
// Role:    HostBottleneckStage (which stage of capture->encode->send took longest this frame),
//          D3DReadbackTiming (lock wait / copy+map / memcpy / unmap split for a GPU readback),
//          and the small helpers that pick the slowest stage and classify the encoder API path.
// Thread:  plain data + pure functions; instances are owned by whichever thread measured them
//          (main encode loop, GPU scaler, readback worker) and only copied across threads.
// Input:   per-stage microsecond durations / encoder backend name.
// Output:  slowest stage (code + name) / small integer codes for the stats line.
// Callers: native_video_host_main.cpp (stats tick, trace lines), host_gpu_scaler (timing out-param),
//          d3d_capture_readback glue.
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-10). Header-only
// so no new translation unit is added and behavior is byte-identical.

#include <cstdint>
#include <string>

namespace remote60::native_poc {

struct HostBottleneckStage {
  uint32_t code = 0;
  uint64_t us = 0;
  const char* name = "none";
};

struct D3DReadbackTiming {
  uint64_t d3dWaitUs = 0;
  uint64_t copyMapUs = 0;
  uint64_t memcpyUs = 0;
  uint64_t unmapWaitUs = 0;
  uint64_t unmapUs = 0;
};

inline void update_host_bottleneck_stage(uint32_t code, uint64_t us, const char* name,
                                         HostBottleneckStage* stage) {
  if (!stage || !name) return;
  if (us > stage->us) {
    stage->code = code;
    stage->us = us;
    stage->name = name;
  }
}

inline HostBottleneckStage detect_host_bottleneck_stage(uint64_t queueWaitUs, uint64_t queueToEncodeUs,
                                                        uint64_t preEncodePrepUs, uint64_t scaleUs,
                                                        uint64_t nv12Us, uint64_t encUs,
                                                        uint64_t queueToSendUs, uint64_t sendDurUs,
                                                        uint64_t sendIntervalErrUs) {
  HostBottleneckStage stage{};
  update_host_bottleneck_stage(1, queueWaitUs, "queue_wait", &stage);
  update_host_bottleneck_stage(2, queueToEncodeUs, "queue_to_encode", &stage);
  update_host_bottleneck_stage(3, preEncodePrepUs, "pre_encode_prep", &stage);
  update_host_bottleneck_stage(4, scaleUs, "scale", &stage);
  update_host_bottleneck_stage(5, nv12Us, "bgra_to_nv12", &stage);
  update_host_bottleneck_stage(6, encUs, "encoder", &stage);
  update_host_bottleneck_stage(7, queueToSendUs, "queue_to_send", &stage);
  update_host_bottleneck_stage(8, sendDurUs, "send_io", &stage);
  // Not the wire: this is the interval between AUs being *enqueued* to the sender by the encode/main
  // thread. A large value means the host failed to supply AUs steadily (async MFT bursting, main
  // scheduling), which the client sees as a gap -- the actual wire timing is the "wire seq=" lines.
  update_host_bottleneck_stage(9, sendIntervalErrUs, "encode_au_enqueue_jitter", &stage);
  return stage;
}

inline uint32_t encoder_api_path_code(const char* backendName) {
  if (!backendName) return 0;
  const std::string name = backendName;
  if (name.find("amf") != std::string::npos) return 1;
  if (name.find("nvenc") != std::string::npos || name.find("nvidia") != std::string::npos) return 2;
  if (name.find("qsv") != std::string::npos || name.find("intel") != std::string::npos) return 3;
  if (name.find("mft") != std::string::npos) return 4;
  if (name.find("clsid") != std::string::npos) return 5;
  return 6;
}

}  // namespace remote60::native_poc
