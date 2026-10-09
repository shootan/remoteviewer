#pragma once
// TEST ONLY (t-970r4zgo r5): a stand-in for the slice of the Android NDK the APK's decoder sink
// uses (android_video_decoder.cpp), so that source -- unchanged -- runs in a Windows test. The
// codec takes input into a buffer and records what was queued; it never produces output. The
// test can make the codec fail to be created / configured / started, or have no free input buffer.
#include <cstdint>
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
};

std::mutex& mu();
Controls& controls();  // take mu() around reads/writes from the test

}  // namespace fake_ndk
