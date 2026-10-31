#pragma once

// What the encode loop does with one access unit at the sender queue.
//
// Role:    the pure decision behind the queue policy in encode_send_h264_emit_au -- key frames
//          re-anchor the stream, deltas are held while the media barrier is closed, and a delta
//          that lands on a backlog resyncs instead of stacking. Extracted so the policy can be
//          exercised without a socket, an encoder or a live session (ledger H-19).
// Thread:  pure; no state. The caller holds sender.mu while applying the result.
// Callers: host_stage_encode_send_h264_au.cpp, host_sender_queue_policy_test.cpp.

#include <cstddef>

namespace remote60::native_poc {

enum class SenderQueueAction {
  // Key AU: discard whatever the sender has not drained, open the media barrier, enqueue.
  EnqueueKey,
  // Delta while the barrier is closed. It references pictures the client never got, so it is
  // dropped and a key is requested.
  HoldForKey,
  // Delta landing on a backlog (or a full queue): drop the backlog AND this delta, close the
  // barrier and resync with a fresh IDR. Stacking it would only add latency.
  DropAndResync,
  // Delta, ordinary case.
  Enqueue,
};

/**
 * `backlogged` is judged ONCE per encode call, on the queue depth that existed before the batch
 * -- an async MFT can release several AUs microseconds apart, and counting those as congestion
 * discarded whole GOPs on a healthy link.
 *
 * But it must be cleared as soon as a key AU in that same batch clears the queue: at that point
 * the backlog it describes no longer exists. Leaving it set made the delta that followed an IDR
 * in the same batch drop the queue the IDR had just re-anchored -- and then request another key,
 * which arrives with the same backlog reading, which drops it again. The caller owns that reset
 * (see H264AuBatch::senderBacklogged); this function only reads the flag it is given.
 */
inline SenderQueueAction decide_sender_queue_action(bool keyFrame, bool waitingForKey,
                                                    bool backlogged, std::size_t queueSize,
                                                    std::size_t maxFrames) {
  if (keyFrame) return SenderQueueAction::EnqueueKey;
  if (waitingForKey) return SenderQueueAction::HoldForKey;
  if (backlogged || queueSize >= maxFrames) return SenderQueueAction::DropAndResync;
  return SenderQueueAction::Enqueue;
}

}  // namespace remote60::native_poc
