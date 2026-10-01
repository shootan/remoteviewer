#pragma once

// The encode-input load gate for the hard wire-rate cap (bitrate-hard-cap r1, plan points 2/5/6).
//
// When the wire is capped, the limiter paces bytes out at the user's bitrate. If the encoder keeps
// producing deltas faster than the wire drains, the 2-frame sender queue overflows, the existing
// policy clears the backlog and requests an IDR, and the IDR -- larger still -- overflows again: an
// IDR -> delay -> IDR loop that shows as a frozen, stuttering picture (a low send rate but no moving
// image, which the contract calls a FAIL). This gate pre-empts that by skipping a NEW capture frame
// *before it is encoded* -- before it joins the reference chain -- while the wire is backlogged, so
// the stream simply drops to the frame rate the wire can carry. The frames already encoded keep
// draining and displaying; the reference chain is intact (a skipped capture never entered the
// encoder). A keyframe, a bootstrap/kick synthetic, and recovery are never skipped here -- they must
// reach the wire (bounded by the existing force-key latch), so a first frame / single input / loss
// recovery still arrives.
//
// "Backlogged" is two signals, because an async MFT releases several AUs per call: the sender queue
// depth now, AND the inputs the MFT has accepted but not yet drained (they will become queued AUs in
// a burst). Either over its threshold means the wire is behind.
//
// Pure, so every branch is a unit test; the thresholds come from the caller.

#include <cstdint>

namespace remote60::native_poc {

struct EncodeAdmissionInputs {
  bool wireCapActive = false;      // the hard cap is on; with it off the gate never skips (legacy)
  bool keyWanted = false;          // first frame / forced / scheduled / recovery IDR -- always admit
  bool servedBootstrap = false;    // a bootstrap/kick synthetic frame -- always admit
  uint32_t senderQueueDepth = 0;   // sender.queue.size() now
  uint32_t senderQueueMax = 2;     // kSenderQueueMaxFrames: at/above this the wire has not drained
  uint32_t mftPendingDepth = 0;    // encoder inputs accepted but not yet drained as output
  uint32_t mftPendingMax = 4;      // at/above this the MFT itself is backed up (steady state is 1-2)
};

enum class EncodeAdmission { Admit, SkipOverloaded };

inline EncodeAdmission decide_encode_admission(const EncodeAdmissionInputs& in) {
  if (!in.wireCapActive) return EncodeAdmission::Admit;      // no gate unless the wire is bounded
  if (in.keyWanted || in.servedBootstrap) return EncodeAdmission::Admit;  // these must reach the wire
  if (in.senderQueueDepth >= in.senderQueueMax) return EncodeAdmission::SkipOverloaded;
  if (in.mftPendingDepth >= in.mftPendingMax) return EncodeAdmission::SkipOverloaded;
  return EncodeAdmission::Admit;
}

}  // namespace remote60::native_poc
