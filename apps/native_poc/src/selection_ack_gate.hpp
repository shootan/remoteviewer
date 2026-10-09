#pragma once

#include <cstddef>
#include <cstdint>

namespace remote60::native_poc {

// The selection gate of a viewer that switches what it shows (the Android decoder sink): after
// the user picks a window or a screen, no frame is shown until the host has ANSWERED that pick
// with the stream generation it now sends, and then only frames of that generation.
//
// t-970r4zgo. Pulled out of android_video_decoder.cpp so the rule can be run where there is no
// MediaCodec -- the same object the APK uses, driven by Windows tests against a real host. What
// it adds to the rule the decoder had:
//   * the answer must be to THIS selection (requestTag == the selection armed by Prepare), so a
//     late answer to an earlier pick cannot open a later pick's gate;
//   * control and video travel separately, so the new generation's first IDR can arrive BEFORE
//     the answer and be dropped. If any frame of the answered generation was dropped while
//     waiting, the answer asks for a fresh IDR (bounded by the session's keyframe rate limit)
//     instead of waiting on deltas that reference a picture the decoder never had.
// It never opens on a frame before the answer.
class SelectionAckGate {
 public:
  enum class AckResult { Ignored, Applied, Failed };
  struct Ack {
    AckResult result = AckResult::Ignored;
    bool requestKeyframe = false;
  };

  void Prepare(uint64_t selection) {
    pending_ = selection;
    awaiting_ = true;
    expected_ = 0;
    droppedCount_ = 0;
  }

  void Reset() {
    pending_ = 0;
    awaiting_ = false;
    expected_ = 0;
    droppedCount_ = 0;
  }

  Ack OnAck(bool ok, uint64_t streamGeneration, uint64_t requestTag) {
    if (pending_ == 0 || !awaiting_ || requestTag != pending_) return {AckResult::Ignored, false};
    awaiting_ = false;
    if (!ok || streamGeneration == 0) {
      expected_ = 0;
      return {AckResult::Failed, false};
    }
    expected_ = streamGeneration;
    return {AckResult::Applied, DroppedWhileAwaiting(streamGeneration)};
  }

  // True = the frame may be decoded. Frames before the answer are dropped (and their generation
  // remembered); after it, only the answered generation passes.
  bool Admit(uint64_t frameStreamGeneration) {
    if (pending_ != 0 && awaiting_) {
      RememberDropped(frameStreamGeneration);
      return false;
    }
    if (pending_ != 0 && expected_ == 0) return false;
    if (expected_ != 0 && frameStreamGeneration != expected_) return false;
    return true;
  }

  uint64_t pending() const { return pending_; }
  bool awaiting() const { return awaiting_; }
  uint64_t expected() const { return expected_; }

 private:
  static constexpr size_t kDroppedMemory = 4;

  void RememberDropped(uint64_t gen) {
    for (size_t i = 0; i < droppedCount_; ++i) {
      if (dropped_[i] == gen) return;
    }
    if (droppedCount_ < kDroppedMemory) {
      dropped_[droppedCount_++] = gen;
      return;
    }
    for (size_t i = 1; i < kDroppedMemory; ++i) dropped_[i - 1] = dropped_[i];
    dropped_[kDroppedMemory - 1] = gen;
  }

  bool DroppedWhileAwaiting(uint64_t gen) const {
    for (size_t i = 0; i < droppedCount_; ++i) {
      if (dropped_[i] == gen) return true;
    }
    return false;
  }

  uint64_t pending_ = 0;
  bool awaiting_ = false;
  uint64_t expected_ = 0;
  uint64_t dropped_[kDroppedMemory] = {};
  size_t droppedCount_ = 0;
};

}  // namespace remote60::native_poc
