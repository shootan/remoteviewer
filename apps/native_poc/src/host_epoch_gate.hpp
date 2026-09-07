#pragma once

// The epoch gate: the first access unit the host sends after a capture flush is an IDR of the
// new epoch, and nothing encoded from a pre-flush input goes out after it (P11).
//
// Role:    pure decision + counters, so the rule is testable without the stage
//          (host_encode_epoch_test drives it with a real H264Encoder; the viewer integration test's
//          fake host runs the same header on its emit path). The stage
//          (host_stage_encode_send_h264_au.cpp) owns the logging, the forceKey re-request and the
//          encoder reset the gate can ask for.
// Thread:  main loop only.
//
// Why: an asynchronous MFT returns an OLDER input's AU during the current encode call. A flush
// (backend switch, restart, geometry change) empties the capture ring but not the encoder, so the
// first call after the flush emits the pre-flush picture -- true old capture stamp, flagged real,
// on the new generation. On 2026-09-07 15:15:59 that was seq 24484: a 101-byte P of the
// pre-switch screen 820 ms behind its send time, presented as the resume anchor; the fresh frames
// right behind it then read as an 848 ms decode backlog (viewer Congested, IDR, burst drops).
//
// The rule (Codex review seq 1052): every input carries the flush epoch it was accepted in
// (H264Encoder FIFO, lockstep with timestamp and synthetic). After a flush the gate waits for a
// KEYFRAME OF THE NEW EPOCH:
//   - an AU from an older epoch is discarded, key or not -- it is the previous dependent chain,
//     and an old-epoch key must not count as the IDR the flush forced (latch / barrier);
//   - a current-epoch non-key AU before that IDR is discarded too, and the stage re-forces the key
//     (the forced IDR did not come: an encoder that ignored or delayed it);
//   - the first current-epoch keyframe opens the gate.
// Bounded: if the wait exceeds kAwaitMaxUs or kAwaitMaxDropped AUs, the verdict is ResetEncoder --
// the stage rebuilds the MFT (the existing stale-output reset path), which yields an IDR next.
// Nothing is held for later: a discarded chain is resynced by the IDR, never re-sent, so the
// old-stamp-then-fresh-burst shape cannot recur and a previous target's picture is never labelled
// as the new one. Consecutive flushes just move the epoch again; the wait restarts.

#include <cstddef>
#include <cstdint>

namespace remote60::native_poc {

struct EpochGate {
  static constexpr uint64_t kAwaitMaxUs = 700000;  // > 2x the 300 ms forceKey in-flight window
  static constexpr uint32_t kAwaitMaxDropped = 12;

  uint64_t epoch = 0;          // the epoch the gate is judging for
  bool awaitingKey = false;    // true from the flush until the first current-epoch keyframe
  uint64_t awaitSinceUs = 0;   // first AU judged in this epoch (0 = none yet)
  uint32_t awaitDropped = 0;   // AUs discarded while waiting, this epoch
  // Process-lifetime counters (stats line).
  uint64_t droppedOldEpoch = 0;
  uint64_t droppedAwaitingKey = 0;
  uint64_t droppedBytes = 0;
  uint64_t keysAccepted = 0;
  uint64_t resetsRequested = 0;
  uint64_t epochsSeen = 0;
};

enum class EpochVerdict : uint8_t {
  Emit = 0,           // current epoch, gate open (or an untagged AU): send as usual
  DropOldEpoch = 1,   // pre-flush input: discard, count
  DropAwaitingKey = 2,  // current epoch but not a keyframe while the gate is closed: discard; caller re-forces the key
  AcceptKey = 3,      // the new epoch's first keyframe: send, gate opens
  ResetEncoder = 4,   // the wait exceeded its bound: caller rebuilds the encoder and forces a key
};

/**
 * Judge one AU. `epochNow` is CaptureState::inputEpoch at emission; `auEpoch` the AU's
 * H264AccessUnit::inputEpoch (0 = untagged -> never held back); `nowUs` any monotonic clock.
 */
inline EpochVerdict epoch_gate_judge(EpochGate& g, uint64_t epochNow, uint64_t auEpoch, bool auKey,
                                     size_t auBytes, uint64_t nowUs) {
  if (epochNow != g.epoch) {
    // A flush happened (possibly several): judge for the newest epoch, wait for its keyframe.
    g.epoch = epochNow;
    g.awaitingKey = true;
    g.awaitSinceUs = nowUs;
    g.awaitDropped = 0;
    ++g.epochsSeen;
  }
  if (auEpoch == 0) return EpochVerdict::Emit;
  if (auEpoch < epochNow) {
    ++g.droppedOldEpoch;
    g.droppedBytes += auBytes;
    if (g.awaitingKey) ++g.awaitDropped;
    return (g.awaitingKey && (g.awaitDropped >= EpochGate::kAwaitMaxDropped || nowUs >= g.awaitSinceUs + EpochGate::kAwaitMaxUs))
               ? (++g.resetsRequested, EpochVerdict::ResetEncoder)
               : EpochVerdict::DropOldEpoch;
  }
  if (!g.awaitingKey) return EpochVerdict::Emit;
  if (auKey) {
    g.awaitingKey = false;
    ++g.keysAccepted;
    return EpochVerdict::AcceptKey;
  }
  ++g.droppedAwaitingKey;
  g.droppedBytes += auBytes;
  ++g.awaitDropped;
  if (g.awaitDropped >= EpochGate::kAwaitMaxDropped || nowUs >= g.awaitSinceUs + EpochGate::kAwaitMaxUs) {
    ++g.resetsRequested;
    return EpochVerdict::ResetEncoder;
  }
  return EpochVerdict::DropAwaitingKey;
}

/** After the caller rebuilt the encoder: the wait restarts with a fresh bound. */
inline void epoch_gate_note_reset(EpochGate& g, uint64_t nowUs) {
  g.awaitingKey = true;
  g.awaitSinceUs = nowUs;
  g.awaitDropped = 0;
}

inline const char* epoch_verdict_name(EpochVerdict v) {
  switch (v) {
    case EpochVerdict::Emit: return "emit";
    case EpochVerdict::DropOldEpoch: return "drop-old-epoch";
    case EpochVerdict::DropAwaitingKey: return "drop-awaiting-key";
    case EpochVerdict::AcceptKey: return "accept-key";
    case EpochVerdict::ResetEncoder: return "reset-encoder";
  }
  return "?";
}

}  // namespace remote60::native_poc
