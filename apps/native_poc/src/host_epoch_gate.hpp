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
// The mechanism is reproduced on this PC's hardware MFT through the D3D11 NV12 surface input
// path (host_encode_epoch_test); the field host of that day ran the BGRA buffer input path
// (stats nv12SurfaceFrames=0), on which the same MFT answered each call with its own AU in the
// test, so the field host's exact cause stays unconfirmed. The gate is a contract on the wire
// order either way: what it drops can only ever be followed by the new epoch's IDR.
//
// Scope of the guarantee: this gate sits at the EMIT stage. An old-epoch AU already handed to
// the sender is fenced at dequeue by EncodedSendItem::inputEpoch (host_encoded_sender.cpp); one
// AU whose chunks were already being sent when the flush happened completes -- that in-flight AU
// is the documented exception, and the viewer's held-resume rule (viewer_frame_gate.cpp) keeps it
// from becoming the anchor.
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
  // Policy values under test, not a field latency guarantee: how long / how many AUs the gate
  // waits for the new epoch's IDR before asking for an encoder rebuild.
  static constexpr uint64_t kAwaitMaxUs = 700000;  // > 2x the 300 ms forceKey in-flight window
  static constexpr uint32_t kAwaitMaxDropped = 12;
  // Rebuild cap: at most kResetMaxPerWindow rebuilds per kResetWindowUs; past it the gate keeps
  // dropping and re-forcing the key but stops rebuilding, so a wedged encoder cannot turn into
  // an endless re-initialisation loop.
  static constexpr uint32_t kResetMaxPerWindow = 3;
  static constexpr uint64_t kResetWindowUs = 10'000'000;

  uint64_t epoch = 0;          // the epoch the gate is judging for
  bool awaitingKey = false;    // true from the flush until the first current-epoch keyframe
  // A06: the encoder lost input provenance (its accepted-input FIFO overflowed), so no output
  // may be trusted -- not even one that looks like the current epoch's keyframe. The stage sets
  // this from H264Encoder::provenance_invalid() and clears it after a successful rebuild.
  bool provenanceInvalid = false;
  uint64_t awaitSinceUs = 0;   // first AU judged in this epoch (0 = none yet)
  uint32_t awaitDropped = 0;   // AUs discarded while waiting, this epoch
  uint64_t resetWindowStartUs = 0;
  uint32_t resetsInWindow = 0;
  // Process-lifetime counters (stats line).
  uint64_t droppedOldEpoch = 0;
  uint64_t droppedUnknownEpoch = 0;  // untagged (0) or future epoch: fail closed
  uint64_t droppedAwaitingKey = 0;
  uint64_t droppedBytes = 0;
  uint64_t keysAccepted = 0;
  uint64_t resetsRequested = 0;
  uint64_t resetsSuppressed = 0;
  uint64_t epochsSeen = 0;
  uint64_t reclosedByUnknown = 0;    // HN07: verified chains closed again by an unprovenanced output
  uint64_t droppedProvenanceInvalid = 0;  // A06: dropped because the encoder's provenance is void
};

enum class EpochVerdict : uint8_t {
  Emit = 0,             // current epoch, gate open: send as usual
  DropOldEpoch = 1,     // pre-flush input: discard, count
  DropAwaitingKey = 2,  // current epoch but not a keyframe while the gate is closed: discard; caller re-forces the key
  AcceptKey = 3,        // the new epoch's first keyframe: send, gate opens
  ResetEncoder = 4,     // the wait exceeded its bound: caller rebuilds the encoder and forces a key
  DropUnknownEpoch = 5, // no FIFO provenance (0) or an epoch the host has not reached: never sent
};

/**
 * Judge one AU. `epochNow` is CaptureState::inputEpoch at emission; `auEpoch` the AU's
 * H264AccessUnit::inputEpoch; `nowUs` any monotonic clock.
 *
 * Fail closed: an AU whose epoch is unknown (0: the encoder FIFO had no entry for it -- an
 * overflow or a desynchronised output) or ahead of the host's own epoch (a tag the host never
 * issued) is never sent and never opens the gate. A valid current-epoch keyframe is accepted
 * before any bound is checked: the bound only applies to what is being dropped.
 */
inline EpochVerdict epoch_gate_judge(EpochGate& g, uint64_t epochNow, uint64_t auEpoch, bool auKey,
                                     size_t auBytes, uint64_t nowUs) {
  if (g.provenanceInvalid && !g.awaitingKey) {
    // A06: provenance is void from here until the encoder is rebuilt. Close the verified chain
    // now, so nothing rides out on a tag the FIFO can no longer vouch for.
    g.awaitingKey = true;
    g.awaitSinceUs = nowUs;
    g.awaitDropped = 0;
  }
  if (epochNow != g.epoch) {
    // A flush happened (possibly several): judge for the newest epoch, wait for its keyframe.
    g.epoch = epochNow;
    g.awaitingKey = true;
    g.awaitSinceUs = nowUs;
    g.awaitDropped = 0;
    ++g.epochsSeen;
  }
  if (auEpoch == epochNow && !g.provenanceInvalid) {
    if (!g.awaitingKey) return EpochVerdict::Emit;
    if (auKey) {
      g.awaitingKey = false;
      ++g.keysAccepted;
      return EpochVerdict::AcceptKey;
    }
  }
  // Everything below is a drop; which kind, and whether the wait has run out.
  auto bound_exceeded = [&]() {
    return g.awaitingKey &&
           (g.awaitDropped >= EpochGate::kAwaitMaxDropped || nowUs >= g.awaitSinceUs + EpochGate::kAwaitMaxUs);
  };
  auto request_reset = [&]() -> EpochVerdict {
    // Same budget as a provenance rebuild (epoch_gate_take_reset_budget), declared below.
    if (g.resetWindowStartUs == 0 || nowUs >= g.resetWindowStartUs + EpochGate::kResetWindowUs) {
      g.resetWindowStartUs = nowUs;
      g.resetsInWindow = 0;
    }
    if (g.resetsInWindow >= EpochGate::kResetMaxPerWindow) {
      ++g.resetsSuppressed;
      return EpochVerdict::DropAwaitingKey;  // keep dropping + re-forcing, no more rebuilds this window
    }
    ++g.resetsInWindow;
    ++g.resetsRequested;
    return EpochVerdict::ResetEncoder;
  };
  g.droppedBytes += auBytes;
  if (g.awaitingKey) ++g.awaitDropped;
  if (g.provenanceInvalid) {
    // Everything is refused while the latch is up; the stage is rebuilding the encoder. The
    // bound still applies, so a rebuild that never happens becomes a ResetEncoder request.
    ++g.droppedProvenanceInvalid;
    if (auEpoch == 0 || auEpoch > epochNow) ++g.droppedUnknownEpoch;
    return bound_exceeded() ? request_reset() : EpochVerdict::DropUnknownEpoch;
  }
  if (auEpoch == 0 || auEpoch > epochNow) {
    ++g.droppedUnknownEpoch;
    if (!g.awaitingKey) {
      // HN07: an output with no provenance (0) or an epoch this host never issued may belong to
      // a chain the viewer cannot have. The verified chain is closed again and the caller forces
      // a fresh IDR. A DropOldEpoch below deliberately does NOT do this: its provenance is
      // certain, the current chain is still valid, and re-closing would only cost an extra IDR.
      g.awaitingKey = true;
      g.awaitSinceUs = nowUs;
      g.awaitDropped = 1;
      ++g.reclosedByUnknown;
    }
    return bound_exceeded() ? request_reset() : EpochVerdict::DropUnknownEpoch;
  }
  if (auEpoch < epochNow) {
    ++g.droppedOldEpoch;
    return bound_exceeded() ? request_reset() : EpochVerdict::DropOldEpoch;
  }
  ++g.droppedAwaitingKey;  // current epoch, not a key, gate closed
  return bound_exceeded() ? request_reset() : EpochVerdict::DropAwaitingKey;
}

/**
 * Takes one rebuild from the gate's budget (kResetMaxPerWindow per kResetWindowUs), the same one
 * request_reset() uses, so a provenance rebuild cannot bypass it. False = spent for this window;
 * the caller keeps its request pending and retries on a later tick.
 */
inline bool epoch_gate_take_reset_budget(EpochGate& g, uint64_t nowUs) {
  if (g.resetWindowStartUs == 0 || nowUs >= g.resetWindowStartUs + EpochGate::kResetWindowUs) {
    g.resetWindowStartUs = nowUs;
    g.resetsInWindow = 0;
  }
  if (g.resetsInWindow >= EpochGate::kResetMaxPerWindow) {
    ++g.resetsSuppressed;
    return false;
  }
  ++g.resetsInWindow;
  ++g.resetsRequested;
  return true;
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
    case EpochVerdict::DropUnknownEpoch: return "drop-unknown-epoch";
  }
  return "?";
}

}  // namespace remote60::native_poc
