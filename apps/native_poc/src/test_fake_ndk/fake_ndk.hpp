#pragma once
// TEST ONLY (t-970r4zgo r5): a stand-in for the slice of the Android NDK the APK's decoder sink
// uses (android_video_decoder.cpp), so that source -- unchanged -- runs in a Windows test. The
// codec takes input into a buffer and records what was queued. It produces output only when the test
// queues one (mobile-abr r1: `outputs`); otherwise it never does, as before. The test can make the codec
// fail to be created / configured / started, or have no free input buffer.
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
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
    // viewer-sharpness r1: the crop a format change carries -- as the one "crop" rect key (Codec2), as the four
    // crop-left/top/right/bottom integers, or neither. Inclusive, as MediaFormat reports it.
    bool cropRect = false;
    bool cropKeys = false;
    int32_t cropL = 0, cropT = 0, cropR = 0, cropB = 0;
    int32_t size = 1;  // info.size of a picture buffer (0: an empty buffer, e.g. an EOS-only one)
  };
  std::deque<Output> outputs;
  int32_t outputWidth = 0;   // the format getOutputFormat reports (set by a format change)
  int32_t outputHeight = 0;
  bool outputCropRect = false, outputCropKeys = false;
  int32_t outputCropL = 0, outputCropT = 0, outputCropR = 0, outputCropB = 0;
  int dequeued = 0;          // pictures handed out
  int released = 0;          // releaseOutputBuffer / AtTime calls
  // apk-decoder-liveness r1: who touched the codec, what was made and let go, and which window a codec was configured
  // with (the stand-in's window is the Surface pointer itself).
  std::set<std::thread::id> codecThreads;
  int codecsDeleted = 0;
  int windowsAcquired = 0;
  int windowsReleased = 0;
  const void* lastConfiguredWindow = nullptr;
  std::vector<const void*> queuedWindow;  // the window of the codec each queued payload went into
};

std::mutex& mu();
Controls& controls();  // take mu() around reads/writes from the test

// apk-decoder-liveness r1: a slow codec, deterministically. While `held`, AMediaCodec_createDecoderByType /
// configure / start / stop (the ones whose `which` bit is set) wait -- WITHOUT mu() -- until the test lets go.
// `waiting` counts the calls blocked there right now.
enum HoldWhich : uint32_t { kHoldCreate = 1, kHoldConfigure = 2, kHoldStart = 4, kHoldStop = 8 };
struct Barrier {
  std::atomic<uint32_t> which{0};
  std::atomic<bool> held{false};
  std::atomic<int> waiting{0};
  std::atomic<int> passed{0};  // calls that waited and went on
};
Barrier& barrier();

}  // namespace fake_ndk
