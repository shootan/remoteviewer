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
//     waiting, the selection is OWED a key frame (KeyOwed) until an IDR of that generation is
//     actually admitted -- not merely until one was asked for: a request can be refused on the
//     way (either side's limiter). The session asks again, bounded, while it is owed (r4 M1-A).
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
    keyOwed_ = false;
  }

  void Reset() {
    pending_ = 0;
    awaiting_ = false;
    expected_ = 0;
    droppedCount_ = 0;
    keyOwed_ = false;
  }

  Ack OnAck(bool ok, uint64_t streamGeneration, uint64_t requestTag) {
    if (pending_ == 0 || !awaiting_ || requestTag != pending_) return {AckResult::Ignored, false};
    awaiting_ = false;
    if (!ok || streamGeneration == 0) {
      expected_ = 0;
      return {AckResult::Failed, false};
    }
    expected_ = streamGeneration;
    keyOwed_ = DroppedWhileAwaiting(streamGeneration);
    return {AckResult::Applied, keyOwed_};
  }

  // True = the frame may be decoded. Frames before the answer are dropped (and their generation
  // remembered); after it, only the answered generation passes.
  // isKey: the frame is an IDR -- one of the answered generation pays what the selection is owed.
  bool Admit(uint64_t frameStreamGeneration, bool isKey = false) {
    if (pending_ != 0 && awaiting_) {
      RememberDropped(frameStreamGeneration);
      return false;
    }
    if (pending_ != 0 && expected_ == 0) return false;
    if (expected_ != 0 && frameStreamGeneration != expected_) return false;
    if (isKey && expected_ != 0 && frameStreamGeneration == expected_) keyOwed_ = false;
    return true;
  }

  // The answered selection still has no IDR of its generation (see the class comment).
  bool KeyOwed() const { return keyOwed_; }
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
  bool keyOwed_ = false;
};

}  // namespace remote60::native_poc
