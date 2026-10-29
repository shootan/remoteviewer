#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "native_video_client_shared_core.hpp"
#include "selection_ack_gate.hpp"

namespace remote60::native_poc {

// apk-0232 r2 (Codex codex_apk_prelogin_select_src_r1.md N2): a decoder's one held input frame -- the frame the
// codec had no input buffer for -- and the rules for it, shared by the Android decoder sink and its Windows tests.
//
// What r1 got wrong: the held frame was retried later WITHOUT asking the selection gate again, and a newer frame
// could be queued while an older one was still held. A local selection's first IDR G held, its next IDR H queued
// (binding H), then G queued after it: bytes of a generation the gate no longer admits reached the codec.
//
// Now:
//   * the gate is asked at the moment a frame would really go to the codec (PermitsQueue), the held one included;
//     a frame it no longer admits is dropped, never queued;
//   * a newer frame never overtakes a held one: while one is held, an arriving frame goes through the hold rules
//     (an IDR replaces the held frame; a delta is dropped and an IDR asked for), as r1's sink did when its own
//     queue attempt failed.
// `tryQueue(frame)` is the codec's input: true = the frame is in the codec now.
class DecoderPendingSlot {
 public:
  // A frame arriving from the session (after the gate's Admit). keyRequest: set when a dropped delta leaves the
  // decoder needing an IDR.
  template <class TryQueue>
  void Offer(UdpH264AssembledFrame&& frame, const SelectionAckGate& gate, TryQueue&& tryQueue, bool* keyRequest) {
    if (!gate.PermitsQueue(frame.header.streamGeneration, (frame.header.flags & 1u) != 0)) {
      ++gateDrops_;
      return;
    }
    if (!frame_.has_value() && tryQueue(frame)) return;
    if (!frame_.has_value()) {
      frame_ = std::move(frame);
      return;
    }
    const bool isKey = (frame.header.flags & 1u) != 0;
    if (isKey || frame_->payload.empty()) {
      // the held frame is discarded in favour of this one (a held delta: the chain needs an IDR)
      if ((frame_->header.flags & 1u) == 0 && keyRequest) *keyRequest = true;
      frame_ = std::move(frame);
    } else if (keyRequest) {
      // this delta is dropped: everything after it references a picture the decoder will never see
      *keyRequest = true;
    }
  }

  // The held frame, tried again: queued if the gate still admits its generation (and the codec takes it), dropped if
  // the gate no longer does. Returns true when nothing is held afterwards.
  template <class TryQueue>
  bool Pump(const SelectionAckGate& gate, TryQueue&& tryQueue) {
    if (!frame_.has_value()) return true;
    if (!gate.PermitsQueue(frame_->header.streamGeneration, (frame_->header.flags & 1u) != 0)) {
      ++gateDrops_;
      frame_.reset();
      return true;
    }
    if (tryQueue(*frame_)) {
      frame_.reset();
      return true;
    }
    return false;
  }

  bool has() const { return frame_.has_value(); }
  const UdpH264AssembledFrame* held() const { return frame_ ? &*frame_ : nullptr; }
  void Reset() { frame_.reset(); }
  // Frames the gate refused at the moment of queueing (diagnostics).
  uint64_t gateDrops() const { return gateDrops_; }

 private:
  std::optional<UdpH264AssembledFrame> frame_;
  uint64_t gateDrops_ = 0;
};

}  // namespace remote60::native_poc
