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

/**
 * Whether the pre-batch backlog flag may still force a resync (bitrate-hard-cap r4 R3).
 *
 * Under the hard wire cap the input gate (host_encode_admission) already suppresses NEW captures when
 * the queue is backlogged, and the emit path gives already-accepted MFT output a bounded chance to
 * drain into the queue (staging) rather than discarding the reference chain. So under the cap the
 * pre-batch backlog flag must NOT be a resync trigger -- the queue decision is purely current depth vs
 * the hard cap, and accepted output enqueues once a slot frees. With the cap OFF there is no input
 * gate, so the legacy pre-batch-backlog resync (the old flood defence) stays. A genuine stall still
 * resyncs when the current depth reaches maxFrames after the bounded staging wait.
 */
inline bool backlog_resync_active(bool capActive, bool preBatchBacklogged) {
  return capActive ? false : preBatchBacklogged;
}

}  // namespace remote60::native_poc
