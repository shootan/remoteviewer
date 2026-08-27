#pragma once

// Stage 11 (H.264 path) internals shared by host_stage_encode_send_h264.cpp and its per-access-unit
// half host_stage_encode_send_h264_au.cpp.
//
// Role:    H264AuBatch is the set of encode_send_h264() locals the AU loop body reads, bound by
//          reference once per encoded frame (same pattern as HostContext); AuFlow is what the body
//          reports where it used to `continue` / `break` the loop.
// Thread:  main encode loop only.
// Callers: encode_send_h264() (loop) -> encode_send_h264_emit_au() (one access unit).
//
// Host split refactor Phase 2-13: the loop body moved verbatim out of encode_send_h264().

#include <cstddef>
#include <cstdint>

#include "host_bottleneck.hpp"
#include "host_main_loop.hpp"
#include "host_sender_queue_policy.hpp"
#include "mf_h264_codec.hpp"

namespace remote60::native_poc {

// What one AU-loop iteration asks the loop to do next.
enum class AuFlow { Next, Continue, Break };

// Absolute cap on the sender queue (frames); see the backlog comment above the loop in
// encode_send_h264().
constexpr size_t kSenderQueueMaxFrames = 6;

// encode_send_h264() locals the AU body reads / updates, in their declaration order.
struct H264AuBatch {
  D3DReadbackTiming& scaleReadbackTiming;
  uint64_t& preEncodePrepUs;
  uint64_t& scaleUs;
  uint64_t& nv12Us;
  const uint64_t& encodeStartUs;
  const uint64_t& encodeInputUs;
  const uint64_t& queueToEncodeUs;
  const uint64_t& callbackToEncodeStartUs;
  H264EncodeFrameStats& encodeStats;
  const uint64_t& encodeEndUs;
  bool& encoderResetTriggered;
  bool& sessionReconnectTriggered;
  bool& countedRawForInput;
  // Not const: a key AU inside this batch clears the backlog it describes, and the deltas that
  // follow in the SAME batch must see that. (Ledger H-19.)
  bool senderBacklogged;
};

// One access unit: timestamps / kick cancel, key-frame bookkeeping, sender-queue policy (UDP) or the
// direct TCP send, per-AU telemetry.
//
// `au` is non-const because the UDP path MOVES its bytes into the sender queue item. It used to be
// a const& with a std::move() on the member, which yields `const vector&&` -- that binds to the
// copy assignment, not the move, so every access unit was silently deep-copied (a 1080p IDR is
// 40-160KB). Nothing reads au.bytes after the move; the later reads are scalars. (Ledger H-08.)
AuFlow encode_send_h264_emit_au(HostContext& hx, TickContext& tc, H264AuBatch& b, H264AccessUnit& au);

}  // namespace remote60::native_poc
