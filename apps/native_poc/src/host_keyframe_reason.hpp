#pragma once

// Why the host encodes a keyframe -- one bit per cause, accumulated until the key is produced.
//
// Role:    the reasons behind EncoderState::forceKeyNext, and the keyint schedule, named, so the
//          `[keyframe]` line can say what asked for each IDR (quality r1, 2026-09-27).
// Why:     measured on this PC: three keyframes within 0.13 s of a connection and a re-key at every
//          resolution change, 10 Mb/s in the second it happened. Thirty-three places set
//          forceKeyNext and none said why, so which of them fired could only be guessed.
// Contract: diagnostic only. Nothing reads these bits to decide anything; setting a reason never
//          changes whether a key is forced (EncoderState::RequestKey sets forceKeyNext exactly as
//          the plain assignment it replaced).

#include <cstdint>
#include <string>

namespace remote60::native_poc {

enum HostKeyReason : uint32_t {
  kHostKeyReasonNone = 0,
  kHostKeyReasonInitial = 1u << 0,          // encoder state as constructed (forceKeyNext starts true)
  kHostKeyReasonPeer = 1u << 1,             // UDP peer changed: media session rollover
  kHostKeyReasonSession = 1u << 2,          // a new control session (client connected)
  kHostKeyReasonSelection = 1u << 3,        // capture target selected / switched
  kHostKeyReasonGeometry = 1u << 4,         // capture geometry changed
  kHostKeyReasonEncoderTarget = 1u << 5,    // encoder rebuilt for a new size/bitrate/fps (ApplyTarget)
  kHostKeyReasonAbr = 1u << 6,              // ABR profile change
  kHostKeyReasonM9 = 1u << 7,               // M9 level change
  kHostKeyReasonTune = 1u << 8,             // runtime tune from the viewer
  kHostKeyReasonUiQualityMode = 1u << 9,    // picker/overview quality mode entered or left
  kHostKeyReasonRefit = 1u << 10,           // encode size refit on the encode path
  kHostKeyReasonViewer = 1u << 11,          // the viewer asked (ControlRequestKeyFrame)
  kHostKeyReasonSenderBarrier = 1u << 12,   // a send failed; the media barrier re-armed
  kHostKeyReasonSenderBacklog = 1u << 13,   // the sender dropped a backlog and needs a resync
  kHostKeyReasonCaptureRestart = 1u << 14,  // capture session restarted / reattached (watchdogs too)
  kHostKeyReasonStreamActive = 1u << 15,    // streaming resumed (stream-active control)
  kHostKeyReasonEncodeLoss = 1u << 16,      // encoder output lost; the reference chain broke
  kHostKeyReasonEncoderReset = 1u << 17,    // encoder reset / reinit after an error
  kHostKeyReasonEpochGate = 1u << 18,       // the epoch gate closed or still awaits its key
  kHostKeyReasonBarrierKick = 1u << 19,     // trailing kick served into a closed barrier
  kHostKeyReasonProvenance = 1u << 20,      // provenance resync
  kHostKeyReasonFirstFrame = 1u << 21,      // encodedSeq == 0
  kHostKeyReasonScheduled = 1u << 22,       // the keyint schedule (periodic)
};

/** "peer|encoder_target|abr" -- the set bits by name, lowest first; "none" when empty. */
inline std::string host_key_reason_names(uint32_t reasons) {
  static const char* const kNames[] = {
      "initial",      "peer",          "session",        "selection",      "geometry",
      "encoder_target", "abr",         "m9",             "tune",           "ui_quality_mode",
      "refit",        "viewer",        "sender_barrier", "sender_backlog", "capture_restart",
      "stream_active", "encode_loss",  "encoder_reset",  "epoch_gate",     "barrier_kick",
      "provenance",   "first_frame",   "scheduled",
  };
  std::string out;
  for (uint32_t bit = 0; bit < sizeof(kNames) / sizeof(kNames[0]); ++bit) {
    if ((reasons & (1u << bit)) == 0) continue;
    if (!out.empty()) out += '|';
    out += kNames[bit];
  }
  const uint32_t known = (1u << (sizeof(kNames) / sizeof(kNames[0]))) - 1u;
  if ((reasons & ~known) != 0) {
    if (!out.empty()) out += '|';
    out += "unknown";
  }
  return out.empty() ? std::string("none") : out;
}

}  // namespace remote60::native_poc
