// mobile-abr r3: the APK's decoder sink on a real MediaCodec -- TEST ONLY, never part of the APK.
//
// An Android command-line executable (pushed to an emulator's /data/local/tmp, run from adb shell) that
// links the product code the APK links -- ClientSessionController (native_video_client_session.cpp) and
// AndroidVideoDecoderSink (android_video_decoder.cpp), unchanged -- and gives the sink a real Surface: an
// AImageReader's window, handed over through the one NDK call the sink makes to get its window
// (ANativeWindow_fromSurface, defined below for this executable only). The real MediaCodec decodes into it;
// every image that reaches the reader is counted: what the decoder actually rendered.
//
// It connects to an isolated host on the PC (through the PC harness's relay: mobile_abr_session_e2e_test
// --scenario external), and on a fixed schedule from its first frame:
//   +25 s  the surface goes and the stream is paused (what the app does when its view goes away)
//   +28 s  the surface comes back and the stream resumes
//   +45 s  a window selection is attempted (the window-locked test host refuses it: prepare -> answer
//          -> abort, the sink's selection gate on a real codec)
//   +70 s  disconnect
// It prints one line a second: images rendered, the sink's own status line. The reports themselves are
// counted where the host receives them (the PC relay).
//
//   mediacodec_smoke <host> <port> [seconds=70]

#include <android/native_window.h>
#include <media/NdkImageReader.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <jni.h>

#include "android_video_decoder.hpp"
#include "native_video_client_session.hpp"

namespace {
ANativeWindow* gReaderWindow = nullptr;
std::atomic<uint64_t> gImages{0};

void OnImage(void*, AImageReader* reader) {
  AImage* image = nullptr;
  while (AImageReader_acquireNextImage(reader, &image) == AMEDIA_OK && image) {
    gImages.fetch_add(1, std::memory_order_relaxed);
    AImage_delete(image);
    image = nullptr;
  }
}
uint64_t now_ms() {
  using namespace std::chrono;
  return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
}  // namespace

// The sink asks the platform for the window behind a Java Surface. There is no Java here: this executable
// answers with the reader's window. The reader owns it, so "release" is a no-op.
extern "C" ANativeWindow* ANativeWindow_fromSurface(JNIEnv*, jobject surface) {
  return surface ? gReaderWindow : nullptr;
}
extern "C" void ANativeWindow_release(ANativeWindow*) {}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: mediacodec_smoke <host> <port> [seconds]\n");
    return 2;
  }
  const std::string host = argv[1];
  const int port = std::atoi(argv[2]);
  const int seconds = argc > 3 ? std::atoi(argv[3]) : 70;
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  AImageReader* reader = nullptr;
  if (AImageReader_new(1920, 1088, AIMAGE_FORMAT_PRIVATE, 4, &reader) != AMEDIA_OK || !reader) {
    std::printf("SMOKE FAIL no AImageReader\n");
    return 1;
  }
  AImageReader_ImageListener listener{nullptr, OnImage};
  AImageReader_setImageListener(reader, &listener);
  AImageReader_getWindow(reader, &gReaderWindow);

  static int surfaceToken = 1;
  remote60::android_direct::AndroidVideoDecoderSink sink;
  sink.SetSurface(nullptr, reinterpret_cast<jobject>(&surfaceToken));

  remote60::native_poc::ClientSessionController session;
  remote60::native_poc::ClientSessionConnectArgs args;
  args.host = host;
  args.videoPort = port;
  args.controlPort = port;
  args.requireUdpHello = true;
  args.requireTcpControl = false;
  args.controlOverUdp = true;  // the directory path's shape: control tunnelled over the media socket
  args.encodedFrameSink = &sink;
  if (!session.Connect(args)) {
    std::printf("SMOKE FAIL connect\n");
    return 1;
  }
  const uint64_t t0 = now_ms();
  while (gImages.load() == 0 && now_ms() < t0 + 20000) {
    (void)sink.ReadySelectionGeneration();  // the app's UI polls the sink like this
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  if (gImages.load() == 0) {
    std::printf("SMOKE FAIL no image rendered in 20 s (%s)\n", sink.DebugStatus().c_str());
    session.Disconnect();
    return 1;
  }
  const uint64_t first = now_ms();
  std::printf("SMOKE first image at +%llu ms\n", static_cast<unsigned long long>(first - t0));
  uint64_t lastImages = gImages.load(), nextLog = first + 1000;
  bool surfaceOff = false, surfaceBack = false, selected = false, aborted = false;
  while (now_ms() < first + static_cast<uint64_t>(seconds) * 1000ULL) {
    const uint64_t t = now_ms() - first;
    if (!surfaceOff && t >= 25000) {
      surfaceOff = true;
      sink.SetSurface(nullptr, nullptr);
      (void)session.RequestStreamActive(false);
      std::printf("SMOKE event +%llu surface-off stream-pause\n", static_cast<unsigned long long>(t));
    }
    if (!surfaceBack && t >= 28000) {
      surfaceBack = true;
      sink.SetSurface(nullptr, reinterpret_cast<jobject>(&surfaceToken));
      (void)session.RequestStreamActive(true);
      std::printf("SMOKE event +%llu surface-on stream-resume\n", static_cast<unsigned long long>(t));
    }
    if (!selected && t >= 45000) {
      selected = true;
      const auto panel = session.WindowPanelSnapshotCopy();
      uint64_t id = 0;
      for (const auto& w : panel.items) {
        if (w.title.find("mobile abr target") != std::string::npos) id = w.id;
      }
      sink.PrepareForWindowSelection(4501);
      const bool asked = id != 0 && session.RequestWindowSelect(id);
      std::printf("SMOKE event +%llu select id=%llu asked=%d\n", static_cast<unsigned long long>(t),
                  static_cast<unsigned long long>(id), asked ? 1 : 0);
    }
    if (selected && !aborted && sink.ReadySelectionGeneration() != 4501 && t >= 47000) {
      aborted = true;
      // The window-locked host refuses a selection: the app abandons the switch.
      sink.AbortWindowSelection();
      session.ClearPendingDecoderKeyframe();
      std::printf("SMOKE event +%llu select-abort\n", static_cast<unsigned long long>(t));
    }
    (void)sink.ReadySelectionGeneration();
    if (now_ms() >= nextLog) {
      const uint64_t img = gImages.load();
      // r4 F2: what reached the session's socket (bytes) and the session's own state, next to the sink's.
      const auto snap = session.Snapshot();
      std::printf("SMOKE t=%llu images=%llu (+%llu) bytes=%llu state=%d status=%s | %s\n",
                  static_cast<unsigned long long>(t / 1000), static_cast<unsigned long long>(img),
                  static_cast<unsigned long long>(img - lastImages),
                  static_cast<unsigned long long>(session.SessionBytesReceived()), static_cast<int>(snap.state),
                  snap.status.c_str(), sink.DebugStatus().c_str());
      lastImages = img;
      nextLog += 1000;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  session.Disconnect();
  sink.SetSurface(nullptr, nullptr);
  AImageReader_delete(reader);
  std::printf("SMOKE done images=%llu\n", static_cast<unsigned long long>(gImages.load()));
  return 0;
}
