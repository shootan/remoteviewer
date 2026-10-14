#pragma once
// TEST ONLY (t-970r4zgo r5): a stand-in for the slice of the Android NDK the APK's decoder sink
// uses (android_video_decoder.cpp), so that source -- unchanged -- runs in a Windows test. The
// codec takes input into a buffer and records what was queued. It produces output only when the test
// queues one (mobile-abr r1: `outputs`); otherwise it never does, as before. The test can make the codec
// fail to be created / configured / started, or have no free input buffer.
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace fake_ndk {

struct Controls {
  bool createFails = false;
  bool configureFails = false;
  bool startFails = false;
  bool inputAvailable = true;
  int codecsCreated = 0;
  int codecsStarted = 0;
  std::vector<std::vector<uint8_t>> queued;  // payloads handed to a codec, in order
  std::vector<int64_t> queuedPts;            // mobile-abr r3: the PTS each of them was queued with
  // mobile-abr r1: what dequeueOutputBuffer hands out next, one per call. A picture (index 0) with its
  // PTS and buffer flags, or an output format change to width x height (crop not reported).
  struct Output {
    bool formatChange = false;
    int64_t presentationTimeUs = 0;
    uint32_t flags = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t size = 1;  // info.size of a picture buffer (0: an empty buffer, e.g. an EOS-only one)
  };
  std::deque<Output> outputs;
  int32_t outputWidth = 0;   // the format getOutputFormat reports (set by a format change)
  int32_t outputHeight = 0;
  int dequeued = 0;          // pictures handed out
  int released = 0;          // releaseOutputBuffer / AtTime calls
};

std::mutex& mu();
Controls& controls();  // take mu() around reads/writes from the test

}  // namespace fake_ndk
