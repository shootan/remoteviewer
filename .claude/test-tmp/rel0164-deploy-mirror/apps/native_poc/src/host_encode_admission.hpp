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
// "Backlogged" is the sender queue depth now. The async MFT's own accepted-but-undrained inputs are
// deliberately NOT a gate signal (bitrate-hard-cap r2): the MFT is drained only INSIDE the encode
// call (mf_h264_codec encode_sample_common's drain loop), so skipping the encode because the MFT is
// backed up would also skip the drain -- the pending inputs would never fall and the gate would skip
// for ever (a self-perpetuating stall, the pending=4/keyWanted=false case). The sender queue, by
// contrast, is drained by the wire on its own thread, so a queue-depth gate is self-correcting: as
// soon as the wire clears one AU the gate admits, the encode runs, and the MFT drains. Every admit
// therefore drains the MFT; every skip is bounded by the wire draining the queue.
//
// Pure, so every branch is a unit test; the thresholds come from the caller.

#include <cstdint>

namespace remote60::native_poc {

struct EncodeAdmissionInputs {
  bool wireCapActive = false;      // the hard cap is on; with it off the gate never skips (legacy)
  bool keyWanted = false;          // this frame will ACTUALLY be forced as a key now (forceKeyFrame)
                                   // -- always admit. A keyWanted frame that cannot force (a key of
                                   // this session already in flight) is a delta and must be false here,
                                   // so it is gated like any delta instead of overflowing the queue
                                   // into a resync-IDR loop while the real key is on its way. (r3 F2)
  bool servedBootstrap = false;    // a bootstrap/kick synthetic frame -- always admit
  uint32_t senderQueueDepth = 0;   // sender.queue.size() now
  uint32_t senderQueueMax = 2;     // kSenderQueueMaxFrames: at/above this the wire has not drained
};

enum class EncodeAdmission { Admit, SkipOverloaded };

inline EncodeAdmission decide_encode_admission(const EncodeAdmissionInputs& in) {
  if (!in.wireCapActive) return EncodeAdmission::Admit;      // no gate unless the wire is bounded
  if (in.keyWanted || in.servedBootstrap) return EncodeAdmission::Admit;  // these must reach the wire
  if (in.senderQueueDepth >= in.senderQueueMax) return EncodeAdmission::SkipOverloaded;
  return EncodeAdmission::Admit;
}

}  // namespace remote60::native_poc
